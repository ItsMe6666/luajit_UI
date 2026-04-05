#include "appwindow/AppWindowInternal.h"

namespace appwindow {

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
std::string g_orphanLastLuacOutPathUtf8;

std::unordered_set<int> g_sidebarSel;
int g_sidebarAnchor = -1;
int g_sidebarShiftAnchor = -1;

} // namespace appwindow
