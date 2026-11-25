// Headless smoke test for the viewer: drives the UI for a few hundred frames
// with no window or GPU, clicking through the main features. Catches Dear
// ImGui usage errors (mismatched Begin/End, ID clashes trip IM_ASSERT) and
// crashes in the viewer's own logic. Runs in CI.

#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "viewer.hpp"

// Turn ImGui assertions into test failures instead of aborts.
void hsim_imgui_assert(bool ok, const char* expr, const char* file, int line) {
    if (!ok) {
        std::fprintf(stderr, "ImGui assertion failed: %s (%s:%d)\n", expr, file, line);
        std::exit(1);
    }
}

namespace {
std::filesystem::path g_save_path;
std::filesystem::path open_dialog() { return g_save_path; }
std::filesystem::path save_dialog() { return g_save_path; }

void run_frames(hsim::app::Viewer& viewer, int frames) {
    for (int i = 0; i < frames; ++i) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1600, 1000);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        viewer.frame(1.0 / 60.0, 50.0);
        ImGui::Render();
    }
}
}  // namespace

int main() {
    hsim::SessionDir saves;
    g_save_path = saves.path() / "smoke.hsim";

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;  // pretend; we never draw
    io.Fonts->AddFontDefault();

    {
        hsim::app::Platform platform{&open_dialog, &save_dialog};
        hsim::app::Viewer viewer(platform);
        run_frames(viewer, 10);
        viewer.debug_run_until_year(-9900);
        run_frames(viewer, 200);
        if (viewer.debug_today() != hsim::first_day_of_year(-9900)) throw std::runtime_error("run-until did not stop at the target year");
        if (!viewer.debug_select_some_event()) throw std::runtime_error("no events to select");
        run_frames(viewer, 10);
        if (!viewer.debug_save_and_reload()) throw std::runtime_error("save and reload changed the world");
        run_frames(viewer, 10);
    }
    ImGui::DestroyContext();
    std::puts("viewer smoke test passed");
    return 0;
}
