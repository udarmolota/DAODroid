// Xenia's window and UI-thread app context over SDL3's Android window: the presenter
// paints into the activity's surface (an ANativeWindow SDL owns) and the SDL thread is
// the UI thread, running the event loop in RunUILoop. After Xenia's own AndroidWindow /
// AndroidWindowedAppContext (which assume Xenia's activity and JNI).
#pragma once

#include <atomic>
#include <memory>

#include <SDL3/SDL.h>

#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"

namespace dao::app
{
    class SdlWindow;

    class SdlWindowedAppContext final : public xe::ui::WindowedAppContext
    {
    public:
        // On the SDL thread.
        SdlWindowedAppContext();

        // The SDL event loop: UI-thread functions, paint requests, the window's surface
        // and size, gamepads, quit. Returns when the app quits.
        void RunUILoop(SDL_Window* window, SdlWindow* xeWindow);

        void NotifyUILoopOfPendingFunctions() override;
        void PlatformQuitFromUIThread() override;

        // From any thread: a paint on the UI thread as soon as possible.
        void RequestPaint();

    private:
        Uint32 userEvent_ = 0;
        std::atomic<bool> paintRequested_{ false };
    };

    class SdlWindow final : public xe::ui::Window
    {
    public:
        SdlWindow(SdlWindowedAppContext& appContext, SDL_Window* window);
        ~SdlWindow() override;

        uint32_t GetMediumDpi() const override { return 160; }

        // From the UI loop.
        void OnSdlSizeChanged();
        void OnSdlSurfaceLost() { OnSurfaceChanged(false); }
        void OnSdlSurfaceAvailable() { OnSurfaceChanged(true); }
        void OnSdlFocus(bool focused);
        void Paint(bool force) { OnPaint(force); }

    protected:
        uint32_t GetLatestDpiImpl() const override;
        bool OpenImpl() override;
        void RequestCloseImpl() override;
        std::unique_ptr<xe::ui::Surface> CreateSurfaceImpl(xe::ui::Surface::TypeFlags allowedTypes) override;
        void RequestPaintImpl() override;

    private:
        SDL_Window* window_;
    };
}
