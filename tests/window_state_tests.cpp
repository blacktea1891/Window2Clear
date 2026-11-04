#include "../Window2Clear/WindowState.h"
#include <stdio.h>
#define CHECK(expr) do { if (!(expr)) { printf("FAIL line %d: %s (error %lu)\n", __LINE__, #expr, GetLastError()); return 1; } } while (0)

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"Window2Clear test", WS_OVERLAPPEDWINDOW,
        100, 100, 400, 300, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    CHECK(hwnd);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    WindowStateManager windows;
    LONG_PTR original = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    CHECK(windows.SetAlpha(hwnd, 160));
    CHECK(windows.ToggleClick(hwnd));
    CHECK(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT);
    CHECK(windows.SetAlpha(hwnd, 255));
    CHECK(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED);
    CHECK(windows.ToggleClick(nullptr)); // recovery independent of foreground
    CHECK(!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT));
    CHECK(!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED));
    CHECK(windows.ToggleHidden(hwnd));
    CHECK(!IsWindowVisible(hwnd));
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
    puts("PASS: window state transitions, original layers, identity, recovery");
    return 0;
}
