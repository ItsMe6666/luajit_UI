#include "appwindow/AppWindowInternal.h"
#include "LuaBytecode.h"

#include <commdlg.h>

#include <cstdio>
#include <string_view>

namespace appwindow {

namespace {

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
	ofn.lpstrFilter = L"Lua 腳本 (*.lua)\0*.lua\0所有檔案 (*.*)\0*.*\0";
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
	ofn.lpstrFilter = L"Lua 腳本 (*.lua)\0*.lua\0";
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
	ofn.lpstrFilter = L"Lua bytecode (*.luac)\0*.luac\0所有檔案 (*.*)\0*.*\0";
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
	std::snprintf(statusBuf, statusSz, "已儲存 Lua");
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
		std::snprintf(statusBuf, statusSz, "無法顯示儲存對話框");
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
		std::snprintf(statusBuf, statusSz, "輸出路徑編碼失敗");
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
		std::snprintf(statusBuf, statusSz, "成功寫入: %s", pathUtf8.c_str());
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
		std::snprintf(statusBuf, statusSz, "此編輯分頁尚無紀錄的 .luac 路徑，請先用 F5 編譯並選擇儲存位置");
		return;
	}
	std::string err;
	const std::string luaSrc = LuaEditorGetBufferUtf8();
	LuaBytecode::CompileOptions opt;
	opt.stripDebug = !keepDebug;
	const bool ok = LuaBytecode::CompileUtf8ToFile(std::string_view(luaSrc), target, opt, err);
	if (ok)
		std::snprintf(statusBuf, statusSz, "成功寫入: %s", target.c_str());
	else
		std::snprintf(statusBuf, statusSz, "%s", err.c_str());
}

} // namespace appwindow
