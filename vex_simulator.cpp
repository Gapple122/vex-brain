#include "vex.h"

// MSVC: make this a GUI program that starts in WinMain. With the default
// console subsystem the C runtime would call your main() directly on the
// main thread (no window, endless loop = "stuck").
#if defined(_MSC_VER)
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:WinMainCRTStartup")
#endif

#include <windows.h>
#include <thread>
#include <chrono>
#include <cstdio>

// Defined exactly once in your robot code (main.cpp / robot-config.cpp):
//     vex::brain Brain;
extern vex::brain Brain;

// Your actual main.cpp provides this.
extern int main();

namespace vex {

// These are used by vex.h for mouse input.
std::atomic<bool> g_mousePressed(false);
std::atomic<int>  g_mouseX(0);
std::atomic<int>  g_mouseY(0);
std::atomic<long long> g_pressTimeNs(0);

HWND g_simulatorWindow = nullptr;

} // namespace vex


// -----------------------------------------------------------------------------
// Simulator configuration
// -----------------------------------------------------------------------------

static constexpr int SCREEN_WIDTH  = vex::kScreenWidth;    // 480
static constexpr int SCREEN_HEIGHT = vex::kScreenHeight;   // 240
static constexpr int SCALE         = vex::kScale;          // 3

static constexpr int WINDOW_WIDTH  = SCREEN_WIDTH  * SCALE; // 1440
static constexpr int WINDOW_HEIGHT = SCREEN_HEIGHT * SCALE; // 720

static const char* WINDOW_CLASS_NAME = "VEXBrainSimulatorWindow";


// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);


// -----------------------------------------------------------------------------
// Window creation
// -----------------------------------------------------------------------------

static HWND createSimulatorWindow(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEXA wc{};

    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassExA(&wc))
    {
        MessageBoxA(nullptr,
                    "Failed to register the VEX Brain simulator window class.",
                    "VEX Brain Simulator", MB_ICONERROR);
        return nullptr;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    // We want the CLIENT area to be exactly 1440x720.
    RECT rect{0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};

    if (!AdjustWindowRectEx(&rect, style, FALSE, 0))
    {
        MessageBoxA(nullptr,
                    "Failed to calculate the simulator window size.",
                    "VEX Brain Simulator", MB_ICONERROR);
        return nullptr;
    }

    HWND hwnd = CreateWindowExA(
        0,
        WINDOW_CLASS_NAME,
        "VEX V5 Brain Simulator",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
    {
        MessageBoxA(nullptr, "CreateWindowEx failed.",
                    "VEX Brain Simulator", MB_ICONERROR);
        return nullptr;
    }

    return hwnd;
}


// -----------------------------------------------------------------------------
// Convert window mouse coordinates (1440x720) to Brain coordinates (480x240).
// -----------------------------------------------------------------------------

static void updateMousePosition(HWND hwnd, LPARAM lParam)
{
    // Coordinates can be negative while the mouse is captured.
    int x = static_cast<SHORT>(LOWORD(lParam));
    int y = static_cast<SHORT>(HIWORD(lParam));

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    const int cw = std::max(1, static_cast<int>(clientRect.right));
    const int ch = std::max(1, static_cast<int>(clientRect.bottom));

    x = std::max(0, std::min(x, cw - 1));
    y = std::max(0, std::min(y, ch - 1));

    // Scale by the actual client size so clicks line up with what is drawn.
    int brainX = std::max(0, std::min(x * SCREEN_WIDTH  / cw, SCREEN_WIDTH  - 1));
    int brainY = std::max(0, std::min(y * SCREEN_HEIGHT / ch, SCREEN_HEIGHT - 1));

    vex::g_mouseX.store(brainX);
    vex::g_mouseY.store(brainY);
}


// -----------------------------------------------------------------------------
// Windows message handler
// -----------------------------------------------------------------------------

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
        {
            return 0;
        }

        // Like a real touchscreen: the position only follows the finger
        // while it is down. Just hovering must not change xPosition()/
        // yPosition(), or a click can "move" before the program reads it.
        case WM_MOUSEMOVE:
        {
            if (vex::g_mousePressed.load())
                updateMousePosition(hwnd, lParam);

            return 0;
        }

        // Left button down -> Brain.Screen.pressing() == true
        case WM_LBUTTONDOWN:
        {
            SetCapture(hwnd);
            updateMousePosition(hwnd, lParam);

            vex::g_pressTimeNs.store(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
            vex::g_mousePressed.store(true);
            Brain.Screen.notifyTouch(true);

            return 0;
        }

        case WM_LBUTTONUP:
        {
            const bool wasPressed = vex::g_mousePressed.exchange(false);

            ReleaseCapture();

            if (wasPressed)
                Brain.Screen.notifyTouch(false);

            return 0;
        }

        case WM_CAPTURECHANGED:
        {
            vex::g_mousePressed.store(false);
            return 0;
        }

        // Avoid flicker: WM_PAINT overwrites the whole client area.
        case WM_ERASEBKGND:
        {
            return 1;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT cr;
            GetClientRect(hwnd, &cr);

            Brain.Screen.paintToWindow(hdc, cr.right, cr.bottom);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
        {
            int result = MessageBoxA(hwnd,
                                     "Close the VEX Brain simulator?",
                                     "VEX Brain Simulator",
                                     MB_YESNO | MB_ICONQUESTION);

            if (result == IDYES)
                DestroyWindow(hwnd);

            return 0;
        }

        case WM_DESTROY:
        {
            vex::g_mousePressed.store(false);
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}


// -----------------------------------------------------------------------------
// Simulator entry point
// -----------------------------------------------------------------------------

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    // Prevent Windows DPI scaling from changing our coordinates.
    SetProcessDPIAware();

    // Windows sleeps in ~15.6 ms steps by default. Ask for 1 ms so
    // wait(), render() and touch polling are responsive.
    if (HMODULE winmm = LoadLibraryA("winmm.dll"))
    {
        using TimeBeginPeriodFn = UINT (WINAPI*)(UINT);

        auto timeBeginPeriod = reinterpret_cast<TimeBeginPeriodFn>(
            reinterpret_cast<void*>(GetProcAddress(winmm, "timeBeginPeriod")));

        if (timeBeginPeriod)
            timeBeginPeriod(1);
    }


    // 1. Create the window (hidden until the screen buffers exist).
    HWND hwnd = createSimulatorWindow(hInstance, SW_HIDE);

    if (!hwnd)
        return 1;

    vex::g_simulatorWindow = hwnd;


    // 2. Create the Brain's off-screen framebuffers.
    Brain.Screen.initialize(hwnd);


    // 3. Show the window.
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);


    // 4. Run the user's REAL VEX main() on its own thread so the
    //    Windows message loop keeps running.
    std::thread userProgram([]()
    {
        try
        {
            main();
        }
        catch (...)
        {
            MessageBoxA(vex::g_simulatorWindow,
                        "The VEX program threw an exception.",
                        "VEX Brain Simulator", MB_ICONERROR);
        }
    });

    userProgram.detach();


    // 5. Windows GUI message loop.
    MSG msg{};

    while (true)
    {
        BOOL result = GetMessageA(&msg, nullptr, 0, 0);

        if (result == 0 || result == -1)   // WM_QUIT or error
            break;

        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }


    // The user's program usually loops forever, and static destructors
    // (Brain, mutexes...) would run while it is still using them.
    // End the process immediately instead of returning.
    ExitProcess(0);

    return 0;
}