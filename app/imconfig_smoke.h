#pragma once
// Dear ImGui config for the headless smoke test: assertions report and exit.
void hsim_imgui_assert(bool ok, const char* expr, const char* file, int line);
#define IM_ASSERT(_EXPR) hsim_imgui_assert(static_cast<bool>(_EXPR), #_EXPR, __FILE__, __LINE__)
