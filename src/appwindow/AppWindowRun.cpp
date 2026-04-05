#include "AppWindow.h"
#include "appwindow/AppWindowInternal.h"
#include "AppSettings.h"
#include "appwindow/GuiSkin.h"
#include "appwindow/I18n.h"

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

#include <algorithm>
#include <string>

namespace AppWindow {

// 建立主視窗、載入設定與文件，執行訊息與繪製主迴圈
bool Run()
{
	using namespace appwindow;

	AppPersistState bootPersist;
	const bool bootOk = AppSettings_Load(bootPersist);
	AppLanguageSetCurrent(bootOk ? bootPersist.uiLanguage : AppLanguage::En);

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
		MessageBoxW(nullptr, TrW(I18nSysW::D3dCreateFailed), L"luajit_UI", MB_OK | MB_ICONERROR);
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
