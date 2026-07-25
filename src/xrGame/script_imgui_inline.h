#pragma once
#include "stdafx.h"
#include "imgui/imgui.h"

static bool ugly_hack = false;
static ::std::string imgui_text_buffer;

IC LPCSTR ImGui_GetVersion()
{
	return ImGui::GetVersion();
}

IC void ImGui_SetNextWindowPos(Fvector2& pos)
{
	ImGui::SetNextWindowPos(*(ImVec2*)&pos, 0, *(ImVec2*)&Fvector2().set(0, 0));
}

IC void ImGui_SetNextWindowPos(Fvector2& pos, ImGuiCond cond)
{
	ImGui::SetNextWindowPos(*(ImVec2*)&pos, cond, *(ImVec2*)&Fvector2().set(0,0));
}

IC void ImGui_SetNextWindowPos(Fvector2& pos, ImGuiCond cond, Fvector2& pivot)
{
	ImGui::SetNextWindowPos(*(ImVec2*)&pos, cond, *(ImVec2*)&pivot);
}

IC void ImGui_SetNextWindowSize(Fvector2& size, ImGuiCond cond = 0)
{
	ImGui::SetNextWindowSize(*(ImVec2*)&size, cond);
}

IC void ImGui_SetNextWindowSizeConstraints(Fvector2& size_min, Fvector2& size_max)
{
	ImGui::SetNextWindowSizeConstraints(*(ImVec2*)&size_min, *(ImVec2*)&size_max);
}

IC void ImGui_SetNextWindowContentSize(Fvector2& size)
{
	ImGui::SetNextWindowContentSize(*(ImVec2*)&size);
}

IC void ImGui_SetNextWindowScroll(Fvector2& scroll)
{
	ImGui::SetNextWindowScroll(*(ImVec2*)&scroll);
}

IC Fvector2 ImGui_GetWindowPos()
{
	return *(Fvector2*)&ImGui::GetWindowPos();
}

IC Fvector2 ImGui_GetWindowSize()
{
	return *(Fvector2*)&ImGui::GetWindowSize();
}

IC void ImGui_SetWindowPos(Fvector2& pos, ImGuiCond cond = 0)
{
	ImGui::SetWindowPos(*(ImVec2*)&pos, cond);
}

IC void ImGui_SetWindowSize(Fvector2& size, ImGuiCond cond = 0)
{
	ImGui::SetWindowSize(*(ImVec2*)&size, cond);
}

IC void ImGui_SetWindowCollapsed(bool collapsed)
{
	ImGui::SetWindowCollapsed(collapsed);
}

IC void ImGui_SetScrollFromPosX(float pos)
{
	ImGui::SetScrollFromPosX(pos);
}

IC void ImGui_SetScrollFromPosY(float pos)
{
	ImGui::SetScrollFromPosY(pos);
}

IC Fvector2 ImGui_GetCursorScreenPos()
{
	return *(Fvector2*)&ImGui::GetCursorScreenPos();
}

IC void ImGui_SetCursorScreenPos(Fvector2& pos)
{
	ImGui::SetCursorScreenPos(*(ImVec2*)&pos);
}

IC Fvector2 ImGui_GetContentRegionAvail()
{
	return *(Fvector2*)&ImGui::GetContentRegionAvail();
}

IC Fvector2 ImGui_GetCursorPos()
{
	return *(Fvector2*)&ImGui::GetCursorPos();
}

IC void ImGui_SetCursorPos(Fvector2& pos)
{
	ImGui::SetCursorPos(*(ImVec2*)&pos);
}

IC Fvector2 ImGui_GetCursorStartPos()
{
	return *(Fvector2*)&ImGui::GetCursorStartPos();
}

IC void ImGui_SameLine()
{
	ImGui::SameLine();
}

IC void ImGui_SameLine(float offset)
{
	ImGui::SameLine(offset);
}

IC void ImGui_Dummy(Fvector2& size)
{
	ImGui::Dummy(*(ImVec2*)&size);
}

IC void ImGui_TextDisabled(LPCSTR text)
{
	ImGui::TextDisabled(text, 0);
}

IC void ImGui_TextWrapped(LPCSTR text)
{
	ImGui::TextWrapped(text, 0);
}

IC void ImGui_TextColored(Fcolor& col, LPCSTR text)
{
	ImGui::TextColoredV(ImVec4{ col.r, col.g, col.b, col.a }, text, 0);
}

IC void ImGui_LabelText(LPCSTR label, LPCSTR text)
{
	ImGui::LabelText(label, text, 0);
}

IC void ImGui_BulletText(LPCSTR text)
{
	ImGui::BulletText(text, 0);
}

IC bool ImGui_Button(LPCSTR label)
{
	return ImGui::Button(label, *(ImVec2*)&Fvector2().set(80,22));
}

IC bool ImGui_Button(LPCSTR label, Fvector2 size)
{
	return ImGui::Button(label, *(ImVec2*)&size);
}

IC bool ImGui_InvisibleButton(LPCSTR label)
{
	return ImGui::InvisibleButton(label, *(ImVec2*)&Fvector2().set(80,22), 0);
}

IC bool ImGui_InvisibleButton(LPCSTR label, Fvector2 size)
{
	return ImGui::InvisibleButton(label, *(ImVec2*)&size, 0);
}

IC bool ImGui_InvisibleButton(LPCSTR label, Fvector2 size, ImGuiButtonFlags flags)
{
	return ImGui::InvisibleButton(label, *(ImVec2*)&size, flags);
}

IC void ImGui_ProgressBar(float fraction, Fvector2 size = Fvector2{FLT_MAX,0}, LPCSTR overlay = 0)
{
	ImGui::ProgressBar(fraction, *(ImVec2*)&size, overlay);
}

IC bool ImGui_DragFloat2(LPCSTR name, Fvector2& vec, float speed = 1.f, float min = 0.f, float max = 0.f, LPCSTR format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragFloat2(name, (float*)&vec, speed, min, max, format, flags);
}

IC bool ImGui_DragFloat3(LPCSTR name, Fvector& vec, float speed = 1.f, float min = 0.f, float max = 0.f, LPCSTR format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragFloat3(name, (float*)&vec, speed, min, max, format, flags);
}

IC bool ImGui_DragFloat4(LPCSTR name, Fvector4& vec, float speed = 1.f, float min = 0.f, float max = 0.f, LPCSTR format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragFloat4(name, (float*)&vec, speed, min, max, format, flags);
}

IC bool ImGui_MagnificationCurve(LPCSTR label, Fvector4& low, Fvector4& high,
	float value_min, float value_max, Fvector2 size)
{
	static const float magnifications[8] = {1.f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 20.f};
	float* values[8] = {&low.x, &low.y, &low.z, &low.w, &high.x, &high.y, &high.z, &high.w};
	const float graph_width = size.x > 0.f ? size.x : 500.f;
	const float graph_height = size.y > 0.f ? size.y : 190.f;
	const ImGuiID curve_id = ImGui::GetID(label);
	static ImGuiID active_curve = 0;
	static int active_point = -1;

	ImGui::PushID(label);
	ImGui::InvisibleButton("curve", ImVec2(graph_width, graph_height), ImGuiButtonFlags_MouseButtonLeft);
	const ImVec2 canvas_min = ImGui::GetItemRectMin();
	const ImVec2 canvas_max = ImGui::GetItemRectMax();
	const ImVec2 plot_min(canvas_min.x + 42.f, canvas_min.y + 10.f);
	const ImVec2 plot_max(canvas_max.x - 10.f, canvas_max.y - 24.f);
	const float log_max = logf(magnifications[7]);
	auto point_position = [&](int index)
	{
		const float x = logf(magnifications[index]) / log_max;
		const float y = (*values[index] - value_min) / _max(value_max - value_min, EPS);
		return ImVec2(plot_min.x + x * (plot_max.x - plot_min.x),
			plot_max.y - clampr(y, 0.f, 1.f) * (plot_max.y - plot_min.y));
	};

	if (ImGui::IsItemActivated())
	{
		const ImVec2 mouse = ImGui::GetMousePos();
		float nearest = FLT_MAX;
		for (int i = 0; i < 8; ++i)
		{
			const float distance = _abs(mouse.x - point_position(i).x);
			if (distance < nearest)
			{
				nearest = distance;
				active_point = i;
			}
		}
		active_curve = curve_id;
	}

	bool changed = false;
	if (ImGui::IsItemActive() && active_curve == curve_id && active_point >= 0)
	{
		const float normalized = (plot_max.y - ImGui::GetMousePos().y) / _max(plot_max.y - plot_min.y, 1.f);
		const float value = clampr(value_min + normalized * (value_max - value_min), value_min, value_max);
		if (_abs(*values[active_point] - value) > EPS)
		{
			*values[active_point] = value;
			changed = true;
		}
	}
	else if (active_curve == curve_id)
	{
		active_curve = 0;
		active_point = -1;
	}

	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(canvas_min, canvas_max, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.f);
	draw->AddRect(canvas_min, canvas_max, ImGui::GetColorU32(ImGuiCol_Border), 3.f);
	for (int grid = 0; grid <= 4; ++grid)
	{
		const float y = plot_min.y + (plot_max.y - plot_min.y) * (float(grid) / 4.f);
		draw->AddLine(ImVec2(plot_min.x, y), ImVec2(plot_max.x, y), ImGui::GetColorU32(ImGuiCol_Separator), 1.f);
		char value_text[16];
		const float grid_value = value_max - (value_max - value_min) * (float(grid) / 4.f);
		xr_sprintf(value_text, "%.2g", grid_value);
		draw->AddText(ImVec2(canvas_min.x + 6.f, y - 7.f), ImGui::GetColorU32(ImGuiCol_TextDisabled), value_text);
	}

	if (value_min <= 1.f && value_max >= 1.f)
	{
		const float neutral = (1.f - value_min) / _max(value_max - value_min, EPS);
		const float neutral_y = plot_max.y - neutral * (plot_max.y - plot_min.y);
		draw->AddLine(ImVec2(plot_min.x, neutral_y), ImVec2(plot_max.x, neutral_y),
			ImGui::GetColorU32(ImGuiCol_PlotHistogram), 2.f);
		draw->AddText(ImVec2(plot_min.x + 5.f, neutral_y - 15.f),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), "profile baseline");
	}

	ImVec4 fill_color = ImGui::GetStyleColorVec4(ImGuiCol_PlotLines);
	fill_color.w = 0.10f;
	for (int i = 1; i < 8; ++i)
	{
		const ImVec2 previous = point_position(i - 1);
		const ImVec2 current = point_position(i);
		const ImVec2 area[4] = {
			previous,
			current,
			ImVec2(current.x, plot_max.y),
			ImVec2(previous.x, plot_max.y),
		};
		draw->AddConvexPolyFilled(area, 4, ImGui::GetColorU32(fill_color));
	}

	int hovered_point = -1;
	if (ImGui::IsItemHovered())
	{
		const float mouse_x = ImGui::GetMousePos().x;
		float nearest = FLT_MAX;
		for (int i = 0; i < 8; ++i)
		{
			const float distance = _abs(mouse_x - point_position(i).x);
			if (distance < nearest)
			{
				nearest = distance;
				hovered_point = i;
			}
		}
	}

	for (int i = 0; i < 8; ++i)
	{
		const ImVec2 point = point_position(i);
		draw->AddLine(ImVec2(point.x, plot_min.y), ImVec2(point.x, plot_max.y),
			ImGui::GetColorU32(hovered_point == i ? ImGuiCol_PlotLinesHovered : ImGuiCol_Separator),
			hovered_point == i ? 1.5f : 1.f);
		char text[16];
		xr_sprintf(text, "%gx", magnifications[i]);
		const ImVec2 axis_text_size = ImGui::CalcTextSize(text);
		const float axis_text_x = clampr(point.x - axis_text_size.x * 0.5f,
			canvas_min.x + 3.f, canvas_max.x - axis_text_size.x - 3.f);
		draw->AddText(ImVec2(axis_text_x, plot_max.y + 4.f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
		if (i > 0)
			draw->AddLine(point_position(i - 1), point, ImGui::GetColorU32(ImGuiCol_PlotLines), 2.f);
		const bool highlighted = (active_curve == curve_id && active_point == i) || hovered_point == i;
		draw->AddCircleFilled(point, highlighted ? 7.f : 5.f,
			ImGui::GetColorU32(highlighted ? ImGuiCol_PlotLinesHovered : ImGuiCol_PlotLines));
		if (hovered_point == i)
		{
			char point_text[32];
			xr_sprintf(point_text, "%gx  %.2f", magnifications[i], *values[i]);
			const ImVec2 text_size = ImGui::CalcTextSize(point_text);
			const float text_x = i >= 6 ? point.x - text_size.x - 12.f : point.x + 10.f;
			const float text_y = _max(plot_min.y + 4.f, point.y - text_size.y - 10.f);
			const ImVec2 bubble_min(text_x - 5.f, text_y - 3.f);
			const ImVec2 bubble_max(text_x + text_size.x + 5.f, text_y + text_size.y + 3.f);
			draw->AddRectFilled(bubble_min, bubble_max, ImGui::GetColorU32(ImGuiCol_PopupBg), 3.f);
			draw->AddRect(bubble_min, bubble_max, ImGui::GetColorU32(ImGuiCol_Border), 3.f);
			draw->AddText(ImVec2(text_x, text_y), ImGui::GetColorU32(ImGuiCol_Text), point_text);
		}
	}
	ImGui::PopID();
	return changed;
}

IC bool ImGui_ColorPicker3(LPCSTR name, Fcolor& color, ImGuiColorEditFlags flags = 0)
{
	return ImGui::ColorPicker3(name, (float*)&color, flags);
}

IC bool ImGui_ColorPicker4(LPCSTR name, Fcolor& color, ImGuiColorEditFlags flags = 0)
{
	return ImGui::ColorPicker4(name, (float*)&color, flags);
}

IC bool ImGui_ColorEdit3(LPCSTR name, Fcolor& color, ImGuiColorEditFlags flags = 0)
{
	return ImGui::ColorEdit3(name, (float*)&color, flags);
}

IC bool ImGui_ColorEdit4(LPCSTR name, Fcolor& color, ImGuiColorEditFlags flags = 0)
{
	return ImGui::ColorEdit4(name, (float*)&color, flags);
}

IC bool ImGui_SliderFloat2(LPCSTR name, Fvector2& vec, float min = 0.f, float max = 0.f, LPCSTR format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderFloat2(name, (float*)&vec, min, max, format, flags);
}

IC bool ImGui_SliderFloat3(LPCSTR name, Fvector& vec, float min = 0.f, float max = 0.f, LPCSTR format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderFloat3(name, (float*)&vec, min, max, format, flags);
}

IC bool ImGui_SliderFloat4(LPCSTR name, Fvector4& vec, float min = 0.f, float max = 0.f, LPCSTR format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderFloat4(name, (float*)&vec, min, max, format, flags);
}

IC LPCSTR ImGui_InputText(LPCSTR label, LPCSTR text, int capacity = 100, ImGuiInputTextFlags flags = 0, bool& changed = ugly_hack)
{
	imgui_text_buffer = (text && xr_strlen(text)) ? text : "";
	changed = ImGui::InputText(label, (char*)imgui_text_buffer.c_str(), capacity, flags);
	return imgui_text_buffer.c_str();
}

IC LPCSTR ImGui_InputTextMultiline(LPCSTR label, LPCSTR text, int capacity = 100, Fvector2 size = Fvector2{ 0,0 }, ImGuiInputTextFlags flags = 0, bool& changed = ugly_hack)
{
	imgui_text_buffer = (text && xr_strlen(text)) ? text : "";
	changed = ImGui::InputTextMultiline(label, (char*)imgui_text_buffer.c_str(), capacity, *(ImVec2*)&size, flags);
	return imgui_text_buffer.c_str();
}

IC LPCSTR ImGui_InputTextWithHint(LPCSTR label, LPCSTR hint, LPCSTR text, int capacity = 100, ImGuiInputTextFlags flags = 0, bool& changed = ugly_hack)
{
	imgui_text_buffer = (text && xr_strlen(text)) ? text : "";
	changed = ImGui::InputTextWithHint(label, hint, (char*)imgui_text_buffer.c_str(), capacity, flags);
	return imgui_text_buffer.c_str();
}

IC bool ImGui_CollapsingHeader(LPCSTR label)
{
	return ImGui::CollapsingHeader(label);
}

IC bool ImGui_CollapsingHeader(LPCSTR label, bool* visible)
{
	return ImGui::CollapsingHeader(label, visible);
}

IC bool ImGui_InputFloat2(LPCSTR name, Fvector2& vec, LPCSTR format = "%.3f", ImGuiInputTextFlags flags = 0)
{
	return ImGui::InputFloat2(name, (float*)&vec, format, flags);
}

IC bool ImGui_InputFloat3(LPCSTR name, Fvector& vec, LPCSTR format = "%.3f", ImGuiInputTextFlags flags = 0)
{
	return ImGui::InputFloat3(name, (float*)&vec, format, flags);
}

IC bool ImGui_InputFloat4(LPCSTR name, Fvector4& vec, LPCSTR format = "%.3f", ImGuiInputTextFlags flags = 0)
{
	return ImGui::InputFloat4(name, (float*)&vec, format, flags);
}

IC bool ImGui_ColorButton(LPCSTR name, Fcolor& color, ImGuiColorEditFlags flags = 0, Fvector2 size = Fvector2{ 0,0 })
{
	return ImGui::ColorButton(name, *(ImVec4*)&color, flags, *(ImVec2*)&size);
}

IC bool ImGui_Selectable(LPCSTR name, bool selected, ImGuiSelectableFlags flags = 0, Fvector2 size = Fvector2{ 0,0 })
{
	return ImGui::Selectable(name, selected, flags, *(ImVec2*)&size);
}

IC bool ImGui_BeginMenu(LPCSTR name)
{
	return ImGui::BeginMenu(name, true);
}

IC bool ImGui_BeginListBox(LPCSTR name, Fvector2 size = Fvector2{ 0,0 })
{
	return ImGui::BeginListBox(name, *(ImVec2*)&size);
}

IC void ImGui_SetTooltip(LPCSTR text)
{
	ImGui::SetTooltip(text, 0);
}

IC void ImGui_SetItemTooltip(LPCSTR text)
{
	ImGui::SetItemTooltip(text, 0);
}

IC void ImGui_OpenPopup(LPCSTR name)
{
	ImGui::OpenPopup(name);
}

IC void ImGui_OpenPopup(ImGuiID id)
{
	ImGui::OpenPopup(id);
}

IC bool ImGui_IsPopupOpen(LPCSTR name)
{
	return ImGui::IsPopupOpen(name);
}

IC bool ImGui_BeginTable(LPCSTR name, int colums, ImGuiTableFlags flags = 0, Fvector2 size = Fvector2{ 0,0 }, float width = 0)
{
	return ImGui::BeginTable(name, colums, flags, *(ImVec2*)&size, width);
}

IC void ImGui_TableSetBgColor(ImGuiTableBgTarget target, Fcolor& color, int column_n = -1)
{
	ImGui::TableSetBgColor(target, color.get(), column_n);
}

IC void ImGui_BeginDisabled()
{
	ImGui::BeginDisabled(true);
}

IC void ImGui_PushClipRect(Fvector2 min, Fvector2 max, bool intersect)
{
	ImGui::PushClipRect(*(ImVec2*)&min, *(ImVec2*)&max, intersect);
}

IC Fvector2 ImGui_GetItemRectMin()
{
	return *(Fvector2*)&ImGui::GetItemRectMin();
}

IC Fvector2 ImGui_GetItemRectMax()
{
	return *(Fvector2*)&ImGui::GetItemRectMax();
}

IC Fvector2 ImGui_GetItemRectSize()
{
	return *(Fvector2*)&ImGui::GetItemRectSize();
}

IC bool ImGui_IsKeyPressed(ImGuiKey key)
{
	return ImGui::IsKeyPressed(key, true);
}

IC LPCSTR ImGui_GetKeyName(ImGuiKey key)
{
	return ImGui::GetKeyName(key);
}

IC bool ImGui_Shortcut(ImGuiKeyChord keys)
{
	return ImGui::Shortcut(keys, 0);
}

IC Fvector2 ImGui_CalcTextSize(LPCSTR text, bool hide_after_double_hash = false, float wrap_width = -1.f)
{
	return *(Fvector2*)&ImGui::CalcTextSize(text, 0, hide_after_double_hash, wrap_width);
}

IC bool ImGui_IsMouseHoveringRect(Fvector2 min, Fvector2 max)
{
	return ImGui::IsMouseHoveringRect(*(ImVec2*)&min, *(ImVec2*)&max, true);
}

IC bool ImGui_IsMouseHoveringRect(Fvector2 min, Fvector2 max, bool clip)
{
	return ImGui::IsMouseHoveringRect(*(ImVec2*)&min, *(ImVec2*)&max, clip);
}

IC bool ImGui_IsMousePosValid(Fvector2 pos = {0,0})
{
	return ImGui::IsMousePosValid((ImVec2*)&pos);
}

IC Fvector2 ImGui_GetMousePos()
{
	return *(Fvector2*)&ImGui::GetMousePos();
}

IC Fvector2 ImGui_GetMousePosOnOpeningCurrentPopup()
{
	return *(Fvector2*)&ImGui::GetMousePosOnOpeningCurrentPopup();
}

IC Fvector2 ImGui_GetMouseDragDelta(ImGuiMouseButton button = 0, float lock_treshold = -1.f)
{
	return *(Fvector2*)&ImGui::GetMouseDragDelta(button, lock_treshold);
}

IC LPCSTR ImGui_GetClipboardText()
{
	return ImGui::GetClipboardText();
}

IC Fcolor ImGui_GetStyleColorVec4(ImGuiCol idx)
{
	return *(Fcolor*)&ImGui::GetStyleColorVec4(idx);
}

IC void ImGui_PushStyleVar(ImGuiStyleVar var,Fvector2 val)
{
	ImGui::PushStyleVar(var, *(ImVec2*)&val);
}

IC void ImGui_PopStyleVar()
{
	ImGui::PopStyleVar(1);
}

IC void ImGui_PushStyleColor(ImGuiCol idx, Fcolor& col)
{
	ImGui::PushStyleColor(idx, *(ImVec4*)&col);
}

IC void ImGui_PopStyleColor()
{
	ImGui::PopStyleColor(1);
}

IC void ImGui_PushFont(LPCSTR name)
{
	ImGui::PushFont(Device.imgui().GetFont(name));
}
