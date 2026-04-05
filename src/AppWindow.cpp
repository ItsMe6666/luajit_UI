#include "AppWindow.h"
#include "AppSettings.h"
#include "GuiSkin.h"
#include "LuaBytecode.h"

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "TextEditor.h"
#include <d3d9.h>

#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <cfloat>
#include <cstdio>
#include <string>
#include <string_view>
#include <algorithm>
#include <unordered_set>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 + D3D9 + ImGui 主視窗：多分頁 Lua 編輯、settings.ini 還原、F5/F6 輸出 .luac。
namespace {

// 執行期全域（單一視窗）；輔助函式皆在此匿名命名空間內。
bool g_running = false;
HWND g_hwnd = nullptr;

LPDIRECT3D9 g_pD3D = nullptr;
LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
bool g_deviceLost = false;
UINT g_resizeW = 0;
UINT g_resizeH = 0;
D3DPRESENT_PARAMETERS g_d3dpp = {};

TextEditor g_luaEditor;
bool g_luaEditorInited = false;

struct LuaDoc {
	std::wstring path;
	std::string text;
	/** 與磁碟一致的最後內容（載入或成功儲存後更新）；用於未儲存提示 */
	std::string lastSavedTextUtf8;
	// 本分頁上次 F5/F6 成功寫入的 .luac 路徑（UTF-8）。
	std::string lastLuacOutPathUtf8;
};

std::vector<LuaDoc> g_docs;
int g_activeDoc = -1;
float g_sidebarWidth = 220.0f;
bool g_keepBytecodeDebug = false;
bool g_requestSavePersist = false;
float g_cachedFontScaleForSave = 1.0f;

std::vector<wchar_t> g_openFileBuf;
std::vector<wchar_t> g_saveFileBuf;
std::vector<wchar_t> g_saveLuacBuf;
bool g_pendingSaveLua = false;
bool g_pendingOpenLua = false;
bool g_pendingCompileBytecode = false;
bool g_pendingCompileBytecodeLastPath = false;
std::vector<std::wstring> g_pendingDropPaths;
// 無作用中分頁時，F6「覆寫上次路徑」仍要記得的 .luac 路徑。
std::string g_orphanLastLuacOutPathUtf8;

/** 側邊欄檔案列多選（索引）；與 g_activeDoc 可並存（Ctrl+點選不會切換編輯分頁） */
std::unordered_set<int> g_sidebarSel;
int g_sidebarAnchor = -1;
/** 最後一次「非 Ctrl」單選的起點，供 Shift+點選延伸範圍（Ctrl+點選不會改此值） */
int g_sidebarShiftAnchor = -1;

void EnsureLuaEditorInited();

void SidebarOnRemovedOne(int removedIdx);
void RemoveDocsSetFromList(const std::unordered_set<int>& which);

// --- 路徑與 UTF-8/寬字元轉換、讀寫檔案 ---

bool WStringEndsWithLuaCI(const std::wstring& p)
{
	if (p.size() < 4)
		return false;
	const wchar_t* e = L".lua";
	for (int i = 0; i < 4; ++i) {
		wchar_t a = p[p.size() - 4 + (size_t)i];
		wchar_t b = e[i];
		if (a >= L'A' && a <= L'Z')
			a = (wchar_t)(a - L'A' + L'a');
		if (b >= L'A' && b <= L'Z')
			b = (wchar_t)(b - L'A' + L'a');
		if (a != b)
			return false;
	}
	return true;
}

bool WStringEndsWithLuacCI(const std::wstring& p)
{
	if (p.size() < 5)
		return false;
	const wchar_t* e = L".luac";
	for (int i = 0; i < 5; ++i) {
		wchar_t a = p[p.size() - 5 + (size_t)i];
		wchar_t b = e[i];
		if (a >= L'A' && a <= L'Z')
			a = (wchar_t)(a - L'A' + L'a');
		if (b >= L'A' && b <= L'Z')
			b = (wchar_t)(b - L'A' + L'a');
		if (a != b)
			return false;
	}
	return true;
}

bool Utf8ToWide(std::string_view utf8, std::wstring& outWide)
{
	outWide.clear();
	if (utf8.empty())
		return true;
	const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
	if (n <= 0)
		return false;
	outWide.resize((size_t)n);
	return MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), outWide.data(), n) > 0;
}

bool WidePathToUtf8(const std::wstring& wpath, std::string& outUtf8)
{
	if (wpath.empty())
		return false;
	const int n = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), (int)wpath.size(), nullptr, 0, nullptr, nullptr);
	if (n <= 0)
		return false;
	outUtf8.resize((size_t)n);
	if (WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), (int)wpath.size(), outUtf8.data(), n, nullptr, nullptr) <= 0)
		return false;
	return true;
}

bool ReadWholeFileUtf8(const std::wstring& wpath, std::string& out, std::string& err)
{
	FILE* fp = nullptr;
	if (_wfopen_s(&fp, wpath.c_str(), L"rb") != 0 || !fp) {
		err = "無法開啟檔案";
		return false;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		err = "無法讀取檔案";
		return false;
	}
	const long sz = ftell(fp);
	if (sz < 0) {
		fclose(fp);
		err = "無法讀取檔案大小";
		return false;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		err = "無法讀取檔案";
		return false;
	}
	out.resize((size_t)sz);
	if (sz > 0 && fread(out.data(), 1, (size_t)sz, fp) != (size_t)sz) {
		fclose(fp);
		err = "讀取失敗";
		return false;
	}
	fclose(fp);
	if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
		out.erase(0, 3);
	return true;
}

bool WriteWholeFileUtf8(const std::wstring& wpath, std::string_view data, std::string& err)
{
	FILE* fp = nullptr;
	if (_wfopen_s(&fp, wpath.c_str(), L"wb") != 0 || !fp) {
		err = "無法寫入檔案";
		return false;
	}
	if (!data.empty() && fwrite(data.data(), 1, data.size(), fp) != data.size()) {
		fclose(fp);
		err = "寫入失敗";
		return false;
	}
	fclose(fp);
	return true;
}

// --- 編輯器緩衝與 g_docs 同步、分頁切換／關閉 ---

/** TextEditor::GetText() 會在結尾多一個 '\\n'，切換分頁反覆 Sync 會不斷長出空白行；改以 GetTextLines 銜接。 */
std::string LuaEditorGetBufferUtf8()
{
	const std::vector<std::string> lines = g_luaEditor.GetTextLines();
	std::string out;
	size_t reserve = 0;
	for (const auto& ln : lines)
		reserve += ln.size() + 1;
	if (reserve > 0)
		--reserve;
	out.reserve(reserve);
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i)
			out += '\n';
		out += lines[i];
	}
	return out;
}

void SyncEditorToActiveDoc()
{
	if (g_activeDoc >= 0 && g_activeDoc < (int)g_docs.size())
		g_docs[(size_t)g_activeDoc].text = LuaEditorGetBufferUtf8();
}

/** 目前緩衝與 lastSavedTextUtf8 不同視為未儲存；作用中分頁讀編輯器，其餘讀 g_docs[].text */
bool DocIsDirty(int docIdx, const std::string* activeEditorUtf8)
{
	if (docIdx < 0 || docIdx >= (int)g_docs.size())
		return false;
	const LuaDoc& d = g_docs[(size_t)docIdx];
	if (docIdx == g_activeDoc && activeEditorUtf8)
		return *activeEditorUtf8 != d.lastSavedTextUtf8;
	return d.text != d.lastSavedTextUtf8;
}

int FindDocByPath(const std::wstring& p)
{
	for (size_t i = 0; i < g_docs.size(); ++i) {
		if (_wcsicmp(g_docs[i].path.c_str(), p.c_str()) == 0)
			return (int)i;
	}
	return -1;
}

void SwitchToDoc(int idx, char* statusBuf, size_t statusSz)
{
	if (idx < 0 || idx >= (int)g_docs.size())
		return;
	EnsureLuaEditorInited();
	SyncEditorToActiveDoc();
	g_activeDoc = idx;
	g_sidebarSel.clear();
	g_sidebarSel.insert(idx);
	g_sidebarAnchor = idx;
	g_sidebarShiftAnchor = idx;
	g_luaEditor.SetText(g_docs[(size_t)g_activeDoc].text);
	if (statusBuf && statusSz)
		std::snprintf(statusBuf, statusSz, "已切換編輯檔");
	g_requestSavePersist = true;
}

void AddOrSelectLuaFile(const std::wstring& wpath, char* statusBuf, size_t statusSz)
{
	EnsureLuaEditorInited();
	if (!WStringEndsWithLuaCI(wpath)) {
		std::snprintf(statusBuf, statusSz, "僅支援拖入或開啟 .lua 檔");
		return;
	}
	const int existing = FindDocByPath(wpath);
	if (existing >= 0) {
		SwitchToDoc(existing, statusBuf, statusSz);
		std::snprintf(statusBuf, statusSz, "已切換至已開啟的檔案");
		return;
	}
	std::string err;
	std::string body;
	if (!ReadWholeFileUtf8(wpath, body, err)) {
		std::snprintf(statusBuf, statusSz, "%s", err.c_str());
		return;
	}
	SyncEditorToActiveDoc();
	LuaDoc d;
	d.path = wpath;
	d.text = std::move(body);
	d.lastSavedTextUtf8 = d.text;
	g_docs.push_back(std::move(d));
	g_activeDoc = (int)g_docs.size() - 1;
	g_sidebarSel.clear();
	g_sidebarSel.insert(g_activeDoc);
	g_sidebarAnchor = g_activeDoc;
	g_sidebarShiftAnchor = g_activeDoc;
	g_luaEditor.SetText(g_docs[(size_t)g_activeDoc].text);
	std::snprintf(statusBuf, statusSz, "已加入 Lua 檔");
	g_requestSavePersist = true;
}

void SidebarOnRemovedOne(int removedIdx)
{
	std::unordered_set<int> nxt;
	for (int s : g_sidebarSel) {
		if (s == removedIdx)
			continue;
		if (s > removedIdx)
			nxt.insert(s - 1);
		else
			nxt.insert(s);
	}
	g_sidebarSel.swap(nxt);
	if (g_sidebarAnchor == removedIdx)
		g_sidebarAnchor = -1;
	else if (g_sidebarAnchor > removedIdx)
		--g_sidebarAnchor;
	if (g_sidebarShiftAnchor == removedIdx)
		g_sidebarShiftAnchor = -1;
	else if (g_sidebarShiftAnchor > removedIdx)
		--g_sidebarShiftAnchor;
}

void RemoveDocsSetFromList(const std::unordered_set<int>& which)
{
	if (which.empty())
		return;
	SyncEditorToActiveDoc();
	std::wstring activePath;
	if (g_activeDoc >= 0 && g_activeDoc < (int)g_docs.size())
		activePath = g_docs[(size_t)g_activeDoc].path;

	std::vector<int> sorted(which.begin(), which.end());
	std::sort(sorted.begin(), sorted.end(), std::greater<int>());
	for (int idx : sorted) {
		if (idx >= 0 && idx < (int)g_docs.size())
			g_docs.erase(g_docs.begin() + idx);
	}

	g_sidebarSel.clear();
	g_sidebarAnchor = -1;
	g_sidebarShiftAnchor = -1;
	g_activeDoc = -1;

	if (g_docs.empty()) {
		g_luaEditor.SetText("");
	} else {
		int newActive = -1;
		if (!activePath.empty()) {
			for (int j = 0; j < (int)g_docs.size(); ++j) {
				if (_wcsicmp(g_docs[(size_t)j].path.c_str(), activePath.c_str()) == 0) {
					newActive = j;
					break;
				}
			}
		}
		if (newActive < 0)
			newActive = 0;
		g_activeDoc = newActive;
		g_sidebarSel.insert(g_activeDoc);
		g_sidebarAnchor = g_activeDoc;
		g_sidebarShiftAnchor = g_activeDoc;
		g_luaEditor.SetText(g_docs[(size_t)g_activeDoc].text);
	}
	g_requestSavePersist = true;
}

void RemoveDocAt(int idx)
{
	if (idx < 0 || idx >= (int)g_docs.size())
		return;
	SyncEditorToActiveDoc();
	g_docs.erase(g_docs.begin() + idx);
	SidebarOnRemovedOne(idx);
	if (g_docs.empty()) {
		g_activeDoc = -1;
		g_sidebarSel.clear();
		g_sidebarAnchor = -1;
		g_sidebarShiftAnchor = -1;
		g_luaEditor.SetText("");
	} else {
		if (g_activeDoc >= (int)g_docs.size())
			g_activeDoc = (int)g_docs.size() - 1;
		else if (idx < g_activeDoc)
			--g_activeDoc;
		if (g_sidebarAnchor < 0) {
			g_sidebarAnchor = g_activeDoc;
		}
		if (g_sidebarShiftAnchor < 0 || g_sidebarShiftAnchor >= (int)g_docs.size()) {
			g_sidebarShiftAnchor = g_activeDoc;
		}
		if (g_sidebarSel.empty()) {
			g_sidebarSel.insert(g_activeDoc);
			g_sidebarAnchor = g_activeDoc;
			g_sidebarShiftAnchor = g_activeDoc;
		}
		g_luaEditor.SetText(g_docs[(size_t)g_activeDoc].text);
	}
	g_requestSavePersist = true;
}

std::wstring FileNameFromPath(const std::wstring& p)
{
	if (p.empty())
		return L"(未命名)";
	const size_t slash = p.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return p;
	return p.substr(slash + 1);
}

void OpenExplorerSelectFile(const std::wstring& wpath)
{
	if (wpath.empty())
		return;
	std::wstring params = L"/select,\"";
	params += wpath;
	params += L"\"";
	ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOW);
}

// --- 編譯輸出路徑、對話框、儲存/編譯 ---

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

// --- Direct3D 9 裝置（ImGui 後端用）---

bool CreateDeviceD3D(HWND hWnd)
{
	if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr)
		return false;

	ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
	g_d3dpp.Windowed = TRUE;
	g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
	g_d3dpp.EnableAutoDepthStencil = TRUE;
	g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
	g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
	g_d3dpp.hDeviceWindow = hWnd;

	if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0) {
		if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
			return false;
	}

	return true;
}

void CleanupDeviceD3D()
{
	if (g_pd3dDevice) {
		g_pd3dDevice->Release();
		g_pd3dDevice = nullptr;
	}
	if (g_pD3D) {
		g_pD3D->Release();
		g_pD3D = nullptr;
	}
}

void ResetDevice()
{
	ImGui_ImplDX9_InvalidateDeviceObjects();
	HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
	if (hr == D3DERR_INVALIDCALL)
		return;
	ImGui_ImplDX9_CreateDeviceObjects();
}

// 一整幀的 ImGui：自訂標題列、選單、側欄檔案列、TextEditor、狀態列與持久化觸發。
void DrawUi()
{
	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->Pos);
	ImGui::SetNextWindowSize(vp->Size);

	const ImGuiStyle& styPreBegin = ImGui::GetStyle();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(styPreBegin.WindowPadding.x, 4.0f));

	ImGui::Begin(
		"LuaJIT_UI_Main",
		nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

	const float titleH = 26.0f;
	const float capBtn = 23.0f;
	const ImVec2 capSize(capBtn, capBtn);
	const ImU32 titleTextCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.82f, 0.90f, 1.0f));
	const float availForTitle = ImGui::GetContentRegionAvail().x;
	const float capGap = 3.0f;
	float dragW = availForTitle - capBtn * 2.0f - capGap - 4.0f;
	if (dragW < 120.0f)
		dragW = 120.0f;
	const ImVec2 dragP0 = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##titleDrag", ImVec2(dragW, titleH));
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && g_hwnd) {
		ReleaseCapture();
		SendMessageW(g_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
	}
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddText(ImVec2(dragP0.x + 10.0f, dragP0.y + (titleH - ImGui::GetFontSize()) * 0.5f), titleTextCol, "luajit_UI");
	}

	const float btnY = dragP0.y + (titleH - capBtn) * 0.5f;
	const float btnX0 = dragP0.x + dragW + 4.0f;

	const ImGuiStyle& capSt = ImGui::GetStyle();
	const ImU32 minN = ImGui::ColorConvertFloat4ToU32(capSt.Colors[ImGuiCol_Button]);
	const ImU32 minH = ImGui::ColorConvertFloat4ToU32(capSt.Colors[ImGuiCol_ButtonHovered]);
	const ImU32 minA = ImGui::ColorConvertFloat4ToU32(capSt.Colors[ImGuiCol_ButtonActive]);
	const ImU32 closeN = ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.18f, 0.22f, 0.85f));
	const ImU32 closeH = ImGui::ColorConvertFloat4ToU32(ImVec4(0.75f, 0.22f, 0.28f, 1.0f));
	const ImU32 closeA = ImGui::ColorConvertFloat4ToU32(ImVec4(0.45f, 0.12f, 0.16f, 1.0f));

	if (GuiSkin::TitleBarIconButton("##capMin", ImVec2(btnX0, btnY), capSize, minN, minH, minA, "\xe2\x80\x94") && g_hwnd)
		ShowWindow(g_hwnd, SW_MINIMIZE);
	if (GuiSkin::TitleBarIconButton("##capClose", ImVec2(btnX0 + capBtn + capGap, btnY), capSize, closeN, closeH, closeA, "X") && g_hwnd)
		PostMessageW(g_hwnd, WM_CLOSE, 0, 0);

	static char s_status[2048] = "";

	EnsureLuaEditorInited();
	g_cachedFontScaleForSave = ImGui::GetIO().FontGlobalScale;

	if (!g_pendingDropPaths.empty()) {
		for (const auto& dp : g_pendingDropPaths)
			AddOrSelectLuaFile(dp, s_status, sizeof(s_status));
		g_pendingDropPaths.clear();
	}
	if (g_pendingSaveLua) {
		g_pendingSaveLua = false;
		if (g_hwnd)
			TrySaveLuaSource(g_hwnd, s_status, sizeof(s_status));
	}
	if (g_pendingOpenLua) {
		g_pendingOpenLua = false;
		if (g_hwnd && PickOpenLuaPath(g_hwnd))
			AddOrSelectLuaFile(std::wstring(g_openFileBuf.data()), s_status, sizeof(s_status));
	}

	if (g_pendingCompileBytecode) {
		g_pendingCompileBytecode = false;
		if (g_hwnd)
			TryCompileBytecode(g_hwnd, g_keepBytecodeDebug, s_status, sizeof(s_status));
	}
	if (g_pendingCompileBytecodeLastPath) {
		g_pendingCompileBytecodeLastPath = false;
		TryCompileBytecodeLastPath(g_keepBytecodeDebug, s_status, sizeof(s_status));
	}

	const float bodyH = (std::max)(1.0f, ImGui::GetContentRegionAvail().y);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(styPreBegin.WindowPadding.x, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
	ImGui::BeginChild(
		"##mainBody",
		ImVec2(0.0f, bodyH),
		ImGuiChildFlags_None,
		ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings);

	if (ImGui::BeginMenuBar()) {
		const ImGuiStyle& menuSt = ImGui::GetStyle();
		const float rowW1 = ImGui::CalcTextSize("開啟 .lua").x + ImGui::CalcTextSize("Ctrl+O").x;
		const float rowW2 = ImGui::CalcTextSize("儲存 .lua").x + ImGui::CalcTextSize("Ctrl+S").x;
		const float rowW3 = ImGui::CalcTextSize("編譯 bytecode").x + ImGui::CalcTextSize("F5/F6").x;
		const float menuMinW = (std::max)((std::max)(rowW1, rowW2), rowW3) + menuSt.ItemInnerSpacing.x * 6.0f + menuSt.WindowPadding.x * 2.0f + 56.0f;
		ImGui::SetNextWindowSizeConstraints(ImVec2(menuMinW, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 14.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 11.0f));
		if (ImGui::BeginMenu("檔案")) {
			if (ImGui::MenuItem("開啟 .lua", "Ctrl+O")) {
				if (g_hwnd && PickOpenLuaPath(g_hwnd))
					AddOrSelectLuaFile(std::wstring(g_openFileBuf.data()), s_status, sizeof(s_status));
			}
			if (ImGui::MenuItem("儲存 .lua", "Ctrl+S")) {
				if (g_hwnd)
					TrySaveLuaSource(g_hwnd, s_status, sizeof(s_status));
			}
			ImGui::Separator();
			if (ImGui::MenuItem("編譯 bytecode", "F5/F6") && g_hwnd)
				TryCompileBytecode(g_hwnd, g_keepBytecodeDebug, s_status, sizeof(s_status));
			ImGui::EndMenu();
		}
		ImGui::PopStyleVar(3);

		const float setMenuMinW =
			(std::max)(
				(std::max)(ImGui::CalcTextSize(u8"［ ］保留除錯資訊").x, ImGui::CalcTextSize(u8"［O］保留除錯資訊").x),
				ImGui::CalcTextSize(u8"介面縮放").x + 120.0f)
			+ menuSt.WindowPadding.x * 2.0f + 48.0f;
		ImGui::SetNextWindowSizeConstraints(ImVec2(setMenuMinW, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 14.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 11.0f));
		if (ImGui::BeginMenu(u8"設定")) {
			ImGuiIO& ioSet = ImGui::GetIO();
			float fs = ioSet.FontGlobalScale;
			if (ImGui::SliderFloat(u8"介面縮放", &fs, 0.75f, 2.0f, "%.2f"))
				ioSet.FontGlobalScale = fs;
			if (ImGui::IsItemDeactivatedAfterEdit())
				g_requestSavePersist = true;
			ImGui::Separator();
			const char* dbgLabel = g_keepBytecodeDebug ? u8"［O］保留除錯資訊" : u8"［ ］保留除錯資訊";
			if (ImGui::MenuItem(dbgLabel)) {
				g_keepBytecodeDebug = !g_keepBytecodeDebug;
				g_requestSavePersist = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					u8"［ ］：等同 luajit -b -s（strip）預設。\n"
					u8"［O］：等同 luajit -b -g，bytecode 含行號等除錯資訊。");
			ImGui::EndMenu();
		}
		ImGui::PopStyleVar(3);
		ImGui::EndMenuBar();
	}

	const float rowAvailH = ImGui::GetContentRegionAvail().y;
	const ImGuiStyle& edSt = ImGui::GetStyle();
	float statusReserve = 0.0f;
	if (s_status[0] != '\0')
		statusReserve = edSt.ItemSpacing.y + ImGui::GetTextLineHeightWithSpacing() * 2.0f;
	const float editorRowH = (std::max)(1.0f, rowAvailH - statusReserve);

	g_sidebarWidth = std::clamp(g_sidebarWidth, 120.0f, 640.0f);

	ImGui::BeginChild("##fileSidebarCol", ImVec2(g_sidebarWidth, editorRowH), ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings);
	ImGui::SeparatorText(u8"Lua 檔案");
	const float fileListH = (std::max)(1.0f, ImGui::GetContentRegionAvail().y);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	/* 預設 WindowPadding 會讓列背景無法貼齊子視窗左右邊框 */
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, edSt.WindowPadding.y));
	ImGui::BeginChild("##fileList", ImVec2(-1.0f, fileListH), ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
	{
		ImGuiIO& ioList = ImGui::GetIO();
		if (ImGui::IsWindowFocused() && ioList.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A) && !ioList.KeyShift && !g_docs.empty()) {
			g_sidebarSel.clear();
			for (int j = 0; j < (int)g_docs.size(); ++j)
				g_sidebarSel.insert(j);
			g_sidebarAnchor = (int)g_docs.size() - 1;
			g_sidebarShiftAnchor = (g_activeDoc >= 0 && g_activeDoc < (int)g_docs.size()) ? g_activeDoc : 0;
		}
	}
	int pendingRemove = -1;
	const float rowPadX = 8.0f;
	const float closeSz = 20.0f;
	const float closePadRight = 6.0f;
	/* 列高用字行高 + 少量內距；列與列之間再用較小的 ItemSpacing.y（勿用 GetTextLineHeightWithSpacing 當列高，否則會疊上兩次 ItemSpacing） */
	const float rowH = ImGui::GetTextLineHeight() + 4.0f;
	const float rowGapY = 3.0f;
	/* Popup 的 WindowRounding / WindowPadding 在 BeginPopup 內讀取，須在 BeginPopupContextItem 之前 push */
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(edSt.ItemSpacing.x, rowGapY));
	std::string activeEditorUtf8Snapshot;
	if (g_activeDoc >= 0 && g_activeDoc < (int)g_docs.size())
		activeEditorUtf8Snapshot = LuaEditorGetBufferUtf8();
	for (size_t i = 0; i < g_docs.size(); ++i) {
		ImGui::PushID((int)i);
		const int ii = (int)i;
		const bool rowActive = (ii == g_activeDoc);
		const bool rowInSel = g_sidebarSel.count(ii) != 0;
		const bool dirty = DocIsDirty(ii, &activeEditorUtf8Snapshot);
		const std::wstring fnameW = FileNameFromPath(g_docs[i].path);
		std::string fnameU8;
		if (!WidePathToUtf8(fnameW, fnameU8))
			fnameU8 = "(?)";

		const float fullW = ImGui::GetContentRegionAvail().x;
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1 = ImVec2(p0.x + fullW, p0.y + rowH);
		const float xLeft = p1.x - closeSz - closePadRight;

		ImGui::InvisibleButton("##row", ImVec2(fullW, rowH));
		const bool rowHover = ImGui::IsItemHovered();
		const ImVec2 mouse = ImGui::GetMousePos();
		const bool onClose = rowHover && mouse.x >= xLeft && mouse.x < p1.x && mouse.y >= p0.y && mouse.y < p1.y;

		ImDrawList* dl = ImGui::GetWindowDrawList();
		if (rowActive)
			dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_HeaderActive));
		else if (rowInSel)
			dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_Header));
		else if (rowHover)
			dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_HeaderHovered));

		dl->PushClipRect(p0, ImVec2((std::max)(p0.x + 1.0f, xLeft - 4.0f), p1.y), true);
		const float ty = p0.y + (rowH - ImGui::GetFontSize()) * 0.5f;
		/* 未儲存：檔名改黃字提示（底色維持一般列樣式） */
		const ImU32 fnameCol = dirty ? IM_COL32(255, 220, 72, 255) : ImGui::GetColorU32(ImGuiCol_Text);
		dl->AddText(ImVec2(p0.x + rowPadX, ty), fnameCol, fnameU8.c_str());
		dl->PopClipRect();

		if (rowHover) {
			const ImVec2 xP0(xLeft, p0.y + (rowH - closeSz) * 0.5f);
			const bool xHot = onClose;
			const ImVec2 ts = ImGui::CalcTextSize("X");
			const ImU32 redX = xHot ? IM_COL32(255, 110, 118, 255) : IM_COL32(220, 72, 82, 255);
			dl->AddText(ImVec2(xP0.x + (closeSz - ts.x) * 0.5f, xP0.y + (closeSz - ts.y) * 0.5f), redX, "X");
		}

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
			ImGuiIO& ioRow = ImGui::GetIO();
			const bool rowShift = (ioRow.KeyMods & ImGuiMod_Shift) != 0 || ioRow.KeyShift;
			if (onClose) {
				pendingRemove = ii;
			} else if (rowShift) {
				int anchor = g_sidebarShiftAnchor;
				if (anchor < 0 || anchor >= (int)g_docs.size())
					anchor = g_sidebarAnchor;
				if (anchor >= 0 && anchor < (int)g_docs.size()) {
					const int a = (std::min)(anchor, ii);
					const int b = (std::max)(anchor, ii);
					g_sidebarSel.clear();
					for (int j = a; j <= b; ++j)
						g_sidebarSel.insert(j);
					EnsureLuaEditorInited();
					SyncEditorToActiveDoc();
					g_activeDoc = ii;
					g_sidebarAnchor = ii;
					g_luaEditor.SetText(g_docs[(size_t)g_activeDoc].text);
					std::snprintf(s_status, sizeof(s_status), u8"已選取範圍並切換編輯檔");
					g_requestSavePersist = true;
				} else {
					SwitchToDoc(ii, s_status, sizeof(s_status));
				}
			} else if (ioRow.KeyCtrl) {
				if (g_sidebarSel.count(ii))
					g_sidebarSel.erase(ii);
				else
					g_sidebarSel.insert(ii);
				g_sidebarAnchor = ii;
			} else {
				SwitchToDoc(ii, s_status, sizeof(s_status));
			}
		}

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 8.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
		if (ImGui::BeginPopupContextItem()) {
			const bool hasPath = !g_docs[i].path.empty();
			if (ImGui::MenuItem(u8"開啟檔案所在目錄", nullptr, false, hasPath))
				OpenExplorerSelectFile(g_docs[i].path);
			const int selN = (int)g_sidebarSel.size();
			const bool inSel = g_sidebarSel.count(ii) != 0;
			char rmLabel[96];
			if (inSel && selN > 1)
				std::snprintf(rmLabel, sizeof(rmLabel), u8"從清單移除已選 (%d)", selN);
			else
				std::snprintf(rmLabel, sizeof(rmLabel), u8"從清單移除");
			if (ImGui::MenuItem(rmLabel)) {
				if (inSel && selN > 1)
					RemoveDocsSetFromList(g_sidebarSel);
				else
					pendingRemove = ii;
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar(3);
		ImGui::PopID();
	}
	ImGui::PopStyleVar(2);
	ImGui::EndChild();
	ImGui::PopStyleVar(2);

	ImGui::EndChild();

	if (pendingRemove >= 0)
		RemoveDocAt(pendingRemove);

	ImGui::SameLine();
	const float splitW = 8.0f;
	const ImVec2 splitP0 = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##sidebarSplit", ImVec2(splitW, editorRowH));
	const bool splitHover = ImGui::IsItemHovered();
	const bool splitActive = ImGui::IsItemActive();
	if (splitHover || splitActive)
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	if (splitActive)
		g_sidebarWidth += ImGui::GetIO().MouseDelta.x;
	g_sidebarWidth = std::clamp(g_sidebarWidth, 120.0f, 640.0f);
	if (ImGui::IsItemDeactivatedAfterEdit())
		g_requestSavePersist = true;
	{
		const ImVec2 splitP1 = ImVec2(splitP0.x + splitW, splitP0.y + editorRowH);
		const float midX = (splitP0.x + splitP1.x) * 0.5f;
		ImDrawList* sdl = ImGui::GetWindowDrawList();
		const ImU32 splitCol = ImGui::GetColorU32(
			splitActive ? ImGuiCol_SeparatorActive : splitHover ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
		sdl->AddLine(ImVec2(midX, splitP0.y), ImVec2(midX, splitP1.y), splitCol, splitActive ? 2.0f : 1.0f);
	}
	ImGui::SameLine();
	ImGui::BeginChild("##editorColumn", ImVec2(0.0f, editorRowH), ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings);

	ImGui::SeparatorText("Lua 原始碼");
	/* 與左欄 ##fileList 相同：用 SeparatorText 之後的剩餘高度，勿再扣 ItemSpacing，否則兩框底部不對齊 */
	float editorH = ImGui::GetContentRegionAvail().y;
	editorH = (std::max)(1.0f, editorH);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
	/* 頂端留白在「編輯器子視窗內」：略過 TextEditor 內建 BeginChild，改由外層包一層（不修改 vendor） */
	const float kLuaEditorInnerTopPad = 8.0f;
	const ImGuiWindowFlags luaScrollFlags =
		ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NoMove;
	g_luaEditor.SetImGuiChildIgnored(true);
	ImGui::BeginChild("##luaSrcShell", ImVec2(-1.0f, editorH), ImGuiChildFlags_Borders, luaScrollFlags);
	ImGui::Dummy(ImVec2(0.0f, kLuaEditorInnerTopPad));
	const float innerH = (std::max)(40.0f, ImGui::GetContentRegionAvail().y);
	g_luaEditor.Render("##luaSrc", ImVec2(-1.0f, innerH), false);
	ImGui::EndChild();
	g_luaEditor.SetImGuiChildIgnored(false);
	ImGui::PopStyleVar(2);

	ImGui::EndChild();

	if (s_status[0] != '\0') {
		ImGui::Spacing();
		ImGui::TextWrapped("%s", s_status);
	}

	if (g_requestSavePersist) {
		g_requestSavePersist = false;
		SavePersistNow();
	}

	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	ImGui::End();
	ImGui::PopStyleVar();
}

void ApplyPrimaryWindowCornerPreference(HWND hwnd);

// 快捷鍵、拖放 .lua、邊緣縮放命中測試、關閉前寫入設定；其餘交給 ImGui。
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
		if (wParam == 'S' || wParam == 's') {
			g_pendingSaveLua = true;
			return 0;
		}
		if (wParam == 'O' || wParam == 'o') {
			g_pendingOpenLua = true;
			return 0;
		}
	}
	if (msg == WM_KEYDOWN && wParam == VK_F5) {
		g_pendingCompileBytecode = true;
		return 0;
	}
	if (msg == WM_KEYDOWN && wParam == VK_F6) {
		g_pendingCompileBytecodeLastPath = true;
		return 0;
	}

	if (msg == WM_DROPFILES) {
		HDROP hDrop = (HDROP)wParam;
		const UINT cnt = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
		for (UINT fi = 0; fi < cnt; ++fi) {
			wchar_t pathBuf[4096];
			const UINT n = DragQueryFileW(hDrop, fi, pathBuf, (UINT)(sizeof(pathBuf) / sizeof(pathBuf[0])));
			if (n > 0 && WStringEndsWithLuaCI(std::wstring(pathBuf)))
				g_pendingDropPaths.push_back(std::wstring(pathBuf));
		}
		DragFinish(hDrop);
		return 0;
	}

	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
		return true;

	switch (msg) {
	case WM_NCHITTEST: {
		if (IsZoomed(hwnd))
			break;
		POINT pt = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
		ScreenToClient(hwnd, &pt);
		RECT rc{};
		GetClientRect(hwnd, &rc);
		const int edge = 8;
		const bool left = pt.x < edge;
		const bool right = pt.x >= rc.right - edge;
		const bool top = pt.y < edge;
		const bool bottom = pt.y >= rc.bottom - edge;
		if (top && left)
			return HTTOPLEFT;
		if (top && right)
			return HTTOPRIGHT;
		if (bottom && left)
			return HTBOTTOMLEFT;
		if (bottom && right)
			return HTBOTTOMRIGHT;
		if (top)
			return HTTOP;
		if (bottom)
			return HTBOTTOM;
		if (left)
			return HTLEFT;
		if (right)
			return HTRIGHT;
		break;
	}
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		g_resizeW = (UINT)LOWORD(lParam);
		g_resizeH = (UINT)HIWORD(lParam);
		ApplyPrimaryWindowCornerPreference(hwnd);
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU)
			return 0;
		break;
	case WM_CLOSE:
		SyncEditorToActiveDoc();
		{
			AppPersistState s;
			s.fontGlobalScale = g_cachedFontScaleForSave;
			s.sidebarWidth = g_sidebarWidth;
			s.keepBytecodeDebug = g_keepBytecodeDebug;
			AppendPersistFileSlots(s);
			AppSettings_Save(hwnd, s);
		}
		g_running = false;
		return 0;
	case WM_DESTROY:
		DragAcceptFiles(hwnd, FALSE);
		g_running = false;
		return 0;
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/** 與 ImGui 圓角搭配：勿使用 DWMWCP_DONOTROUND，否則 HWND 為直角、內容圓角時四角會露出楔形。 */
void ApplyPrimaryWindowCornerPreference(HWND hwnd)
{
	if (!hwnd)
		return;
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
	const DWORD pref = DWMWCP_ROUND;
	(void)DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

} // namespace

namespace AppWindow {

bool Run()
{
	// 註冊視窗類別、建立 HWND/D3D、ImGui 初始化、主訊息迴圈與清理。
	AppPersistState bootPersist;
	const bool bootOk = AppSettings_Load(bootPersist);

	static constexpr wchar_t kClassName[] = L"LuaJIT_UI_ImGuiDx9";

	WNDCLASSW wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = kClassName;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

	if (!RegisterClassW(&wc))
		return false;

	HWND hwnd = CreateWindowExW(
		WS_EX_APPWINDOW,
		kClassName,
		L"luajit_UI",
		WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		1600,
		900,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr);

	if (!hwnd) {
		UnregisterClassW(kClassName, wc.hInstance);
		return false;
	}

	ApplyPrimaryWindowCornerPreference(hwnd);

	g_hwnd = hwnd;

	if (bootOk) {
		const int rw = bootPersist.normalRect.right - bootPersist.normalRect.left;
		const int rh = bootPersist.normalRect.bottom - bootPersist.normalRect.top;
		if (rw >= 200 && rh >= 200) {
			WINDOWPLACEMENT wp = {};
			wp.length = sizeof(WINDOWPLACEMENT);
			wp.flags = 0;
			wp.showCmd = bootPersist.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
			wp.rcNormalPosition = bootPersist.normalRect;
			wp.ptMinPosition.x = -1;
			wp.ptMinPosition.y = -1;
			wp.ptMaxPosition.x = -1;
			wp.ptMaxPosition.y = -1;
			SetWindowPlacement(hwnd, &wp);
		}
	}

	g_openFileBuf.resize(65536);
	g_saveFileBuf.resize(65536);
	g_saveLuacBuf.resize(65536);
	g_openFileBuf[0] = L'\0';
	g_saveFileBuf[0] = L'\0';
	g_saveLuacBuf[0] = L'\0';
	DragAcceptFiles(hwnd, TRUE);

	ImGui_ImplWin32_EnableDpiAwareness();

	if (!CreateDeviceD3D(hwnd)) {
		CleanupDeviceD3D();
		DragAcceptFiles(hwnd, FALSE);
		g_hwnd = nullptr;
		DestroyWindow(hwnd);
		UnregisterClassW(kClassName, wc.hInstance);
		MessageBoxW(nullptr, L"無法建立 Direct3D 9 裝置", L"luajit_UI", MB_OK | MB_ICONERROR);
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	GuiSkin::ApplyStyle();
	GuiSkin::LoadCjkFont();

	if (bootOk) {
		io.FontGlobalScale = bootPersist.fontGlobalScale;
		g_cachedFontScaleForSave = bootPersist.fontGlobalScale;
		g_sidebarWidth = bootPersist.sidebarWidth;
		g_keepBytecodeDebug = bootPersist.keepBytecodeDebug;
	}

	EnsureLuaEditorInited();
	if (bootOk && !bootPersist.openLuaPaths.empty()) {
		g_docs.clear();
		g_activeDoc = -1;
		for (size_t j = 0; j < bootPersist.openLuaPaths.size(); ++j) {
			const std::wstring& wpth = bootPersist.openLuaPaths[j];
			if (GetFileAttributesW(wpth.c_str()) == INVALID_FILE_ATTRIBUTES)
				continue;
			std::string body, err;
			if (!ReadWholeFileUtf8(wpth, body, err))
				continue;
			LuaDoc d;
			d.path = wpth;
			d.text = std::move(body);
			d.lastSavedTextUtf8 = d.text;
			if (j < bootPersist.lastLuacOutPathsWide.size()) {
				const std::wstring& lw = bootPersist.lastLuacOutPathsWide[j];
				if (!lw.empty())
					WidePathToUtf8(lw, d.lastLuacOutPathUtf8);
			}
			g_docs.push_back(std::move(d));
		}
		if (!g_docs.empty()) {
			g_activeDoc = std::clamp(bootPersist.activeLuaIndex, 0, (int)g_docs.size() - 1);
			g_sidebarSel.clear();
			g_sidebarSel.insert(g_activeDoc);
			g_sidebarAnchor = g_activeDoc;
			g_sidebarShiftAnchor = g_activeDoc;
			g_luaEditor.SetText(g_docs[(size_t)g_activeDoc].text);
		}
	}

	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX9_Init(g_pd3dDevice);

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);

	g_running = true;
	while (g_running) {
		MSG msg = {};
		while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		bool canRender = true;
		if (g_deviceLost) {
			HRESULT hrLost = g_pd3dDevice->TestCooperativeLevel();
			if (hrLost == D3DERR_DEVICELOST) {
				Sleep(10);
				canRender = false;
			}
			if (hrLost == D3DERR_DEVICENOTRESET)
				ResetDevice();
			g_deviceLost = false;
		}

		if (g_resizeW != 0 && g_resizeH != 0) {
			g_d3dpp.BackBufferWidth = g_resizeW;
			g_d3dpp.BackBufferHeight = g_resizeH;
			g_resizeW = g_resizeH = 0;
			ResetDevice();
		}

		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		DrawUi();
		ImGui::EndFrame();
		ImGui::Render();

		if (canRender) {
			const ImVec4& wb = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
			const D3DCOLOR clearDx = D3DCOLOR_RGBA(
				(int)(wb.x * 255.0f + 0.5f),
				(int)(wb.y * 255.0f + 0.5f),
				(int)(wb.z * 255.0f + 0.5f),
				255);
			g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
			g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
			g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
			g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clearDx, 1.0f, 0);

			if (g_pd3dDevice->BeginScene() >= 0) {
				ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
				g_pd3dDevice->EndScene();
			}

			HRESULT hrPresent = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
			if (hrPresent == D3DERR_DEVICELOST)
				g_deviceLost = true;
		}

		Sleep(1);
	}

	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	DestroyWindow(hwnd);
	g_hwnd = nullptr;
	UnregisterClassW(kClassName, wc.hInstance);
	return true;
}

} // namespace AppWindow
