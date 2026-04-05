#include "appwindow/AppWindowInternal.h"
#include "appwindow/I18n.h"
#include "LuaBytecode.h"

#include <commdlg.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

namespace appwindow {

namespace {

static void SplitPathUtf8(const std::string& path, std::string& dirOut, std::string& nameOut)
{
	const size_t i = path.find_last_of("/\\");
	if (i == std::string::npos) {
		dirOut.clear();
		nameOut = path;
	} else {
		dirOut = path.substr(0, i);
		nameOut = path.substr(i + 1);
	}
}

static std::string EscapeBatchSetValueUtf8(std::string_view s)
{
	std::string r;
	r.reserve(s.size() + 8);
	for (unsigned char c : s) {
		if (c == '"')
			r += "\"\"";
		else
			r += (char)c;
	}
	return r;
}

static void TrimScriptInPlace(std::string_view& sv)
{
	while (!sv.empty() && std::isspace((unsigned char)sv.front()))
		sv.remove_prefix(1);
	while (!sv.empty() && std::isspace((unsigned char)sv.back()))
		sv.remove_suffix(1);
}

static void AppendStatusPipe(char* statusBuf, size_t statusSz, const char* extra)
{
	if (!extra || !extra[0] || statusSz < 4)
		return;
	const size_t len = std::strlen(statusBuf);
	if (len >= statusSz - 1)
		return;
	const char* sep = " | ";
	const size_t sepL = std::strlen(sep);
	const size_t exL = std::strlen(extra);
	if (len + sepL + exL + 1 > statusSz)
		return;
	std::memcpy(statusBuf + len, sep, sepL);
	std::memcpy(statusBuf + len + sepL, extra, exL + 1);
}

// GetTempFileNameW 固定產生 .tmp；在 Win11 上 cmd /c "…\.tmp" 常觸發「用 App 開啟 .tmp」而非執行批次。改為唯一路徑 + .bat。
static bool AllocUniqueTempBatPath(const wchar_t* tempDir, const wchar_t* prefix, wchar_t outPath[MAX_PATH])
{
	if (GetTempFileNameW(tempDir, prefix, 0, outPath) == 0)
		return false;
	DeleteFileW(outPath);
	const size_t len = std::wcslen(outPath);
	if (len < 4)
		return false;
	wchar_t* ext = outPath + len - 4;
	if (ext[0] != L'.')
		return false;
	if ((ext[1] != L't' && ext[1] != L'T') || (ext[2] != L'm' && ext[2] != L'M') || (ext[3] != L'p' && ext[3] != L'P'))
		return false;
	ext[1] = L'b';
	ext[2] = L'a';
	ext[3] = L't';
	return true;
}

// 由 .lua 路徑推導預設的 .luac 輸出路徑
std::wstring LuacDefaultPathWideFromLua(const std::wstring& luaPath)
{
	if (luaPath.empty())
		return L"";
	std::wstring w = luaPath;
	if (WStringEndsWithLuaCI(w)) {
		w.resize(w.size() - 4);
		w += L".luac";
	} else
		w += L".luac";
	return w;
}

} // namespace

// 依目前作用中文件預填「另存 luac」緩衝區
void PrepareLuacSaveDialogInitialPath()
{
	if (g_activeDoc >= 0 && g_activeDoc < (int)g_docs.size() && !g_docs[(size_t)g_activeDoc].path.empty()) {
		const std::wstring def = LuacDefaultPathWideFromLua(g_docs[(size_t)g_activeDoc].path);
		if (def.size() + 1 < g_saveLuacBuf.size())
			wcscpy_s(g_saveLuacBuf.data(), g_saveLuacBuf.size(), def.c_str());
		else
			g_saveLuacBuf[0] = L'\0';
	} else
		g_saveLuacBuf[0] = L'\0';
}

// 將已開啟 Lua 路徑與對應 last luac 路徑填入持久化結構
void AppendPersistFileSlots(AppPersistState& s)
{
	int activeMapped = -1;
	for (size_t i = 0; i < g_docs.size(); ++i) {
		if (g_docs[i].path.empty())
			continue;
		if ((int)i == g_activeDoc)
			activeMapped = (int)s.openLuaPaths.size();
		s.openLuaPaths.push_back(g_docs[i].path);
		std::wstring lw;
		if (!g_docs[i].lastLuacOutPathUtf8.empty())
			Utf8ToWide(g_docs[i].lastLuacOutPathUtf8, lw);
		s.lastLuacOutPathsWide.push_back(std::move(lw));
	}
	if (activeMapped >= 0)
		s.activeLuaIndex = activeMapped;
	else if (!s.openLuaPaths.empty())
		s.activeLuaIndex = 0;
}

// 同步編輯器後立即寫入視窗／文件相關設定檔
void SavePersistNow()
{
	if (!g_hwnd)
		return;
	SyncEditorToActiveDoc();
	AppPersistState s;
	s.fontGlobalScale = g_cachedFontScaleForSave;
	s.sidebarWidth = g_sidebarWidth;
	s.keepBytecodeDebug = g_keepBytecodeDebug;
	s.uiLanguage = AppLanguageGetCurrent();
	s.afterBuildScriptUtf8 = g_afterBuildScriptUtf8;
	AppendPersistFileSlots(s);
	AppSettings_Save(g_hwnd, s);
}

// 顯示開啟檔對話框，路徑寫入 g_openFileBuf
bool PickOpenLuaPath(HWND owner)
{
	if (g_openFileBuf.empty())
		return false;
	g_openFileBuf[0] = L'\0';
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = owner;
	ofn.lpstrFile = g_openFileBuf.data();
	ofn.nMaxFile = (DWORD)g_openFileBuf.size();
	ofn.lpstrFilter = I18nOpenLuaFileFilterW();
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
	return GetOpenFileNameW(&ofn) == TRUE;
}

// 顯示另存 .lua 對話框，路徑寫入 g_saveFileBuf
bool PickSaveLuaPath(HWND owner)
{
	if (g_saveFileBuf.empty())
		return false;
	if (g_saveFileBuf[0] == L'\0')
		wcscpy_s(g_saveFileBuf.data(), g_saveFileBuf.size(), L"untitled.lua");
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = owner;
	ofn.lpstrFile = g_saveFileBuf.data();
	ofn.nMaxFile = (DWORD)g_saveFileBuf.size();
	ofn.lpstrFilter = I18nSaveLuaFileFilterW();
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	ofn.lpstrDefExt = L"lua";
	return GetSaveFileNameW(&ofn) == TRUE;
}

// 顯示另存 .luac 對話框，路徑寫入 g_saveLuacBuf
bool PickSaveLuacPath(HWND owner)
{
	if (g_saveLuacBuf.empty())
		return false;
	if (g_saveLuacBuf[0] == L'\0')
		wcscpy_s(g_saveLuacBuf.data(), g_saveLuacBuf.size(), L"output.luac");
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = owner;
	ofn.lpstrFile = g_saveLuacBuf.data();
	ofn.nMaxFile = (DWORD)g_saveLuacBuf.size();
	ofn.lpstrFilter = I18nSaveLuacFileFilterW();
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	ofn.lpstrDefExt = L"luac";
	return GetSaveFileNameW(&ofn) == TRUE;
}

// 儲存目前 Lua 原始碼；無路徑時先跳出另存對話框
void TrySaveLuaSource(HWND owner, char* statusBuf, size_t statusSz)
{
	EnsureLuaEditorInited();
	SyncEditorToActiveDoc();
	std::wstring path;
	if (g_activeDoc >= 0 && g_activeDoc < (int)g_docs.size())
		path = g_docs[(size_t)g_activeDoc].path;
	if (path.empty()) {
		g_saveFileBuf[0] = L'\0';
		if (!PickSaveLuaPath(owner))
			return;
		path.assign(g_saveFileBuf.data());
	}
	if (!WStringEndsWithLuaCI(path))
		path += L".lua";
	std::string err;
	const std::string text = LuaEditorGetBufferUtf8();
	if (!WriteWholeFileUtf8(path, text, err)) {
		std::snprintf(statusBuf, statusSz, "%s", err.c_str());
		return;
	}
	if (g_activeDoc < 0) {
		LuaDoc d;
		d.path = path;
		d.text = text;
		d.lastSavedTextUtf8 = text;
		g_docs.push_back(std::move(d));
		g_activeDoc = (int)g_docs.size() - 1;
	} else {
		g_docs[(size_t)g_activeDoc].path = path;
		g_docs[(size_t)g_activeDoc].text = text;
		g_docs[(size_t)g_activeDoc].lastSavedTextUtf8 = text;
	}
	std::snprintf(statusBuf, statusSz, "%s", Tr(I18nMsg::SavedLua));
	g_requestSavePersist = true;
}

// 首次設定 Lua 語法、配色與編輯器選項
void EnsureLuaEditorInited()
{
	if (g_luaEditorInited)
		return;
	g_luaEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
	g_luaEditor.SetPalette(TextEditor::GetDarkPalette());
	g_luaEditor.SetTabSize(4);
	g_luaEditor.SetShowWhitespaces(false);
	g_luaEditorInited = true;
}

// 編譯目前內容為 bytecode，經對話框選輸出路徑並更新紀錄
void TryCompileBytecode(HWND owner, bool keepDebug, char* statusBuf, size_t statusSz)
{
	EnsureLuaEditorInited();
	SyncEditorToActiveDoc();
	if (!owner) {
		std::snprintf(statusBuf, statusSz, "%s", Tr(I18nMsg::CannotShowSaveDlg));
		return;
	}
	PrepareLuacSaveDialogInitialPath();
	if (!PickSaveLuacPath(owner))
		return;
	std::wstring wpath(g_saveLuacBuf.data());
	if (!WStringEndsWithLuacCI(wpath))
		wpath += L".luac";
	std::string pathUtf8;
	if (!WidePathToUtf8(wpath, pathUtf8)) {
		std::snprintf(statusBuf, statusSz, "%s", Tr(I18nMsg::OutputPathEncodeFail));
		return;
	}
	std::string err;
	const std::string luaSrc = LuaEditorGetBufferUtf8();
	LuaBytecode::CompileOptions opt;
	opt.stripDebug = !keepDebug;
	const bool ok = LuaBytecode::CompileUtf8ToFile(std::string_view(luaSrc), pathUtf8, opt, err);
	if (ok) {
		if (g_activeDoc >= 0 && g_activeDoc < (int)g_docs.size())
			g_docs[(size_t)g_activeDoc].lastLuacOutPathUtf8 = pathUtf8;
		else
			g_orphanLastLuacOutPathUtf8 = pathUtf8;
		g_requestSavePersist = true;
		std::snprintf(statusBuf, statusSz, Tr(I18nMsg::WrittenToFmt), pathUtf8.c_str());
		RunAfterBuildHook(pathUtf8, statusBuf, statusSz);
	} else
		std::snprintf(statusBuf, statusSz, "%s", err.c_str());
}

// 使用上次記錄的 .luac 路徑重新編譯（無對話框）
void TryCompileBytecodeLastPath(bool keepDebug, char* statusBuf, size_t statusSz)
{
	EnsureLuaEditorInited();
	SyncEditorToActiveDoc();
	std::string target;
	if (g_activeDoc >= 0 && g_activeDoc < (int)g_docs.size())
		target = g_docs[(size_t)g_activeDoc].lastLuacOutPathUtf8;
	else
		target = g_orphanLastLuacOutPathUtf8;
	if (target.empty()) {
		std::snprintf(statusBuf, statusSz, "%s", Tr(I18nMsg::NoLuacPathHint));
		return;
	}
	std::string err;
	const std::string luaSrc = LuaEditorGetBufferUtf8();
	LuaBytecode::CompileOptions opt;
	opt.stripDebug = !keepDebug;
	const bool ok = LuaBytecode::CompileUtf8ToFile(std::string_view(luaSrc), target, opt, err);
	if (ok) {
		std::snprintf(statusBuf, statusSz, Tr(I18nMsg::WrittenToFmt), target.c_str());
		RunAfterBuildHook(target, statusBuf, statusSz);
	} else
		std::snprintf(statusBuf, statusSz, "%s", err.c_str());
}

void RunAfterBuildHook(const std::string& luacOutPathUtf8, char* statusBuf, size_t statusSz)
{
	std::string_view script(g_afterBuildScriptUtf8);
	TrimScriptInPlace(script);
	if (script.empty() || luacOutPathUtf8.empty())
		return;

	std::string dir8, name8;
	SplitPathUtf8(luacOutPathUtf8, dir8, name8);
	const std::string escPath = EscapeBatchSetValueUtf8(luacOutPathUtf8);
	const std::string escDir = EscapeBatchSetValueUtf8(dir8);
	const std::string escName = EscapeBatchSetValueUtf8(name8);

	wchar_t tempDir[MAX_PATH];
	if (GetTempPathW(MAX_PATH, tempDir) == 0 || tempDir[0] == L'\0') {
		AppendStatusPipe(statusBuf, statusSz, Tr(I18nMsg::AfterBuildRunFailed));
		return;
	}
	wchar_t wrapPath[MAX_PATH];
	wchar_t userPath[MAX_PATH];
	if (!AllocUniqueTempBatPath(tempDir, L"ljw", wrapPath)) {
		AppendStatusPipe(statusBuf, statusSz, Tr(I18nMsg::AfterBuildRunFailed));
		return;
	}
	if (!AllocUniqueTempBatPath(tempDir, L"lju", userPath)) {
		DeleteFileW(wrapPath);
		AppendStatusPipe(statusBuf, statusSz, Tr(I18nMsg::AfterBuildRunFailed));
		return;
	}

	std::string userPathUtf8;
	if (!WidePathToUtf8(std::wstring(userPath), userPathUtf8)) {
		DeleteFileW(wrapPath);
		DeleteFileW(userPath);
		AppendStatusPipe(statusBuf, statusSz, Tr(I18nMsg::AfterBuildRunFailed));
		return;
	}
	const std::string escUserCall = EscapeBatchSetValueUtf8(userPathUtf8);

	std::string userBody;
	userBody.reserve(script.size() + 4);
	userBody.append("\xEF\xBB\xBF");
	userBody.append(script.data(), script.size());

	std::string wrap;
	wrap.reserve(512 + escPath.size() + escDir.size() + escName.size() + userPathUtf8.size());
	wrap.append("\xEF\xBB\xBF");
	wrap += "@echo off\r\nchcp 65001 >nul\r\n";
	wrap += "set \"OUTPATH=";
	wrap += escPath;
	wrap += "\"\r\nset \"OUTDIR=";
	wrap += escDir;
	wrap += "\"\r\nset \"OUTNAME=";
	wrap += escName;
	wrap += "\"\r\ncall \"";
	wrap += escUserCall;
	wrap += "\"\r\n";

	std::string werr;
	if (!WriteWholeFileUtf8(std::wstring(userPath), userBody, werr)) {
		DeleteFileW(wrapPath);
		DeleteFileW(userPath);
		AppendStatusPipe(statusBuf, statusSz, Tr(I18nMsg::AfterBuildRunFailed));
		return;
	}
	if (!WriteWholeFileUtf8(std::wstring(wrapPath), wrap, werr)) {
		DeleteFileW(wrapPath);
		DeleteFileW(userPath);
		AppendStatusPipe(statusBuf, statusSz, Tr(I18nMsg::AfterBuildRunFailed));
		return;
	}

	std::wstring cmdLine = L"cmd.exe /d /s /c \"";
	cmdLine += wrapPath;
	cmdLine += L'"';
	std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
	cmdBuf.push_back(L'\0');

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};
	const BOOL started = CreateProcessW(
		nullptr,
		cmdBuf.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&si,
		&pi);

	if (!started) {
		DeleteFileW(wrapPath);
		DeleteFileW(userPath);
		AppendStatusPipe(statusBuf, statusSz, Tr(I18nMsg::AfterBuildRunFailed));
		return;
	}
	CloseHandle(pi.hThread);
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD exitCode = 1;
	GetExitCodeProcess(pi.hProcess, &exitCode);
	CloseHandle(pi.hProcess);
	DeleteFileW(wrapPath);
	DeleteFileW(userPath);

	if (exitCode != 0) {
		char extra[96];
		std::snprintf(extra, sizeof(extra), Tr(I18nMsg::AfterBuildExitCodeFmt), (unsigned)exitCode);
		AppendStatusPipe(statusBuf, statusSz, extra);
	}
}

} // namespace appwindow
