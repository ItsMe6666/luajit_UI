#pragma once

#include "appwindow/I18n.h"

#include <Windows.h>

#include <string>
#include <vector>

// 關閉後要還原的狀態（與執行檔同目錄的 settings.ini）。
struct AppPersistState {
	RECT normalRect{ 0, 0, 0, 0 };
	bool maximized = false;
	float fontGlobalScale = 1.0f;
	float sidebarWidth = 220.0f;
	bool keepBytecodeDebug = false;
	AppLanguage uiLanguage = AppLanguage::En;
	std::vector<std::wstring> openLuaPaths;
	// 與 openLuaPaths 一一對應：各分頁上次 F5/F6 輸出的 .luac 路徑（寬字元）。
	std::vector<std::wstring> lastLuacOutPathsWide;
	int activeLuaIndex = 0;
};

bool AppSettings_Load(AppPersistState& out); // 讀取 ini；檔案不存在則回 false
void AppSettings_Save(HWND hwnd, const AppPersistState& state); // 寫入 ini（視窗位置優先取自 hwnd）
