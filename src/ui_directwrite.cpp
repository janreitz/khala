#ifdef _WIN32

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <chrono>

// Use Microsoft::WRL::ComPtr for automatic COM cleanup
using Microsoft::WRL::ComPtr;

// Forward declarations - these should match your existing types
struct Config;
struct State;
struct ContextMenu;

// Singleton for DirectWrite/Direct2D resources (expensive to create)
class DWriteRenderer {
public:
    static DWriteRenderer& instance() {
        static DWriteRenderer inst;
        return inst;
    }

    ID2D1Factory* d2dFactory() { return m_d2dFactory.Get(); }
    IDWriteFactory* dwriteFactory() { return m_dwriteFactory.Get(); }
    IDWriteTextFormat* textFormat() { return m_textFormat.Get(); }
    
    // Call once at startup or when font changes
    void setFont(const std::wstring& fontFamily, float fontSize) {
        m_dwriteFactory->CreateTextFormat(
            fontFamily.c_str(),
            nullptr,  // font collection (nullptr = system)
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize,
            L"en-us",
            &m_textFormat
        );
        m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

private:
    DWriteRenderer() {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
        );
        // Default font
        setFont(L"Segoe UI", 12.0f);
    }

    ComPtr<ID2D1Factory> m_d2dFactory;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<IDWriteTextFormat> m_textFormat;
};

// Helper to convert UTF-8 to wide string
std::wstring utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], size);
    return result;
}

// Color helper
D2D1_COLOR_F make_color(float r, float g, float b, float a = 1.0f) {
    return D2D1::ColorF(r, g, b, a);
}

// Rounded rectangle helper
void draw_rounded_rect(ID2D1RenderTarget* rt, ID2D1Brush* brush, 
                       float x, float y, float w, float h, float radius, bool fill) {
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radius, radius);
    if (fill) {
        rt->FillRoundedRectangle(rr, brush);
    } else {
        rt->DrawRoundedRectangle(rr, brush);
    }
}

// Text measurement helper
D2D1_SIZE_F measure_text(IDWriteFactory* factory, IDWriteTextFormat* format,
                         const std::wstring& text, float maxWidth = 10000.0f) {
    ComPtr<IDWriteTextLayout> layout;
    factory->CreateTextLayout(
        text.c_str(), 
        static_cast<UINT32>(text.length()),
        format,
        maxWidth,
        10000.0f,
        &layout
    );
    
    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    return D2D1::SizeF(metrics.width, metrics.height);
}

// Main draw function - DirectWrite version
void draw_directwrite(
    ID2D1RenderTarget* rt,  // You'll create this from your HWND or cairo surface
    unsigned int window_width, 
    unsigned int window_height,
    const Config& config, 
    const State& state)
{
    const auto tik = std::chrono::steady_clock::now();
    
    auto& renderer = DWriteRenderer::instance();
    auto* dwFactory = renderer.dwriteFactory();
    auto* textFormat = renderer.textFormat();
    
    const float content_width = static_cast<float>(window_width) - 2.0f * BORDER_WIDTH;
    const size_t max_visible_items = calculate_max_visible_items(window_height, config.font_size);

    rt->BeginDraw();
    
    // Clear with transparent
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));

    // Create brushes (could cache these too for even more perf)
    ComPtr<ID2D1SolidColorBrush> bgBrush, inputBgBrush, textBrush, selectionBrush;
    ComPtr<ID2D1SolidColorBrush> selTextBrush, descBrush, errorBrush, errorTextBrush;
    
    rt->CreateSolidColorBrush(
        D2D1::ColorF(config.background_color.r, config.background_color.g, 
                     config.background_color.b, config.background_color.a), 
        &bgBrush);
    rt->CreateSolidColorBrush(
        D2D1::ColorF(config.input_background_color.r, config.input_background_color.g,
                     config.input_background_color.b, config.input_background_color.a),
        &inputBgBrush);
    rt->CreateSolidColorBrush(
        D2D1::ColorF(config.text_color.r, config.text_color.g,
                     config.text_color.b, config.text_color.a),
        &textBrush);
    rt->CreateSolidColorBrush(
        D2D1::ColorF(config.selection_color.r, config.selection_color.g,
                     config.selection_color.b, config.selection_color.a),
        &selectionBrush);
    rt->CreateSolidColorBrush(
        D2D1::ColorF(config.selection_text_color.r, config.selection_text_color.g,
                     config.selection_text_color.b, config.selection_text_color.a),
        &selTextBrush);
    rt->CreateSolidColorBrush(
        D2D1::ColorF(config.description_color.r, config.description_color.g,
                     config.description_color.b, config.description_color.a),
        &descBrush);
    rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.8f, 0.8f, 1.0f), &errorBrush);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.7f, 0.0f, 0.0f, 1.0f), &errorTextBrush);

    // Draw background
    draw_rounded_rect(rt, bgBrush.Get(), 0, 0, 
                      static_cast<float>(window_width), static_cast<float>(window_height), 
                      CORNER_RADIUS, true);

    // Draw Input Area
    const float input_height = static_cast<float>(calculate_abs_input_height(config.font_size));
    
    ID2D1SolidColorBrush* inputFillBrush = state.has_error() ? errorBrush.Get() : inputBgBrush.Get();
    ID2D1SolidColorBrush* inputStrokeBrush = state.has_error() ? errorTextBrush.Get() : selectionBrush.Get();
    
    draw_rounded_rect(rt, inputFillBrush, BORDER_WIDTH, BORDER_WIDTH, 
                      content_width, input_height, CORNER_RADIUS, true);
    draw_rounded_rect(rt, inputStrokeBrush, BORDER_WIDTH, BORDER_WIDTH,
                      content_width, input_height, CORNER_RADIUS, false);

    // Determine display text
    std::wstring display_text;
    if (state.has_error()) {
        display_text = utf8_to_wide(*state.error_message);
    } else if (std::holds_alternative<ContextMenu>(state.mode)) {
        display_text = utf8_to_wide(std::get<ContextMenu>(state.mode).title + " › Actions");
    } else if (state.input_buffer.empty()) {
        const size_t total_files = state.cached_file_search_update.has_value()
            ? state.cached_file_search_update->total_files : 0;
        display_text = utf8_to_wide("Search " + format_file_count(total_files) + 
                                    " files... (prefix > for utility actions, ! for applications)");
    } else {
        display_text = utf8_to_wide(state.input_buffer);
    }

    // Create text layout for input
    ComPtr<IDWriteTextLayout> inputLayout;
    dwFactory->CreateTextLayout(
        display_text.c_str(),
        static_cast<UINT32>(display_text.length()),
        textFormat,
        content_width - 2 * INPUT_TEXT_MARGIN,
        input_height,
        &inputLayout
    );

    // Get metrics for centering
    DWRITE_TEXT_METRICS inputMetrics;
    inputLayout->GetMetrics(&inputMetrics);
    
    const float input_text_y = BORDER_WIDTH + (input_height - inputMetrics.height) / 2.0f;
    
    ID2D1Brush* inputTextBrush = state.has_error() ? errorTextBrush.Get() : textBrush.Get();
    rt->DrawTextLayout(
        D2D1::Point2F(BORDER_WIDTH + INPUT_TEXT_MARGIN, input_text_y),
        inputLayout.Get(),
        inputTextBrush
    );

    // Draw cursor (when not in context menu and no error)
    if (!std::holds_alternative<ContextMenu>(state.mode) && !state.has_error()) {
        std::wstring text_before_cursor = utf8_to_wide(
            state.input_buffer.substr(0, state.cursor_position));
        
        auto cursor_size = measure_text(dwFactory, textFormat, text_before_cursor);
        float cursor_x = BORDER_WIDTH + INPUT_TEXT_MARGIN + cursor_size.width;
        
        rt->DrawLine(
            D2D1::Point2F(cursor_x, input_text_y),
            D2D1::Point2F(cursor_x, input_text_y + inputMetrics.height),
            textBrush.Get(),
            1.0f
        );
    }

    // Draw progress indicator (file search mode)
    if (std::holds_alternative<ui::FileSearch>(state.mode) &&
        state.cached_file_search_update.has_value() &&
        state.cached_file_search_update->total_files > 0) {
        
        const auto& update = *state.cached_file_search_update;
        std::wstring indicator_text;
        
        if (state.input_buffer.empty()) {
            indicator_text = update.scan_complete ? L"✓" : L"⟳";
        } else if (update.total_available_results == 0) {
            indicator_text = L"0";
        } else {
            indicator_text = utf8_to_wide(create_pagination_text(
                state.visible_range_offset, max_visible_items,
                state.items.size(), update.total_available_results));
        }

        auto indicator_size = measure_text(dwFactory, textFormat, indicator_text);
        
        ComPtr<ID2D1SolidColorBrush> indicatorBrush;
        if (update.scan_complete) {
            rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.8f, 0.0f), &indicatorBrush);
        } else {
            rt->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.9f, 0.0f), &indicatorBrush);
        }
        
        float indicator_x = BORDER_WIDTH + content_width - indicator_size.width - INPUT_TEXT_MARGIN;
        float indicator_y = BORDER_WIDTH + (input_height - indicator_size.height) / 2.0f;
        
        rt->DrawText(
            indicator_text.c_str(),
            static_cast<UINT32>(indicator_text.length()),
            textFormat,
            D2D1::RectF(indicator_x, indicator_y, 
                        indicator_x + indicator_size.width + 10, 
                        indicator_y + indicator_size.height + 10),
            indicatorBrush.Get()
        );
    }

    // Dropdown items
    const float dropdown_start_y = BORDER_WIDTH + input_height + ITEMS_SPACING;
    const float item_height = static_cast<float>(calculate_abs_item_height(config.font_size));
    
    const auto query_opt = get_query(state.mode);
    const std::string query = query_opt.value_or("");
    
    const size_t range_end = std::min(
        state.visible_range_offset + max_visible_items, state.items.size());

    for (size_t i = state.visible_range_offset; i < range_end; ++i) {
        const float y_pos = dropdown_start_y + 
            static_cast<float>(i - state.visible_range_offset) * item_height;
        
        const bool is_selected = (i == state.selected_item_index);
        
        // Selection highlight
        if (is_selected) {
            draw_rounded_rect(rt, selectionBrush.Get(), 
                              BORDER_WIDTH, y_pos, content_width, item_height, 
                              CORNER_RADIUS, true);
        }

        // Title text
        std::wstring title = utf8_to_wide(state.items[i].title);
        
        ComPtr<IDWriteTextLayout> titleLayout;
        dwFactory->CreateTextLayout(
            title.c_str(),
            static_cast<UINT32>(title.length()),
            textFormat,
            content_width - 2 * TEXT_MARGIN,
            item_height,
            &titleLayout
        );
        
        // Apply highlighting for fuzzy matches
        if (query_opt) {
            auto match_positions = fuzzy::fuzzy_match_optimal(state.items[i].title, query);
            for (size_t pos : match_positions) {
                DWRITE_TEXT_RANGE range = { static_cast<UINT32>(pos), 1 };
                titleLayout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
                // Could also set underline: titleLayout->SetUnderline(TRUE, range);
            }
        }

        DWRITE_TEXT_METRICS titleMetrics;
        titleLayout->GetMetrics(&titleMetrics);
        
        float text_y = y_pos + (item_height - titleMetrics.height) / 2.0f;
        
        rt->DrawTextLayout(
            D2D1::Point2F(BORDER_WIDTH + TEXT_MARGIN, text_y),
            titleLayout.Get(),
            is_selected ? selTextBrush.Get() : textBrush.Get()
        );

        // Description (to the right of title)
        if (!state.items[i].description.empty()) {
            std::wstring desc = utf8_to_wide(state.items[i].description);
            float desc_x = BORDER_WIDTH + TEXT_MARGIN + titleMetrics.width + DESCRIPTION_SPACING;
            float available_width = content_width - TEXT_MARGIN - titleMetrics.width - DESCRIPTION_SPACING;
            
            if (available_width > 50) {  // Only draw if there's reasonable space
                ComPtr<IDWriteTextLayout> descLayout;
                dwFactory->CreateTextLayout(
                    desc.c_str(),
                    static_cast<UINT32>(desc.length()),
                    textFormat,
                    available_width,
                    item_height,
                    &descLayout
                );
                
                // Set ellipsis for overflow
                DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                ComPtr<IDWriteInlineObject> ellipsis;
                dwFactory->CreateEllipsisTrimmingSign(textFormat, &ellipsis);
                descLayout->SetTrimming(&trimming, ellipsis.Get());

                ID2D1Brush* descDrawBrush = is_selected ? 
                    static_cast<ID2D1Brush*>(selTextBrush.Get()) : // Could use selection_description_color
                    static_cast<ID2D1Brush*>(descBrush.Get());
                
                rt->DrawTextLayout(
                    D2D1::Point2F(desc_x, text_y),
                    descLayout.Get(),
                    descDrawBrush
                );
            }
        }
    }

    rt->EndDraw();

    const auto tok = std::chrono::steady_clock::now();
    LOG_DEBUG("DirectWrite draw took %lld ms",
              std::chrono::duration_cast<std::chrono::milliseconds>(tok - tik).count());
}

// ============================================================================
// Integration with Cairo (if you still want to use Cairo for the window)
// ============================================================================

// Option A: Create D2D render target from HWND directly (recommended)
class D2DWindowRenderer {
public:
    void initialize(HWND hwnd) {
        auto& renderer = DWriteRenderer::instance();
        
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            hwnd, 
            D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)
        );
        
        renderer.d2dFactory()->CreateHwndRenderTarget(props, hwndProps, &m_renderTarget);
    }
    
    void resize(unsigned int width, unsigned int height) {
        if (m_renderTarget) {
            m_renderTarget->Resize(D2D1::SizeU(width, height));
        }
    }
    
    ID2D1HwndRenderTarget* target() { return m_renderTarget.Get(); }

private:
    ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
};

// Option B: Render to a bitmap and blit to Cairo surface (for gradual migration)
class D2DBitmapRenderer {
public:
    void initialize(unsigned int width, unsigned int height) {
        auto& renderer = DWriteRenderer::instance();
        
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        
        // Create a WIC bitmap render target
        // This is more complex - you'd need IWICImagingFactory
        // For simplicity, using HWND target is recommended
    }
};

#endif // _WIN32