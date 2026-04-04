#include "AppWindow.h"
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
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

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

std::wstring g_luaSourcePathWide;
std::vector<wchar_t> g_openFileBuf;
std::vector<wchar_t> g_saveFileBuf;
std::vector<wchar_t> g_saveLuacBuf;
bool g_pendingSaveLua = false;
bool g_pendingOpenLua = false;
bool g_pendingCompileBytecode = false;
bool g_pendingCompileBytecodeLastPath = false;
bool g_hasPendingDrop = false;
std::string g_lastLuacOutPathUtf8;
std::wstring g_pendingDropPath;

void EnsureLuaEditorInited();

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

void LoadLuaFromPath(const std::wstring& wpath, char* statusBuf, size_t statusSz)
{
	EnsureLuaEditorInited();
	if (!WStringEndsWithLuaCI(wpath)) {
		std::snprintf(statusBuf, statusSz, "僅支援拖入或開啟 .lua 檔");
		return;
	}
	std::string err;
	std::string body;
	if (!ReadWholeFileUtf8(wpath, body, err)) {
		std::snprintf(statusBuf, statusSz, "%s", err.c_str());
		return;
	}
	g_luaEditor.SetText(body);
	g_luaSourcePathWide = wpath;
	std::snprintf(statusBuf, statusSz, "已開啟 Lua 檔");
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
	std::wstring path = g_luaSourcePathWide;
	if (path.empty()) {
		g_saveFileBuf[0] = L'\0';
		if (!PickSaveLuaPath(owner))
			return;
		path.assign(g_saveFileBuf.data());
	}
	if (!WStringEndsWithLuaCI(path))
		path += L".lua";
	std::string err;
	const std::string text = g_luaEditor.GetText();
	if (!WriteWholeFileUtf8(path, text, err)) {
		std::snprintf(statusBuf, statusSz, "%s", err.c_str());
		return;
	}
	g_luaSourcePathWide = path;
	std::snprintf(statusBuf, statusSz, "已儲存 Lua");
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
	if (!owner) {
		std::snprintf(statusBuf, statusSz, "無法顯示儲存對話框");
		return;
	}
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
	const std::string luaSrc = g_luaEditor.GetText();
	LuaBytecode::CompileOptions opt;
	opt.stripDebug = !keepDebug;
	const bool ok = LuaBytecode::CompileUtf8ToFile(std::string_view(luaSrc), pathUtf8, opt, err);
	if (ok) {
		g_lastLuacOutPathUtf8 = pathUtf8;
		std::snprintf(statusBuf, statusSz, "成功寫入: %s", pathUtf8.c_str());
	} else
		std::snprintf(statusBuf, statusSz, "%s", err.c_str());
}

void TryCompileBytecodeLastPath(bool keepDebug, char* statusBuf, size_t statusSz)
{
	EnsureLuaEditorInited();
	if (g_lastLuacOutPathUtf8.empty()) {
		std::snprintf(statusBuf, statusSz, "尚無上次 .luac 路徑，請先用 F5 編譯並選擇儲存位置");
		return;
	}
	std::string err;
	const std::string luaSrc = g_luaEditor.GetText();
	LuaBytecode::CompileOptions opt;
	opt.stripDebug = !keepDebug;
	const bool ok = LuaBytecode::CompileUtf8ToFile(std::string_view(luaSrc), g_lastLuacOutPathUtf8, opt, err);
	if (ok)
		std::snprintf(statusBuf, statusSz, "成功寫入: %s", g_lastLuacOutPathUtf8.c_str());
	else
		std::snprintf(statusBuf, statusSz, "%s", err.c_str());
}

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

	static bool s_keepDebug = false;
	static char s_status[2048] = "";

	EnsureLuaEditorInited();

	if (g_hasPendingDrop) {
		g_hasPendingDrop = false;
		LoadLuaFromPath(g_pendingDropPath, s_status, sizeof(s_status));
	}
	if (g_pendingSaveLua) {
		g_pendingSaveLua = false;
		if (g_hwnd)
			TrySaveLuaSource(g_hwnd, s_status, sizeof(s_status));
	}
	if (g_pendingOpenLua) {
		g_pendingOpenLua = false;
		if (g_hwnd && PickOpenLuaPath(g_hwnd))
			LoadLuaFromPath(std::wstring(g_openFileBuf.data()), s_status, sizeof(s_status));
	}

	if (g_pendingCompileBytecode) {
		g_pendingCompileBytecode = false;
		if (g_hwnd)
			TryCompileBytecode(g_hwnd, s_keepDebug, s_status, sizeof(s_status));
	}
	if (g_pendingCompileBytecodeLastPath) {
		g_pendingCompileBytecodeLastPath = false;
		TryCompileBytecodeLastPath(s_keepDebug, s_status, sizeof(s_status));
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
					LoadLuaFromPath(std::wstring(g_openFileBuf.data()), s_status, sizeof(s_status));
			}
			if (ImGui::MenuItem("儲存 .lua", "Ctrl+S")) {
				if (g_hwnd)
					TrySaveLuaSource(g_hwnd, s_status, sizeof(s_status));
			}
			ImGui::Separator();
			if (ImGui::MenuItem("編譯 bytecode", "F5/F6") && g_hwnd)
				TryCompileBytecode(g_hwnd, s_keepDebug, s_status, sizeof(s_status));
			ImGui::EndMenu();
		}
		ImGui::PopStyleVar(3);

		const float setMenuMinW =
			(std::max)(ImGui::CalcTextSize(u8"［ ］保留除錯資訊").x, ImGui::CalcTextSize(u8"［O］保留除錯資訊").x) + menuSt.WindowPadding.x * 2.0f + 48.0f;
		ImGui::SetNextWindowSizeConstraints(ImVec2(setMenuMinW, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 14.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 11.0f));
		if (ImGui::BeginMenu(u8"設定")) {
			const char* dbgLabel = s_keepDebug ? u8"［O］保留除錯資訊" : u8"［ ］保留除錯資訊";
			if (ImGui::MenuItem(dbgLabel))
				s_keepDebug = !s_keepDebug;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					u8"［ ］：等同 luajit -b -s（strip）預設。\n"
					u8"［O］：等同 luajit -b -g，bytecode 含行號等除錯資訊。");
			ImGui::EndMenu();
		}
		ImGui::PopStyleVar(3);
		ImGui::EndMenuBar();
	}

	ImGui::SeparatorText("Lua 原始碼");

	const ImGuiStyle& edSt = ImGui::GetStyle();
	float editorH = ImGui::GetContentRegionAvail().y - edSt.ItemSpacing.y;
	if (s_status[0] != '\0')
		editorH -= edSt.ItemSpacing.y + ImGui::GetTextLineHeightWithSpacing() * 2.0f;
	editorH = (std::max)(120.0f, editorH);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
	g_luaEditor.Render("##luaSrc", ImVec2(-1.0f, editorH), true);
	ImGui::PopStyleVar(2);

	if (s_status[0] != '\0') {
		ImGui::Spacing();
		ImGui::TextWrapped("%s", s_status);
	}

	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	ImGui::End();
	ImGui::PopStyleVar();
}

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
		wchar_t pathBuf[4096];
		UINT n = DragQueryFileW(hDrop, 0, pathBuf, (UINT)(sizeof(pathBuf) / sizeof(pathBuf[0])));
		DragFinish(hDrop);
		if (n > 0 && WStringEndsWithLuaCI(std::wstring(pathBuf))) {
			g_pendingDropPath.assign(pathBuf);
			g_hasPendingDrop = true;
		}
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
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU)
			return 0;
		break;
	case WM_CLOSE:
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

void DisableSystemWindowCornerRounding(HWND hwnd)
{
	if (!hwnd)
		return;
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
	const DWORD pref = 1;
	DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

} // namespace

namespace AppWindow {

bool Run()
{
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

	DisableSystemWindowCornerRounding(hwnd);

	g_hwnd = hwnd;

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
