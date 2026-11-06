/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Windows overlay window (overlay_windows.h). */
#include "overlay_windows.h"

#include <windows.h>

#include <dcomp.h>
#include <dxgi1_2.h>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_system.h>

extern "C" {
#include "desktop_melee_game.h"
}

namespace {

/* IDXGIFactory2's vtable: IUnknown (3), IDXGIObject (4), IDXGIFactory (5),
 * IDXGIFactory1 (EnumAdapters1, IsCurrent), then IsWindowedStereoEnabled and
 * CreateSwapChainForHwnd. Every factory dxgi.dll makes shares the one table. */
constexpr int kCreateSwapChainForHwndSlot = 15;

using CreateSwapChainForHwnd = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2* factory, IUnknown* device,
    HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
    IDXGIOutput* output, IDXGISwapChain1** swap_chain);

CreateSwapChainForHwnd s_original = nullptr;
bool s_transparent = false;

/* The composition tree on the overlay: a visual showing the swap chain, with
 * an effect group for Pause and hide's opacity. */
IDCompositionDevice* s_dcomp = nullptr;
IDCompositionTarget* s_target = nullptr;
IDCompositionVisual* s_visual = nullptr;
IDCompositionEffectGroup* s_effect = nullptr;

HWND window_hwnd(SDL_Window* window) {
    return static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

/* The overlay is the SDL window aurora made transparent; the settings and
 * welcome windows are not. */
bool is_overlay(HWND hwnd) {
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    bool overlay = false;
    for (int i = 0; windows != nullptr && i < count; i++) {
        if (window_hwnd(windows[i]) == hwnd) {
            overlay = (SDL_GetWindowFlags(windows[i]) & SDL_WINDOW_TRANSPARENT) != 0;
            break;
        }
    }
    SDL_free(windows);
    return overlay;
}

/* Shows swap_chain on the window, building the composition tree the first
 * time. A new swap chain (Dawn may make one again) replaces the old. */
HRESULT attach(HWND hwnd, IDXGISwapChain1* swap_chain) {
    HRESULT hr = S_OK;
    if (s_dcomp == nullptr) {
        /* IDCompositionDevice's id, spelled out: older mingw-w64 headers
         * don't declare it for __uuidof. */
        static const IID kIDCompositionDevice = {
            0xc37ea93a, 0xe7aa, 0x450d, {0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3}};
        hr = DCompositionCreateDevice(
            nullptr, kIDCompositionDevice, reinterpret_cast<void**>(&s_dcomp));
        if (FAILED(hr)) {
            return hr;
        }
        /* topmost: above the window's own (empty) redirection surface. */
        if (FAILED(hr = s_dcomp->CreateTargetForHwnd(hwnd, TRUE, &s_target)) ||
            FAILED(hr = s_dcomp->CreateVisual(&s_visual)) ||
            FAILED(hr = s_dcomp->CreateEffectGroup(&s_effect)) ||
            FAILED(hr = s_visual->SetEffect(s_effect)) || FAILED(hr = s_target->SetRoot(s_visual)))
        {
            return hr;
        }
    }
    if (FAILED(hr = s_visual->SetContent(swap_chain))) {
        return hr;
    }
    return s_dcomp->Commit();
}

HRESULT STDMETHODCALLTYPE create_swap_chain_for_hwnd(IDXGIFactory2* factory, IUnknown* device,
    HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
    IDXGIOutput* output, IDXGISwapChain1** swap_chain) {
    if (desc == nullptr || swap_chain == nullptr || !is_overlay(hwnd)) {
        return s_original(factory, device, hwnd, desc, fullscreen, output, swap_chain);
    }
    DXGI_SWAP_CHAIN_DESC1 composed = *desc;
    composed.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    composed.Scaling = DXGI_SCALING_STRETCH; /* the only scaling composition allows */
    HRESULT hr = factory->CreateSwapChainForComposition(device, &composed, output, swap_chain);
    if (SUCCEEDED(hr)) {
        hr = attach(hwnd, *swap_chain);
        if (SUCCEEDED(hr)) {
            s_transparent = true;
            SDL_Log("desktop-melee: overlay: premultiplied swap chain on a composition visual "
                    "(%ux%u, format %d)",
                composed.Width, composed.Height, (int)composed.Format);
            return S_OK;
        }
        (*swap_chain)->Release();
        *swap_chain = nullptr;
    }
    SDL_Log("desktop-melee: overlay: no composition swap chain (0x%08lx); the overlay will be "
            "opaque",
        (unsigned long)hr);
    s_transparent = false;
    return s_original(factory, device, hwnd, desc, fullscreen, output, swap_chain);
}

}  // namespace

extern "C" bool desktop_melee_win_overlay_prepare(void) {
    if (s_original != nullptr) {
        return true;
    }
    IDXGIFactory2* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) {
        SDL_Log("desktop-melee: overlay: no DXGI factory (0x%08lx), so a normal window",
            (unsigned long)hr);
        return false;
    }
    void** vtable = *reinterpret_cast<void***>(factory);
    void** slot = &vtable[kCreateSwapChainForHwndSlot];
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &protection)) {
        SDL_Log("desktop-melee: overlay: can't reach DXGI's swap chains, so a normal window");
        factory->Release();
        return false;
    }
    s_original = reinterpret_cast<CreateSwapChainForHwnd>(*slot);
    *slot = reinterpret_cast<void*>(&create_swap_chain_for_hwnd);
    VirtualProtect(slot, sizeof *slot, protection, &protection);
    /* The table lives in dxgi.dll, which this keeps loaded. */
    factory->Release();
    return true;
}

extern "C" bool desktop_melee_win_overlay_transparent(void) {
    return s_transparent;
}

extern "C" void desktop_melee_win_overlay_setup(SDL_Window* window) {
    HWND hwnd = window_hwnd(window);
    if (hwnd == nullptr) {
        return;
    }
    /* WS_EX_LAYERED | WS_EX_TRANSPARENT: hit-testing skips the window, so
     * clicks reach whatever is underneath. A layered window shows only once
     * given its alpha. WS_EX_TOOLWINDOW keeps it out of the taskbar and
     * Alt-Tab, which Windows reads when the window is shown, so it is hidden
     * and shown again around the change. */
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const LONG_PTR wanted = (style & ~(LONG_PTR)WS_EX_APPWINDOW) | WS_EX_LAYERED |
                            WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (style != wanted) {
        const bool visible = IsWindowVisible(hwnd) != FALSE;
        if (visible) {
            ShowWindow(hwnd, SW_HIDE);
        }
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, wanted);
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        if (visible) {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }
    }
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

extern "C" void desktop_melee_win_overlay_set_faded(SDL_Window* window, bool faded) {
    /* Not SDL_SetWindowOpacity: at 1.0 it takes WS_EX_LAYERED off, and with
     * it click-through. */
    if (s_effect != nullptr) {
        s_effect->SetOpacity(faded ? 0.0f : 1.0f);
        s_dcomp->Commit();
        return;
    }
    HWND hwnd = window_hwnd(window);
    if (hwnd != nullptr) {
        SetLayeredWindowAttributes(hwnd, 0, faded ? 0 : 255, LWA_ALPHA);
    }
}

/* ---- the Pause and hide hotkey ------------------------------------------ */

namespace {

constexpr int kHotkeyId = 0x444d; /* "DM"; any id the thread doesn't use */
bool s_hotkey = false;

/* Sees each message before SDL does. WM_HOTKEY is a thread message (no
 * window), so only this sees it. */
bool SDLCALL message_hook(void* userdata, MSG* msg) {
    (void)userdata;
    if (msg->message == WM_HOTKEY && msg->wParam == kHotkeyId) {
        desktop_melee_toggle_pause_and_hide();
        return false;
    }
    return true;
}

}  // namespace

extern "C" bool desktop_melee_win_hotkey_install(void) {
    if (s_hotkey) {
        return true;
    }
    if (!RegisterHotKey(nullptr, kHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'M')) {
        SDL_Log("desktop-melee: Ctrl+Alt+M is taken by another app (%lu): no Pause and hide key",
            (unsigned long)GetLastError());
        return false;
    }
    SDL_SetWindowsMessageHook(message_hook, nullptr);
    s_hotkey = true;
    SDL_Log("desktop-melee: Pause and hide: Ctrl+Alt+M");
    return true;
}

extern "C" const char* desktop_melee_win_hotkey_label(void) {
    return s_hotkey ? "Ctrl+Alt+M" : "";
}

extern "C" void desktop_melee_win_hotkey_remove(void) {
    if (s_hotkey) {
        UnregisterHotKey(nullptr, kHotkeyId);
        SDL_SetWindowsMessageHook(nullptr, nullptr);
        s_hotkey = false;
    }
}
