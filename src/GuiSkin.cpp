#include "GuiSkin.h"

namespace GuiSkin {

void ApplyStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	const ImVec4 accent = ImVec4(0.28f, 0.78f, 0.85f, 1.0f);
	const ImVec4 accentDim = ImVec4(0.16f, 0.48f, 0.55f, 0.9f);

	style.WindowRounding = 10.0f;
	style.ChildRounding = 8.0f;
	style.FrameRounding = 6.0f;
	style.GrabRounding = 6.0f;
	style.PopupRounding = 8.0f;
	style.ScrollbarRounding = 8.0f;
	style.WindowPadding = ImVec2(16.0f, 14.0f);
	style.FramePadding = ImVec2(10.0f, 6.0f);
	style.ItemSpacing = ImVec2(10.0f, 8.0f);
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize = 1.0f;
	style.IndentSpacing = 22.0f;

	colors[ImGuiCol_Text] = ImVec4(0.93f, 0.95f, 0.97f, 1.0f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.48f, 0.52f, 1.0f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.11f, 1.0f);
	colors[ImGuiCol_MenuBarBg] = colors[ImGuiCol_WindowBg];
	colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.12f, 0.16f, 1.0f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.10f, 0.14f, 0.98f);
	colors[ImGuiCol_Border] = ImVec4(0.42f, 0.48f, 0.56f, 0.55f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.26f, 0.30f, 0.38f, 1.0f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.32f, 0.37f, 0.46f, 1.0f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.36f, 0.42f, 0.52f, 1.0f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.12f, 1.0f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.12f, 0.17f, 1.0f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.95f, 1.0f, 1.0f);
	colors[ImGuiCol_SliderGrab] = accent;
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.88f, 0.95f, 1.0f);
	colors[ImGuiCol_Button] = accentDim;
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.62f, 0.70f, 1.0f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.14f, 0.42f, 0.50f, 1.0f);
	colors[ImGuiCol_Header] = ImVec4(0.18f, 0.46f, 0.54f, 0.55f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.55f, 0.63f, 0.85f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.60f, 0.68f, 1.0f);
	colors[ImGuiCol_Separator] = ImVec4(0.24f, 0.30f, 0.38f, 0.45f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.28f, 0.55f, 0.62f, 0.6f);
	colors[ImGuiCol_SeparatorActive] = accent;
}

void LoadCjkFont()
{
	ImGuiIO& io = ImGui::GetIO();
	ImFontConfig cfg;
	cfg.OversampleH = 2;
	cfg.OversampleV = 2;
	cfg.FontDataOwnedByAtlas = false;

	const char* fontPaths[] = {
		"C:\\Windows\\Fonts\\msjh.ttc",
		"C:\\Windows\\Fonts\\msjhbd.ttc",
		"C:\\Windows\\Fonts\\msyh.ttc",
	};
	for (const char* path : fontPaths) {
		ImFont* f = io.Fonts->AddFontFromFileTTF(path, 18.0f, &cfg, io.Fonts->GetGlyphRangesChineseFull());
		if (f)
			return;
	}
	io.Fonts->AddFontDefault();
}

bool TitleBarIconButton(const char* str_id, ImVec2 screen_pos, const ImVec2& size,
	ImU32 col_n, ImU32 col_h, ImU32 col_a, const char* glyph)
{
	ImGui::SetCursorScreenPos(screen_pos);
	ImGui::InvisibleButton(str_id, size);
	const bool hovered = ImGui::IsItemHovered();
	const bool held = ImGui::IsItemActive();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 rmin = ImGui::GetItemRectMin();
	const ImVec2 rmax = ImGui::GetItemRectMax();
	const ImU32 bg = (held && hovered) ? col_a : hovered ? col_h : col_n;
	const float rad = 3.0f;
	dl->AddRectFilled(rmin, rmax, bg, rad);
	const ImVec2 ts = ImGui::CalcTextSize(glyph);
	dl->AddText(ImVec2(rmin.x + (size.x - ts.x) * 0.5f, rmin.y + (size.y - ts.y) * 0.5f), IM_COL32_WHITE, glyph);
	return ImGui::IsItemClicked(ImGuiMouseButton_Left);
}

} // namespace GuiSkin
