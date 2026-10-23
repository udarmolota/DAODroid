// The app's side of the Xenia host (xenia_host_vulkan.cpp): how the game gets a window.
// The headless front end sets nothing.
#pragma once

#include <functional>
#include <memory>

namespace xe::ui
{
    class Window;
    class WindowedAppContext;
}

namespace dao::gpu::android
{
    struct WindowHost
    {
        // The UI thread's app context (the SDL thread in the app): the presenter is
        // created on it, and the window opened there.
        xe::ui::WindowedAppContext* appContext = nullptr;
        // Makes the window object over the app's window; called on the UI thread.
        std::function<std::unique_ptr<xe::ui::Window>()> createWindow;
        // A Vulkan loader library the app opened itself (the private loader on
        // Turnip), or null for the phone's own.
        void* loaderLibrary = nullptr;
        // The aspect ratio the frame is shown at (0: the game's 16:9, with bars on a
        // wider screen); with a matching display mode (gpu/video.h SetDisplayMode) the
        // game fills the screen.
        uint32_t aspectWidth = 0, aspectHeight = 0;
    };

    // Before InitializeXeniaGraphics.
    void SetWindowHost(const WindowHost& host);
}
