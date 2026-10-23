#include "stdafx.h"

#include "sdl_window.h"

#include <android/native_window.h>

#include "input/input.h"
#include "kernel/game_exit.h"
#include "xenia/ui/surface_android.h"

namespace dao::app
{
    namespace
    {
        enum UserEventCode : Sint32
        {
            kExecutePendingFunctions = 1,
            kPaint = 2,
        };
    }

    // ------------------------------------------------------------------ app context

    SdlWindowedAppContext::SdlWindowedAppContext()
    {
        userEvent_ = SDL_RegisterEvents(1);
    }

    void SdlWindowedAppContext::NotifyUILoopOfPendingFunctions()
    {
        SDL_Event event{};
        event.type = userEvent_;
        event.user.code = kExecutePendingFunctions;
        SDL_PushEvent(&event);
    }

    void SdlWindowedAppContext::PlatformQuitFromUIThread()
    {
        SDL_Event event{};
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
    }

    void SdlWindowedAppContext::RequestPaint()
    {
        // One event per request burst.
        if (paintRequested_.exchange(true, std::memory_order_acq_rel))
            return;
        SDL_Event event{};
        event.type = userEvent_;
        event.user.code = kPaint;
        SDL_PushEvent(&event);
    }

    void SdlWindowedAppContext::RunUILoop(SDL_Window* window, SdlWindow* xeWindow)
    {
        bool surfaceLost = false;
        for (;;)
        {
            SDL_Event event;
            if (!SDL_WaitEvent(&event))
            {
                LOGE("SDL_WaitEvent failed: {}", SDL_GetError());
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (event.type == userEvent_)
            {
                if (event.user.code == kExecutePendingFunctions)
                {
                    ExecutePendingFunctionsFromUIThread();
                }
                else if (event.user.code == kPaint)
                {
                    paintRequested_.store(false, std::memory_order_release);
                    if (xeWindow && !surfaceLost)
                        xeWindow->Paint(false);
                }
                if (HasQuitFromUIThread())
                    return;
                continue;
            }
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_TERMINATING:
                LOGI("Window closed, exiting");
                QuitFromUIThread();
                return;
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
                // The activity's surface is about to go: disconnect the presenter first.
                LOGI("Entering the background: surface disconnected");
                surfaceLost = true;
                if (xeWindow)
                    xeWindow->OnSdlSurfaceLost();
                break;
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                LOGI("Back in the foreground: surface reconnected");
                surfaceLost = false;
                if (xeWindow)
                {
                    xeWindow->OnSdlSizeChanged();
                    xeWindow->OnSdlSurfaceAvailable();
                }
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_RESIZED:
                if (xeWindow && !surfaceLost)
                    xeWindow->OnSdlSizeChanged();
                break;
            case SDL_EVENT_WINDOW_EXPOSED:
                if (xeWindow && !surfaceLost)
                    xeWindow->Paint(true);
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (xeWindow)
                    xeWindow->OnSdlFocus(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
            case SDL_EVENT_GAMEPAD_REMOVED:
                dao::input::OnSdlEvent(event);
                break;
            default:
                break;
            }
            if (HasQuitFromUIThread())
                return;
        }
    }

    // ------------------------------------------------------------------ window

    SdlWindow::SdlWindow(SdlWindowedAppContext& appContext, SDL_Window* window)
        : Window(appContext, "Dragon Age: Origins", 1280, 720), window_(window)
    {
    }

    SdlWindow::~SdlWindow()
    {
        EnterDestructor();
    }

    uint32_t SdlWindow::GetLatestDpiImpl() const
    {
        const float scale = SDL_GetWindowDisplayScale(window_);
        return scale > 0 ? uint32_t(std::lround(scale * 160.0f)) : 160;
    }

    void SdlWindow::OnSdlSizeChanged()
    {
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        if (width <= 0 || height <= 0)
            return;
        OnDesiredLogicalSizeUpdate(SizeToLogical(uint32_t(width)), SizeToLogical(uint32_t(height)));
        WindowDestructionReceiver destructionReceiver(this);
        OnActualSizeUpdate(uint32_t(width), uint32_t(height), WindowResizeAction::kManual, destructionReceiver);
    }

    void SdlWindow::OnSdlFocus(bool focused)
    {
        WindowDestructionReceiver destructionReceiver(this);
        OnFocusUpdate(focused, destructionReceiver);
    }

    bool SdlWindow::OpenImpl()
    {
        OnSdlSizeChanged();
        return true;
    }

    void SdlWindow::RequestCloseImpl()
    {
        WindowDestructionReceiver destructionReceiver(this);
        OnBeforeClose(destructionReceiver);
        if (destructionReceiver.IsWindowDestroyed())
            return;
        OnAfterClose();
    }

    std::unique_ptr<xe::ui::Surface> SdlWindow::CreateSurfaceImpl(xe::ui::Surface::TypeFlags allowedTypes)
    {
        if (!(allowedTypes & xe::ui::Surface::kTypeFlag_AndroidNativeWindow))
            return nullptr;
        auto* nativeWindow = static_cast<ANativeWindow*>(
            SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr));
        if (!nativeWindow)
        {
            LOGW("Window: no native window yet");
            return nullptr;
        }
        return std::make_unique<xe::ui::AndroidNativeWindowSurface>(nativeWindow);
    }

    void SdlWindow::RequestPaintImpl()
    {
        static_cast<SdlWindowedAppContext&>(app_context()).RequestPaint();
    }
}
