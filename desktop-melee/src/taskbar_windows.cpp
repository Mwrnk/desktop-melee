/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The taskbar's buttons through UI Automation (taskbar_windows.h). */
#include "taskbar_windows.h"

#include <windows.h>

#include <uiautomation.h>

namespace {

template <typename T>
void release(T*& object) {
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

/* The buttons under the taskbar's window: Start, search, Task view, widgets,
 * each app, and the notification area's icons and clock. */
int read_buttons(
    IUIAutomation* uia, HWND bar, const RECT& frame, DesktopMeleeWinSpan* spans, int max) {
    IUIAutomationElement* root = nullptr;
    IUIAutomationCondition* buttons = nullptr;
    IUIAutomationElementArray* found = nullptr;
    int count = 0;
    VARIANT type;
    VariantInit(&type);
    type.vt = VT_I4;
    type.lVal = UIA_ButtonControlTypeId;
    if (SUCCEEDED(uia->ElementFromHandle(bar, &root)) && root != nullptr &&
        SUCCEEDED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &buttons)) &&
        SUCCEEDED(root->FindAll(TreeScope_Descendants, buttons, &found)) && found != nullptr)
    {
        int length = 0;
        found->get_Length(&length);
        const LONG middle = (frame.top + frame.bottom) / 2;
        for (int i = 0; i < length && count < max; i++) {
            IUIAutomationElement* element = nullptr;
            RECT r;
            if (SUCCEEDED(found->GetElement(i, &element)) && element != nullptr &&
                SUCCEEDED(element->get_CurrentBoundingRectangle(&r)))
            {
                /* On the bar, and not something as wide as it (a hidden
                 * flyout's, or the bar's own frame). */
                const bool on_bar = r.right > r.left && r.top <= middle && r.bottom >= middle;
                if (on_bar && r.right - r.left < (frame.right - frame.left) / 2) {
                    spans[count++] = {(float)r.left, (float)r.right};
                }
            }
            release(element);
        }
    }
    release(found);
    release(buttons);
    release(root);
    return count;
}

}  // namespace

extern "C" int desktop_melee_win_taskbar_buttons(DesktopMeleeWinSpan* spans, int max) {
    HWND bar = FindWindowW(L"Shell_TrayWnd", nullptr);
    RECT frame;
    if (bar == nullptr || max < 1 || !GetWindowRect(bar, &frame)) {
        return 0;
    }
    /* S_OK and S_FALSE are ours to undo; RPC_E_CHANGED_MODE means the thread
     * is already in COM's other apartment, which UI Automation is fine in. */
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        return 0;
    }
    int count = 0;
    IUIAutomation* uia = nullptr;
    if (SUCCEEDED(CoCreateInstance(
            CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, (void**)&uia)))
    {
        /* A busy explorer must not stall the game for long. */
        IUIAutomation2* uia2 = nullptr;
        if (SUCCEEDED(uia->QueryInterface(IID_IUIAutomation2, (void**)&uia2))) {
            uia2->put_ConnectionTimeout(500);
            uia2->put_TransactionTimeout(1000);
            release(uia2);
        }
        count = read_buttons(uia, bar, frame, spans, max - 1);
        release(uia);
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    if (count == 0) {
        return 0;
    }
    /* The notification area as a whole, in case its clock isn't a button
     * (Windows 10). */
    HWND tray = FindWindowExW(bar, nullptr, L"TrayNotifyWnd", nullptr);
    RECT r;
    if (tray != nullptr && GetWindowRect(tray, &r) && r.right > r.left) {
        spans[count++] = {(float)r.left, (float)r.right};
    }
    return count;
}
