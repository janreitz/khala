#include "config.h"
#include "fuzzy.h"
#include "logger.h"
#include "ui.h"
#include "utility.h"
#include "window.h"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <windowsx.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class D2DResources
{
  public:
    static D2DResources &instance()
    {
        static D2DResources inst;
        return inst;
    }

    ID2D1Factory *d2dFactory() { return m_d2dFactory.Get(); }
    IDWriteFactory *dwriteFactory() { return m_dwriteFactory.Get(); }
    IDWriteTextFormat *textFormat() { return m_textFormat.Get(); }

    void setFont(const std::wstring &fontFamily, float fontSize)
    {
        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            fontFamily.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize,
            L"en-us", &m_textFormat);
        if (FAILED(hr)) {
            throw std::runtime_error(
                "Failed to create DirectWrite text format");
        }
        m_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_fontSize = fontSize;
    }

    float fontSize() const { return m_fontSize; }

  private:
    D2DResources()
    {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       m_d2dFactory.GetAddressOf());
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create D2D factory");
        }

        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown **>(m_dwriteFactory.GetAddressOf()));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create DirectWrite factory");
        }

        // Default font - will be updated when config is loaded
        setFont(L"Segoe UI", 14.0f);
    }

    ComPtr<ID2D1Factory> m_d2dFactory;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<IDWriteTextFormat> m_textFormat;
    float m_fontSize = 14.0f;
};

static std::wstring utf8_to_wide(const std::string &utf8)
{
    if (utf8.empty())
        return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (size <= 0)
        return L"";
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], size);
    return result;
}

std::vector<size_t>
utf8_positions_to_utf16(const std::string &utf8_str,
                        const std::vector<size_t> &utf8_positions)
{
    std::vector<size_t> utf16_positions;
    utf16_positions.reserve(utf8_positions.size());

    size_t utf8_idx = 0;
    size_t utf16_idx = 0;
    size_t pos_idx = 0;

    while (utf8_idx < utf8_str.length() && pos_idx < utf8_positions.size()) {
        if (utf8_idx == utf8_positions[pos_idx]) {
            utf16_positions.push_back(utf16_idx);
            pos_idx++;
        }

        unsigned char c = utf8_str[utf8_idx];
        if (c < 0x80) {
            // 1-byte UTF-8 (ASCII) -> 1 UTF-16 code unit
            utf8_idx += 1;
            utf16_idx += 1;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8 -> 1 UTF-16 code unit
            utf8_idx += 2;
            utf16_idx += 1;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8 -> 1 UTF-16 code unit
            utf8_idx += 3;
            utf16_idx += 1;
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte UTF-8 -> 2 UTF-16 code units (surrogate pair)
            utf8_idx += 4;
            utf16_idx += 2;
        } else {
            // Invalid UTF-8, skip
            utf8_idx += 1;
        }
    }

    return utf16_positions;
}

static D2D1_COLOR_F to_d2d_color(const Color &c)
{
    return D2D1::ColorF(static_cast<float>(c.r), static_cast<float>(c.g),
                        static_cast<float>(c.b), static_cast<float>(c.a));
}

static void draw_rounded_rect(ID2D1RenderTarget *rt, ID2D1Brush *brush, float x,
                              float y, float w, float h, float radius,
                              bool fill)
{
    D2D1_ROUNDED_RECT rr =
        D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radius, radius);
    if (fill) {
        rt->FillRoundedRectangle(rr, brush);
    } else {
        rt->DrawRoundedRectangle(rr, brush, 1.0f);
    }
}

static D2D1_SIZE_F measure_text(IDWriteFactory *factory,
                                IDWriteTextFormat *format,
                                const std::wstring &text,
                                float maxWidth = 10000.0f)
{
    ComPtr<IDWriteTextLayout> layout;
    factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.length()),
                              format, maxWidth, 10000.0f, &layout);

    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    return D2D1::SizeF(metrics.width, metrics.height);
}

// ============================================================================
// Window Procedure (static, forward declaration)
// ============================================================================

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                LPARAM lParam);

// ============================================================================
// PlatformWindow Implementation Data
// ============================================================================

struct PlatformWindowData {
    ComPtr<ID2D1HwndRenderTarget> renderTarget;

    // Cached brushes (created once, reused)
    ComPtr<ID2D1SolidColorBrush> backgroundBrush;
    ComPtr<ID2D1SolidColorBrush> inputBackgroundBrush;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    ComPtr<ID2D1SolidColorBrush> selectionBrush;
    ComPtr<ID2D1SolidColorBrush> selectionTextBrush;
    ComPtr<ID2D1SolidColorBrush> descriptionBrush;
    ComPtr<ID2D1SolidColorBrush> selectionDescBrush;
    ComPtr<ID2D1SolidColorBrush> errorBackgroundBrush;
    ComPtr<ID2D1SolidColorBrush> errorBorderBrush;
    ComPtr<ID2D1SolidColorBrush> errorTextBrush;
    ComPtr<ID2D1SolidColorBrush> indicatorGreenBrush;
    ComPtr<ID2D1SolidColorBrush> indicatorYellowBrush;

    // Pending input events
    std::vector<ui::UserInputEvent> pendingEvents;

    // Track if brushes need recreation (e.g., after config change)
    bool brushesValid = false;

    // Track mouse inside window state
    bool mouseInside = false;

    void createBrushes(ID2D1RenderTarget *rt, const Config &config)
    {
        if (brushesValid)
            return;

        rt->CreateSolidColorBrush(to_d2d_color(config.background_color),
                                  &backgroundBrush);
        rt->CreateSolidColorBrush(to_d2d_color(config.input_background_color),
                                  &inputBackgroundBrush);
        rt->CreateSolidColorBrush(to_d2d_color(config.text_color), &textBrush);
        rt->CreateSolidColorBrush(to_d2d_color(config.selection_color),
                                  &selectionBrush);
        rt->CreateSolidColorBrush(to_d2d_color(config.selection_text_color),
                                  &selectionTextBrush);
        rt->CreateSolidColorBrush(to_d2d_color(config.description_color),
                                  &descriptionBrush);
        rt->CreateSolidColorBrush(
            to_d2d_color(config.selection_description_color),
            &selectionDescBrush);

        // Error colors (hardcoded)
        rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.8f, 0.8f, 1.0f),
                                  &errorBackgroundBrush);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.8f, 0.0f, 0.0f, 1.0f),
                                  &errorBorderBrush);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.7f, 0.0f, 0.0f, 1.0f),
                                  &errorTextBrush);

        // Indicator colors
        rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.8f, 0.0f, 1.0f),
                                  &indicatorGreenBrush);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.9f, 0.0f, 1.0f),
                                  &indicatorYellowBrush);

        brushesValid = true;
    }

    void invalidateBrushes()
    {
        brushesValid = false;
        backgroundBrush.Reset();
        inputBackgroundBrush.Reset();
        textBrush.Reset();
        selectionBrush.Reset();
        selectionTextBrush.Reset();
        descriptionBrush.Reset();
        selectionDescBrush.Reset();
        errorBackgroundBrush.Reset();
        errorBorderBrush.Reset();
        errorTextBrush.Reset();
        indicatorGreenBrush.Reset();
        indicatorYellowBrush.Reset();
    }
};

// Store window data pointer in HWND user data
static PlatformWindowData *getWindowData(HWND hwnd)
{
    return reinterpret_cast<PlatformWindowData *>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA));
}

// ============================================================================
// PlatformWindow - Constructor / Destructor
// ============================================================================

PlatformWindow::PlatformWindow(ui::RelScreenCoord top_left,
                               ui::RelScreenCoord dimension)
{
    // Get screen dimensions
    screen_width = GetSystemMetrics(SM_CXSCREEN);
    screen_height = GetSystemMetrics(SM_CYSCREEN);

    // Calculate actual window position and size
    int x = static_cast<int>(top_left.x * screen_width);
    int y = static_cast<int>(top_left.y * screen_height);
    width = static_cast<unsigned int>(dimension.x * screen_width);
    height = static_cast<unsigned int>(dimension.y * screen_height);

    // Register window class
    static bool classRegistered = false;
    static const wchar_t *CLASS_NAME = L"LauncherWindowClass";

    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; // We handle painting
        wc.lpszClassName = CLASS_NAME;

        if (!RegisterClassExW(&wc)) {
            throw std::runtime_error("Failed to register window class");
        }
        classRegistered = true;
    }

    // Create window with layered style for transparency
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    DWORD style = WS_POPUP; // Borderless

    hwnd = CreateWindowExW(exStyle, CLASS_NAME, L"Launcher", style, x, y, width,
                           height, nullptr, nullptr, GetModuleHandle(nullptr),
                           nullptr);

    if (!hwnd) {
        throw std::runtime_error("Failed to create window");
    }

    // Allocate window data
    auto *data = new PlatformWindowData();
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

    // Create D2D render target
    auto &resources = D2DResources::instance();

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED));

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps =
        D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height),
                                         D2D1_PRESENT_OPTIONS_IMMEDIATELY);

    HRESULT hr = resources.d2dFactory()->CreateHwndRenderTarget(
        rtProps, hwndProps, &data->renderTarget);

    if (FAILED(hr)) {
        delete data;
        DestroyWindow(hwnd);
        throw std::runtime_error("Failed to create D2D render target");
    }

    // Enable per-pixel alpha for the window (layered window)
    // This allows the rounded corners to be transparent
    SetWindowLongW(hwnd, GWL_EXSTYLE,
                   GetWindowLongW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);

    // Makes the window fully opaque while still allowing the
    // region to work
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    // Use a region for rounded corners (simpler than UpdateLayeredWindow)
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                     static_cast<int>(ui::CORNER_RADIUS * 2),
                                     static_cast<int>(ui::CORNER_RADIUS * 2));
    SetWindowRgn(hwnd, region, TRUE);

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
}

PlatformWindow::~PlatformWindow()
{
    if (hwnd) {
        auto *data = getWindowData(hwnd);
        if (data) {
            // Clear the pointer FIRST so WndProc sees nullptr
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            delete data;
        }
        DestroyWindow(hwnd);
    }
}

// ============================================================================
// PlatformWindow - Resize
// ============================================================================

void PlatformWindow::resize(const ui::WindowDimension &dimensions)
{
    if (dimensions.width == width && dimensions.height == height) {
        return;
    }

    width = dimensions.width;
    height = dimensions.height;

    // Resize the window
    SetWindowPos(hwnd, nullptr, 0, 0, width, height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    // Update window region for rounded corners
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                     static_cast<int>(ui::CORNER_RADIUS * 2),
                                     static_cast<int>(ui::CORNER_RADIUS * 2));
    SetWindowRgn(hwnd, region, TRUE);

    // Resize render target
    auto *data = getWindowData(hwnd);
    if (data && data->renderTarget) {
        HRESULT hr = data->renderTarget->Resize(D2D1::SizeU(width, height));
        if (FAILED(hr)) {
            // Recreate render target on failure
            data->renderTarget.Reset();
            data->invalidateBrushes();

            auto &resources = D2DResources::instance();
            D2D1_RENDER_TARGET_PROPERTIES rtProps =
                D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_DEFAULT,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                      D2D1_ALPHA_MODE_PREMULTIPLIED));
            D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps =
                D2D1::HwndRenderTargetProperties(
                    hwnd, D2D1::SizeU(width, height),
                    D2D1_PRESENT_OPTIONS_IMMEDIATELY);
            resources.d2dFactory()->CreateHwndRenderTarget(rtProps, hwndProps,
                                                           &data->renderTarget);
        }
    }
}

// ============================================================================
// PlatformWindow - Draw
// ============================================================================

void PlatformWindow::draw(const Config &config, const ui::State &state)
{
    auto tik = std::chrono::steady_clock::now();

    auto *data = getWindowData(hwnd);
    if (!data || !data->renderTarget) {
        throw std::runtime_error("Render target not available");
    }

    auto *rt = data->renderTarget.Get();
    auto &resources = D2DResources::instance();
    auto *dwFactory = resources.dwriteFactory();
    auto *textFormat = resources.textFormat();

    // Update font if needed (compare with config)
    // TODO: Track config.font_size changes and call resources.setFont()

    const float content_width =
        static_cast<float>(width - 2.0f * ui::BORDER_WIDTH);
    const size_t max_visible_items =
        ui::calculate_max_visible_items(height, config.font_size);

    // Ensure brushes are created
    data->createBrushes(rt, config);

    rt->BeginDraw();

    // Clear
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));

    // Draw background
    draw_rounded_rect(rt, data->backgroundBrush.Get(), 0, 0,
                      static_cast<float>(width), static_cast<float>(height),
                      ui::CORNER_RADIUS, true);
    draw_rounded_rect(rt, data->inputBackgroundBrush.Get(), 0, 0,
                      static_cast<float>(width), static_cast<float>(height),
                      ui::CORNER_RADIUS, false);

    // Draw input area
    const float input_height =
        static_cast<float>(ui::calculate_abs_input_height(config.font_size));

    ID2D1SolidColorBrush *inputFillBrush =
        state.has_errors() ? data->errorBackgroundBrush.Get()
                           : data->inputBackgroundBrush.Get();
    ID2D1SolidColorBrush *inputStrokeBrush = state.has_errors()
                                                 ? data->errorBorderBrush.Get()
                                                 : data->selectionBrush.Get();

    draw_rounded_rect(rt, inputFillBrush, ui::BORDER_WIDTH, ui::BORDER_WIDTH,
                      content_width, input_height, ui::CORNER_RADIUS, true);
    draw_rounded_rect(rt, inputStrokeBrush, ui::BORDER_WIDTH, ui::BORDER_WIDTH,
                      content_width, input_height, ui::CORNER_RADIUS, false);

    // Determine display text
    std::wstring display_text;
    if (state.has_errors()) {
        display_text =
            utf8_to_wide("Encountered " + std::to_string(state.items.size()) +
                         " error(s). Press any key to dismiss.");
    } else if (state.mode == ui::AppMode::ContextMenu) {
        display_text =
            utf8_to_wide(state.context_menu_title + " › Actions");
    } else if (state.input_buffer.empty()) {
        const size_t total_files =
            state.cached_file_search_update.has_value()
                ? state.cached_file_search_update->total_files
                : 0;
        display_text = utf8_to_wide(
            "Search " + ui::format_file_count(total_files) +
            " files... (prefix > for utility actions, ! for applications)");
    } else {
        display_text = utf8_to_wide(state.input_buffer);
    }

    // Create text layout for input
    ComPtr<IDWriteTextLayout> inputLayout;
    dwFactory->CreateTextLayout(
        display_text.c_str(), static_cast<UINT32>(display_text.length()),
        textFormat,
        content_width - static_cast<float>(2.0 * ui::INPUT_TEXT_MARGIN),
        input_height, &inputLayout);

    DWRITE_TEXT_METRICS inputMetrics;
    inputLayout->GetMetrics(&inputMetrics);

    const float input_text_y = static_cast<float>(ui::BORDER_WIDTH) +
                               (input_height - inputMetrics.height) / 2.0f;

    ID2D1Brush *inputTextBrush =
        state.has_errors() ? data->errorTextBrush.Get() : data->textBrush.Get();

    rt->DrawTextLayout(
        D2D1::Point2F(ui::BORDER_WIDTH + ui::INPUT_TEXT_MARGIN, input_text_y),
        inputLayout.Get(), inputTextBrush);

    // Draw cursor (when not in context menu or error mode)
    if (state.mode != ui::AppMode::ContextMenu && !state.has_errors()) {
        std::wstring text_before_cursor =
            utf8_to_wide(state.input_buffer.substr(0, state.cursor_position));

        const auto cursor_size =
            measure_text(dwFactory, textFormat, text_before_cursor);
        const float cursor_x =
            static_cast<float>(ui::BORDER_WIDTH + ui::INPUT_TEXT_MARGIN) +
            cursor_size.width;

        rt->DrawLine(
            D2D1::Point2F(cursor_x, input_text_y),
            D2D1::Point2F(cursor_x, input_text_y + inputMetrics.height),
            data->textBrush.Get(), 1.0f);
    }

    // Draw progress indicator (file search mode)
    if (state.mode == ui::AppMode::FileSearch &&
        state.cached_file_search_update.has_value() &&
        state.cached_file_search_update->total_files > 0) {

        const auto &update = *state.cached_file_search_update;
        std::wstring indicator_text;

        if (state.input_buffer.empty()) {
            indicator_text = update.scan_complete ? L"✓" : L"⟳";
        } else if (update.total_available_results == 0) {
            indicator_text = L"0";
        } else {
            indicator_text = utf8_to_wide(ui::create_pagination_text(
                state.visible_range_offset, max_visible_items,
                state.items.size(), update.total_available_results));
        }

        const auto indicator_size =
            measure_text(dwFactory, textFormat, indicator_text);

        ID2D1SolidColorBrush *indicatorBrush =
            update.scan_complete ? data->indicatorGreenBrush.Get()
                                 : data->indicatorYellowBrush.Get();

        const float indicator_x =
            static_cast<float>(ui::BORDER_WIDTH - ui::INPUT_TEXT_MARGIN) +
            content_width - indicator_size.width;
        const float indicator_y = static_cast<float>(ui::BORDER_WIDTH) +
                                  (input_height - indicator_size.height) / 2.0f;

        rt->DrawText(indicator_text.c_str(),
                     static_cast<UINT32>(indicator_text.length()), textFormat,
                     D2D1::RectF(indicator_x, indicator_y,
                                 indicator_x + indicator_size.width + 10,
                                 indicator_y + indicator_size.height + 10),
                     indicatorBrush);
    }

    // Draw dropdown items
    const float dropdown_start_y =
        static_cast<float>(ui::BORDER_WIDTH + ui::ITEMS_SPACING) + input_height;
    const float item_height =
        static_cast<float>(ui::calculate_abs_item_height(config.font_size));

    const std::string query_lower = to_lower(state.input_buffer);

    const size_t range_end = std::min(
        state.visible_range_offset + max_visible_items, state.items.size());

    for (size_t i = state.visible_range_offset; i < range_end; ++i) {
        const float y_pos =
            dropdown_start_y +
            static_cast<float>(i - state.visible_range_offset) * item_height;

        const bool is_selected = (i == state.selected_item_index);

        // Measure hotkey hint width first to reserve space for it
        float hint_reserved_width = 0.0f;
        std::string hotkey_hint;
        if (!state.has_errors() && i < state.items.size() &&
            state.items[i].hotkey.has_value()) {
            hotkey_hint = to_string(*state.items[i].hotkey);
        } else if (!state.has_errors() && i >= state.visible_range_offset &&
                   i < state.visible_range_offset + 10) {
            const size_t visible_pos = i - state.visible_range_offset;
            hotkey_hint = "Ctrl+" + std::to_string((visible_pos + 1) % 10);
        }
        if (!hotkey_hint.empty()) {
            std::wstring hint_wide = utf8_to_wide(hotkey_hint);
            const auto hint_size =
                measure_text(dwFactory, textFormat, hint_wide);
            hint_reserved_width =
                hint_size.width + static_cast<float>(ui::DESCRIPTION_SPACING);
        }

        // Selection highlight
        if (is_selected) {
            draw_rounded_rect(rt, data->selectionBrush.Get(), ui::BORDER_WIDTH,
                              y_pos, content_width, item_height,
                              ui::CORNER_RADIUS, true);
        }

        // Title text
        std::wstring title = utf8_to_wide(state.items[i].title);

        ComPtr<IDWriteTextLayout> titleLayout;
        dwFactory->CreateTextLayout(
            title.c_str(), static_cast<UINT32>(title.length()), textFormat,
            content_width - static_cast<float>(2.0 * ui::TEXT_MARGIN) -
                hint_reserved_width,
            item_height, &titleLayout);

        // Apply bold highlighting for fuzzy matches
        if (query_opt.has_value() && !query_lower.empty()) {

            const auto match_positions =
                fuzzy::fuzzy_match_optimal(state.items[i].title, query_lower);
            const auto wide_positions =
                utf8_positions_to_utf16(state.items[i].title, match_positions);
            for (size_t pos : wide_positions) {
                DWRITE_TEXT_RANGE range = {static_cast<UINT32>(pos), 1};
                titleLayout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
            }
        }

        DWRITE_TEXT_METRICS titleMetrics;
        titleLayout->GetMetrics(&titleMetrics);

        float text_y = y_pos + (item_height - titleMetrics.height) / 2.0f;

        rt->DrawTextLayout(
            D2D1::Point2F(ui::BORDER_WIDTH + ui::TEXT_MARGIN, text_y),
            titleLayout.Get(),
            is_selected ? data->selectionTextBrush.Get()
                        : data->textBrush.Get());

        // Description (to the right of title)
        if (!state.items[i].description.empty()) {
            std::wstring desc = utf8_to_wide(state.items[i].description);
            const float desc_x =
                static_cast<float>(ui::BORDER_WIDTH + ui::TEXT_MARGIN +
                                   ui::DESCRIPTION_SPACING) +
                titleMetrics.width;
            const float available_width =
                content_width - static_cast<float>(ui::TEXT_MARGIN) -
                titleMetrics.width -
                static_cast<float>(ui::DESCRIPTION_SPACING) -
                static_cast<float>(ui::TEXT_MARGIN) - hint_reserved_width;

            if (available_width > 50) {
                ComPtr<IDWriteTextLayout> descLayout;
                dwFactory->CreateTextLayout(
                    desc.c_str(), static_cast<UINT32>(desc.length()),
                    textFormat, available_width, item_height, &descLayout);

                // Apply highlighting for description matches too
                if (query_opt.has_value() && !query_lower.empty()) {
                    const auto match_positions = fuzzy::fuzzy_match_optimal(
                        state.items[i].description, query_lower);
                    const auto wide_positions = utf8_positions_to_utf16(
                        state.items[i].description, match_positions);
                    for (size_t pos : wide_positions) {
                        DWRITE_TEXT_RANGE range = {static_cast<UINT32>(pos), 1};
                        descLayout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD,
                                                  range);
                    }
                }

                // Set ellipsis for overflow
                DWRITE_TRIMMING trimming = {
                    DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                ComPtr<IDWriteInlineObject> ellipsis;
                dwFactory->CreateEllipsisTrimmingSign(textFormat, &ellipsis);
                descLayout->SetTrimming(&trimming, ellipsis.Get());

                ID2D1Brush *descDrawBrush =
                    is_selected ? static_cast<ID2D1Brush *>(
                                      data->selectionDescBrush.Get())
                                : static_cast<ID2D1Brush *>(
                                      data->descriptionBrush.Get());

                rt->DrawTextLayout(D2D1::Point2F(desc_x, text_y),
                                   descLayout.Get(), descDrawBrush);
            }
        }

        // Draw hotkey hint on the right side of the item
        if (!hotkey_hint.empty()) {
            std::wstring hint_wide = utf8_to_wide(hotkey_hint);
            const auto hint_size =
                measure_text(dwFactory, textFormat, hint_wide);

            // Position at far right of item area
            const float hint_x = static_cast<float>(ui::BORDER_WIDTH) +
                                 content_width - hint_size.width -
                                 static_cast<float>(ui::TEXT_MARGIN);
            const float hint_y =
                y_pos + (item_height - hint_size.height) / 2.0f;

            ID2D1Brush *hintBrush =
                is_selected
                    ? static_cast<ID2D1Brush *>(data->selectionDescBrush.Get())
                    : static_cast<ID2D1Brush *>(data->descriptionBrush.Get());

            rt->DrawText(hint_wide.c_str(),
                         static_cast<UINT32>(hint_wide.length()), textFormat,
                         D2D1::RectF(hint_x, hint_y,
                                     hint_x + hint_size.width + 10,
                                     hint_y + hint_size.height + 10),
                         hintBrush);
        }
    }

    HRESULT hr = rt->EndDraw();

    if (hr == D2DERR_RECREATE_TARGET) {
        // Device lost - need to recreate
        data->renderTarget.Reset();
        data->invalidateBrushes();
        LOG_WARNING("D2D device lost, will recreate on next frame");
    } else if (FAILED(hr)) {
        throw std::runtime_error("D2D EndDraw failed");
    }

    auto tok = std::chrono::steady_clock::now();
    LOG_DEBUG("D2D draw took %lld ms",
              std::chrono::duration_cast<std::chrono::milliseconds>(tok - tik)
                  .count());
}

// ============================================================================
// PlatformWindow - Present
// ============================================================================

void PlatformWindow::commit_surface()
{
    // HwndRenderTarget handles presentation automatically in EndDraw()
    // This function exists for API consistency with other platforms
}

// ============================================================================
// PlatformWindow - Visibility Control
// ============================================================================

void PlatformWindow::show()
{
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
}

void PlatformWindow::hide() { ShowWindow(hwnd, SW_HIDE); }

bool PlatformWindow::is_visible() const
{
    return IsWindowVisible(hwnd) != FALSE;
}

// === Global Hotkey Implementation ===

static constexpr int KHALA_HOTKEY_ID = 1;

// Convert ui::KeyCode to Windows Virtual Key code
static UINT keycode_to_vk(ui::KeyCode key)
{
    switch (key) {
    case ui::KeyCode::Escape:
        return VK_ESCAPE;
    case ui::KeyCode::Return:
        return VK_RETURN;
    case ui::KeyCode::BackSpace:
        return VK_BACK;
    case ui::KeyCode::Delete:
        return VK_DELETE;
    case ui::KeyCode::Tab:
        return VK_TAB;
    case ui::KeyCode::Space:
        return VK_SPACE;
    case ui::KeyCode::Up:
        return VK_UP;
    case ui::KeyCode::Down:
        return VK_DOWN;
    case ui::KeyCode::Left:
        return VK_LEFT;
    case ui::KeyCode::Right:
        return VK_RIGHT;
    case ui::KeyCode::Home:
        return VK_HOME;
    case ui::KeyCode::End:
        return VK_END;
    // Letters A-Z map to 'A'-'Z' (0x41-0x5A)
    case ui::KeyCode::A:
        return 'A';
    case ui::KeyCode::B:
        return 'B';
    case ui::KeyCode::C:
        return 'C';
    case ui::KeyCode::D:
        return 'D';
    case ui::KeyCode::E:
        return 'E';
    case ui::KeyCode::F:
        return 'F';
    case ui::KeyCode::G:
        return 'G';
    case ui::KeyCode::H:
        return 'H';
    case ui::KeyCode::I:
        return 'I';
    case ui::KeyCode::J:
        return 'J';
    case ui::KeyCode::K:
        return 'K';
    case ui::KeyCode::L:
        return 'L';
    case ui::KeyCode::M:
        return 'M';
    case ui::KeyCode::N:
        return 'N';
    case ui::KeyCode::O:
        return 'O';
    case ui::KeyCode::P:
        return 'P';
    case ui::KeyCode::Q:
        return 'Q';
    case ui::KeyCode::R:
        return 'R';
    case ui::KeyCode::S:
        return 'S';
    case ui::KeyCode::T:
        return 'T';
    case ui::KeyCode::U:
        return 'U';
    case ui::KeyCode::V:
        return 'V';
    case ui::KeyCode::W:
        return 'W';
    case ui::KeyCode::X:
        return 'X';
    case ui::KeyCode::Y:
        return 'Y';
    case ui::KeyCode::Z:
        return 'Z';
    // Numbers 0-9 map to '0'-'9' (0x30-0x39)
    case ui::KeyCode::Num0:
        return '0';
    case ui::KeyCode::Num1:
        return '1';
    case ui::KeyCode::Num2:
        return '2';
    case ui::KeyCode::Num3:
        return '3';
    case ui::KeyCode::Num4:
        return '4';
    case ui::KeyCode::Num5:
        return '5';
    case ui::KeyCode::Num6:
        return '6';
    case ui::KeyCode::Num7:
        return '7';
    case ui::KeyCode::Num8:
        return '8';
    case ui::KeyCode::Num9:
        return '9';
    // Function keys
    case ui::KeyCode::F1:
        return VK_F1;
    case ui::KeyCode::F2:
        return VK_F2;
    case ui::KeyCode::F3:
        return VK_F3;
    case ui::KeyCode::F4:
        return VK_F4;
    case ui::KeyCode::F5:
        return VK_F5;
    case ui::KeyCode::F6:
        return VK_F6;
    case ui::KeyCode::F7:
        return VK_F7;
    case ui::KeyCode::F8:
        return VK_F8;
    case ui::KeyCode::F9:
        return VK_F9;
    case ui::KeyCode::F10:
        return VK_F10;
    case ui::KeyCode::F11:
        return VK_F11;
    case ui::KeyCode::F12:
        return VK_F12;
    default:
        return 0;
    }
}

bool PlatformWindow::register_global_hotkey(const ui::KeyboardEvent &hotkey)
{
    UINT modifiers = MOD_NOREPEAT;
    if (ui::has_modifier(hotkey.modifiers, ui::KeyModifier::Ctrl))
        modifiers |= MOD_CONTROL;
    if (ui::has_modifier(hotkey.modifiers, ui::KeyModifier::Alt))
        modifiers |= MOD_ALT;
    if (ui::has_modifier(hotkey.modifiers, ui::KeyModifier::Shift))
        modifiers |= MOD_SHIFT;
    if (ui::has_modifier(hotkey.modifiers, ui::KeyModifier::Super))
        modifiers |= MOD_WIN;

    UINT vk = keycode_to_vk(hotkey.key);
    if (vk == 0) {
        return false;
    }

    // RegisterHotKey with HWND delivers WM_HOTKEY to the window
    return RegisterHotKey(hwnd, KHALA_HOTKEY_ID, modifiers, vk) != FALSE;
}

void PlatformWindow::unregister_global_hotkey()
{
    UnregisterHotKey(hwnd, KHALA_HOTKEY_ID);
}

// ============================================================================
// PlatformWindow - Input Events
// ============================================================================

std::vector<ui::UserInputEvent> PlatformWindow::get_input_events(bool blocking)
{
    auto *data = getWindowData(hwnd);
    if (!data) {
        return {};
    }

    MSG msg;
    if (blocking) {
        // Blocking: wait for at least one message
        if (GetMessage(&msg, hwnd, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Process all pending messages
    while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Return collected events
    std::vector<ui::UserInputEvent> events = std::move(data->pendingEvents);
    data->pendingEvents.clear();
    return events;
}

static ui::KeyModifier get_active_modifiers()
{
    ui::KeyModifier mods = ui::KeyModifier::NoModifier;
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        mods |= ui::KeyModifier::Ctrl;
    }
    if (GetKeyState(VK_MENU) & 0x8000) { // Alt
        mods |= ui::KeyModifier::Alt;
    }
    if (GetKeyState(VK_SHIFT) & 0x8000) {
        mods |= ui::KeyModifier::Shift;
    }
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) {
        mods |= ui::KeyModifier::Super;
    }
    return mods;
}

// ============================================================================
// Window Procedure
// ============================================================================

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                LPARAM lParam)
{
    auto *data = getWindowData(hwnd);

    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_HOTKEY:
        // Global hotkey was pressed - generate HotkeyEvent
        if (data) {
            data->pendingEvents.push_back(ui::HotkeyEvent{});
        }
        return 0;

    case WM_KILLFOCUS:
        // Window lost focus - send Escape to trigger close/hide
        // Only do this if window is visible to avoid double-toggle in
        // background mode
        if (data && IsWindowVisible(hwnd)) {
            ui::KeyboardEvent event{.key = ui::KeyCode::Escape,
                                    .modifiers = ui::KeyModifier::NoModifier,
                                    .character = std::nullopt};
            data->pendingEvents.push_back(event);
        }
        return 0;

    case WM_KEYDOWN: {
        if (!data)
            return 0;

        ui::KeyboardEvent event{
            .key = ui::KeyCode::Character, // Default, will be overwritten
            .modifiers = get_active_modifiers(),
            .character = std::nullopt};

        // Map virtual key codes
        switch (wParam) {
        case VK_ESCAPE:
            event.key = ui::KeyCode::Escape;
            break;
        case VK_RETURN:
            event.key = ui::KeyCode::Return;
            break;
        case VK_BACK:
            event.key = ui::KeyCode::BackSpace;
            break;
        case VK_DELETE:
            event.key = ui::KeyCode::Delete;
            break;
        case VK_TAB:
            event.key = ui::KeyCode::Tab;
            break;
        case VK_UP:
            event.key = ui::KeyCode::Up;
            break;
        case VK_DOWN:
            event.key = ui::KeyCode::Down;
            break;
        case VK_LEFT:
            event.key = ui::KeyCode::Left;
            break;
        case VK_RIGHT:
            event.key = ui::KeyCode::Right;
            break;
        case VK_HOME:
            event.key = ui::KeyCode::Home;
            break;
        case VK_END:
            event.key = ui::KeyCode::End;
            break;
        default:
            // For regular characters, handle in WM_CHAR
            // But if Ctrl is held, WM_CHAR won't fire for letters,
            // so handle Ctrl+letter here
            if (ui::has_modifier(event.modifiers, ui::KeyModifier::Ctrl) &&
                wParam >= 'A' && wParam <= 'Z') {
                // Set proper KeyCode enum for hotkey matching
                event.key = static_cast<ui::KeyCode>(
                    static_cast<int>(ui::KeyCode::A) + (wParam - 'A'));
                data->pendingEvents.push_back(event);
            } else if (ui::has_modifier(event.modifiers,
                                        ui::KeyModifier::Ctrl) &&
                       wParam >= '0' && wParam <= '9') {
                // Set proper KeyCode enum for hotkey matching
                event.key = static_cast<ui::KeyCode>(
                    static_cast<int>(ui::KeyCode::Num0) + (wParam - '0'));
                data->pendingEvents.push_back(event);
            }
            return 0;
        }

        data->pendingEvents.push_back(event);
        return 0;
    }

    case WM_CHAR: {
        if (!data)
            return 0;

        // Filter out control characters (handled by WM_KEYDOWN)
        // But allow regular printable characters
        if (wParam < 32)
            return 0;

        // Filter out characters > 127 for now (char is signed)
        // For full Unicode support, you'd need to change character to wchar_t
        // or use UTF-8
        if (wParam > 127)
            return 0;

        ui::KeyboardEvent event{
            .key = ui::KeyCode::Character,
            .modifiers =
                ui::KeyModifier::NoModifier, // No modifier for typed characters
            .character = static_cast<char>(wParam)};

        data->pendingEvents.push_back(event);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!data)
            return 0;

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (!data->mouseInside) {
            data->mouseInside = true;
            data->pendingEvents.push_back(ui::CursorEnterEvent{
                .position = ui::WindowCoord{.x = x, .y = y}});

            // Track mouse leave events
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }

        data->pendingEvents.push_back(ui::MousePositionEvent{
            .position = ui::WindowCoord{.x = x, .y = y}});
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (!data)
            return 0;

        data->mouseInside = false;
        data->pendingEvents.push_back(ui::CursorLeaveEvent{});
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        if (!data)
            return 0;

        SetFocus(hwnd);

        ui::MouseButtonEvent::Button button;
        if (msg == WM_LBUTTONDOWN) {
            button = ui::MouseButtonEvent::Button::Left;
        } else if (msg == WM_RBUTTONDOWN) {
            button = ui::MouseButtonEvent::Button::Right;
        } else {
            button = ui::MouseButtonEvent::Button::Middle;
        }

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        data->pendingEvents.push_back(
            ui::MouseButtonEvent{.button = button,
                                 .pressed = true,
                                 .position = ui::WindowCoord{.x = x, .y = y}});
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        if (!data)
            return 0;

        ui::MouseButtonEvent::Button button;
        if (msg == WM_LBUTTONUP) {
            button = ui::MouseButtonEvent::Button::Left;
        } else if (msg == WM_RBUTTONUP) {
            button = ui::MouseButtonEvent::Button::Right;
        } else {
            button = ui::MouseButtonEvent::Button::Middle;
        }

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        data->pendingEvents.push_back(
            ui::MouseButtonEvent{.button = button,
                                 .pressed = false,
                                 .position = ui::WindowCoord{.x = x, .y = y}});
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (!data)
            return 0;

        int delta = GET_WHEEL_DELTA_WPARAM(wParam);

        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);

        // Positive delta = scroll up, negative = scroll down
        ui::MouseScrollEvent::Direction direction;
        if (delta > 0) {
            direction = ui::MouseScrollEvent::Direction::Up;
        } else {
            direction = ui::MouseScrollEvent::Direction::Down;
        }

        data->pendingEvents.push_back(ui::MouseScrollEvent{
            .direction = direction,
            .position = ui::WindowCoord{.x = static_cast<int>(pt.x),
                                        .y = static_cast<int>(pt.y)}});
        return 0;
    }

    case WM_PAINT: {
        // We handle painting ourselves via draw()
        ValidateRect(hwnd, nullptr);
        return 0;
    }

    case WM_ERASEBKGND:
        // Prevent flicker - we draw the entire window
        return 1;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
