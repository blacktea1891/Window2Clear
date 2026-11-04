#include "../Window2Clear/WindowState.h"
#include <stdio.h>
#define CHECK(expr) do { if (!(expr)) { printf("FAIL line %d: %s (error %lu)\n", __LINE__, #expr, GetLastError()); return 1; } } while (0)
int wheelCount = 0;
UINT wheelMessage = 0;
WPARAM wheelParam = 0;
HWND wheelWindow = nullptr;
LRESULT CALLBACK WheelReceiver(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
        ++wheelCount; wheelMessage = msg; wheelParam = wp; wheelWindow = hwnd;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
void PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}

struct TaskbarStub : ITaskbarList {
    int added = 0, removed = 0;
    bool failAdd = false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE HrInit() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddTab(HWND) override { ++added; return failAdd ? E_FAIL : S_OK; }
    HRESULT STDMETHODCALLTYPE DeleteTab(HWND) override { ++removed; return S_OK; }
    HRESULT STDMETHODCALLTYPE ActivateTab(HWND) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetActiveAlt(HWND) override { return S_OK; }
};

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WheelReceiver;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"Window2Clear.TestReceiver";
    CHECK(RegisterClassW(&wc));
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Window2Clear test", WS_OVERLAPPEDWINDOW,
        100, 100, 400, 300, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    CHECK(hwnd);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    WindowStateManager windows;
    LONG_PTR original = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    CHECK(windows.SetAlpha(hwnd, 160));
    CHECK(windows.ToggleClick(hwnd));
    CHECK(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT);
    RECT content;
    CHECK(GetClientRect(hwnd, &content));
    HWND child = CreateWindowExW(0, wc.lpszClassName, L"Reading surface", WS_CHILD | WS_VISIBLE,
        0, 0, content.right, content.bottom, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
    CHECK(child);
    PumpMessages();
    HWND foreground = GetForegroundWindow();
    CHECK(windows.ForwardWheel(WM_MOUSEWHEEL, -WHEEL_DELTA, POINT{0,0}, 0, false));
    MSG queued = {};
    CHECK(PeekMessageW(&queued, nullptr, WM_MOUSEWHEEL, WM_MOUSEWHEEL, PM_NOREMOVE));
    printf("Wheel queued: receiver=%p child=%p root=%p delta=%d\n", queued.hwnd, child, hwnd, (short)HIWORD(queued.wParam));
    PumpMessages();
    CHECK(wheelCount == 1 && (short)HIWORD(wheelParam) == -WHEEL_DELTA);
    CHECK(wheelWindow == child);
    CHECK(GetForegroundWindow() == foreground);
    CHECK(windows.ForwardWheel(WM_MOUSEHWHEEL, 60, POINT{0,0}, MK_SHIFT, false));
    PumpMessages();
    CHECK(wheelCount == 2 && wheelMessage == WM_MOUSEHWHEEL && LOWORD(wheelParam) == MK_SHIFT && (short)HIWORD(wheelParam) == 60);
    CHECK(!windows.ForwardWheel(WM_LBUTTONDOWN, 120, POINT{0,0}, 0, false));
    CHECK(!windows.ForwardWheel(WM_MOUSEWHEEL, 120, POINT{-30000,-30000}, 0));
    CHECK(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT);
    DestroyWindow(child);
    CHECK(windows.SetAlpha(hwnd, 255));
    CHECK(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED);
    CHECK(windows.ToggleClick(nullptr)); // recovery independent of foreground
    CHECK(!windows.ForwardWheel(WM_MOUSEWHEEL, 120, POINT{0,0}, 0, false));
    CHECK(!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT));
    CHECK(!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED));
    CHECK(windows.ToggleHidden(hwnd));
    CHECK(!IsWindowVisible(hwnd));
    CHECK(!windows.ForwardWheel(WM_MOUSEWHEEL, 120, POINT{0,0}, 0, false));
    CHECK(windows.ToggleHidden(nullptr));
    CHECK(IsWindowVisible(hwnd));
    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    CHECK(windows.ToggleHidden(hwnd));
    CHECK(windows.ToggleHidden(nullptr));
    CHECK(IsZoomed(hwnd));
    ShowWindow(hwnd, SW_MINIMIZE);
    CHECK(windows.ToggleHidden(hwnd));
    CHECK(windows.ToggleHidden(nullptr));
    CHECK(IsIconic(hwnd));
    CHECK(windows.SetAlpha(hwnd, 100));
    CHECK(windows.ToggleClick(hwnd));
    CHECK(windows.ToggleHidden(hwnd));
    CHECK(windows.RestoreAll());
    CHECK(IsWindowVisible(hwnd));
    CHECK(IsIconic(hwnd));
    CHECK(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) == original);
    CHECK(windows.RestoreAll()); // idempotent

    ShowWindow(hwnd, SW_RESTORE);
    RECT beforePin, afterPin;
    CHECK(GetWindowRect(hwnd, &beforePin));
    foreground = GetForegroundWindow();
    CHECK(windows.ToggleTopmost(hwnd));
    CHECK(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST);
    CHECK(GetWindowRect(hwnd, &afterPin) && EqualRect(&beforePin, &afterPin));
    CHECK(GetForegroundWindow() == foreground);
    CHECK(windows.ToggleTopmost(hwnd));
    CHECK(!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST));
    CHECK(windows.ToggleTopmost(hwnd));
    CHECK(windows.SetAlpha(hwnd, 160));
    CHECK(windows.ToggleClick(hwnd));
    CHECK(windows.ToggleHidden(hwnd));
    CHECK(windows.RestoreAll());
    CHECK(IsWindowVisible(hwnd));
    CHECK(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) == original);

    CHECK(SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    CHECK(windows.ToggleTopmost(hwnd));
    CHECK(!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST));
    CHECK(windows.RestoreAll());
    CHECK(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST);
    CHECK(SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));

    HWND second = CreateWindowExW(WS_EX_LAYERED, wc.lpszClassName, L"Uninitialized layered target",
        WS_OVERLAPPEDWINDOW, 20, 20, 200, 200, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    CHECK(second);
    CHECK(windows.ToggleTopmost(hwnd));
    CHECK(windows.ToggleTopmost(second)); // pinning does not require readable layer attributes
    CHECK(GetWindowLongPtrW(second, GWL_EXSTYLE) & WS_EX_TOPMOST);
    CHECK(!IsWindowVisible(second)); // pinning does not show hidden windows
    CHECK(!windows.SetAlpha(second, 100)); // unsupported layers remain protected
    CHECK(windows.RestoreAll());
    CHECK(!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST));
    CHECK(!(GetWindowLongPtrW(second, GWL_EXSTYLE) & WS_EX_TOPMOST));
    CHECK(GetWindowLongPtrW(second, GWL_EXSTYLE) & WS_EX_LAYERED);
    DestroyWindow(second);
    CHECK(!windows.ToggleTopmost(nullptr));

    TaskbarStub taskbar;
    WindowStateManager tabs(&taskbar);
    tabs.hideTaskbar = true;
    CHECK(tabs.SetAlpha(hwnd, 150));
    CHECK(taskbar.removed == 1 && taskbar.added == 0);
    CHECK(!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW));
    CHECK(tabs.SetAlpha(hwnd, 255));
    CHECK(taskbar.added == 1);
    CHECK(tabs.SetAlpha(hwnd, 150));
    tabs.hideTaskbar = false;
    CHECK(tabs.SyncTaskbar());
    CHECK(taskbar.added == 2);
    tabs.hideTaskbar = true;
    CHECK(tabs.SyncTaskbar());
    taskbar.failAdd = true;
    CHECK(!tabs.RestoreAll()); // failed restoration remains retryable
    taskbar.failAdd = false;
    CHECK(tabs.RestoreAll());
    tabs.Shutdown();

    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, original | WS_EX_LAYERED);
    CHECK(SetLayeredWindowAttributes(hwnd, RGB(10,20,30), 190, LWA_ALPHA | LWA_COLORKEY));
    CHECK(windows.SetAlpha(hwnd, 70));
    CHECK(windows.ToggleClick(hwnd));
    CHECK(windows.RestoreAll());
    BYTE alpha = 0; COLORREF color = 0; DWORD flags = 0;
    CHECK(GetLayeredWindowAttributes(hwnd, &color, &alpha, &flags));
    CHECK(alpha == 190 && color == RGB(10,20,30) && flags == (LWA_ALPHA | LWA_COLORKEY));
    CHECK(windows.SetAlpha(hwnd, 80));
    RemovePropW(hwnd, L"Window2Clear.State.v1"); // simulate lost identity/reused handle
    CHECK(windows.RestoreAll());
    CHECK(GetLayeredWindowAttributes(hwnd, &color, &alpha, &flags) && alpha == 80);
    CHECK(windows.SetAlpha(hwnd, 90));
    DestroyWindow(hwnd);
    CHECK(windows.RestoreAll()); // destroyed target is harmless
    CHECK(!windows.ToggleClick(nullptr));
    CHECK(!windows.ToggleHidden(nullptr));
    windows.Shutdown();
    CoUninitialize();
    puts("PASS: window state transitions, wheel delivery, topmost, original layers, identity, recovery");
    return 0;
}
