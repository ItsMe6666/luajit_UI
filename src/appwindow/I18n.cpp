#include "appwindow/I18n.h"

namespace {

AppLanguage g_lang = AppLanguage::En;

inline bool IsEn()
{
	return g_lang == AppLanguage::En;
}

} // namespace

AppLanguage AppLanguageFromInt(int v)
{
	return (v == (int)AppLanguage::En) ? AppLanguage::En : AppLanguage::ZhHant;
}

int AppLanguageToInt(AppLanguage lang)
{
	return (int)lang;
}

void AppLanguageSetCurrent(AppLanguage lang)
{
	g_lang = lang;
}

AppLanguage AppLanguageGetCurrent()
{
	return g_lang;
}

const char* Tr(I18nMsg id)
{
	const bool en = IsEn();
	switch (id) {
	case I18nMsg::MenuFile:
		return en ? "File" : u8"檔案";
	case I18nMsg::MenuOpenLua:
		return en ? "Open .lua" : u8"開啟 .lua";
	case I18nMsg::MenuSaveLua:
		return en ? "Save .lua" : u8"儲存 .lua";
	case I18nMsg::MenuCompileBytecode:
		return en ? "Compile bytecode" : u8"編譯 bytecode";
	case I18nMsg::MenuSettings:
		return en ? "Settings" : u8"設定";
	case I18nMsg::UiScale:
		return en ? "UI scale" : u8"介面縮放";
	case I18nMsg::SettingsLanguage:
		return en ? "Language" : u8"介面語言";
	case I18nMsg::LangZhHant:
		return en ? "Traditional Chinese" : u8"中文（繁體）";
	case I18nMsg::LangEnglish:
		return "English";
	case I18nMsg::KeepDebugUnchecked:
		return en ? "[ ] Strip debug (default)" : u8"［ ］保留除錯資訊";
	case I18nMsg::KeepDebugChecked:
		return en ? "[X] Keep debug info" : u8"［O］保留除錯資訊";
	case I18nMsg::TooltipKeepDebug:
		return en ? "[ ]: same as luajit -b -s (strip) by default.\n[X]: same as luajit -b -g (line numbers, etc.)."
			  : u8"［ ］：等同 luajit -b -s（strip）預設。\n［O］：等同 luajit -b -g，bytecode 含行號等除錯資訊。";
	case I18nMsg::SidebarLuaFiles:
		return en ? "Lua files" : u8"Lua 檔案";
	case I18nMsg::StatusRangeSelectSwitch:
		return en ? "Range selected; switched editor tab" : u8"已選取範圍並切換編輯檔";
	case I18nMsg::CtxOpenFolder:
		return en ? "Open file location" : u8"開啟檔案所在目錄";
	case I18nMsg::RemoveSelectedFmt:
		return en ? "Remove selected from list (%d)" : u8"從清單移除已選 (%d)";
	case I18nMsg::RemoveFromList:
		return en ? "Remove from list" : u8"從清單移除";
	case I18nMsg::EditorLuaSource:
		return en ? "Lua source" : u8"Lua 原始碼";
	case I18nMsg::SavedLua:
		return en ? "Lua saved" : u8"已儲存 Lua";
	case I18nMsg::CannotShowSaveDlg:
		return en ? "Cannot show save dialog" : u8"無法顯示儲存對話框";
	case I18nMsg::OutputPathEncodeFail:
		return en ? "Failed to encode output path" : u8"輸出路徑編碼失敗";
	case I18nMsg::WrittenToFmt:
		return en ? "Written: %s" : u8"成功寫入: %s";
	case I18nMsg::NoLuacPathHint:
		return en ? "No saved .luac path for this tab; use F5 first to compile and choose where to save."
			  : u8"此編輯分頁尚無紀錄的 .luac 路徑，請先用 F5 編譯並選擇儲存位置";
	case I18nMsg::SwitchedEditorFile:
		return en ? "Switched editor tab" : u8"已切換編輯檔";
	case I18nMsg::LuaOnly:
		return en ? "Only .lua files can be opened or dropped here." : u8"僅支援拖入或開啟 .lua 檔";
	case I18nMsg::SwitchedToOpened:
		return en ? "Switched to an already open file" : u8"已切換至已開啟的檔案";
	case I18nMsg::AddedLuaFile:
		return en ? "Lua file added" : u8"已加入 Lua 檔";
	case I18nMsg::ErrOpenFile:
		return en ? "Cannot open file" : u8"無法開啟檔案";
	case I18nMsg::ErrReadFile:
		return en ? "Cannot read file" : u8"無法讀取檔案";
	case I18nMsg::ErrReadSize:
		return en ? "Cannot read file size" : u8"無法讀取檔案大小";
	case I18nMsg::ErrReadFail:
		return en ? "Read failed" : u8"讀取失敗";
	case I18nMsg::ErrWriteFile:
		return en ? "Cannot write file" : u8"無法寫入檔案";
	case I18nMsg::ErrWriteFail:
		return en ? "Write failed" : u8"寫入失敗";
	case I18nMsg::LbcNewstateFail:
		return en ? "luaL_newstate failed" : u8"luaL_newstate 失敗";
	case I18nMsg::LbcLoadFail:
		return en ? "Load failed" : u8"載入失敗";
	case I18nMsg::LbcDumpFail:
		return en ? "string.dump failed" : u8"string.dump 失敗";
	case I18nMsg::LbcDumpEmpty:
		return en ? "string.dump returned empty" : u8"string.dump 回傳空字串";
	case I18nMsg::LbcPathInvalid:
		return en ? "Invalid output path or bad UTF-8" : u8"輸出路徑無效或 UTF-8 編碼錯誤";
	case I18nMsg::LbcCannotCreateOut:
		return en ? "Cannot create output file" : u8"無法建立輸出檔案";
	case I18nMsg::LbcWriteIncomplete:
		return en ? "Incomplete write to file" : u8"寫入檔案不完整";
	}
	return "";
}

const wchar_t* TrW(I18nSysW id)
{
	switch (id) {
	case I18nSysW::D3dCreateFailed:
		return IsEn() ? L"Failed to create Direct3D 9 device" : L"無法建立 Direct3D 9 裝置";
	}
	return L"";
}

static const wchar_t kOpenFilterZh[] = L"Lua 腳本 (*.lua)\0*.lua\0所有檔案 (*.*)\0*.*\0";
static const wchar_t kOpenFilterEn[] = L"Lua script (*.lua)\0*.lua\0All files (*.*)\0*.*\0";
static const wchar_t kSaveLuaFilterZh[] = L"Lua 腳本 (*.lua)\0*.lua\0";
static const wchar_t kSaveLuaFilterEn[] = L"Lua script (*.lua)\0*.lua\0";
static const wchar_t kSaveLuacFilterZh[] = L"Lua bytecode (*.luac)\0*.luac\0所有檔案 (*.*)\0*.*\0";
static const wchar_t kSaveLuacFilterEn[] = L"Lua bytecode (*.luac)\0*.luac\0All files (*.*)\0*.*\0";

const wchar_t* I18nOpenLuaFileFilterW()
{
	return IsEn() ? kOpenFilterEn : kOpenFilterZh;
}

const wchar_t* I18nSaveLuaFileFilterW()
{
	return IsEn() ? kSaveLuaFilterEn : kSaveLuaFilterZh;
}

const wchar_t* I18nSaveLuacFileFilterW()
{
	return IsEn() ? kSaveLuacFilterEn : kSaveLuacFilterZh;
}

const wchar_t* I18nUnnamedFileW()
{
	return IsEn() ? L"(untitled)" : L"(未命名)";
}
