// 讀寫 settings.ini（PrivateProfile API），存放視窗、UI 與上次開啟的 Lua 清單。
#include "AppSettings.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace {

// 與 .exe 同目錄的 settings.ini 完整路徑。
std::wstring SettingsIniPath()
{
	wchar_t path[MAX_PATH];
	const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH)
		return L"";
	std::wstring p(path);
	const size_t slash = p.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return L"";
	p.resize(slash + 1);
	p += L"settings.ini";
	return p;
}

std::wstring AfterBuildTxtPath()
{
	std::wstring p = SettingsIniPath();
	if (p.empty())
		return L"";
	const size_t slash = p.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return L"";
	return p.substr(0, slash + 1) + L"afterbuild.txt";
}

bool ReadOptionalUtf8File(const std::wstring& wpath, std::string& out)
{
	out.clear();
	FILE* fp = nullptr;
	if (_wfopen_s(&fp, wpath.c_str(), L"rb") != 0 || !fp)
		return true;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		out.clear();
		return false;
	}
	const long sz = ftell(fp);
	if (sz < 0) {
		fclose(fp);
		out.clear();
		return false;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		out.clear();
		return false;
	}
	out.resize((size_t)sz);
	if (sz > 0 && fread(out.data(), 1, (size_t)sz, fp) != (size_t)sz) {
		fclose(fp);
		out.clear();
		return false;
	}
	fclose(fp);
	if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
		out.erase(0, 3);
	return true;
}

bool WriteOptionalUtf8File(const std::wstring& wpath, std::string_view data)
{
	FILE* fp = nullptr;
	if (_wfopen_s(&fp, wpath.c_str(), L"wb") != 0 || !fp)
		return false;
	if (!data.empty() && fwrite(data.data(), 1, data.size(), fp) != data.size()) {
		fclose(fp);
		return false;
	}
	fclose(fp);
	return true;
}

int ReadInt(const std::wstring& ini, const wchar_t* sec, const wchar_t* key, int def)
{
	wchar_t buf[64];
	GetPrivateProfileStringW(sec, key, L"", buf, (DWORD)(sizeof(buf) / sizeof(buf[0])), ini.c_str());
	if (buf[0] == L'\0')
		return def;
	return _wtoi(buf);
}

float ReadFloat(const std::wstring& ini, const wchar_t* sec, const wchar_t* key, float def)
{
	wchar_t buf[64];
	GetPrivateProfileStringW(sec, key, L"", buf, (DWORD)(sizeof(buf) / sizeof(buf[0])), ini.c_str());
	if (buf[0] == L'\0')
		return def;
	return (float)_wtof(buf);
}

void WriteInt(const std::wstring& ini, const wchar_t* sec, const wchar_t* key, int v)
{
	wchar_t buf[64];
	swprintf_s(buf, L"%d", v);
	WritePrivateProfileStringW(sec, key, buf, ini.c_str());
}

void WriteFloat(const std::wstring& ini, const wchar_t* sec, const wchar_t* key, float v)
{
	wchar_t buf[64];
	swprintf_s(buf, L"%.4f", (double)v);
	WritePrivateProfileStringW(sec, key, buf, ini.c_str());
}

} // namespace

// 載入設定；成功時會 clamp 字體縮放與側欄寬度，並修正 active 索引。
bool AppSettings_Load(AppPersistState& out)
{
	std::wstring ini = SettingsIniPath();
	{
		std::wstring abp = AfterBuildTxtPath();
		if (!abp.empty())
			ReadOptionalUtf8File(abp, out.afterBuildScriptUtf8);
		else
			out.afterBuildScriptUtf8.clear();
	}
	if (ini.empty() || GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES)
		return false;

	out.normalRect.left = ReadInt(ini, L"Window", L"x", 120);
	out.normalRect.top = ReadInt(ini, L"Window", L"y", 80);
	const int w0 = ReadInt(ini, L"Window", L"w", 1600);
	const int h0 = ReadInt(ini, L"Window", L"h", 900);
	out.normalRect.right = out.normalRect.left + (std::max)(w0, 200);
	out.normalRect.bottom = out.normalRect.top + (std::max)(h0, 200);
	out.maximized = ReadInt(ini, L"Window", L"maximized", 0) != 0;

	out.fontGlobalScale = ReadFloat(ini, L"UI", L"fontScale", 1.0f);
	out.sidebarWidth = ReadFloat(ini, L"UI", L"sidebarWidth", 220.0f);
	out.keepBytecodeDebug = ReadInt(ini, L"Editor", L"keepDebug", 0) != 0;

	out.uiLanguage = AppLanguageFromInt(ReadInt(ini, L"UI", L"language", 1));

	out.fontGlobalScale = std::clamp(out.fontGlobalScale, 0.5f, 3.0f);
	out.sidebarWidth = std::clamp(out.sidebarWidth, 120.0f, 640.0f);

	const int n = ReadInt(ini, L"Session", L"fileCount", 0);
	out.activeLuaIndex = ReadInt(ini, L"Session", L"active", 0);
	out.openLuaPaths.clear();
	out.lastLuacOutPathsWide.clear();
	if (n > 0 && n < 512) {
		wchar_t val[32768];
		wchar_t lval[32768];
		for (int i = 0; i < n; ++i) {
			wchar_t key[32];
			swprintf_s(key, L"p%d", i);
			GetPrivateProfileStringW(L"Files", key, L"", val, (DWORD)(sizeof(val) / sizeof(val[0])), ini.c_str());
			if (val[0] == L'\0')
				continue;
			out.openLuaPaths.push_back(val);
			wchar_t lk[32];
			swprintf_s(lk, L"l%d", i);
			GetPrivateProfileStringW(L"LuacOut", lk, L"", lval, (DWORD)(sizeof(lval) / sizeof(lval[0])), ini.c_str());
			out.lastLuacOutPathsWide.push_back(lval);
		}
	}
	if (out.openLuaPaths.empty())
		out.activeLuaIndex = 0;
	else
		out.activeLuaIndex = std::clamp(out.activeLuaIndex, 0, (int)out.openLuaPaths.size() - 1);

	return true;
}

// 儲存設定；若有 hwnd 則用 GetWindowPlacement 取得還原後矩形與是否最大化。
void AppSettings_Save(HWND hwnd, const AppPersistState& state)
{
	std::wstring ini = SettingsIniPath();
	if (ini.empty())
		return;

	RECT rc = state.normalRect;
	bool maximized = state.maximized;
	if (hwnd) {
		WINDOWPLACEMENT wp = { sizeof(wp) };
		if (GetWindowPlacement(hwnd, &wp)) {
			rc = wp.rcNormalPosition;
			maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
		}
	}

	WriteInt(ini, L"Window", L"x", rc.left);
	WriteInt(ini, L"Window", L"y", rc.top);
	int w = rc.right - rc.left;
	int h = rc.bottom - rc.top;
	if (w < 200)
		w = 1600;
	if (h < 200)
		h = 900;
	WriteInt(ini, L"Window", L"w", w);
	WriteInt(ini, L"Window", L"h", h);
	WriteInt(ini, L"Window", L"maximized", maximized ? 1 : 0);

	WriteFloat(ini, L"UI", L"fontScale", state.fontGlobalScale);
	WriteFloat(ini, L"UI", L"sidebarWidth", state.sidebarWidth);
	WriteInt(ini, L"UI", L"language", AppLanguageToInt(state.uiLanguage));
	WriteInt(ini, L"Editor", L"keepDebug", state.keepBytecodeDebug ? 1 : 0);

	const int n = (int)state.openLuaPaths.size();
	WriteInt(ini, L"Session", L"fileCount", n);
	WriteInt(ini, L"Session", L"active", n > 0 ? std::clamp(state.activeLuaIndex, 0, n - 1) : 0);

	WritePrivateProfileStringW(L"Files", nullptr, nullptr, ini.c_str());
	WritePrivateProfileStringW(L"LuacOut", nullptr, nullptr, ini.c_str());
	for (int i = 0; i < n; ++i) {
		wchar_t key[32];
		swprintf_s(key, L"p%d", i);
		WritePrivateProfileStringW(L"Files", key, state.openLuaPaths[i].c_str(), ini.c_str());
		swprintf_s(key, L"l%d", i);
		const wchar_t* lu = (i < (int)state.lastLuacOutPathsWide.size()) ? state.lastLuacOutPathsWide[i].c_str() : L"";
		WritePrivateProfileStringW(L"LuacOut", key, lu, ini.c_str());
	}

	std::wstring ab = AfterBuildTxtPath();
	if (!ab.empty())
		WriteOptionalUtf8File(ab, state.afterBuildScriptUtf8);
}
