#include "appwindow/AppWindowInternal.h"

#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace appwindow {

// 將編輯器各行合併為單一 UTF-8 字串
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

// 把編輯器內容寫回目前作用中的文件緩衝
void SyncEditorToActiveDoc()
{
	if (g_activeDoc >= 0 && g_activeDoc < (int)g_docs.size())
		g_docs[(size_t)g_activeDoc].text = LuaEditorGetBufferUtf8();
}

// 判斷文件是否與上次儲存內容不同（作用中文件可傳入編輯器快照）
bool DocIsDirty(int docIdx, const std::string* activeEditorUtf8)
{
	if (docIdx < 0 || docIdx >= (int)g_docs.size())
		return false;
	const LuaDoc& d = g_docs[(size_t)docIdx];
	if (docIdx == g_activeDoc && activeEditorUtf8)
		return *activeEditorUtf8 != d.lastSavedTextUtf8;
	return d.text != d.lastSavedTextUtf8;
}

// 依路徑（不分大小寫）尋找已開啟文件索引，找不到回傳 -1
int FindDocByPath(const std::wstring& p)
{
	for (size_t i = 0; i < g_docs.size(); ++i) {
		if (_wcsicmp(g_docs[i].path.c_str(), p.c_str()) == 0)
			return (int)i;
	}
	return -1;
}

// 切換至指定文件並載入編輯器，更新側欄選取
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

// 開啟或選取 .lua：已開則切換，否則讀檔加入清單
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

// 側欄移除一筆文件後，調整多選與錨點索引
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

// 從清單一次移除多個索引，並重選作用中文件
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

// 移除單一文件並更新作用中與側欄狀態
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

// 由完整路徑取出檔名；空路徑顯示未命名
std::wstring FileNameFromPath(const std::wstring& p)
{
	if (p.empty())
		return L"(未命名)";
	const size_t slash = p.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return p;
	return p.substr(slash + 1);
}

// 在檔案總管中開啟並選取指定檔案
void OpenExplorerSelectFile(const std::wstring& wpath)
{
	if (wpath.empty())
		return;
	std::wstring params = L"/select,\"";
	params += wpath;
	params += L"\"";
	ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOW);
}

} // namespace appwindow
