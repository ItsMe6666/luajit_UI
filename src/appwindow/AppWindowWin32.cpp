#include "appwindow/AppWindowInternal.h"
#include "AppSettings.h"

#include "imgui.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace appwindow {

// 主視窗訊息：快捷鍵、拖放、邊框調整大小、關閉時持久化
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

// 透過 DWM 將主視窗設為圓角
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

} // namespace appwindow
