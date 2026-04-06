# luajit_UI

Windows 桌面小工具：以 **Dear ImGui**（DirectX 9）提供介面，內建 **Lua** 原始碼編輯器（[ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit)），並透過 **LuaJIT** 將 `.lua` 編譯為位元組碼（`.luac`）。支援開啟／儲存檔案、拖放 `.lua` 等操作。

## 需求

- **Windows** x64  
- [xmake](https://xmake.io/)（建置系統）  
- **Visual Studio** 或已安裝的 **MSVC** 工具鏈（xmake 在 Windows 上通常搭配 MSVC）  
- **Git**（取得第三方原始碼子目錄）

## 取得原始碼

```bash
git clone --recurse-submodules https://github.com/ItsMe6666/luajit_UI.git
cd luajit_UI
```

若已 clone 但未含子模組：

```bash
git submodule update --init --recursive
```

### 子模組（`vendor/`）

- **`vendor/imgui`** — Dear ImGui（[ocornut/imgui](https://github.com/ocornut/imgui)，`docking` 分支）  
- **`vendor/ImGuiColorTextEdit`** — 程式碼編輯器元件（[BalazsJako/ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit)）  

**LuaJIT**：標頭與函式庫由 xmake 套件 `luajit` 提供（`add_requires("luajit", { configs = { shared = false } })`，靜態連結）。

## 建置

使用 `buildtool` 批次檔（自專案根目錄相對路徑執行）：

- **`buildtool\build_debug.bat`** — Debug 建置  
- **`buildtool\build_release.bat`** — Release 建置  
- **`buildtool\run_debug.bat`** — 建置 Debug 並啟動  
- **`buildtool\run_release.bat`** — 建置 Release 並啟動  
- **`buildtool\clean.bat`** — 清除 `bin`、`bin-int`、`.xmake`、`build`、`.cache` 等建置產物  

### 輸出位置

- Debug：`bin\debug-windows-x64\luajit_ui.exe`  
- Release：`bin\release-windows-x64\luajit_ui.exe`

## 開發者備註

- **語言標準**：C++17  
- **圖形後端**：Direct3D 9、Win32 子系統視窗應用程式  
- **IDE／clangd**：Lua 標頭來自 xmake 套件路徑；可在專案根目錄執行 `xmake project -k compile_commands` 產生 `compile_commands.json`，與建置時的 include 一致。

## 授權

遵守各子模組與第三方元件之授權條款（ImGui、ImGuiColorTextEdit、LuaJIT 等）。
