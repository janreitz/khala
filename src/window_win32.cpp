#include "logger.h"
#include "ui.h"
#include "utility.h"
#include "window.h"

#include <cairo-win32.h>
#include <cairo.h>

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>
#include <windowsx.h>

namespace
{

struct MonitorInfo {
    unsigned int width;
    unsigned int height;
    int x;
    int y;
};

// Callback for EnumDisplayMonitors to find primary monitor
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor,
                              LPRECT lprcMonitor, LPARAM dwData)
{
    auto *result = reinterpret_cast<MonitorInfo *>(dwData);

    MONITORINFOEX monitorInfo;
    monitorInfo.cbSize = sizeof(MONITORINFOEX);

    if (GetMonitorInfo(hMonitor, &monitorInfo)) {
        // Check if this is the primary monitor
        if (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) {
            result->width =
                monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
            result->height =
                monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
            result->x = monitorInfo.rcMonitor.left;
            result->y = monitorInfo.rcMonitor.top;
            return FALSE; // Stop enumeration
        }
    }
    return TRUE; // Continue enumeration
}

std::optional<MonitorInfo> get_primary_monitor_info()
{
    MonitorInfo info = {0, 0, 0, 0};

    // Enumerate monitors to find the primary one
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc,
                        reinterpret_cast<LPARAM>(&info));

    if (info.width > 0 && info.height > 0) {
        LOG_INFO("Found primary monitor: %ux%u at (%d,%d)", info.width,
                 info.height, info.x, info.y);
        return info;
    }

    LOG_WARNING("Failed to get primary monitor info via EnumDisplayMonitors");
    return std::nullopt;
}

// Global message queue for storing input events
static std::vector<ui::UserInputEvent> g_event_queue;
static HWND g_hwnd = nullptr;

// Window procedure to handle messages
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        // Regain focus when clicked
        SetFocus(hwnd);
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        ui::KeyboardEvent key_event;
        key_event.modifier = std::nullopt;
        key_event.character = std::nullopt;

        switch (wParam) {
        case VK_ESCAPE:
            key_event.key = ui::KeyCode::Escape;
            g_event_queue.push_back(key_event);
            break;
        case VK_UP:
            key_event.key = ui::KeyCode::Up;
            g_event_queue.push_back(key_event);
            break;
        case VK_DOWN:
            key_event.key = ui::KeyCode::Down;
            g_event_queue.push_back(key_event);
            break;
        case VK_TAB:
            key_event.key = ui::KeyCode::Tab;
            g_event_queue.push_back(key_event);
            break;
        case VK_LEFT:
            key_event.key = ui::KeyCode::Left;
            g_event_queue.push_back(key_event);
            break;
        case VK_RIGHT:
            key_event.key = ui::KeyCode::Right;
            g_event_queue.push_back(key_event);
            break;
        case VK_HOME:
            key_event.key = ui::KeyCode::Home;
            g_event_queue.push_back(key_event);
            break;
        case VK_END:
            key_event.key = ui::KeyCode::End;
            g_event_queue.push_back(key_event);
            break;
        case VK_RETURN:
            key_event.key = ui::KeyCode::Return;
            g_event_queue.push_back(key_event);
            break;
        case VK_BACK:
            key_event.key = ui::KeyCode::BackSpace;
            g_event_queue.push_back(key_event);
            break;
        default:
            // Will be handled by WM_CHAR for printable characters
            break;
        }
        return 0;
    }

    case WM_CHAR: {
        // Handle printable character input
        wchar_t wch = static_cast<wchar_t>(wParam);

        // Convert wide char to narrow char (ASCII range)
        if (wch >= 32 && wch < 127) {
            ui::KeyboardEvent key_event;
            key_event.key = ui::KeyCode::Character;
            key_event.modifier = std::nullopt;
            key_event.character = static_cast<char>(wch);
            g_event_queue.push_back(key_event);
        }
        return 0;
    }

    case WM_KILLFOCUS:
        // Handle focus loss if needed
        return 0;

    case WM_SETFOCUS:
        // Handle focus gain if needed
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

} // anonymous namespace

PlatformWindow::PlatformWindow(ui::RelScreenCoord top_left,
                               ui::RelScreenCoord dimension)
{
    // Get primary monitor info
    unsigned int primary_screen_width, primary_screen_height;
    int primary_screen_x = 0, primary_screen_y = 0;

    if (const auto primary_monitor = get_primary_monitor_info()) {
        primary_screen_width = primary_monitor->width;
        primary_screen_height = primary_monitor->height;
        primary_screen_x = primary_monitor->x;
        primary_screen_y = primary_monitor->y;
        LOG_INFO("Using primary monitor info");
    } else {
        // Fallback to system metrics
        LOG_INFO("Falling back to GetSystemMetrics");
        primary_screen_width = GetSystemMetrics(SM_CXSCREEN);
        primary_screen_height = GetSystemMetrics(SM_CYSCREEN);
        primary_screen_x = 0;
        primary_screen_y = 0;
    }

    screen_height = primary_screen_height;

    // Calculate window dimensions based on primary screen size and config
    // ratios
    width = static_cast<unsigned int>(primary_screen_width * dimension.x);
    height = static_cast<unsigned int>(primary_screen_height * dimension.y);

    const int x =
        primary_screen_x + static_cast<int>(primary_screen_width * top_left.x);
    const int y =
        primary_screen_y + static_cast<int>(primary_screen_height * top_left.y);

    // Debug output
    LOG_DEBUG("Primary monitor: %dx%d at (%d,%d)", primary_screen_width,
              primary_screen_height, primary_screen_x, primary_screen_y);
    LOG_DEBUG("Window: %dx%d at (%d,%d)", width, height, x, y);

    // Get the module handle
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    // Register window class
    const CHAR CLASS_NAME[] = "LauncherWindowClass";

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // No background brush for transparency
    wc.lpszClassName = CLASS_NAME;

    // Register the class (ignore error if already registered)
    RegisterClassEx(&wc);

    // Create a layered window for transparency support
    DWORD dwExStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    DWORD dwStyle = WS_POPUP;

    hwnd = CreateWindowEx(dwExStyle, CLASS_NAME, "Launcher", dwStyle, x, y,
                          width, height, nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        throw std::runtime_error("Failed to create window: " +
                                 std::to_string(GetLastError()));
    }

    g_hwnd = hwnd;
    
    // Show and focus the window
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    UpdateWindow(hwnd);
}

PlatformWindow::~PlatformWindow()
{
    if (cached_context) {
        cairo_destroy(cached_context);
        cached_context = nullptr;
    }
    if (cached_surface) {
        cairo_surface_destroy(cached_surface);
        cached_surface = nullptr;
    }
    if (hbitmap) {
        DeleteObject(hbitmap);
    }
    if (hdc_mem) {
        DeleteDC(hdc_mem);
    }
    if (hdc_screen) {
        ReleaseDC(nullptr, hdc_screen);
    }
    if (hwnd) {
        DestroyWindow(hwnd);
    }
    if (hwnd) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
        g_hwnd = nullptr;
    }
}

void PlatformWindow::resize(unsigned int new_height, unsigned int new_width)
{
    SetWindowPos(hwnd, nullptr, 0, 0, new_width, new_height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    height = new_height;
    width = new_width;
}

bool PlatformWindow::cairo_cache_valid() const
{
    return cached_context && cached_surface_width == width &&
           cached_surface_height == height &&
           cairo_surface_status(cached_surface) == CAIRO_STATUS_SUCCESS;
}

cairo_t *PlatformWindow::get_cairo_context()
{
    if (cairo_cache_valid()) {
        return cached_context;
    }

    // Destroy in correct order: context first, then surface, then GDI
    if (cached_context) {
        cairo_destroy(cached_context);
        cached_context = nullptr;
    }
    if (cached_surface) {
        cairo_surface_destroy(cached_surface);
        cached_surface = nullptr;
    }
    if (hbitmap) {
        DeleteObject(hbitmap);
        hbitmap = nullptr;
    }
    if (hdc_mem) {
        DeleteDC(hdc_mem);
        hdc_mem = nullptr;
    }
    if (hdc_screen) {
        ReleaseDC(nullptr, hdc_screen);
        hdc_screen = nullptr;
    }

    // Create GDI resources
    hdc_screen = GetDC(nullptr);
    hdc_mem = CreateCompatibleDC(hdc_screen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -static_cast<int>(height);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hbitmap = CreateDIBSection(hdc_mem, &bmi, DIB_RGB_COLORS, &bitmap_bits,
                               nullptr, 0);
    SelectObject(hdc_mem, hbitmap);

    // Create Cairo surface and context
    cached_surface = cairo_image_surface_create_for_data(
        static_cast<unsigned char *>(bitmap_bits), CAIRO_FORMAT_ARGB32, width,
        height, width * 4);
    LOG_DEBUG("Backend: %d\n", cairo_surface_get_type(cached_surface));

    cached_context = cairo_create(cached_surface);

    if (cairo_status(cached_context) != CAIRO_STATUS_SUCCESS) {
        throw std::runtime_error("Failed to create Cairo context: " +
                                 std::to_string(cairo_status(cached_context)));
    }

    cached_surface_width = width;
    cached_surface_height = height;

    return cached_context;
}

std::vector<ui::UserInputEvent> PlatformWindow::get_input_events(bool blocking)
{
    std::vector<ui::UserInputEvent> events;
    MSG msg;

    // Clear the event queue and prepare to collect new events
    g_event_queue.clear();

    if (blocking) {
        // Wait for at least one message
        if (GetMessage(&msg, hwnd, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Process all pending messages without blocking
    while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            // Handle quit if needed
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Return collected events from the global queue
    events = std::move(g_event_queue);
    g_event_queue.clear();

    return events;
}

void PlatformWindow::commit_surface()
{
    if (!cairo_cache_valid()) {
        throw std::runtime_error(
            "Cannot commit surface: no valid cairo surface available");
    }

    cairo_surface_flush(cached_surface);
    GdiFlush();

    SIZE size = {static_cast<LONG>(width), static_cast<LONG>(height)};
    POINT pt_src = {0, 0};

    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    RECT wr;
    GetWindowRect(hwnd, &wr);
    POINT pt_dst = {wr.left, wr.top};

    UpdateLayeredWindow(hwnd, hdc_screen, &pt_dst, &size, hdc_mem, &pt_src, 0,
                        &blend, ULW_ALPHA);
}