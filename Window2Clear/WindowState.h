#pragma once
#include <windows.h>
#include <shobjidl.h>
#include <map>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

// Only restore windows still carrying our per-window identity marker. HWNDs can be reused.
class WindowStateManager {
    struct State {
        LONG_PTR style;
        BYTE alpha = 255;
        COLORREF color = 0;
        DWORD flags = LWA_ALPHA;
        int currentAlpha = 255;
        bool click = false, hidden = false, tabRemoved = false, layeredChanged = false;
        WINDOWPLACEMENT placement = { sizeof(WINDOWPLACEMENT) };
    };
    std::map<HWND, State> states;
    HWND clickTarget = nullptr, hiddenTarget = nullptr;
    ITaskbarList* taskbar = nullptr;
    const wchar_t* Marker() const { return L"Window2Clear.State.v1"; }
    bool Valid(HWND hwnd, State& s) {
        return IsWindow(hwnd) && GetPropW(hwnd, Marker()) == reinterpret_cast<HANDLE>(&s);
    }
    State* Track(HWND hwnd) {
        auto it = states.find(hwnd);
        if (it != states.end()) {
            if (Valid(hwnd, it->second)) return &it->second;
            states.erase(it);
        }
        if (!IsWindow(hwnd)) return nullptr;
        State s;
        s.style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if ((s.style & WS_EX_LAYERED) &&
            !GetLayeredWindowAttributes(hwnd, &s.color, &s.alpha, &s.flags)) return nullptr;
        s.currentAlpha = (s.flags & LWA_ALPHA) ? s.alpha : 255;
        auto& saved = states.emplace(hwnd, s).first->second;
        if (!SetPropW(hwnd, Marker(), reinterpret_cast<HANDLE>(&saved))) {
            states.erase(hwnd);
            return nullptr;
        }
        return &saved;
    }
    bool Style(HWND hwnd, LONG_PTR value) {
        SetLastError(0);
        return SetWindowLongPtrW(hwnd, GWL_EXSTYLE, value) != 0 || GetLastError() == 0;
    }
    bool Apply(HWND hwnd, State& s) {
        LONG_PTR old = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        LONG_PTR next = old;
        if (s.click || s.currentAlpha < 255 || (s.style & WS_EX_LAYERED)) next |= WS_EX_LAYERED;
        else next &= ~WS_EX_LAYERED;
        if (s.click) next |= WS_EX_TRANSPARENT;
        else next = (next & ~WS_EX_TRANSPARENT) | (s.style & WS_EX_TRANSPARENT);
        if (!Style(hwnd, next)) return false;
        DWORD flags = s.flags | LWA_ALPHA;
        if ((next & WS_EX_LAYERED) && !SetLayeredWindowAttributes(hwnd, s.color, (BYTE)s.currentAlpha, flags)) {
            Style(hwnd, old);
            return false;
        }
        s.layeredChanged = true;
        return true;
    }
    bool Tab(HWND hwnd, State& s, bool remove) {
        if (!remove && !s.tabRemoved) return true;
        // Never add a taskbar button for an original tool/owned window.
        if (remove && ((s.style & WS_EX_TOOLWINDOW) ||
            (GetWindow(hwnd, GW_OWNER) && !(s.style & WS_EX_APPWINDOW)))) return true;
        if (!taskbar) {
            if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&taskbar)))) return false;
            if (FAILED(taskbar->HrInit())) { taskbar->Release(); taskbar = nullptr; return false; }
        }
        HRESULT hr = remove ? taskbar->DeleteTab(hwnd) : taskbar->AddTab(hwnd);
        if (SUCCEEDED(hr)) s.tabRemoved = remove;
        return SUCCEEDED(hr);
    }
public:
    explicit WindowStateManager(ITaskbarList* api = nullptr) : taskbar(api) { if (taskbar) taskbar->AddRef(); }
    WindowStateManager(const WindowStateManager&) = delete;
    WindowStateManager& operator=(const WindowStateManager&) = delete;
    bool hideTaskbar = false;
    bool SetAlpha(HWND hwnd, int alpha) {
        State* s = Track(hwnd);
        if (!s) return false;
        int old = s->currentAlpha;
        s->currentAlpha = alpha;
        if (!Apply(hwnd, *s)) { s->currentAlpha = old; return false; }
        return Tab(hwnd, *s, hideTaskbar && alpha < 255);
    }
    bool ToggleClick(HWND hwnd) {
        auto it = states.find(clickTarget);
        if (it != states.end() && Valid(it->first, it->second) && it->second.click) {
            it->second.click = false;
            if (!Apply(it->first, it->second)) { it->second.click = true; return false; }
            clickTarget = nullptr;
            return true;
        }
        State* s = Track(hwnd);
        if (!s) return false;
        s->click = true;
        if (!Apply(hwnd, *s)) { s->click = false; return false; }
        clickTarget = hwnd;
        return true;
    }
    bool ToggleHidden(HWND hwnd) {
        auto it = states.find(hiddenTarget);
        if (it != states.end() && Valid(it->first, it->second) && it->second.hidden) {
            if (!SetWindowPlacement(it->first, &it->second.placement)) return false;
            it->second.hidden = false;
            hiddenTarget = nullptr;
            return Tab(it->first, it->second, hideTaskbar && it->second.currentAlpha < 255);
        }
        State* s = Track(hwnd);
        if (!s || !IsWindowVisible(hwnd) || !GetWindowPlacement(hwnd, &s->placement)) return false;
        ShowWindow(hwnd, SW_HIDE);
        if (IsWindowVisible(hwnd)) return false;
        s->hidden = true;
        hiddenTarget = hwnd;
        return true;
    }
    bool SyncTaskbar() {
        bool ok = true;
        for (auto& entry : states) if (Valid(entry.first, entry.second))
            ok = Tab(entry.first, entry.second, hideTaskbar && entry.second.currentAlpha < 255) && ok;
        return ok;
    }
    bool RestoreAll() {
        bool ok = true;
        for (auto it = states.begin(); it != states.end();) {
            HWND hwnd = it->first;
            State& s = it->second;
            if (!Valid(hwnd, s)) { it = states.erase(it); continue; }
            bool restored = true;
            if (s.layeredChanged) {
                LONG_PTR current = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                const LONG_PTR mask = WS_EX_LAYERED | WS_EX_TRANSPARENT;
                restored = Style(hwnd, (current & ~mask) | (s.style & mask));
                if (restored && (s.style & WS_EX_LAYERED))
                    restored = !!SetLayeredWindowAttributes(hwnd, s.color, s.alpha, s.flags);
            }
            if (s.hidden) {
                if (SetWindowPlacement(hwnd, &s.placement)) s.hidden = false;
                else restored = false;
            }
            restored = Tab(hwnd, s, false) && restored;
            if (restored) { RemovePropW(hwnd, Marker()); it = states.erase(it); }
            else { ok = false; ++it; }
        }
        if (ok) clickTarget = hiddenTarget = nullptr;
        return ok;
    }
    void Shutdown() {
        RestoreAll();
        if (taskbar) { taskbar->Release(); taskbar = nullptr; }
    }
};
