#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif
#include <shellapi.h>
#include <commctrl.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <string>
#include <vector>
#include <cmath>
#include <chrono>
#include "aquarium.h"
#include "themes.h"

namespace {

constexpr wchar_t WALLPAPER_CLASS[] = L"AsciiAquariumWallpaperWnd";
constexpr wchar_t TRAY_CLASS[]      = L"AsciiAquariumTrayHostWnd";
constexpr wchar_t APP_NAME[]        = L"ASCII Aquarium Wallpaper";
constexpr wchar_t MUTEX_NAME[]      = L"Local\\AsciiAquariumWallpaper_SingleInstance";
constexpr wchar_t REG_RUN_KEY[]     = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t REG_RUN_VAL[]     = L"AsciiAquariumWallpaper";

constexpr UINT WM_TRAYICON       = WM_APP + 10;
constexpr UINT_PTR TIMER_FRAME   = 1;
constexpr UINT_PTR TIMER_CHECK   = 2;
constexpr UINT FRAME_MS          = 30; // ~33 FPS for silky smooth waves and swimming

// Clean tray menu command IDs
constexpr UINT IDM_FEED          = 1001;
constexpr UINT IDM_MOUSE         = 1002;
constexpr UINT IDM_AUTOPAUSE     = 1003;
constexpr UINT IDM_PAUSE         = 1004;
constexpr UINT IDM_AUTOSTART     = 1005;
constexpr UINT IDM_REATTACH      = 1006;
constexpr UINT IDM_EXIT          = 1007;
constexpr UINT IDM_SHARK         = 1008;
constexpr UINT IDM_THEME_BASE    = 2000;

template <typename T>
void SafeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

struct AppState {
    HINSTANCE hInst = nullptr;
    HWND wallpaperHwnd = nullptr; // Embedded inside Progman/WorkerW (NEVER focused!)
    HWND trayHwnd = nullptr;      // Separate hidden top-level host for tray icon & menu (so icons NEVER vanish!)
    HWND defViewHwnd = nullptr;   // SHELLDLL_DefView reference to keep wallpaper strictly below icons
    UINT msgTaskbarCreated = 0;

    bool windowMode = false;
    bool userPaused = false;
    bool autoPaused = false;
    bool autoPauseEnabled = true;
    bool mouseInteract = true;

    int width = 1920;
    int height = 1080;
    int cellW = 10;
    int cellH = 19;

    HDC memDC = nullptr;
    HBITMAP memBmp = nullptr;
    HBITMAP memOldBmp = nullptr;
    uint32_t* memPixels = nullptr;

    HDC bgDC = nullptr;
    HBITMAP bgBmp = nullptr;
    HBITMAP bgOldBmp = nullptr;

    HFONT font = nullptr;
    HICON trayIcon = nullptr;
    bool trayAdded = false;

    // Direct3D 11 + DXGI + DirectComposition
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    IDXGISwapChain1* swapChain = nullptr;
    IDCompositionDevice* dcompDevice = nullptr;
    IDCompositionTarget* dcompTarget = nullptr;
    IDCompositionVisual* dcompVisual = nullptr;
    bool dcompReady = false;

    Aquarium aquarium;
    std::chrono::steady_clock::time_point lastTick;

    bool lmbDown = false;
    POINT lmbDownPos{};
    DWORD lmbDownTick = 0;
};

AppState g_app;

void EnablePerMonitorDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetDpiCtxFn = BOOL(WINAPI*)(HANDLE);
        auto fn = reinterpret_cast<SetDpiCtxFn>(
            reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
        if (fn && fn(reinterpret_cast<HANDLE>(-4))) {
            return;
        }
    }
    SetProcessDPIAware();
}

std::wstring GetExePath() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf);
}

std::wstring GetConfigPath() {
    std::wstring p = GetExePath();
    size_t slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) p = p.substr(0, slash + 1);
    return p + L"config.ini";
}

void LoadConfig() {
    std::wstring cfg = GetConfigPath();
    int theme = GetPrivateProfileIntW(L"Settings", L"Theme", 0, cfg.c_str());
    g_app.aquarium.SetTheme(theme);
    g_app.mouseInteract = GetPrivateProfileIntW(L"Settings", L"MouseInteract", 1, cfg.c_str()) != 0;
    g_app.autoPauseEnabled = GetPrivateProfileIntW(L"Settings", L"AutoPause", 1, cfg.c_str()) != 0;
}

void SaveConfig() {
    std::wstring cfg = GetConfigPath();
    WritePrivateProfileStringW(L"Settings", L"Theme",
        std::to_wstring(g_app.aquarium.GetTheme()).c_str(), cfg.c_str());
    WritePrivateProfileStringW(L"Settings", L"MouseInteract",
        g_app.mouseInteract ? L"1" : L"0", cfg.c_str());
    WritePrivateProfileStringW(L"Settings", L"AutoPause",
        g_app.autoPauseEnabled ? L"1" : L"0", cfg.c_str());
}

bool IsAutostartEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    bool exists = (RegQueryValueExW(hKey, REG_RUN_VAL, nullptr, &type, nullptr, &size) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return exists;
}

void SetAutostartEnabled(bool enable) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
        return;
    if (enable) {
        std::wstring cmd = L"\"" + GetExePath() + L"\"";
        RegSetValueExW(hKey, REG_RUN_VAL, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(cmd.c_str()),
            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, REG_RUN_VAL);
    }
    RegCloseKey(hKey);
}

HICON CreateFishTrayIcon() {
    const int S = 16;
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = S;
    bi.bV5Height = -S;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    uint32_t* bits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hColor = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi),
                                      DIB_RGB_COLORS, reinterpret_cast<void**>(&bits), nullptr, 0);
    HBITMAP hMask = CreateBitmap(S, S, 1, 1, nullptr);
    ReleaseDC(nullptr, hdc);

    static const char* art[16] = {
        "....oooooooo....",
        "..oooooooooooo..",
        ".ooooBooooooooo.",
        ".oooooooooooooo.",
        "oooooooooooooBoo",
        "ooFFoooFFFFFoooo",
        "ooFFFoFFFFFFFooo",
        "ooFFFFFFFFFFEFoo",
        "ooFFFFFFFFFFFFoo",
        "ooFFFoFFFFFFFooo",
        "ooFFoooFFFFFoooo",
        "ooooooooooooWooo",
        ".ooWoooooooWWoo.",
        ".ooWWoooooWWWoo.",
        "..ooWWoooWWWoo..",
        "....oooooooo...."
    };

    if (bits) {
        for (int y = 0; y < S; ++y) {
            for (int x = 0; x < S; ++x) {
                uint32_t px = 0x00000000;
                switch (art[y][x]) {
                    case 'o': px = 0xFF0A223A; break;
                    case 'F': px = 0xFFFF9F38; break;
                    case 'E': px = 0xFF101820; break;
                    case 'B': px = 0xFF8BE8FF; break;
                    case 'W': px = 0xFF34C878; break;
                    default:  px = 0x00000000; break;
                }
                bits[y * S + x] = px;
            }
        }
    }

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = hColor;
    ii.hbmMask = hMask;
    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hColor);
    DeleteObject(hMask);
    return hIcon;
}

void AddTrayIcon(HWND trayHwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = trayHwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = g_app.trayIcon;
    lstrcpynW(nid.szTip, L"ASCII Aquarium Wallpaper", ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &nid);
    g_app.trayAdded = true;
}

void RemoveTrayIcon(HWND trayHwnd) {
    if (!g_app.trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = trayHwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_app.trayAdded = false;
}

bool IsDesktopWindowClass(HWND h) {
    if (!h) return false;
    if (h == g_app.wallpaperHwnd) return true;
    wchar_t cls[64]{};
    GetClassNameW(h, cls, ARRAYSIZE(cls));
    return (lstrcmpW(cls, L"Progman") == 0 ||
            lstrcmpW(cls, L"WorkerW") == 0 ||
            lstrcmpW(cls, L"SHELLDLL_DefView") == 0 ||
            lstrcmpW(cls, L"SysListView32") == 0);
}

bool IsPointOverDesktop(POINT pt) {
    if (g_app.windowMode) {
        RECT r{};
        GetWindowRect(g_app.wallpaperHwnd, &r);
        return PtInRect(&r, pt) != FALSE;
    }
    HWND under = WindowFromPoint(pt);
    if (!under) return false;
    if (IsDesktopWindowClass(under)) return true;
    HWND root = GetAncestor(under, GA_ROOT);
    return IsDesktopWindowClass(root);
}

bool ShouldAutoPause() {
    if (!g_app.autoPauseEnabled || g_app.windowMode) return false;

    HDESK desk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desk) return true;
    CloseDesktop(desk);

    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
        if (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_BUSY) {
            return true;
        }
    }

    HWND fg = GetForegroundWindow();
    if (!fg || fg == g_app.wallpaperHwnd || fg == g_app.trayHwnd || IsDesktopWindowClass(fg)) return false;
    if (!IsWindowVisible(fg) || IsIconic(fg)) return false;

    wchar_t cls[64]{};
    GetClassNameW(fg, cls, ARRAYSIZE(cls));
    if (lstrcmpW(cls, L"Shell_TrayWnd") == 0 || lstrcmpW(cls, L"Shell_SecondaryTrayWnd") == 0) {
        return false;
    }

    RECT r{};
    if (GetWindowRect(fg, &r)) {
        HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
            int mw = mi.rcMonitor.right - mi.rcMonitor.left;
            int mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
            int ww = r.right - r.left;
            int wh = r.bottom - r.top;
            if (ww >= mw - 2 && wh >= mh - 2 &&
                r.left <= mi.rcMonitor.left + 2 && r.top <= mi.rcMonitor.top + 2) {
                return true;
            }
        }
    }
    return false;
}

void ReleaseDComp() {
    g_app.dcompReady = false;
    SafeRelease(g_app.dcompVisual);
    SafeRelease(g_app.dcompTarget);
    SafeRelease(g_app.dcompDevice);
    SafeRelease(g_app.swapChain);
    SafeRelease(g_app.d3dContext);
    SafeRelease(g_app.d3dDevice);
}

bool InitDComp(HWND hwnd, int w, int h) {
    ReleaseDComp();

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        &g_app.d3dDevice, nullptr, &g_app.d3dContext);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
            &g_app.d3dDevice, nullptr, &g_app.d3dContext);
    }
    if (FAILED(hr)) return false;

    IDXGIDevice* dxgiDevice = nullptr;
    hr = g_app.d3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
    if (FAILED(hr)) return false;

    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (SUCCEEDED(hr)) {
        hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
    }
    SafeRelease(adapter);

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = static_cast<UINT>(w);
    scd.Height = static_cast<UINT>(h);
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    if (SUCCEEDED(hr)) {
        hr = factory->CreateSwapChainForComposition(dxgiDevice, &scd, nullptr, &g_app.swapChain);
    }
    SafeRelease(factory);

    if (SUCCEEDED(hr)) {
        hr = DCompositionCreateDevice(dxgiDevice, __uuidof(IDCompositionDevice),
                                      reinterpret_cast<void**>(&g_app.dcompDevice));
    }
    SafeRelease(dxgiDevice);

    if (SUCCEEDED(hr)) hr = g_app.dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &g_app.dcompTarget);
    if (SUCCEEDED(hr)) hr = g_app.dcompDevice->CreateVisual(&g_app.dcompVisual);
    if (SUCCEEDED(hr)) hr = g_app.dcompVisual->SetContent(g_app.swapChain);
    if (SUCCEEDED(hr)) hr = g_app.dcompTarget->SetRoot(g_app.dcompVisual);
    if (SUCCEEDED(hr)) hr = g_app.dcompDevice->Commit();

    if (FAILED(hr)) {
        ReleaseDComp();
        return false;
    }
    g_app.dcompReady = true;
    return true;
}

void RebuildBackgroundBitmap() {
    if (!g_app.bgDC || g_app.width <= 0 || g_app.height <= 0) return;

    const Theme& t = g_app.aquarium.CurrentTheme();
    int rTop = GetRValue(t.bgTop),    gTop = GetGValue(t.bgTop),    bTop = GetBValue(t.bgTop);
    int rBot = GetRValue(t.bgBottom), gBot = GetGValue(t.bgBottom), bBot = GetBValue(t.bgBottom);
    int rSur = GetRValue(t.waterSurface), gSur = GetGValue(t.waterSurface), bSur = GetBValue(t.waterSurface);

    // Subtle sunlit surface glow blending smoothly into deep ocean gradient (1px precision)
    for (int y = 0; y < g_app.height; ++y) {
        float k = static_cast<float>(y) / static_cast<float>(std::max(1, g_app.height - 1));
        float curve = std::pow(k, 0.85f);
        float shallow = std::max(0.0f, 1.0f - k * 5.2f);
        shallow *= shallow * 0.14f;

        float rf = rTop + (rBot - rTop) * curve + (rSur - rTop) * shallow;
        float gf = gTop + (gBot - gTop) * curve + (gSur - gTop) * shallow;
        float bf = bTop + (bBot - bTop) * curve + (bSur - bTop) * shallow;

        int r = std::max(0, std::min(255, static_cast<int>(rf)));
        int g = std::max(0, std::min(255, static_cast<int>(gf)));
        int b = std::max(0, std::min(255, static_cast<int>(bf)));

        HBRUSH br = CreateSolidBrush(RGB(r, g, b));
        RECT band{ 0, y, g_app.width, y + 1 };
        FillRect(g_app.bgDC, &band, br);
        DeleteObject(br);
    }
}

void ReleaseGraphics() {
    ReleaseDComp();
    if (g_app.memDC) {
        if (g_app.memOldBmp) SelectObject(g_app.memDC, g_app.memOldBmp);
        if (g_app.memBmp) DeleteObject(g_app.memBmp);
        DeleteDC(g_app.memDC);
        g_app.memDC = nullptr;
        g_app.memBmp = nullptr;
        g_app.memOldBmp = nullptr;
        g_app.memPixels = nullptr;
    }
    if (g_app.bgDC) {
        if (g_app.bgOldBmp) SelectObject(g_app.bgDC, g_app.bgOldBmp);
        if (g_app.bgBmp) DeleteObject(g_app.bgBmp);
        DeleteDC(g_app.bgDC);
        g_app.bgDC = nullptr;
        g_app.bgBmp = nullptr;
        g_app.bgOldBmp = nullptr;
    }
    if (g_app.font) {
        DeleteObject(g_app.font);
        g_app.font = nullptr;
    }
}

int ComputeTaskbarBottomMarginRows(int totalHeight, int cellH) {
    if (g_app.windowMode || cellH <= 0) return 0;
    RECT workArea{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        int taskbarPx = totalHeight - workArea.bottom;
        if (taskbarPx > 10 && taskbarPx < totalHeight / 3) {
            return (taskbarPx + cellH - 1) / cellH + 1; // +1 extra row of breathing room above taskbar
        }
    }
    return 4; // Safe default for 1080p @ 125% taskbar (~60px)
}

void InitGraphics(HWND hwnd, int w, int h) {
    ReleaseGraphics();
    g_app.width = std::max(320, w);
    g_app.height = std::max(200, h);

    HDC screenDC = GetDC(hwnd);
    g_app.memDC = CreateCompatibleDC(screenDC);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_app.width;
    bmi.bmiHeader.biHeight = -g_app.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g_app.memBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS,
                                    reinterpret_cast<void**>(&g_app.memPixels), nullptr, 0);
    g_app.memOldBmp = static_cast<HBITMAP>(SelectObject(g_app.memDC, g_app.memBmp));

    g_app.bgDC = CreateCompatibleDC(screenDC);
    g_app.bgBmp = CreateCompatibleBitmap(screenDC, g_app.width, g_app.height);
    g_app.bgOldBmp = static_cast<HBITMAP>(SelectObject(g_app.bgDC, g_app.bgBmp));

    // Refined crisp Consolas font for finer, slightly smaller fish and smoother water/seabed detail
    int fontH = std::max(13, std::min(16, g_app.height / 66));
    g_app.font = CreateFontW(
        -fontH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    SelectObject(g_app.memDC, g_app.font);
    SetBkMode(g_app.memDC, TRANSPARENT);

    TEXTMETRICW tm{};
    GetTextMetricsW(g_app.memDC, &tm);
    g_app.cellW = std::max(6, static_cast<int>(tm.tmAveCharWidth));
    g_app.cellH = std::max(12, static_cast<int>(tm.tmHeight));

    ReleaseDC(hwnd, screenDC);

    RebuildBackgroundBitmap();

    int cols = g_app.width / g_app.cellW;
    int rows = g_app.height / g_app.cellH;
    int bottomMargin = ComputeTaskbarBottomMarginRows(g_app.height, g_app.cellH);
    g_app.aquarium.Resize(cols, rows, bottomMargin);
    g_app.aquarium.Render();

    if (!g_app.windowMode) {
        InitDComp(hwnd, g_app.width, g_app.height);
    }
}

struct WorkerSearch {
    HWND workerW = nullptr;
};

BOOL CALLBACK EnumWindowsForWorkerW(HWND topHandle, LPARAM lParam) {
    HWND defView = FindWindowExW(topHandle, nullptr, L"SHELLDLL_DefView", nullptr);
    if (defView != nullptr) {
        auto* ws = reinterpret_cast<WorkerSearch*>(lParam);
        ws->workerW = FindWindowExW(nullptr, topHandle, L"WorkerW", nullptr);
        return FALSE;
    }
    return TRUE;
}

void PresentFrame();

bool AttachToDesktop(HWND hwnd) {
    if (g_app.windowMode) {
        RECT cr{};
        GetClientRect(hwnd, &cr);
        InitGraphics(hwnd, cr.right - cr.left, cr.bottom - cr.top);
        ShowWindow(hwnd, SW_SHOW);
        PresentFrame();
        return true;
    }

    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) {
        InitGraphics(hwnd, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        PresentFrame();
        return false;
    }

    DWORD_PTR result = 0;
    SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &result);
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);
    Sleep(80);

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) {
        vw = GetSystemMetrics(SM_CXSCREEN);
        vh = GetSystemMetrics(SM_CYSCREEN);
        vx = 0;
        vy = 0;
    }

    HWND defViewInProgman = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
    HWND workerInProgman  = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
    WorkerSearch ws{};
    EnumWindows(EnumWindowsForWorkerW, reinterpret_cast<LPARAM>(&ws));

    bool isRaisedDesktop = (defViewInProgman != nullptr) &&
                           ((workerInProgman != nullptr) ||
                            ((GetWindowLongPtrW(progman, GWL_EXSTYLE) & 0x00200000L) != 0));

    HWND targetParent = isRaisedDesktop ? progman : (ws.workerW ? ws.workerW : progman);
    g_app.defViewHwnd = isRaisedDesktop ? defViewInProgman : nullptr;

    SetParent(hwnd, targetParent);

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style = (style & ~(WS_POPUP | WS_OVERLAPPEDWINDOW)) | WS_CHILD | WS_VISIBLE;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);

    POINT pt{ vx, vy };
    MapWindowPoints(nullptr, targetParent, &pt, 1);

    HWND insertAfter = (isRaisedDesktop && defViewInProgman) ? defViewInProgman : HWND_TOP;
    SetWindowPos(hwnd, insertAfter, pt.x, pt.y, vw, vh,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);

    InitGraphics(hwnd, vw, vh);
    PresentFrame();
    return true;
}

void RestoreOriginalWallpaper() {
    if (g_app.windowMode) return;
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (progman) {
        RedrawWindow(progman, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    WorkerSearch ws{};
    EnumWindows(EnumWindowsForWorkerW, reinterpret_cast<LPARAM>(&ws));
    if (ws.workerW) {
        RedrawWindow(ws.workerW, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

void PollMouseInteraction() {
    if (!g_app.mouseInteract) {
        g_app.aquarium.OnMouseMove(-100.0f, -100.0f, false);
        return;
    }

    POINT pt{};
    if (!GetCursorPos(&pt)) return;

    bool overDesktop = IsPointOverDesktop(pt);
    POINT clientPt = pt;
    ScreenToClient(g_app.wallpaperHwnd, &clientPt);

    float col = static_cast<float>(clientPt.x) / static_cast<float>(std::max(1, g_app.cellW));
    float row = static_cast<float>(clientPt.y) / static_cast<float>(std::max(1, g_app.cellH));

    g_app.aquarium.OnMouseMove(col, row, overDesktop);

    bool downNow = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (downNow && !g_app.lmbDown) {
        if (overDesktop) {
            g_app.lmbDown = true;
            g_app.lmbDownPos = pt;
            g_app.lmbDownTick = GetTickCount();
        }
    } else if (!downNow && g_app.lmbDown) {
        g_app.lmbDown = false;
        DWORD elapsed = GetTickCount() - g_app.lmbDownTick;
        int dx = std::abs(pt.x - g_app.lmbDownPos.x);
        int dy = std::abs(pt.y - g_app.lmbDownPos.y);
        if (overDesktop && elapsed < 280 && dx < 6 && dy < 6) {
            g_app.aquarium.DropFood(col, row, 6);
        }
    }
}

void RenderToMemDC() {
    if (!g_app.memDC || !g_app.bgDC) return;

    BitBlt(g_app.memDC, 0, 0, g_app.width, g_app.height, g_app.bgDC, 0, 0, SRCCOPY);

    const auto& buf = g_app.aquarium.Buffer();
    const int cols = g_app.aquarium.Cols();
    const int rows = g_app.aquarium.Rows();
    const int cw = g_app.cellW;
    const int ch = g_app.cellH;

    std::wstring run;
    run.reserve(static_cast<size_t>(cols));

    for (int y = 0; y < rows; ++y) {
        const size_t rowBase = static_cast<size_t>(y) * cols;
        int x = 0;
        while (x < cols) {
            if (buf[rowBase + x].ch == L' ') {
                ++x;
                continue;
            }
            int startX = x;
            COLORREF col = buf[rowBase + x].color;
            run.clear();
            while (x < cols && buf[rowBase + x].ch != L' ' && buf[rowBase + x].color == col) {
                run.push_back(buf[rowBase + x].ch);
                ++x;
            }
            SetTextColor(g_app.memDC, col);
            TextOutW(g_app.memDC, startX * cw, y * ch, run.c_str(), static_cast<int>(run.size()));
        }
    }
}

void PresentFrame() {
    RenderToMemDC();

    if (!g_app.windowMode && g_app.dcompReady && g_app.swapChain && g_app.d3dContext) {
        if (g_app.memPixels) {
            const size_t total = static_cast<size_t>(g_app.width) * static_cast<size_t>(g_app.height);
            uint32_t* px = g_app.memPixels;
            for (size_t i = 0; i < total; ++i) {
                px[i] |= 0xFF000000u;
            }
        }
        ID3D11Texture2D* backBuf = nullptr;
        if (SUCCEEDED(g_app.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                                 reinterpret_cast<void**>(&backBuf)))) {
            g_app.d3dContext->UpdateSubresource(backBuf, 0, nullptr, g_app.memPixels,
                                                static_cast<UINT>(g_app.width * 4), 0);
            backBuf->Release();
            g_app.swapChain->Present(0, 0);
        }
    } else if (g_app.wallpaperHwnd) {
        HDC hdc = GetDC(g_app.wallpaperHwnd);
        if (hdc) {
            BitBlt(hdc, 0, 0, g_app.width, g_app.height, g_app.memDC, 0, 0, SRCCOPY);
            ReleaseDC(g_app.wallpaperHwnd, hdc);
        }
    }
}

void ShowTrayMenu(HWND trayHwnd) {
    HMENU menu = CreatePopupMenu();
    HMENU themeMenu = CreatePopupMenu();

    for (int i = 0; i < THEME_COUNT; ++i) {
        UINT flags = MF_STRING | ((g_app.aquarium.GetTheme() == i) ? MF_CHECKED : 0);
        AppendMenuW(themeMenu, flags, IDM_THEME_BASE + i, THEMES[i].name);
    }

    AppendMenuW(menu, MF_STRING, IDM_FEED, L"Погодувати рибок");
    AppendMenuW(menu, MF_STRING, IDM_SHARK, L"Покликати акулу");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(themeMenu), L"Тема оформлення");
    AppendMenuW(menu, MF_STRING | (g_app.mouseInteract ? MF_CHECKED : 0),
                IDM_MOUSE, L"Реакція на мишку");
    AppendMenuW(menu, MF_STRING | (g_app.autoPauseEnabled ? MF_CHECKED : 0),
                IDM_AUTOPAUSE, L"Автопауза в іграх");
    AppendMenuW(menu, MF_STRING | (g_app.userPaused ? MF_CHECKED : 0),
                IDM_PAUSE, L"Пауза");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0),
                IDM_AUTOSTART, L"Автозапуск з Windows");
    if (!g_app.windowMode) {
        AppendMenuW(menu, MF_STRING, IDM_REATTACH, L"Перепідключити шпалери");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Вихід");

    POINT pt{};
    GetCursorPos(&pt);
    // Focus ONLY the invisible tray host window — NEVER the wallpaper window inside Progman!
    SetForegroundWindow(trayHwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, trayHwnd, nullptr);
    PostMessageW(trayHwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

// Separate invisible window procedure for tray icon, menu, timers, and shell notifications
LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_app.msgTaskbarCreated != 0 && msg == g_app.msgTaskbarCreated) {
        AddTrayIcon(hwnd);
        if (!g_app.windowMode && g_app.wallpaperHwnd) {
            AttachToDesktop(g_app.wallpaperHwnd);
        }
        return 0;
    }

    switch (msg) {
        case WM_CREATE: {
            g_app.lastTick = std::chrono::steady_clock::now();
            SetTimer(hwnd, TIMER_FRAME, FRAME_MS, nullptr);
            SetTimer(hwnd, TIMER_CHECK, 500, nullptr);
            return 0;
        }

        case WM_TIMER: {
            if (wParam == TIMER_CHECK) {
                g_app.autoPaused = ShouldAutoPause();
                return 0;
            }
            if (wParam == TIMER_FRAME) {
                if (g_app.userPaused || g_app.autoPaused) {
                    g_app.lastTick = std::chrono::steady_clock::now();
                    return 0;
                }
                auto now = std::chrono::steady_clock::now();
                float dt = std::chrono::duration<float>(now - g_app.lastTick).count();
                g_app.lastTick = now;

                PollMouseInteraction();
                g_app.aquarium.Update(dt);
                g_app.aquarium.Render();
                PresentFrame();
                return 0;
            }
            break;
        }

        case WM_DISPLAYCHANGE: {
            if (!g_app.windowMode && g_app.wallpaperHwnd) {
                AttachToDesktop(g_app.wallpaperHwnd);
            }
            return 0;
        }

        case WM_TRAYICON: {
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU || lParam == WM_LBUTTONUP) {
                ShowTrayMenu(hwnd);
            }
            return 0;
        }

        case WM_COMMAND: {
            UINT id = LOWORD(wParam);
            if (id >= IDM_THEME_BASE && id < IDM_THEME_BASE + THEME_COUNT) {
                g_app.aquarium.SetTheme(static_cast<int>(id - IDM_THEME_BASE));
                RebuildBackgroundBitmap();
                g_app.aquarium.Render();
                PresentFrame();
                SaveConfig();
            } else {
                switch (id) {
                    case IDM_FEED:
                        g_app.aquarium.DropFoodFromSurface(14);
                        break;
                    case IDM_SHARK:
                        g_app.aquarium.SummonShark();
                        break;
                    case IDM_MOUSE:
                        g_app.mouseInteract = !g_app.mouseInteract;
                        SaveConfig();
                        break;
                    case IDM_AUTOPAUSE:
                        g_app.autoPauseEnabled = !g_app.autoPauseEnabled;
                        if (!g_app.autoPauseEnabled) g_app.autoPaused = false;
                        SaveConfig();
                        break;
                    case IDM_PAUSE:
                        g_app.userPaused = !g_app.userPaused;
                        break;
                    case IDM_AUTOSTART:
                        SetAutostartEnabled(!IsAutostartEnabled());
                        break;
                    case IDM_REATTACH:
                        if (!g_app.windowMode && g_app.wallpaperHwnd) {
                            AttachToDesktop(g_app.wallpaperHwnd);
                        }
                        break;
                    case IDM_EXIT:
                        DestroyWindow(hwnd);
                        break;
                }
            }
            return 0;
        }

        case WM_DESTROY: {
            KillTimer(hwnd, TIMER_FRAME);
            KillTimer(hwnd, TIMER_CHECK);
            RemoveTrayIcon(hwnd);
            ReleaseGraphics();
            if (g_app.wallpaperHwnd) {
                ShowWindow(g_app.wallpaperHwnd, SW_HIDE);
                SetParent(g_app.wallpaperHwnd, nullptr);
                DestroyWindow(g_app.wallpaperHwnd);
                g_app.wallpaperHwnd = nullptr;
            }
            RestoreOriginalWallpaper();
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Wallpaper canvas window procedure (never activated, always stays strictly behind desktop icons)
LRESULT CALLBACK WallpaperWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return g_app.windowMode ? MA_ACTIVATE : MA_NOACTIVATE;

        case WM_WINDOWPOSCHANGING: {
            if (!g_app.windowMode && g_app.defViewHwnd) {
                auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);
                // Prevent anything from ever raising the wallpaper above desktop icons
                if (!(wp->flags & SWP_NOZORDER)) {
                    wp->hwndInsertAfter = g_app.defViewHwnd;
                }
                wp->flags |= SWP_NOACTIVATE;
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (g_app.windowMode && g_app.memDC) {
                BitBlt(hdc, 0, 0, g_app.width, g_app.height, g_app.memDC, 0, 0, SRCCOPY);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE: {
            if (g_app.windowMode) {
                int w = LOWORD(lParam);
                int h = HIWORD(lParam);
                if (w > 100 && h > 100) {
                    InitGraphics(hwnd, w, h);
                    PresentFrame();
                }
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (g_app.windowMode) {
                if (wParam == VK_ESCAPE && g_app.trayHwnd) DestroyWindow(g_app.trayHwnd);
                else if (wParam == VK_SPACE) g_app.aquarium.DropFoodFromSurface(14);
                else if (wParam == 'T') {
                    int next = (g_app.aquarium.GetTheme() + 1) % THEME_COUNT;
                    g_app.aquarium.SetTheme(next);
                    RebuildBackgroundBitmap();
                    SaveConfig();
                }
            }
            return 0;
        }

        case WM_CLOSE: {
            if (g_app.windowMode && g_app.trayHwnd) {
                DestroyWindow(g_app.trayHwnd);
                return 0;
            }
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int) {
    EnablePerMonitorDpiAwareness();

    LoadConfig();
    if (cmdLine && wcsstr(cmdLine, L"--window") != nullptr) {
        g_app.windowMode = true;
    }

    HANDLE hMutex = nullptr;
    if (!g_app.windowMode) {
        hMutex = CreateMutexW(nullptr, TRUE, MUTEX_NAME);
        if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(hMutex);
            return 0;
        }
    }

    g_app.hInst = hInst;
    g_app.msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_app.trayIcon = CreateFishTrayIcon();

    // Register separate hidden tray host class
    WNDCLASSEXW trayWc{};
    trayWc.cbSize = sizeof(trayWc);
    trayWc.lpfnWndProc = TrayWndProc;
    trayWc.hInstance = hInst;
    trayWc.lpszClassName = TRAY_CLASS;
    if (!RegisterClassExW(&trayWc)) {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // Register wallpaper canvas class
    WNDCLASSEXW wpWc{};
    wpWc.cbSize = sizeof(wpWc);
    wpWc.style = CS_HREDRAW | CS_VREDRAW;
    wpWc.lpfnWndProc = WallpaperWndProc;
    wpWc.hInstance = hInst;
    wpWc.hIcon = g_app.trayIcon;
    wpWc.hIconSm = g_app.trayIcon;
    wpWc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wpWc.lpszClassName = WALLPAPER_CLASS;
    if (!RegisterClassExW(&wpWc)) {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // Create wallpaper canvas window
    DWORD style = g_app.windowMode ? (WS_OVERLAPPEDWINDOW | WS_VISIBLE) : (WS_POPUP | WS_VISIBLE);
    DWORD exStyle = g_app.windowMode
        ? 0
        : (WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    int initW = g_app.windowMode ? 1280 : GetSystemMetrics(SM_CXSCREEN);
    int initH = g_app.windowMode ? 720 : GetSystemMetrics(SM_CYSCREEN);
    int initX = g_app.windowMode ? CW_USEDEFAULT : 0;
    int initY = g_app.windowMode ? CW_USEDEFAULT : 0;

    g_app.wallpaperHwnd = CreateWindowExW(
        exStyle, WALLPAPER_CLASS, APP_NAME, style,
        initX, initY, initW, initH,
        nullptr, nullptr, hInst, nullptr);

    if (!g_app.wallpaperHwnd) {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // Create separate hidden tray host window (never embedded in Progman)
    g_app.trayHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW, TRAY_CLASS, APP_NAME, WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInst, nullptr);

    if (!g_app.trayHwnd) {
        DestroyWindow(g_app.wallpaperHwnd);
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    AddTrayIcon(g_app.trayHwnd);
    AttachToDesktop(g_app.wallpaperHwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_app.trayIcon) DestroyIcon(g_app.trayIcon);
    if (hMutex) CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}
