#pragma once

#include "imgui.h"

namespace GuiSkin {

void ApplyStyle();
void LoadCjkFont();

bool TitleBarIconButton(const char* str_id, ImVec2 screen_pos, const ImVec2& size,
	ImU32 col_n, ImU32 col_h, ImU32 col_a, const char* glyph);

} // namespace GuiSkin
