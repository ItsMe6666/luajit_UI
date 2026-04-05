#include "appwindow/AppWindowInternal.h"
#include "appwindow/GuiSkin.h"
#include "appwindow/I18n.h"

#include "imgui.h"

#include <Windows.h>

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

namespace appwindow {

// 繪製主視窗 ImGui：標題列、選單、側欄、編輯器與狀態列
void DrawUi()
{
	static bool s_openAfterBuildEditor = false;
	static std::vector<char> s_afterBuildEditBuf;

	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->Pos);
	ImGui::SetNextWindowSize(vp->Size);

	const ImGuiStyle& styPreBegin = ImGui::GetStyle();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(styPreBegin.WindowPadding.x, 4.0f));

	ImGui::Begin(
		"LuaJIT_UI_Main",
		nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

	const float titleH = 26.0f;
	const float capBtn = 23.0f;
	const ImVec2 capSize(capBtn, capBtn);
	const ImU32 titleTextCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.82f, 0.90f, 1.0f));
	const float availForTitle = ImGui::GetContentRegionAvail().x;
	const float capGap = 3.0f;
	float dragW = availForTitle - capBtn * 2.0f - capGap - 4.0f;
	if (dragW < 120.0f)
		dragW = 120.0f;
	const ImVec2 dragP0 = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##titleDrag", ImVec2(dragW, titleH));
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && g_hwnd) {
		ReleaseCapture();
		SendMessageW(g_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
	}
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddText(ImVec2(dragP0.x + 10.0f, dragP0.y + (titleH - ImGui::GetFontSize()) * 0.5f), titleTextCol, "luajit_UI");
	}

	const float btnY = dragP0.y + (titleH - capBtn) * 0.5f;
	const float btnX0 = dragP0.x + dragW + 4.0f;

	const ImGuiStyle& capSt = ImGui::GetStyle();
	const ImU32 minN = ImGui::ColorConvertFloat4ToU32(capSt.Colors[ImGuiCol_Button]);
	const ImU32 minH = ImGui::ColorConvertFloat4ToU32(capSt.Colors[ImGuiCol_ButtonHovered]);
	const ImU32 minA = ImGui::ColorConvertFloat4ToU32(capSt.Colors[ImGuiCol_ButtonActive]);
	const ImU32 closeN = ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.18f, 0.22f, 0.85f));
	const ImU32 closeH = ImGui::ColorConvertFloat4ToU32(ImVec4(0.75f, 0.22f, 0.28f, 1.0f));
	const ImU32 closeA = ImGui::ColorConvertFloat4ToU32(ImVec4(0.45f, 0.12f, 0.16f, 1.0f));

	if (GuiSkin::TitleBarIconButton("##capMin", ImVec2(btnX0, btnY), capSize, minN, minH, minA, "—") && g_hwnd)
		ShowWindow(g_hwnd, SW_MINIMIZE);
	if (GuiSkin::TitleBarIconButton("##capClose", ImVec2(btnX0 + capBtn + capGap, btnY), capSize, closeN, closeH, closeA, "X") && g_hwnd)
		PostMessageW(g_hwnd, WM_CLOSE, 0, 0);

	static char s_status[2048] = "";

	EnsureLuaEditorInited();
	g_cachedFontScaleForSave = ImGui::GetIO().FontGlobalScale;

	if (!g_pendingDropPaths.empty()) {
		for (const auto& dp : g_pendingDropPaths)
			AddOrSelectLuaFile(dp, s_status, sizeof(s_status));
		g_pendingDropPaths.clear();
	}
	if (g_pendingSaveLua) {
		g_pendingSaveLua = false;
		if (g_hwnd)
			TrySaveLuaSource(g_hwnd, s_status, sizeof(s_status));
	}
	if (g_pendingOpenLua) {
		g_pendingOpenLua = false;
		if (g_hwnd && PickOpenLuaPath(g_hwnd))
			AddOrSelectLuaFile(std::wstring(g_openFileBuf.data()), s_status, sizeof(s_status));
	}

	if (g_pendingCompileBytecode) {
		g_pendingCompileBytecode = false;
		if (g_hwnd)
			TryCompileBytecode(g_hwnd, g_keepBytecodeDebug, s_status, sizeof(s_status));
	}
	if (g_pendingCompileBytecodeLastPath) {
		g_pendingCompileBytecodeLastPath = false;
		TryCompileBytecodeLastPath(g_keepBytecodeDebug, s_status, sizeof(s_status));
	}

	const float bodyH = (std::max)(1.0f, ImGui::GetContentRegionAvail().y);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(styPreBegin.WindowPadding.x, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
	ImGui::BeginChild(
		"##mainBody",
		ImVec2(0.0f, bodyH),
		ImGuiChildFlags_None,
		ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings);

	if (ImGui::BeginMenuBar()) {
		const ImGuiStyle& menuSt = ImGui::GetStyle();
		const float rowW1 = ImGui::CalcTextSize(Tr(I18nMsg::MenuOpenLua)).x + ImGui::CalcTextSize("Ctrl+O").x;
		const float rowW2 = ImGui::CalcTextSize(Tr(I18nMsg::MenuSaveLua)).x + ImGui::CalcTextSize("Ctrl+S").x;
		const float rowW3 = ImGui::CalcTextSize(Tr(I18nMsg::MenuCompileBytecode)).x + ImGui::CalcTextSize("F5/F6").x;
		const float menuMinW = (std::max)((std::max)(rowW1, rowW2), rowW3) + menuSt.ItemInnerSpacing.x * 6.0f + menuSt.WindowPadding.x * 2.0f + 56.0f;
		ImGui::SetNextWindowSizeConstraints(ImVec2(menuMinW, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 14.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 11.0f));
		if (ImGui::BeginMenu(Tr(I18nMsg::MenuFile))) {
			if (ImGui::MenuItem(Tr(I18nMsg::MenuOpenLua), "Ctrl+O")) {
				if (g_hwnd && PickOpenLuaPath(g_hwnd))
					AddOrSelectLuaFile(std::wstring(g_openFileBuf.data()), s_status, sizeof(s_status));
			}
			if (ImGui::MenuItem(Tr(I18nMsg::MenuSaveLua), "Ctrl+S")) {
				if (g_hwnd)
					TrySaveLuaSource(g_hwnd, s_status, sizeof(s_status));
			}
			ImGui::Separator();
			if (ImGui::MenuItem(Tr(I18nMsg::MenuCompileBytecode), "F5/F6") && g_hwnd)
				TryCompileBytecode(g_hwnd, g_keepBytecodeDebug, s_status, sizeof(s_status));
			ImGui::EndMenu();
		}
		ImGui::PopStyleVar(3);

		const float dbgW = (std::max)(
			ImGui::CalcTextSize(Tr(I18nMsg::KeepDebugUnchecked)).x,
			ImGui::CalcTextSize(Tr(I18nMsg::KeepDebugChecked)).x);
		const float langW = (std::max)(
			ImGui::CalcTextSize(Tr(I18nMsg::LangZhHant)).x,
			ImGui::CalcTextSize(Tr(I18nMsg::LangEnglish)).x);
		const float afterBuildW = ImGui::CalcTextSize(Tr(I18nMsg::SettingsAfterBuild)).x;
		const float setMenuMinW =
			(std::max)((std::max)(dbgW, ImGui::CalcTextSize(Tr(I18nMsg::UiScale)).x + 120.0f),
				(std::max)((std::max)(ImGui::CalcTextSize(Tr(I18nMsg::SettingsLanguage)).x, langW), afterBuildW))
			+ menuSt.WindowPadding.x * 2.0f + 48.0f;
		ImGui::SetNextWindowSizeConstraints(ImVec2(setMenuMinW, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 14.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 11.0f));
		if (ImGui::BeginMenu(Tr(I18nMsg::MenuSettings))) {
			ImGuiIO& ioSet = ImGui::GetIO();
			float fs = ioSet.FontGlobalScale;
			if (ImGui::SliderFloat(Tr(I18nMsg::UiScale), &fs, 0.75f, 2.0f, "%.2f"))
				ioSet.FontGlobalScale = fs;
			if (ImGui::IsItemDeactivatedAfterEdit())
				g_requestSavePersist = true;
			ImGui::Separator();
			const char* dbgLabel = g_keepBytecodeDebug ? Tr(I18nMsg::KeepDebugChecked) : Tr(I18nMsg::KeepDebugUnchecked);
			if (ImGui::MenuItem(dbgLabel)) {
				g_keepBytecodeDebug = !g_keepBytecodeDebug;
				g_requestSavePersist = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", Tr(I18nMsg::TooltipKeepDebug));
			ImGui::Separator();
			const AppLanguage curLang = AppLanguageGetCurrent();
			if (ImGui::MenuItem(Tr(I18nMsg::LangZhHant), nullptr, curLang == AppLanguage::ZhHant)) {
				if (curLang != AppLanguage::ZhHant) {
					AppLanguageSetCurrent(AppLanguage::ZhHant);
					g_requestSavePersist = true;
				}
			}
			if (ImGui::MenuItem(Tr(I18nMsg::LangEnglish), nullptr, curLang == AppLanguage::En)) {
				if (curLang != AppLanguage::En) {
					AppLanguageSetCurrent(AppLanguage::En);
					g_requestSavePersist = true;
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem(Tr(I18nMsg::SettingsAfterBuild)))
				s_openAfterBuildEditor = true;
			ImGui::EndMenu();
		}
		ImGui::PopStyleVar(3);
		ImGui::EndMenuBar();
	}

	if (s_openAfterBuildEditor) {
		char afterBuildPopupName[192];
		std::snprintf(
			afterBuildPopupName,
			sizeof(afterBuildPopupName),
			"%s##AfterBuildDlg",
			Tr(I18nMsg::AfterBuildPopupTitleBar));
		ImGui::OpenPopup(afterBuildPopupName);
		s_openAfterBuildEditor = false;
		const size_t need = g_afterBuildScriptUtf8.size() + 1;
		if (s_afterBuildEditBuf.size() < (std::max)(need, (size_t)16384))
			s_afterBuildEditBuf.resize((std::max)(need, (size_t)16384));
		if (!g_afterBuildScriptUtf8.empty())
			std::memcpy(s_afterBuildEditBuf.data(), g_afterBuildScriptUtf8.data(), g_afterBuildScriptUtf8.size());
		s_afterBuildEditBuf[g_afterBuildScriptUtf8.size()] = '\0';
	}

	ImGui::SetNextWindowSize(ImVec2(580.0f, 380.0f), ImGuiCond_FirstUseEver);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 14.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 11.0f));
	char afterBuildPopupName[192];
	std::snprintf(
		afterBuildPopupName,
		sizeof(afterBuildPopupName),
		"%s##AfterBuildDlg",
		Tr(I18nMsg::AfterBuildPopupTitleBar));
	if (ImGui::BeginPopupModal(afterBuildPopupName, nullptr, ImGuiWindowFlags_None)) {
		ImGui::TextUnformatted(Tr(I18nMsg::AfterBuildModalTitle));
		ImGui::Spacing();
		ImGui::TextWrapped("%s", Tr(I18nMsg::AfterBuildEnvHint));
		ImGui::Spacing();
		const float btnRowH = ImGui::GetFrameHeightWithSpacing() * 1.25f;
		const float textH = (std::max)(140.0f, ImGui::GetContentRegionAvail().y - btnRowH);
		ImGui::InputTextMultiline(
			"##afterbuild_mle",
			s_afterBuildEditBuf.data(),
			s_afterBuildEditBuf.size(),
			ImVec2(-1.0f, textH),
			ImGuiInputTextFlags_AllowTabInput);
		if (ImGui::Button(Tr(I18nMsg::AfterBuildOk), ImVec2(120.0f, 0.0f))) {
			g_afterBuildScriptUtf8.assign(s_afterBuildEditBuf.data());
			SavePersistNow();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(Tr(I18nMsg::AfterBuildCancel), ImVec2(120.0f, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(3);

	const float rowAvailH = ImGui::GetContentRegionAvail().y;
	const ImGuiStyle& edSt = ImGui::GetStyle();
	float statusReserve = 0.0f;
	if (s_status[0] != '\0')
		statusReserve = edSt.ItemSpacing.y + ImGui::GetTextLineHeightWithSpacing() * 2.0f;
	const float editorRowH = (std::max)(1.0f, rowAvailH - statusReserve);

	g_sidebarWidth = std::clamp(g_sidebarWidth, 120.0f, 640.0f);

	ImGui::BeginChild("##fileSidebarCol", ImVec2(g_sidebarWidth, editorRowH), ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings);
	ImGui::SeparatorText(Tr(I18nMsg::SidebarLuaFiles));
	const float fileListH = (std::max)(1.0f, ImGui::GetContentRegionAvail().y);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, edSt.WindowPadding.y));
	ImGui::BeginChild("##fileList", ImVec2(-1.0f, fileListH), ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
	{
		ImGuiIO& ioList = ImGui::GetIO();
		if (ImGui::IsWindowFocused() && ioList.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A) && !ioList.KeyShift && !g_docs.empty()) {
			g_sidebarSel.clear();
			for (int j = 0; j < (int)g_docs.size(); ++j)
				g_sidebarSel.insert(j);
			g_sidebarAnchor = (int)g_docs.size() - 1;
			g_sidebarShiftAnchor = (g_activeDoc >= 0 && g_activeDoc < (int)g_docs.size()) ? g_activeDoc : 0;
		}
	}
	int pendingRemove = -1;
	const float rowPadX = 8.0f;
	const float closeSz = 20.0f;
	const float closePadRight = 6.0f;
	const float rowH = ImGui::GetTextLineHeight() + 4.0f;
	const float rowGapY = 3.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(edSt.ItemSpacing.x, rowGapY));
	std::string activeEditorUtf8Snapshot;
	if (g_activeDoc >= 0 && g_activeDoc < (int)g_docs.size())
		activeEditorUtf8Snapshot = LuaEditorGetBufferUtf8();
	for (size_t i = 0; i < g_docs.size(); ++i) {
		ImGui::PushID((int)i);
		const int ii = (int)i;
		const bool rowActive = (ii == g_activeDoc);
		const bool rowInSel = g_sidebarSel.count(ii) != 0;
		const bool dirty = DocIsDirty(ii, &activeEditorUtf8Snapshot);
		const std::wstring fnameW = FileNameFromPath(g_docs[i].path);
		std::string fnameU8;
		if (!WidePathToUtf8(fnameW, fnameU8))
			fnameU8 = "(?)";

		const float fullW = ImGui::GetContentRegionAvail().x;
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1 = ImVec2(p0.x + fullW, p0.y + rowH);
		const float xLeft = p1.x - closeSz - closePadRight;

		ImGui::InvisibleButton("##row", ImVec2(fullW, rowH));
		const bool rowHover = ImGui::IsItemHovered();
		const ImVec2 mouse = ImGui::GetMousePos();
		const bool onClose = rowHover && mouse.x >= xLeft && mouse.x < p1.x && mouse.y >= p0.y && mouse.y < p1.y;

		ImDrawList* dl = ImGui::GetWindowDrawList();
		if (rowActive)
			dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_HeaderActive));
		else if (rowInSel)
			dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_Header));
		else if (rowHover)
			dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_HeaderHovered));

		dl->PushClipRect(p0, ImVec2((std::max)(p0.x + 1.0f, xLeft - 4.0f), p1.y), true);
		const float ty = p0.y + (rowH - ImGui::GetFontSize()) * 0.5f;
		const ImU32 fnameCol = dirty ? IM_COL32(255, 220, 72, 255) : ImGui::GetColorU32(ImGuiCol_Text);
		dl->AddText(ImVec2(p0.x + rowPadX, ty), fnameCol, fnameU8.c_str());
		dl->PopClipRect();

		if (rowHover) {
			const ImVec2 xP0(xLeft, p0.y + (rowH - closeSz) * 0.5f);
			const bool xHot = onClose;
			const ImVec2 ts = ImGui::CalcTextSize("X");
			const ImU32 redX = xHot ? IM_COL32(255, 110, 118, 255) : IM_COL32(220, 72, 82, 255);
			dl->AddText(ImVec2(xP0.x + (closeSz - ts.x) * 0.5f, xP0.y + (closeSz - ts.y) * 0.5f), redX, "X");
		}

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
			ImGuiIO& ioRow = ImGui::GetIO();
			const bool rowShift = (ioRow.KeyMods & ImGuiMod_Shift) != 0 || ioRow.KeyShift;
			if (onClose) {
				pendingRemove = ii;
			} else if (rowShift) {
				int anchor = g_sidebarShiftAnchor;
				if (anchor < 0 || anchor >= (int)g_docs.size())
					anchor = g_sidebarAnchor;
				if (anchor >= 0 && anchor < (int)g_docs.size()) {
					const int a = (std::min)(anchor, ii);
					const int b = (std::max)(anchor, ii);
					g_sidebarSel.clear();
					for (int j = a; j <= b; ++j)
						g_sidebarSel.insert(j);
					EnsureLuaEditorInited();
					SyncEditorToActiveDoc();
					g_activeDoc = ii;
					g_sidebarAnchor = ii;
					g_luaEditor.SetText(g_docs[(size_t)g_activeDoc].text);
					std::snprintf(s_status, sizeof(s_status), "%s", Tr(I18nMsg::StatusRangeSelectSwitch));
					g_requestSavePersist = true;
				} else {
					SwitchToDoc(ii, s_status, sizeof(s_status));
				}
			} else if (ioRow.KeyCtrl) {
				if (g_sidebarSel.count(ii))
					g_sidebarSel.erase(ii);
				else
					g_sidebarSel.insert(ii);
				g_sidebarAnchor = ii;
			} else {
				SwitchToDoc(ii, s_status, sizeof(s_status));
			}
		}

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 8.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
		if (ImGui::BeginPopupContextItem()) {
			const bool hasPath = !g_docs[i].path.empty();
			if (ImGui::MenuItem(Tr(I18nMsg::CtxOpenFolder), nullptr, false, hasPath))
				OpenExplorerSelectFile(g_docs[i].path);
			const int selN = (int)g_sidebarSel.size();
			const bool inSel = g_sidebarSel.count(ii) != 0;
			char rmLabel[96];
			if (inSel && selN > 1)
				std::snprintf(rmLabel, sizeof(rmLabel), Tr(I18nMsg::RemoveSelectedFmt), selN);
			else
				std::snprintf(rmLabel, sizeof(rmLabel), "%s", Tr(I18nMsg::RemoveFromList));
			if (ImGui::MenuItem(rmLabel)) {
				if (inSel && selN > 1)
					RemoveDocsSetFromList(g_sidebarSel);
				else
					pendingRemove = ii;
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar(3);
		ImGui::PopID();
	}
	ImGui::PopStyleVar(2);
	ImGui::EndChild();
	ImGui::PopStyleVar(2);

	ImGui::EndChild();

	if (pendingRemove >= 0)
		RemoveDocAt(pendingRemove);

	ImGui::SameLine();
	const float splitW = 8.0f;
	const ImVec2 splitP0 = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##sidebarSplit", ImVec2(splitW, editorRowH));
	const bool splitHover = ImGui::IsItemHovered();
	const bool splitActive = ImGui::IsItemActive();
	if (splitHover || splitActive)
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	if (splitActive)
		g_sidebarWidth += ImGui::GetIO().MouseDelta.x;
	g_sidebarWidth = std::clamp(g_sidebarWidth, 120.0f, 640.0f);
	if (ImGui::IsItemDeactivatedAfterEdit())
		g_requestSavePersist = true;
	{
		const ImVec2 splitP1 = ImVec2(splitP0.x + splitW, splitP0.y + editorRowH);
		const float midX = (splitP0.x + splitP1.x) * 0.5f;
		ImDrawList* sdl = ImGui::GetWindowDrawList();
		const ImU32 splitCol = ImGui::GetColorU32(
			splitActive ? ImGuiCol_SeparatorActive : splitHover ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
		sdl->AddLine(ImVec2(midX, splitP0.y), ImVec2(midX, splitP1.y), splitCol, splitActive ? 2.0f : 1.0f);
	}
	ImGui::SameLine();
	ImGui::BeginChild("##editorColumn", ImVec2(0.0f, editorRowH), ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings);

	ImGui::SeparatorText(Tr(I18nMsg::EditorLuaSource));
	float editorH = ImGui::GetContentRegionAvail().y;
	editorH = (std::max)(1.0f, editorH);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
	const float kLuaEditorInnerTopPad = 8.0f;
	const ImGuiWindowFlags luaScrollFlags =
		ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NoMove;
	g_luaEditor.SetImGuiChildIgnored(true);
	ImGui::BeginChild("##luaSrcShell", ImVec2(-1.0f, editorH), ImGuiChildFlags_Borders, luaScrollFlags);
	ImGui::Dummy(ImVec2(0.0f, kLuaEditorInnerTopPad));
	const float innerH = (std::max)(40.0f, ImGui::GetContentRegionAvail().y);
	g_luaEditor.Render("##luaSrc", ImVec2(-1.0f, innerH), false);
	ImGui::EndChild();
	g_luaEditor.SetImGuiChildIgnored(false);
	ImGui::PopStyleVar(2);

	ImGui::EndChild();

	if (s_status[0] != '\0') {
		ImGui::Spacing();
		ImGui::TextWrapped("%s", s_status);
	}

	if (g_requestSavePersist) {
		g_requestSavePersist = false;
		SavePersistNow();
	}

	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	ImGui::End();
	ImGui::PopStyleVar();
}

} // namespace appwindow
