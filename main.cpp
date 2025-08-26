#include "includes.h"
#include "Menu/menu.h"

#define WIN32_LEAN_AND_MEAN

HINSTANCE dll_handle;
typedef HRESULT(__stdcall* present)(IDXGISwapChain*, UINT, UINT);
present p_present;
present p_present_target;

bool get_present_pointer()
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = FindWindow(NULL, L"Counter-Strike 2"); 
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swap_chain = nullptr;
    ID3D11Device* device = nullptr;

    const D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0,
        feature_levels,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &swap_chain,
        &device,
        nullptr,
        nullptr);

    if (FAILED(hr) || !swap_chain || !device) {
        if (swap_chain) swap_chain->Release();
        if (device) device->Release();
        OutputDebugString(L"Failed to create device and swap chain\n");
        return false;
    }

    void** p_vtable = *reinterpret_cast<void***>(swap_chain);
    p_present_target = (present)p_vtable[8];
    swap_chain->Release();
    device->Release();
    return true;
}

WNDPROC oWndProc;
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

bool init = false;
HWND window = NULL;
ID3D11Device* p_device = NULL;
ID3D11DeviceContext* p_context = NULL;
ID3D11RenderTargetView* mainRenderTargetView = NULL;

static HRESULT __stdcall detour_present(IDXGISwapChain* p_swap_chain, UINT sync_interval, UINT flags) {
    if (!init) {
        if (FAILED(p_swap_chain->GetDevice(__uuidof(ID3D11Device), (void**)&p_device))) {
            OutputDebugString(L"Failed to get device\n");
            return p_present(p_swap_chain, sync_interval, flags);
        }

        p_device->GetImmediateContext(&p_context);

        DXGI_SWAP_CHAIN_DESC sd;
        p_swap_chain->GetDesc(&sd);
        window = sd.OutputWindow;

        ID3D11Texture2D* pBackBuffer = nullptr;
        if (SUCCEEDED(p_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer))) {
            HRESULT hr = p_device->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
            pBackBuffer->Release();
            if (FAILED(hr)) {
                OutputDebugString(L"Failed to create render target view\n");
                return p_present(p_swap_chain, sync_interval, flags);
            }
        }
        else {
            OutputDebugString(L"Failed to get back buffer\n");
            return p_present(p_swap_chain, sync_interval, flags);
        }

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        if (!ImGui_ImplWin32_Init(window)) {
            OutputDebugString(L"ImGui Win32 initialization failed\n");
            return p_present(p_swap_chain, sync_interval, flags);
        }
        if (!ImGui_ImplDX11_Init(p_device, p_context)) {
            OutputDebugString(L"ImGui DX11 initialization failed\n");
            return p_present(p_swap_chain, sync_interval, flags);
        }

        oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);
        if (!oWndProc) {
            OutputDebugString(L"Failed to set WndProc\n");
            return p_present(p_swap_chain, sync_interval, flags);
        }

        init = true;
        OutputDebugString(L"Initialization completed\n");
    }

    menu(); // <-- your ImGui menu here !!!
    /*
    bool show_main = true;
    bool esp = false; 
    float speed = 0.5f;
    
    void menu()
    {
      ImGui_ImplDX11_NewFrame();
      ImGui_ImplWin32_NewFrame();
      ImGui::NewFrame();

      // ImGui::ShowDemoWindow();

      if (GetAsyncKeyState(VK_DELETE) & 1)
      {
          show_main = !show_main;
      }

      if (show_main)
      {
          ImGui::Begin("main", &show_main);
          ImGui::SetWindowSize(ImVec2(600, 450), ImGuiCond_Always);
          ImGui::Text("hello");
          ImGui::Checkbox("esp", &esp);
          ImGui::SliderFloat("Speed", &speed, 0.01f, 1.f);
          ImGui::End();
      }
      ImGui::EndFrame();
      ImGui::Render();
      } */

    if (mainRenderTargetView) {
        p_context->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return p_present(p_swap_chain, sync_interval, flags);
}

DWORD __stdcall EjectThread(LPVOID lpParameter) {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (mainRenderTargetView) {
        mainRenderTargetView->Release();
        mainRenderTargetView = NULL;
    }
    if (p_context) {
        p_context->Release();
        p_context = NULL;
    }
    if (p_device) {
        p_device->Release();
        p_device = NULL;
    }
    if (window && oWndProc) {
        SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)oWndProc);
    }

    Sleep(100);
    FreeLibraryAndExitThread(dll_handle, 0);
    return 0;
}

int WINAPI main()
{
    if (!get_present_pointer()) {
        OutputDebugString(L"Failed to get Present pointer\n");
        return 1;
    }

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        OutputDebugString(L"MinHook initialization failed\n");
        return 1;
    }

    if (MH_CreateHook(reinterpret_cast<void**>(p_present_target), &detour_present, reinterpret_cast<void**>(&p_present)) != MH_OK) {
        OutputDebugString(L"Failed to create hook\n");
        return 1;
    }

    if (MH_EnableHook(p_present_target) != MH_OK) {
        OutputDebugString(L"Failed to enable hook\n");
        return 1;
    }

    while (true) {
        Sleep(100);

        if (GetAsyncKeyState(VK_NUMPAD1) & 1) {
            break; // exit - NUMPAD1
        }
    }

    // Clear
    if (MH_DisableHook(MH_ALL_HOOKS) != MH_OK) {
        OutputDebugString(L"Failed to disable hooks\n");
        return 1;
    }
    if (MH_Uninitialize() != MH_OK) {
        OutputDebugString(L"MinHook uninitialization failed\n");
        return 1;
    }

    CreateThread(0, 0, EjectThread, 0, 0, 0);
    return 0;
}

BOOL __stdcall DllMain(HINSTANCE hModule, DWORD dwReason, LPVOID lpReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        dll_handle = hModule;
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)main, NULL, 0, NULL);
    }
    return TRUE;
}
