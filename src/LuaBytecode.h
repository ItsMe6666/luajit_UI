#pragma once

#include <string>
#include <string_view>

namespace LuaBytecode {

struct CompileOptions {
	/// true = 等同 `luajit -b` 預設 -s（strip）；false = -g（保留除錯）
	bool stripDebug = true;
};

bool CompileUtf8ToFile(
	std::string_view sourceUtf8,
	const std::string& outputPathUtf8,
	const CompileOptions& options,
	std::string& errorUtf8);

} // namespace LuaBytecode
