// The game's keyboard (XamShowKeyboardUI, e.g. the character's name) as an Android
// dialog with a text field: the game's thread asks the activity (GameActivity.showKeyboard)
// and waits until the player is done (nativeKeyboardDone). On Windows DAORecomp draws its
// own keyboard over the game (ui/keyboard.cpp); a phone has a better one already.
#include "stdafx.h"

#include <jni.h>

#include <SDL3/SDL_system.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "ui/keyboard.h"

namespace
{
    std::mutex s_mutex;
    std::condition_variable s_done;
    bool s_open = false;
    bool s_answered = false;
    std::optional<std::u16string> s_result;
    // The game's controller input is held back while the dialog is up and a moment after,
    // so the touch that closed it doesn't reach the game.
    std::chrono::steady_clock::time_point s_closedAt;

    jstring ToJava(JNIEnv* env, const std::u16string& text)
    {
        return env->NewString(reinterpret_cast<const jchar*>(text.data()), jsize(text.size()));
    }
}

namespace dao::ui
{
    std::optional<std::u16string> RunKeyboard(const KeyboardRequest& request)
    {
        auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (!env || !activity)
        {
            LOGW("Keyboard: no activity, answering with the default text");
            return request.text;
        }
        {
            std::lock_guard lock(s_mutex);
            s_open = true;
            s_answered = false;
            s_result.reset();
        }
        jclass cls = env->GetObjectClass(activity);
        jmethodID show = env->GetMethodID(cls, "showKeyboard", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V");
        if (!show)
        {
            env->ExceptionClear();
            LOGW("Keyboard: GameActivity.showKeyboard missing, answering with the default text");
            env->DeleteLocalRef(cls);
            env->DeleteLocalRef(activity);
            std::lock_guard lock(s_mutex);
            s_open = false;
            return request.text;
        }
        jstring title = ToJava(env, request.title);
        jstring description = ToJava(env, request.description);
        jstring text = ToJava(env, request.text);
        LOGI("Keyboard: asking the player (max {} characters)", request.maxLength);
        env->CallVoidMethod(activity, show, title, description, text, jint(request.maxLength));
        env->DeleteLocalRef(title);
        env->DeleteLocalRef(description);
        env->DeleteLocalRef(text);
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(activity);

        std::unique_lock lock(s_mutex);
        s_done.wait(lock, [] { return s_answered; });
        s_open = false;
        s_closedAt = std::chrono::steady_clock::now();
        LOGI("Keyboard: {}", s_result ? "done" : "cancelled");
        return s_result;
    }

    bool KeyboardHoldsInput()
    {
        std::lock_guard lock(s_mutex);
        return s_open || std::chrono::steady_clock::now() - s_closedAt < std::chrono::milliseconds(300);
    }
}

// From the dialog: the text entered, or null if the player cancelled.
extern "C" JNIEXPORT void JNICALL Java_com_daodroid_GameActivity_nativeKeyboardDone(JNIEnv* env, jclass, jstring text)
{
    std::optional<std::u16string> result;
    if (text)
    {
        const jchar* chars = env->GetStringChars(text, nullptr);
        result = std::u16string(reinterpret_cast<const char16_t*>(chars), size_t(env->GetStringLength(text)));
        env->ReleaseStringChars(text, chars);
    }
    {
        std::lock_guard lock(s_mutex);
        s_result = std::move(result);
        s_answered = true;
    }
    s_done.notify_all();
}
