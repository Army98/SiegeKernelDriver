#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <tlhelp32.h>
#include <bit>
#include <iostream>
#include <chrono>
#include <cmath>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")

// ------------------------------------------------------------
// Globals / D3D
// ------------------------------------------------------------

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// ------------------------------------------------------------
// Math
// ------------------------------------------------------------

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

// ------------------------------------------------------------
// Position tracking (20s unchanged = skip draw)
// ------------------------------------------------------------

struct TrackedPos
{
    Vec3 lastPos{};
    std::chrono::steady_clock::time_point lastChange{};
    bool initialized = false;
};

static TrackedPos g_tracked[100];

inline bool PosChanged(const Vec3& a, const Vec3& b)
{
    constexpr float eps = 0.001f;
    return fabsf(a.x - b.x) > eps ||
        fabsf(a.y - b.y) > eps ||
        fabsf(a.z - b.z) > eps;
}

// ------------------------------------------------------------

bool WorldToScreen(const Vec3& pos, const float m[16], float sw, float sh, Vec2& out)
{
    float clipX = m[0] * pos.x + m[4] * pos.y + m[8] * pos.z + m[12];
    float clipY = m[1] * pos.x + m[5] * pos.y + m[9] * pos.z + m[13];
    float clipW = m[3] * pos.x + m[7] * pos.y + m[11] * pos.z + m[15];

    if (clipW <= 0.01f)
        return false;

    float ndcX = clipX / clipW;
    float ndcY = clipY / clipW;

    out.x = (ndcX + 1.0f) * 0.5f * sw;
    out.y = (1.0f - ndcY) * 0.5f * sh;
    return true;
}

DWORD GetPID(const wchar_t* name)
{
    PROCESSENTRY32W pe{ sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (!_wcsicmp(pe.szExeFile, name))
            {
                CloseHandle(snap);
                return pe.th32ProcessID;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return 0;
}

void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) && backBuffer)
    {
        g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
        backBuffer->Release();
    }
}

bool CreateDeviceD3D(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl;
    if (D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    if (msg == WM_DESTROY)
        PostQuitMessage(0);

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------

int main()
{
    std::cout << GetCurrentProcessId() << std::endl;

    // Wait for game
    while (true)
    {
        Sleep(100);
        DWORD siegePid = GetPID(L"RainbowSix.exe");
        if (siegePid)
        {
            Sleep(50000);
            siegePid = GetPID(L"RainbowSix.exe");
            *(DWORD*)0x1402BAB40 = siegePid;
            break;
        }
    }

    WNDCLASSEX wc{ sizeof(wc), CS_CLASSDC, WndProc };
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"overlay";
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        wc.lpszClassName, L"", WS_POPUP,
        0, 0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        nullptr, nullptr, wc.hInstance, nullptr);

    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    CreateDeviceD3D(hwnd);
    ShowWindow(hwnd, SW_SHOW);

    ImGui::CreateContext();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    MSG msg{};
    while (msg.message != WM_QUIT)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        float viewMatrix[16];
        for (int i = 0; i < 16; i++)
        {
            viewMatrix[i] = std::bit_cast<float>(*(UINT32*)(0x1402BAB48 + i * 4));
        }

        ImDrawList* draw = ImGui::GetForegroundDrawList();
        ImGuiIO& io = ImGui::GetIO();
        auto now = std::chrono::steady_clock::now();

        for (int i = 0; i < 200; i++)
        {
            if (GetAsyncKeyState(VK_INSERT) & 0x8000)
            {
                *(UINT32*)(0x1402BABAC + i * 12 + 0) = 0;
                *(UINT32*)(0x1402BABAC + i * 12 + 4) = 0;
                *(UINT32*)(0x1402BABAC + i * 12 + 8) = 0;
            }

            Vec3 pos{
                std::bit_cast<float>(*(UINT32*)(0x1402BABAC + i * 12 + 0)),
                std::bit_cast<float>(*(UINT32*)(0x1402BABAC + i * 12 + 4)),
                std::bit_cast<float>(*(UINT32*)(0x1402BABAC + i * 12 + 8))
            };

            Vec2 screen;
            if (!WorldToScreen(pos, viewMatrix, io.DisplaySize.x, io.DisplaySize.y, screen))
                continue;

            float baseH = 45.0f;
            float distScale = fabsf(screen.y - io.DisplaySize.y * 0.5f) * 0.0025f;
            float h = baseH / (1.0f + distScale);
            if (h < 18.0f) h = 18.0f;
            if (h > 45.0f) h = 45.0f;


            float w = h * 0.55f;
            float yOffset = 2.5f;
            float sideSegment = h * 0.25f;

            ImVec2 min(screen.x - w * 0.5f, screen.y - h + yOffset);
            ImVec2 max(screen.x + w * 0.5f, screen.y + yOffset);


            ImU32 col = IM_COL32(255, 255, 160, 255);

            draw->AddRect(
                ImVec2(min.x, min.y),
                ImVec2(max.x, max.y),
                col,
                0.0f,    // rounding (0 = sharp corners)
                0,       // flags
                1.5f     // thickness
            );

        }

        ImGui::Render();

        const float clear[4] = { 0,0,0,1 };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    return 0;
}
