#pragma once

#ifdef _WIN32

#include <d2d1.h>
#include <dwrite.h>
#include <string>

// Forward declarations
struct Config;
struct State;

/*
 * DIRECTWRITE RENDERER FOR WINDOWS
 * ================================
 * 
 * WHY THIS IS FASTER THAN PANGO:
 * 
 * 1. Native Font Caching: DirectWrite uses Windows' native font cache which is
 *    always warm. Pango on Windows has to build its own font cache, often from
 *    scratch on first run.
 * 
 * 2. Hardware Acceleration: Direct2D (used with DirectWrite) can use GPU 
 *    acceleration for rendering. Pango+Cairo is CPU-only on Windows.
 * 
 * 3. No Cross-Platform Abstraction: DirectWrite talks directly to Windows 
 *    font APIs. Pango has abstraction layers that add overhead.
 * 
 * 4. Font Fallback: DirectWrite's font fallback is highly optimized and cached.
 *    Pango has to do expensive lookups for characters not in the primary font.
 *
 * PERFORMANCE TIPS:
 * 
 * 1. CACHE THE FACTORIES: DWriteFactory and D2DFactory are expensive to create.
 *    Create once at startup (DWriteRenderer singleton handles this).
 * 
 * 2. CACHE TEXT FORMATS: IDWriteTextFormat objects should be reused. Only
 *    recreate when font family/size actually changes.
 * 
 * 3. CACHE BRUSHES: For colors that don't change, create brushes once and
 *    reuse them. The example code creates them per-frame for clarity, but
 *    you should cache them.
 * 
 * 4. REUSE TEXT LAYOUTS: If displaying the same text repeatedly (like a
 *    static label), cache the IDWriteTextLayout.
 * 
 * 5. BATCH SIMILAR OPERATIONS: Draw all text of the same color together
 *    to minimize brush switches.
 * 
 * MIGRATION STRATEGY:
 * 
 * Option A - Full Migration (Recommended):
 *   - Replace Cairo window surface with D2D HwndRenderTarget
 *   - Use draw_directwrite() for all rendering
 *   - Pros: Maximum performance, cleanest code
 *   - Cons: More work upfront, Windows-only code path
 * 
 * Option B - Gradual Migration:
 *   - Keep Cairo for window management
 *   - Render text to a D2D bitmap, blit to Cairo surface
 *   - Pros: Easier migration, can keep cross-platform structure
 *   - Cons: Some overhead from bitmap copy
 * 
 * Option C - Hybrid:
 *   - Use Cairo for shapes (rounded rects, etc.)
 *   - Use DirectWrite only for text via cairo_win32_surface
 *   - Pros: Minimal changes to existing code
 *   - Cons: Still some Cairo overhead, complex integration
 */

// Initialize the DirectWrite renderer - call once at startup
void dwrite_init();

// Set the font - call when config changes
void dwrite_set_font(const std::wstring& fontFamily, float fontSize);

// Main draw function
// rt: Direct2D render target (create from HWND or as bitmap)
void draw_directwrite(
    ID2D1RenderTarget* rt,
    unsigned int window_width,
    unsigned int window_height,
    const Config& config,
    const State& state
);

// Helper class for managing the D2D render target
class D2DWindowRenderer {
public:
    void initialize(HWND hwnd);
    void resize(unsigned int width, unsigned int height);
    ID2D1HwndRenderTarget* target();
    
private:
    struct Impl;
    Impl* m_impl = nullptr;
};

/*
 * EXAMPLE USAGE IN YOUR MAIN LOOP:
 * ================================
 * 
 * // At startup:
 * D2DWindowRenderer renderer;
 * renderer.initialize(hwnd);
 * dwrite_set_font(L"Segoe UI", 12.0f);
 * 
 * // On WM_SIZE:
 * renderer.resize(new_width, new_height);
 * 
 * // In your render/paint handler:
 * draw_directwrite(renderer.target(), width, height, config, state);
 * 
 * // That's it! No
 
 
 
 
 
 
 
 
 
 
  
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
  
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
  
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 Present() call needed - HWND target presents automatically
 */

#endif // _WIN32