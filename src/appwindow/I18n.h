#pragma once

// 介面語言（settings.ini [UI] language：0=繁中、1=英文；缺鍵預設英文）
enum class AppLanguage : int {
	ZhHant = 0,
	En = 1,
};

enum class I18nMsg {
	MenuFile,
	MenuOpenLua,
	MenuSaveLua,
	MenuCompileBytecode,
	MenuSettings,
	UiScale,
	SettingsLanguage,
	LangZhHant,
	LangEnglish,
	KeepDebugUnchecked,
	KeepDebugChecked,
	TooltipKeepDebug,
	SidebarLuaFiles,
	StatusRangeSelectSwitch,
	CtxOpenFolder,
	RemoveSelectedFmt,
	RemoveFromList,
	EditorLuaSource,
	SavedLua,
	CannotShowSaveDlg,
	OutputPathEncodeFail,
	WrittenToFmt,
	NoLuacPathHint,
	SwitchedEditorFile,
	LuaOnly,
	SwitchedToOpened,
	AddedLuaFile,
	ErrOpenFile,
	ErrReadFile,
	ErrReadSize,
	ErrReadFail,
	ErrWriteFile,
	ErrWriteFail,
	LbcNewstateFail,
	LbcLoadFail,
	LbcDumpFail,
	LbcDumpEmpty,
	LbcPathInvalid,
	LbcCannotCreateOut,
	LbcWriteIncomplete,
};

enum class I18nSysW {
	D3dCreateFailed,
};

AppLanguage AppLanguageFromInt(int v);
int AppLanguageToInt(AppLanguage lang);

void AppLanguageSetCurrent(AppLanguage lang);
AppLanguage AppLanguageGetCurrent();

const char* Tr(I18nMsg id);
const wchar_t* TrW(I18nSysW id);

const wchar_t* I18nOpenLuaFileFilterW();
const wchar_t* I18nSaveLuaFileFilterW();
const wchar_t* I18nSaveLuacFileFilterW();
const wchar_t* I18nUnnamedFileW();
