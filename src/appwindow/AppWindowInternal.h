#pragma once

#include "AppSettings.h"
#include "TextEditor.h"

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace appwindow {

struct LuaDoc {
	std::wstring path;
	std::string text;
	std::string lastSavedTextUtf8;
	std::string lastLuacOutPathUtf8;
};

extern bool g_running;
extern HWND g_hwnd;

extern LPDIRECT3D9 g_pD3D;
extern LPDIRECT3DDEVICE9 g_pd3dDevice;
extern bool g_deviceLost;
extern UINT g_resizeW;
extern UINT g_resizeH;
extern D3DPRESENT_PARAMETERS g_d3dpp;

extern TextEditor g_luaEditor;
extern bool g_luaEditorInited;

extern std::vector<LuaDoc> g_docs;
extern int g_activeDoc;
extern float g_sidebarWidth;
extern float g_logPanelHeight;
extern bool g_keepBytecodeDebug;
extern std::string g_afterBuildScriptUtf8;
extern bool g_requestSavePersist;
extern float g_cachedFontScaleForSave;

extern std::vector<wchar_t> g_openFileBuf;
extern std::vector<wchar_t> g_saveFileBuf;
extern std::vector<wchar_t> g_saveLuacBuf;
extern bool g_pendingSaveLua;
extern bool g_pendingOpenLua;
extern bool g_pendingCompileBytecode;
extern bool g_pendingCompileBytecodeLastPath;
extern std::vector<std::wstring> g_pendingDropPaths;
extern std::string g_orphanLastLuacOutPathUtf8;

extern std::string g_appLogUtf8;
extern bool g_appLogScrollToBottom;
extern std::string g_pendingAfterBuildLogUtf8;

extern std::unordered_set<int> g_sidebarSel;
extern int g_sidebarAnchor;
extern int g_sidebarShiftAnchor;

// --- 路徑／檔案 ---
// 不區分大小寫判斷寬字串是否以 .lua 結尾
bool WStringEndsWithLuaCI(const std::wstring& p);
// 不區分大小寫判斷寬字串是否以 .luac 結尾
bool WStringEndsWithLuacCI(const std::wstring& p);
// UTF-8 轉寬字元（UTF-16）
bool Utf8ToWide(std::string_view utf8, std::wstring& outWide);
// 寬字元路徑轉 UTF-8
bool WidePathToUtf8(const std::wstring& wpath, std::string& outUtf8);
// 二進位讀取整檔為字串，成功時略過 UTF-8 BOM
bool ReadWholeFileUtf8(const std::wstring& wpath, std::string& out, std::string& err);
// 將資料原樣寫入檔案（二進位）
bool WriteWholeFileUtf8(const std::wstring& wpath, std::string_view data, std::string& err);

// --- 文件／編輯器 ---
// 首次設定 Lua 語法、配色與編輯器選項
void EnsureLuaEditorInited();
// 將編輯器各行合併為單一 UTF-8 字串
std::string LuaEditorGetBufferUtf8();
// 把編輯器內容寫回目前作用中的文件緩衝
void SyncEditorToActiveDoc();
// SetText 後將作用中文件的 text／lastSaved 對齊編輯器序列化結果（例如 CRLF→LF），避免誤判為已修改
void BaselineActiveDocFromEditor();
// 判斷文件是否與上次儲存內容不同（作用中文件可傳入編輯器快照）
bool DocIsDirty(int docIdx, const std::string* activeEditorUtf8);
// 依路徑（不分大小寫）尋找已開啟文件索引，找不到回傳 -1
int FindDocByPath(const std::wstring& p);
// 切換至指定文件並載入編輯器，更新側欄選取
void SwitchToDoc(int idx, char* statusBuf, size_t statusSz);
// 開啟或選取 .lua：已開則切換，否則讀檔加入清單
void AddOrSelectLuaFile(const std::wstring& wpath, char* statusBuf, size_t statusSz);
// 側欄移除一筆文件後，調整多選與錨點索引
void SidebarOnRemovedOne(int removedIdx);
// 從清單一次移除多個索引，並重選作用中文件
void RemoveDocsSetFromList(const std::unordered_set<int>& which);
// 移除單一文件並更新作用中與側欄狀態
void RemoveDocAt(int idx);
// 由完整路徑取出檔名；空路徑顯示未命名
std::wstring FileNameFromPath(const std::wstring& p);
// 在檔案總管中開啟並選取指定檔案
void OpenExplorerSelectFile(const std::wstring& wpath);
// 追加一行／一段 UTF-8 到主視窗日誌（會捲到底、總量上限約 60KB）
void AppendAppLogUtf8(std::string_view textUtf8);

// --- 儲存／編譯／持久化 ---
// 依目前作用中文件預填「另存 luac」緩衝區
void PrepareLuacSaveDialogInitialPath();
// 將已開啟 Lua 路徑與對應 last luac 路徑填入持久化結構
void AppendPersistFileSlots(AppPersistState& s);
// 同步編輯器後立即寫入視窗／文件相關設定檔
void SavePersistNow();
// 顯示開啟檔對話框，路徑寫入 g_openFileBuf
bool PickOpenLuaPath(HWND owner);
// 顯示另存 .lua 對話框，路徑寫入 g_saveFileBuf
bool PickSaveLuaPath(HWND owner);
// 顯示另存 .luac 對話框，路徑寫入 g_saveLuacBuf
bool PickSaveLuacPath(HWND owner);
// 儲存目前 Lua 原始碼；無路徑時先跳出另存對話框
void TrySaveLuaSource(HWND owner, char* statusBuf, size_t statusSz);
// 編譯目前內容為 bytecode，經對話框選輸出路徑並更新紀錄
void TryCompileBytecode(HWND owner, bool keepDebug, char* statusBuf, size_t statusSz);
// 使用上次記錄的 .luac 路徑重新編譯（無對話框）
void TryCompileBytecodeLastPath(bool keepDebug, char* statusBuf, size_t statusSz);
// 編譯成功後執行設定中的批次內容（暫存檔＋環境變數 OUTPATH／OUTDIR／OUTNAME）
void RunAfterBuildHook(const std::string& luacOutPathUtf8, char* statusBuf, size_t statusSz);

// --- D3D9 ---
// 建立 D3D9 裝置與呈現參數（供 ImGui 繪製）
bool CreateDeviceD3D(HWND hWnd);
// 釋放 D3D9 裝置與 IDirect3D9
void CleanupDeviceD3D();
// 重設 D3D9 裝置並重建 ImGui DX9 繪製物件
void ResetDevice();

// --- UI / Win32 ---
// 繪製主視窗 ImGui：標題列、選單、側欄、編輯器與狀態列
void DrawUi();
// 透過 DWM 將主視窗設為圓角
void ApplyPrimaryWindowCornerPreference(HWND hwnd);
// 主視窗訊息：快捷鍵、拖放、邊框調整大小、關閉時持久化
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

} // namespace appwindow
