// Viewer entry point: a Win32 window rendering Dear ImGui with Direct3D 11.
// Adapted from Dear ImGui's example_win32_directx11. All UI lives in Viewer.

#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>

#include <chrono>
#include <filesystem>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "viewer.hpp"

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swap_chain = nullptr;
bool g_swap_chain_occluded = false;
UINT g_resize_width = 0, g_resize_height = 0;
ID3D11RenderTargetView* g_render_target = nullptr;
HWND g_hwnd = nullptr;

void create_render_target() {
    ID3D11Texture2D* back_buffer = nullptr;
    g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    g_device->CreateRenderTargetView(back_buffer, nullptr, &g_render_target);
    back_buffer->Release();
}

void cleanup_render_target() {
    if (g_render_target) {
        g_render_target->Release();
        g_render_target = nullptr;
    }
}

bool create_device(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                                D3D11_SDK_VERSION, &sd, &g_swap_chain, &g_device, &level, &g_context);
    if (res == DXGI_ERROR_UNSUPPORTED)  // fall back to the WARP software rasterizer
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                            &sd, &g_swap_chain, &g_device, &level, &g_context);
    if (res != S_OK) return false;

    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(g_swap_chain->GetParent(IID_PPV_ARGS(&factory)))) {
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
    }
    create_render_target();
    return true;
}

void cleanup_device() {
    cleanup_render_target();
    if (g_swap_chain) g_swap_chain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
    g_swap_chain = nullptr;
    g_context = nullptr;
    g_device = nullptr;
}

std::filesystem::path file_dialog(bool save) {
    wchar_t buffer[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"Human-sim worlds (*.hsim)\0*.hsim\0All files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"hsim";
    ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? std::filesystem::path(buffer) : std::filesystem::path();
}

std::filesystem::path open_dialog() { return file_dialog(false); }
std::filesystem::path save_dialog() { return file_dialog(true); }

}  // namespace

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
    switch (msg) {
        case WM_SIZE:
            if (wparam == SIZE_MINIMIZED) return 0;
            g_resize_width = static_cast<UINT>(LOWORD(lparam));
            g_resize_height = static_cast<UINT>(HIWORD(lparam));
            return 0;
        case WM_SYSCOMMAND:
            if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    ImGui_ImplWin32_EnableDpiAwareness();
    float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, wnd_proc, 0L, 0L, instance, nullptr, nullptr, nullptr, nullptr,
                      L"HumanSimViewer", nullptr};
    ::RegisterClassExW(&wc);
    g_hwnd = ::CreateWindowW(wc.lpszClassName, L"Human History Simulator", WS_OVERLAPPEDWINDOW, 100, 100,
                             static_cast<int>(1400 * scale), static_cast<int>(900 * scale), nullptr, nullptr,
                             wc.hInstance, nullptr);
    if (!create_device(g_hwnd)) {
        cleanup_device();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        ::MessageBoxW(nullptr, L"Could not initialise Direct3D 11.", L"Human History Simulator", MB_ICONERROR);
        return 1;
    }
    ::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    hsim::app::Platform platform;
    platform.open_file_dialog = &open_dialog;
    platform.save_file_dialog = &save_dialog;

    int exit_code = 0;
    {
        hsim::app::Viewer viewer(platform);
        auto last = std::chrono::steady_clock::now();
        bool done = false;
        while (!done) {
            MSG msg;
            while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (msg.message == WM_QUIT) done = true;
            }
            if (done || viewer.wants_quit()) break;

            if (g_swap_chain_occluded && g_swap_chain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
                ::Sleep(10);
                continue;
            }
            g_swap_chain_occluded = false;

            if (g_resize_width != 0 && g_resize_height != 0) {
                cleanup_render_target();
                g_swap_chain->ResizeBuffers(0, g_resize_width, g_resize_height, DXGI_FORMAT_UNKNOWN, 0);
                g_resize_width = g_resize_height = 0;
                create_render_target();
            }

            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - last).count();
            last = now;

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            // Leave the simulation most of a 60 Hz frame; the UI needs the rest.
            viewer.frame(dt, 12.0);
            ImGui::Render();

            const float clear[4] = {0.08f, 0.08f, 0.09f, 1.0f};
            g_context->OMSetRenderTargets(1, &g_render_target, nullptr);
            g_context->ClearRenderTargetView(g_render_target, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            HRESULT hr = g_swap_chain->Present(1, 0);  // vsync
            g_swap_chain_occluded = (hr == DXGI_STATUS_OCCLUDED);
        }
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanup_device();
    ::DestroyWindow(g_hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return exit_code;
}
