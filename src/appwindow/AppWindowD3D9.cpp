#include "appwindow/AppWindowInternal.h"

#include "imgui_impl_dx9.h"


namespace appwindow {

// 建立 D3D9 裝置與呈現參數（供 ImGui 繪製）
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

// 釋放 D3D9 裝置與 IDirect3D9
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

// 重設 D3D9 裝置並重建 ImGui DX9 繪製物件
void ResetDevice()
{
	ImGui_ImplDX9_InvalidateDeviceObjects();
	HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
	if (hr == D3DERR_INVALIDCALL)
		return;
	ImGui_ImplDX9_CreateDeviceObjects();
}

} // namespace appwindow
