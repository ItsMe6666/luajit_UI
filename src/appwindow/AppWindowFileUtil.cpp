#include "appwindow/AppWindowInternal.h"
#include "appwindow/I18n.h"

#include <cstdio>

namespace appwindow {

// 不區分大小寫判斷寬字串是否以 .lua 結尾
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

// 不區分大小寫判斷寬字串是否以 .luac 結尾
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

// UTF-8 轉寬字元（UTF-16）
bool Utf8ToWide(std::string_view utf8, std::wstring& outWide)
{
	outWide.clear();
	if (utf8.empty())
		return true;
	const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
	if (n <= 0)
		return false;
	outWide.resize((size_t)n);
	return MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), outWide.data(), n) > 0;
}

// 寬字元路徑轉 UTF-8
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

// 二進位讀取整檔為字串，成功時略過 UTF-8 BOM
bool ReadWholeFileUtf8(const std::wstring& wpath, std::string& out, std::string& err)
{
	FILE* fp = nullptr;
	if (_wfopen_s(&fp, wpath.c_str(), L"rb") != 0 || !fp) {
		err = Tr(I18nMsg::ErrOpenFile);
		return false;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		err = Tr(I18nMsg::ErrReadFile);
		return false;
	}
	const long sz = ftell(fp);
	if (sz < 0) {
		fclose(fp);
		err = Tr(I18nMsg::ErrReadSize);
		return false;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		err = Tr(I18nMsg::ErrReadFile);
		return false;
	}
	out.resize((size_t)sz);
	if (sz > 0 && fread(out.data(), 1, (size_t)sz, fp) != (size_t)sz) {
		fclose(fp);
		err = Tr(I18nMsg::ErrReadFail);
		return false;
	}
	fclose(fp);
	if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
		out.erase(0, 3);
	return true;
}

// 將資料原樣寫入檔案（二進位）
bool WriteWholeFileUtf8(const std::wstring& wpath, std::string_view data, std::string& err)
{
	FILE* fp = nullptr;
	if (_wfopen_s(&fp, wpath.c_str(), L"wb") != 0 || !fp) {
		err = Tr(I18nMsg::ErrWriteFile);
		return false;
	}
	if (!data.empty() && fwrite(data.data(), 1, data.size(), fp) != data.size()) {
		fclose(fp);
		err = Tr(I18nMsg::ErrWriteFail);
		return false;
	}
	fclose(fp);
	return true;
}

} // namespace appwindow
