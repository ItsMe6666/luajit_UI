#pragma once

#include "imgui.h"

// ImGui 外觀：暗色主題、載入中文字型、自訂標題列小按鈕。
namespace GuiSkin {

void ApplyStyle();
void LoadCjkFont();

bool TitleBarIconButton(const char* str_id, ImVec2 screen_pos, const ImVec2& size,
	ImU32 col_n, ImU32 col_h, ImU32 col_a, const char* glyph);

} // namespace GuiSkin
