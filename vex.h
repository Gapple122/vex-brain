#ifndef VEX_SIMULATOR_VEX_H
#define VEX_SIMULATOR_VEX_H

// Requires C++17.

#ifndef NOMINMAX
#define NOMINMAX            // otherwise windows.h breaks std::min / std::max
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <type_traits>

namespace vex {

// ============================================================
// Simulator globals (defined in vex_simulator.cpp)
// ============================================================

extern std::atomic<bool> g_mousePressed;
extern std::atomic<int>  g_mouseX;
extern std::atomic<int>  g_mouseY;
extern std::atomic<long long> g_pressTimeNs;   // time of the last touch-down

extern HWND g_simulatorWindow;

// Real Brain resolution and the on-screen scale factor.
inline constexpr int kScreenWidth  = 480;
inline constexpr int kScreenHeight = 240;
inline constexpr int kScale        = 3;

// If true, the first call to Brain.Screen.render() switches the screen to
// double-buffered mode: the window only changes when render() is called.
// If false, the window always shows what has been drawn so far.
inline constexpr bool kRenderEnablesDoubleBuffer = true;

// A touch is reported as "pressing" for at least this long, so a quick click
// is not missed by a program that only checks the screen every 50-100 ms.
inline constexpr int kMinPressMs = 100;


// ============================================================
// Units
// ============================================================

struct percentUnits {};
struct rpmUnits {};
struct celsiusUnits {};
struct ampUnits {};
struct voltUnits {};
struct NmUnits {};
struct wattUnits {};
struct degreeUnits {};
struct msecUnits {};
struct secUnits {};

inline constexpr percentUnits percent{};
inline constexpr rpmUnits rpm{};
inline constexpr celsiusUnits celsius{};
inline constexpr ampUnits amp{};
inline constexpr voltUnits volt{};
inline constexpr NmUnits Nm{};
inline constexpr wattUnits watt{};
inline constexpr degreeUnits degrees{};
inline constexpr msecUnits msec{};
inline constexpr secUnits sec{};
inline constexpr secUnits seconds{};


// ============================================================
// Colors
// ============================================================

class color {
public:
    unsigned char r;
    unsigned char g;
    unsigned char b;
    bool isTransparent;   // "no color": nothing is drawn

    constexpr color()
        : r(0), g(0), b(0), isTransparent(false) {}

    constexpr color(int red, int green, int blue, bool transp = false)
        : r((unsigned char)std::clamp(red, 0, 255)),
          g((unsigned char)std::clamp(green, 0, 255)),
          b((unsigned char)std::clamp(blue, 0, 255)),
          isTransparent(transp) {}

    // 0xRRGGBB
    constexpr color(int rgb)
        : r((unsigned char)((rgb >> 16) & 0xFF)),
          g((unsigned char)((rgb >> 8) & 0xFF)),
          b((unsigned char)(rgb & 0xFF)),
          isTransparent(false) {}

    constexpr bool operator==(const color& o) const {
        return r == o.r && g == o.g && b == o.b && isTransparent == o.isTransparent;
    }
    constexpr bool operator!=(const color& o) const { return !(*this == o); }

    static const color black;
    static const color white;
    static const color red;
    static const color green;
    static const color blue;
    static const color yellow;
    static const color orange;
    static const color purple;
    static const color cyan;
    static const color gray;
    static const color grey;
    static const color transparent;
};

inline const color color::black  = color(0, 0, 0);
inline const color color::white  = color(255, 255, 255);
inline const color color::red    = color(255, 0, 0);
inline const color color::green  = color(0, 255, 0);
inline const color color::blue   = color(0, 0, 255);
inline const color color::yellow = color(255, 255, 0);
inline const color color::orange = color(255, 165, 0);
inline const color color::purple = color(160, 32, 240);
inline const color color::cyan   = color(0, 255, 255);
inline const color color::gray   = color(128, 128, 128);
inline const color color::grey   = color(128, 128, 128);
inline const color color::transparent = color(0, 0, 0, true);

// VEXcode lets you write plain "red", "black", ... after "using namespace vex".
inline constexpr color black(0, 0, 0);
inline constexpr color white(255, 255, 255);
inline constexpr color red(255, 0, 0);
inline constexpr color green(0, 255, 0);
inline constexpr color blue(0, 0, 255);
inline constexpr color yellow(255, 255, 0);
inline constexpr color orange(255, 165, 0);
inline constexpr color purple(160, 32, 240);
inline constexpr color cyan(0, 255, 255);
inline constexpr color gray(128, 128, 128);
inline constexpr color grey(128, 128, 128);
inline constexpr color transparent(0, 0, 0, true);


// ============================================================
// Fonts
// ============================================================

enum fontType {
    mono12,
    mono15,
    mono20,
    mono30,
    mono40,
    mono60,
    prop12,
    prop15,
    prop20,
    prop30,
    prop40,
    prop60
};


// ============================================================
// Motor settings
// ============================================================

enum brakeType {
    coast,
    brake,
    hold
};

enum directionType {
    forward,
    reverse
};

enum class gearSetting {
    ratio36_1,
    ratio18_1,
    ratio6_1
};


// ============================================================
// Forward declarations
// ============================================================

class brain;
class controller;
class motor;
class device;


namespace detail {

inline COLORREF toColorRef(const color& c) {
    return RGB(c.r, c.g, c.b);
}

// Character cell size, in Brain pixels, for each font.
inline int cellH(fontType f) {
    switch (f) {
        case mono12: case prop12: return 12;
        case mono15: case prop15: return 15;
        case mono20: case prop20: return 20;
        case mono30: case prop30: return 30;
        case mono40: case prop40: return 40;
        case mono60: case prop60: return 60;
    }
    return 20;
}

inline int cellW(fontType f) {
    return (cellH(f) + 1) / 2;
}

inline bool isMono(fontType f) {
    return f == mono12 || f == mono15 || f == mono20 ||
           f == mono30 || f == mono40 || f == mono60;
}

template<typename... Args>
std::string formatString(const char* fmt, Args... args) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    int n = std::snprintf(nullptr, 0, fmt, args...);
    if (n <= 0)
        return std::string();

    std::vector<char> buf(static_cast<size_t>(n) + 1);
    std::snprintf(buf.data(), buf.size(), fmt, args...);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    return std::string(buf.data(), static_cast<size_t>(n));
}

} // namespace detail


// ============================================================
// Brain Screen
//
// Drawing happens immediately into an off-screen bitmap (just like
// the real Brain's framebuffer), so text and shapes overwrite what
// was there before. The window simply shows that bitmap.
// ============================================================

class brainScreen {

private:

    static constexpr int kFontCount = 12;

    std::mutex mutex_;

    // Drawing state (matches Brain defaults).
    color penColor_  = color::white;
    color fillColor_ = color::black;
    int   penWidth_  = 1;
    fontType font_   = mono20;

    int cursorRow_ = 1;
    int cursorCol_ = 1;

    HWND hwnd_ = nullptr;

    // Two bitmaps, each kScreenWidth*kScale x kScreenHeight*kScale.
    //   back  : everything is drawn here
    //   front : what the window shows once render() has been called
    HDC     backDC_   = nullptr;
    HDC     frontDC_  = nullptr;
    HBITMAP backBmp_  = nullptr;
    HBITMAP frontBmp_ = nullptr;
    HGDIOBJ backOld_  = nullptr;
    HGDIOBJ frontOld_ = nullptr;

    // Only the GUI thread touches this one (see paintToWindow).
    HDC     paintDC_  = nullptr;
    HBITMAP paintBmp_ = nullptr;
    HGDIOBJ paintOld_ = nullptr;

    bool doubleBuffered_ = false;
    std::atomic<bool> dirty_{true};
    std::atomic<bool> repaintQueued_{false};
    std::chrono::steady_clock::time_point lastRender_{};

    // Ask the window to repaint (at most one request outstanding).
    void requestRepaint() {
        dirty_ = true;
        if (hwnd_ && !repaintQueued_.exchange(true))
            InvalidateRect(hwnd_, nullptr, FALSE);
    }

    // Called after every drawing operation.
    void markDirty() {
        dirty_ = true;
        if (!doubleBuffered_)
            requestRepaint();
    }

    // Set while the GUI thread wants to paint. Drawing threads step aside
    // so a program that draws in a tight loop cannot starve the window
    // (std::mutex is not fair, and a starved window looks frozen).
    std::atomic<bool> paintPending_{false};

    class UserLock {
        std::unique_lock<std::mutex> lock_;
    public:
        explicit UserLock(brainScreen& s) {
            while (s.paintPending_.load())
                std::this_thread::yield();
            lock_ = std::unique_lock<std::mutex>(s.mutex_);
        }
    };

    HFONT fonts_[kFontCount] = {};

    void (*pressedCb_)(void)  = nullptr;
    void (*releasedCb_)(void) = nullptr;


    static int bufW() { return kScreenWidth  * kScale; }
    static int bufH() { return kScreenHeight * kScale; }


    // --------------------------------------------------------
    // Buffer management
    // --------------------------------------------------------

    static void createBuffer(HDC ref, HDC& dc, HBITMAP& bmp, HGDIOBJ& old) {
        dc  = CreateCompatibleDC(ref);
        bmp = CreateCompatibleBitmap(ref, bufW(), bufH());
        old = SelectObject(dc, bmp);

        RECT rc{0, 0, bufW(), bufH()};
        FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }

    static void destroyBuffer(HDC& dc, HBITMAP& bmp, HGDIOBJ& old) {
        if (dc) {
            SelectObject(dc, old);
            DeleteObject(bmp);
            DeleteDC(dc);
        }
        dc = nullptr;
        bmp = nullptr;
        old = nullptr;
    }

    void releaseLocked() {
        destroyBuffer(backDC_, backBmp_, backOld_);
        destroyBuffer(frontDC_, frontBmp_, frontOld_);
        destroyBuffer(paintDC_, paintBmp_, paintOld_);

        for (HFONT& f : fonts_) {
            if (f) DeleteObject(f);
            f = nullptr;
        }
    }

    HFONT getFontLocked(fontType f) {
        int i = static_cast<int>(f);
        if (i < 0 || i >= kFontCount)
            i = static_cast<int>(mono20);

        if (!fonts_[i]) {
            const bool mono = detail::isMono(static_cast<fontType>(i));
            const int cw = detail::cellW(static_cast<fontType>(i));
            const int ch = detail::cellH(static_cast<fontType>(i));

            int em;
            if (mono)
                em = (int)std::lround(std::min(cw * kScale / 0.55,
                                               ch * kScale * 0.85));
            else
                em = (int)std::lround(ch * kScale * 0.8);

            fonts_[i] = CreateFontA(
                -em, 0, 0, 0,
                FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_TT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                ANTIALIASED_QUALITY,
                mono ? (FIXED_PITCH | FF_MODERN) : (VARIABLE_PITCH | FF_SWISS),
                mono ? "Consolas" : "Arial");
        }

        return fonts_[i];
    }


    // --------------------------------------------------------
    // GDI helpers (mutex_ must already be held)
    // --------------------------------------------------------

    void fillPx(int l, int t, int r, int b, const color& c) {
        if (c.isTransparent || !backDC_)
            return;

        RECT rc{l, t, r, b};
        HBRUSH br = CreateSolidBrush(detail::toColorRef(c));
        FillRect(backDC_, &rc, br);
        DeleteObject(br);
    }

    // Selects a pen + brush for the lifetime of the object.
    struct Shape {
        HDC dc;
        HPEN pen;
        HBRUSH brush;
        HGDIOBJ oldPen;
        HGDIOBJ oldBrush;
        bool ownPen;
        bool ownBrush;

        Shape(HDC d, const color& pc, int pw, const color& fc)
            : dc(d)
        {
            ownPen   = !pc.isTransparent;
            ownBrush = !fc.isTransparent;

            pen = ownPen
                ? CreatePen(PS_SOLID, std::max(1, pw * kScale),
                            detail::toColorRef(pc))
                : (HPEN)GetStockObject(NULL_PEN);

            brush = ownBrush
                ? CreateSolidBrush(detail::toColorRef(fc))
                : (HBRUSH)GetStockObject(NULL_BRUSH);

            oldPen   = SelectObject(dc, pen);
            oldBrush = SelectObject(dc, brush);
        }

        ~Shape() {
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            if (ownPen)   DeleteObject(pen);
            if (ownBrush) DeleteObject(brush);
        }
    };

    // Draw text with its top-left corner at (x, yTop) in Brain pixels.
    void drawTextLocked(int x, int yTop, const std::string& s, bool opaque) {
        if (!backDC_ || s.empty())
            return;

        const int cw = detail::cellW(font_);
        const int ch = detail::cellH(font_);

        HGDIOBJ oldFont = SelectObject(backDC_, getFontLocked(font_));
        SetBkMode(backDC_, TRANSPARENT);
        SetTextColor(backDC_, detail::toColorRef(penColor_));

        if (detail::isMono(font_)) {
            // Fixed-size cells: one background fill, one text call.
            const int len = static_cast<int>(s.length());

            if (opaque)
                fillPx(x * kScale, yTop * kScale,
                       (x + len * cw) * kScale, (yTop + ch) * kScale,
                       fillColor_);

            std::vector<INT> dx(static_cast<size_t>(len), cw * kScale);

            ExtTextOutA(backDC_, x * kScale, yTop * kScale, 0, nullptr,
                        s.c_str(), len, dx.data());
        }
        else {
            SIZE sz{0, 0};
            GetTextExtentPoint32A(backDC_, s.c_str(),
                                  static_cast<int>(s.length()), &sz);

            if (opaque)
                fillPx(x * kScale, yTop * kScale,
                       x * kScale + sz.cx, (yTop + ch) * kScale,
                       fillColor_);

            TextOutA(backDC_, x * kScale, yTop * kScale, s.c_str(),
                     static_cast<int>(s.length()));
        }

        SelectObject(backDC_, oldFont);
        markDirty();
    }

    void printLocked(const std::string& s) {
        const int cw = detail::cellW(font_);
        const int ch = detail::cellH(font_);

        drawTextLocked((cursorCol_ - 1) * cw,
                       (cursorRow_ - 1) * ch,
                       s, true);

        cursorCol_ += static_cast<int>(s.length());
    }

    void printAtLocked(int x, int y, bool opaque, const std::string& s) {
        // y is the bottom of the text.
        drawTextLocked(x, y - detail::cellH(font_), s, opaque);
    }

    void drawRectLocked(int x, int y, int w, int h, const color& fill) {
        if (!backDC_) return;

        Shape s(backDC_, penColor_, penWidth_, fill);
        Rectangle(backDC_,
                  x * kScale, y * kScale,
                  (x + w) * kScale, (y + h) * kScale);
        markDirty();
    }

    void drawCircleLocked(int x, int y, int r, const color& fill) {
        if (!backDC_) return;

        Shape s(backDC_, penColor_, penWidth_, fill);
        Ellipse(backDC_,
                (x - r) * kScale, (y - r) * kScale,
                (x + r) * kScale, (y + r) * kScale);
        markDirty();
    }


public:

    brainScreen() = default;
    brainScreen(const brainScreen&) = delete;
    brainScreen& operator=(const brainScreen&) = delete;

    ~brainScreen() {
        UserLock lock(*this);
        releaseLocked();
    }


    // ========================================================
    // Initialize (called by the simulator once the window exists)
    // ========================================================

    void initialize(HWND hwnd) {

        UserLock lock(*this);

        releaseLocked();

        hwnd_ = hwnd;

        HDC screenDC = GetDC(nullptr);
        createBuffer(screenDC, backDC_,  backBmp_,  backOld_);
        createBuffer(screenDC, frontDC_, frontBmp_, frontOld_);
        createBuffer(screenDC, paintDC_, paintBmp_, paintOld_);
        ReleaseDC(nullptr, screenDC);

        penColor_  = color::white;
        fillColor_ = color::black;
        penWidth_  = 1;
        font_      = mono20;

        cursorRow_ = 1;
        cursorCol_ = 1;

        doubleBuffered_ = false;
        repaintQueued_ = false;
        requestRepaint();
    }


    // ========================================================
    // Window side: called from the GUI thread
    // ========================================================

    // Draw the current Brain image onto the window.
    void paintToWindow(HDC hdc, int clientW, int clientH) {

        // Hold the draw lock only for a fast memory-to-memory copy. The slow
        // blit to the screen happens afterwards, so the program's drawing
        // thread is never stuck waiting on the window.
        paintPending_ = true;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            paintPending_ = false;

            // Anything drawn from now on needs another repaint.
            repaintQueued_ = false;

            HDC src = doubleBuffered_ ? frontDC_ : backDC_;

            if (src && paintDC_)
                BitBlt(paintDC_, 0, 0, bufW(), bufH(), src, 0, 0, SRCCOPY);
        }

        if (!paintDC_) {
            RECT rc{0, 0, clientW, clientH};
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            return;
        }

        if (clientW == bufW() && clientH == bufH()) {
            BitBlt(hdc, 0, 0, bufW(), bufH(), paintDC_, 0, 0, SRCCOPY);
        }
        else {
            SetStretchBltMode(hdc, HALFTONE);
            SetBrushOrgEx(hdc, 0, 0, nullptr);
            StretchBlt(hdc, 0, 0, clientW, clientH,
                       paintDC_, 0, 0, bufW(), bufH(), SRCCOPY);
        }
    }

    // True (once) if something changed since the last call.
    bool takeDirty() {
        return dirty_.exchange(false);
    }

    void notifyTouch(bool down) {

        void (*cb)(void);

        {
            UserLock lock(*this);
            cb = down ? pressedCb_ : releasedCb_;
        }

        if (cb)
            std::thread([cb]() { cb(); }).detach();
    }


    // ========================================================
    // Render
    // ========================================================

    void render() {
        render(true, true);
    }

    void render(bool vsyncWait, bool = true) {

        std::chrono::steady_clock::time_point wakeAt;

        {
            UserLock lock(*this);

            if (kRenderEnablesDoubleBuffer)
                doubleBuffered_ = true;

            if (backDC_ && frontDC_)
                BitBlt(frontDC_, 0, 0, bufW(), bufH(),
                       backDC_, 0, 0, SRCCOPY);

            requestRepaint();

            // Real render() paces a program to the screen refresh. Only
            // sleep for whatever is left of the frame, never a fixed delay.
            auto now = std::chrono::steady_clock::now();
            wakeAt = std::max(now, lastRender_ +
                                   std::chrono::microseconds(8333));
            lastRender_ = wakeAt;
        }

        if (vsyncWait)
            std::this_thread::sleep_until(wakeAt);
    }


    // ========================================================
    // Clear
    // ========================================================

    void clearScreen(const color& c = color::black) {

        UserLock lock(*this);

        fillPx(0, 0, bufW(), bufH(), c);

        cursorRow_ = 1;
        cursorCol_ = 1;

        markDirty();
    }

    void clearScreen(int r, int g, int b) {
        clearScreen(color(r, g, b));
    }

    void clearLine(int row, const color& c = color::black) {

        UserLock lock(*this);

        const int ch = detail::cellH(font_);

        fillPx(0, (row - 1) * ch * kScale,
               bufW(), row * ch * kScale, c);

        cursorCol_ = 1;
        markDirty();
    }

    void clearLine() {
        int row;
        {
            UserLock lock(*this);
            row = cursorRow_;
        }
        clearLine(row);
    }


    // ========================================================
    // Pen / fill / font
    // ========================================================

    void setPenColor(const color& c) {
        UserLock lock(*this);
        penColor_ = c;
    }

    void setFillColor(const color& c) {
        UserLock lock(*this);
        fillColor_ = c;
    }

    void setPenWidth(int width) {
        UserLock lock(*this);
        penWidth_ = std::max(1, width);
    }

    void setFont(fontType f) {
        UserLock lock(*this);
        font_ = f;
    }

    int getStringWidth(const char* s) {
        UserLock lock(*this);
        return static_cast<int>(std::strlen(s)) * detail::cellW(font_);
    }

    int getStringHeight(const char*) {
        UserLock lock(*this);
        return detail::cellH(font_);
    }


    // ========================================================
    // Cursor
    // ========================================================

    void setCursor(int row, int col) {
        UserLock lock(*this);
        cursorRow_ = row;
        cursorCol_ = col;
    }

    void newLine() {
        UserLock lock(*this);
        cursorRow_++;
        cursorCol_ = 1;
    }


    // ========================================================
    // print() - draws at the text cursor, with the fill color as
    // the text background (like the real Brain).
    // ========================================================

    template<typename... Args>
    void print(const char* format, Args... args) {

        std::string s = detail::formatString(format, args...);

        UserLock lock(*this);
        printLocked(s);
    }

    template<typename T,
             typename = std::enable_if_t<std::is_arithmetic_v<T>>>
    void print(T value) {

        std::string s;

        if constexpr (std::is_same_v<T, char>)
            s = std::string(1, value);
        else if constexpr (std::is_floating_point_v<T>)
            s = detail::formatString("%g", static_cast<double>(value));
        else if constexpr (std::is_signed_v<T>)
            s = detail::formatString("%lld", static_cast<long long>(value));
        else
            s = detail::formatString("%llu",
                                     static_cast<unsigned long long>(value));

        UserLock lock(*this);
        printLocked(s);
    }


    // ========================================================
    // printAt() - (x, y) is the bottom-left of the text.
    // ========================================================

    template<typename... Args>
    void printAt(int x, int y, bool opaque, const char* format, Args... args) {

        std::string s = detail::formatString(format, args...);

        UserLock lock(*this);
        printAtLocked(x, y, opaque, s);
    }

    template<typename... Args>
    void printAt(int x, int y, const char* format, Args... args) {

        std::string s = detail::formatString(format, args...);

        UserLock lock(*this);
        printAtLocked(x, y, true, s);
    }


    // ========================================================
    // Shapes
    // ========================================================

    void drawLine(int x1, int y1, int x2, int y2) {

        UserLock lock(*this);

        if (!backDC_ || penColor_.isTransparent)
            return;

        HPEN pen = CreatePen(PS_SOLID,
                             std::max(1, penWidth_ * kScale),
                             detail::toColorRef(penColor_));
        HGDIOBJ old = SelectObject(backDC_, pen);

        const int h = kScale / 2;   // aim at the middle of the pixel
        MoveToEx(backDC_, x1 * kScale + h, y1 * kScale + h, nullptr);
        LineTo(backDC_, x2 * kScale + h, y2 * kScale + h);

        SelectObject(backDC_, old);
        DeleteObject(pen);

        markDirty();
    }

    void drawRectangle(int x, int y, int width, int height) {
        UserLock lock(*this);
        drawRectLocked(x, y, width, height, fillColor_);
    }

    void drawRectangle(int x, int y, int width, int height, const color& c) {
        UserLock lock(*this);
        drawRectLocked(x, y, width, height, c);
    }

    void drawCircle(int x, int y, int radius) {
        UserLock lock(*this);
        drawCircleLocked(x, y, radius, fillColor_);
    }

    void drawCircle(int x, int y, int radius, const color& c) {
        UserLock lock(*this);
        drawCircleLocked(x, y, radius, c);
    }

    void drawPixel(int x, int y) {

        UserLock lock(*this);

        fillPx(x * kScale, y * kScale,
               (x + 1) * kScale, (y + 1) * kScale,
               penColor_);

        markDirty();
    }


    // ========================================================
    // Touchscreen
    // ========================================================

    bool pressing() {

        if (g_mousePressed.load())
            return true;

        // Keep a short click "down" long enough for slow polling loops.
        const long long t = g_pressTimeNs.load();

        if (t == 0)
            return false;

        const long long now =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();

        return (now - t) < static_cast<long long>(kMinPressMs) * 1000000LL;
    }
    int  xPosition() { return g_mouseX.load(); }
    int  yPosition() { return g_mouseY.load(); }

    void pressed(void (*callback)(void)) {
        UserLock lock(*this);
        pressedCb_ = callback;
    }

    void released(void (*callback)(void)) {
        UserLock lock(*this);
        releasedCb_ = callback;
    }

    // Compatibility method
    void setTouch(bool pressed, int x, int y) {
        g_mouseX.store(x);
        g_mouseY.store(y);
        g_mousePressed.store(pressed);

        if (pressed)
            g_pressTimeNs.store(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
    }
};


// ============================================================
// Timer
// ============================================================

class timer {

private:

    std::chrono::steady_clock::time_point start_ =
        std::chrono::steady_clock::now();

public:

    void clear() { start_ = std::chrono::steady_clock::now(); }

    double time(msecUnits) const {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now() - start_).count();
    }

    double time(secUnits) const {
        return time(msec) / 1000.0;
    }

    unsigned long time() const {
        return static_cast<unsigned long>(time(msec));
    }
};


// ============================================================
// Brain
// ============================================================

class brain {

public:

    brainScreen Screen;
    vex::timer  Timer;

    double timer(msecUnits) const { return Timer.time(msec); }
    double timer(secUnits)  const { return Timer.time(sec); }
};


// ============================================================
// Controller
// ============================================================

class controllerAxis {

private:

    std::atomic<int>* value_;

public:

    controllerAxis(std::atomic<int>* value)
        : value_(value) {}

    int position() const {
        return value_->load();
    }
};


class controllerButton {

private:

    std::atomic<bool>* value_;

public:

    controllerButton(std::atomic<bool>* value)
        : value_(value) {}

    bool pressing() const {
        return value_->load();
    }
};


enum controllerType {
    primary
};


class controller {

private:

    std::atomic<int> axis1_{0};
    std::atomic<int> axis3_{0};

    std::atomic<bool> r1_{false};
    std::atomic<bool> r2_{false};

public:

    controllerAxis Axis1;
    controllerAxis Axis3;

    controllerButton ButtonR1;
    controllerButton ButtonR2;


    controller(controllerType)
        : Axis1(&axis1_),
          Axis3(&axis3_),
          ButtonR1(&r1_),
          ButtonR2(&r2_) {}


    void setAxis1(int value) {
        axis1_.store(std::clamp(value, -100, 100));
    }

    void setAxis3(int value) {
        axis3_.store(std::clamp(value, -100, 100));
    }

    void setR1(bool value) { r1_.store(value); }
    void setR2(bool value) { r2_.store(value); }
};


// ============================================================
// Motor
// ============================================================

class motor {

private:

    int port_;

    gearSetting gear_;

    bool reversed_;

    double commandedPower_ = 0;

    double position_ = 0;

    brakeType stopping_ = coast;


public:

    motor(int port, gearSetting gear, bool reversed = false)
        : port_(port),
          gear_(gear),
          reversed_(reversed) {}


    void setStopping(brakeType type) {
        stopping_ = type;
    }

    void stop() {
        commandedPower_ = 0;
    }

    void spin(directionType direction, double velocity, percentUnits) {

        commandedPower_ = direction == reverse ? -velocity : velocity;

        if (reversed_)
            commandedPower_ *= -1;

        position_ += commandedPower_ * 0.001;
    }

    bool installed() const { return true; }

    double temperature(celsiusUnits) const {
        return 28.0 + std::abs(commandedPower_) * 0.20;
    }

    double velocity(rpmUnits) const {
        return commandedPower_ * 2.0;
    }

    double current(ampUnits) const {
        return 0.3 + std::abs(commandedPower_) * 0.01;
    }

    double voltage(voltUnits) const {
        return 12.2;
    }

    double torque(NmUnits) const {
        return std::abs(commandedPower_) * 0.01;
    }

    double power(wattUnits) const {
        return std::abs(commandedPower_) * 0.12;
    }

    double efficiency(percentUnits) const {
        return 85.0;
    }

    double position(degreeUnits) const {
        return position_;
    }
};


// ============================================================
// Generic V5 device
// ============================================================

class device {

private:

    int port_;

public:

    device(int port)
        : port_(port) {}

    bool installed() const { return true; }

    int type() const { return 2; }
};


// ============================================================
// VEX mutex
// ============================================================

using mutex = std::mutex;


// ============================================================
// VEX thread
// ============================================================

class thread {

private:

    std::thread thread_;

public:

    template<typename Function>
    thread(Function function)
        : thread_(function) {}

    template<typename Function, typename Arg>
    thread(Function function, Arg arg)
        : thread_(function, arg) {}

    thread(thread&& other) noexcept
        : thread_(std::move(other.thread_)) {}

    thread(const thread&) = delete;
    thread& operator=(const thread&) = delete;

    ~thread() {
        if (thread_.joinable())
            thread_.detach();
    }
};


// ============================================================
// this_thread / task / wait
// ============================================================

namespace this_thread {

inline void sleep_for(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

}

class task {
public:
    static void sleep(int milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
};

inline void wait(double amount, msecUnits) {
    std::this_thread::sleep_for(
        std::chrono::duration<double, std::milli>(amount));
}

inline void wait(double amount, secUnits) {
    std::this_thread::sleep_for(
        std::chrono::duration<double>(amount));
}

} // namespace vex

// Rename the robot program's main() so the simulator's WinMain starts first
// and runs the real main() on a separate thread. This must stay at the very
// end of this header (after all system headers). main.cpp includes vex.h, so
// its "int main()" becomes vex_user_main() automatically - no compiler flag
// needed. Define VEX_NO_MAIN_RENAME to turn this off.
#ifndef VEX_NO_MAIN_RENAME
#define main vex_user_main
#endif

#endif