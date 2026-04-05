// luaL_loadbuffer → string.dump(strip) → 二進位寫入指定路徑。
#include "LuaBytecode.h"
#include "appwindow/I18n.h"

#include <Windows.h>
#include <cstdio>
#include <vector>

extern "C" {
#include "../vendor/luajit2/src/lua.h"
#include "../vendor/luajit2/src/lauxlib.h"
#include "../vendor/luajit2/src/lualib.h"
}

namespace LuaBytecode {

namespace {

static std::wstring Utf8ToWide(std::string_view utf8) // 輸出路徑給 _wfopen_s 用
{
	if (utf8.empty())
		return L"";
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
	if (n <= 0)
		return L"";
	std::wstring w((size_t)n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), w.data(), n);
	return w;
}

} // namespace

// 建立 Lua 狀態、載入原始碼、string.dump 取得 bytecode、寫入二進位檔。
bool CompileUtf8ToFile(
	std::string_view sourceUtf8,
	const std::string& outputPathUtf8,
	const CompileOptions& options,
	std::string& errorUtf8)
{
	errorUtf8.clear();

	lua_State* L = luaL_newstate();
	if (!L) {
		errorUtf8 = Tr(I18nMsg::LbcNewstateFail);
		return false;
	}

	luaL_openlibs(L);

	int st = luaL_loadbuffer(L, sourceUtf8.data(), sourceUtf8.size(), "=(buffer)");
	if (st != LUA_OK) {
		const char* msg = lua_tostring(L, -1);
		errorUtf8 = msg ? msg : Tr(I18nMsg::LbcLoadFail);
		lua_pop(L, 1);
		lua_close(L);
		return false;
	}

	// string.dump(func, strip)：strip 對應 luajit -b 的 -s / -g
	lua_getglobal(L, "string");
	lua_getfield(L, -1, "dump");
	lua_remove(L, -2);
	lua_insert(L, 1);
	lua_pushboolean(L, options.stripDebug ? 1 : 0);
	st = lua_pcall(L, 2, 1, 0);
	if (st != LUA_OK) {
		const char* msg = lua_tostring(L, -1);
		errorUtf8 = msg ? msg : Tr(I18nMsg::LbcDumpFail);
		lua_pop(L, 1);
		lua_close(L);
		return false;
	}

	size_t outLen = 0;
	const char* dumped = lua_tolstring(L, -1, &outLen);
	if (!dumped || outLen == 0) {
		errorUtf8 = Tr(I18nMsg::LbcDumpEmpty);
		lua_pop(L, 1);
		lua_close(L);
		return false;
	}

	std::vector<unsigned char> bytes(dumped, dumped + outLen);
	lua_pop(L, 1);
	lua_close(L);

	std::wstring wpath = Utf8ToWide(outputPathUtf8);
	if (wpath.empty()) {
		errorUtf8 = Tr(I18nMsg::LbcPathInvalid);
		return false;
	}

	FILE* fp = nullptr;
	if (_wfopen_s(&fp, wpath.c_str(), L"wb") != 0 || !fp) {
		errorUtf8 = Tr(I18nMsg::LbcCannotCreateOut);
		return false;
	}
	const size_t n = fwrite(bytes.data(), 1, bytes.size(), fp);
	fclose(fp);
	if (n != bytes.size()) {
		errorUtf8 = Tr(I18nMsg::LbcWriteIncomplete);
		return false;
	}

	return true;
}

} // namespace LuaBytecode
