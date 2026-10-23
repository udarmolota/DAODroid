// Headless Android build: what the runtime needs from the parts that are not built
// here, as no-ops. The game window's UI (ui/), SDL3 input (input/input.cpp) and SDL3
// audio output (apu/audio_output.cpp) are stubbed until phase 5 brings SDL3. The GPU
// is Xenia's Vulkan backend (xenia_host_vulkan.cpp), or the runtime's null command
// processor with DAO_GPU=null.
#include "stdafx.h"

#include "apu/audio_output.h"
#include "input/input.h"
#include "settings.h"
#include "ui/cheats.h"
#include "ui/keyboard.h"
#include "ui/menu_movies.h"
#include "ui/notice.h"
#include "ui/photo_mode.h"

// ------------------------------------------------------------------ UI

namespace dao::ui
{
    // The START menu's movie (Photo Mode and Cheats rows; their actions are no-ops here),
    // as photo_mode.cpp installs it on Windows.
    void InitializePhotoMode() { InstallPauseMenuMovie(); }
    void TogglePhotoMode() {}
    void ToggleCheats() {}
    bool CheatsAvailable() { return false; }
    bool KeyboardHoldsInput() { return false; }
    bool CheatsOpen() { return false; }
    uint16_t CheatsFilterButtons(uint16_t buttons) { return buttons; }
    uint16_t PhotoModeFilterButtons(uint16_t buttons) { return buttons; }
    void ShowNotice(std::string text) { LOGW("Notice: {}", text); }

    // No screen: the default text is the answer, as the console's keyboard stub gave.
    std::optional<std::u16string> RunKeyboard(const KeyboardRequest& request)
    {
        LOGI("Keyboard: headless, answering with the default text");
        return request.text;
    }
}

// (Input is the real input/input.cpp over shim/SDL3/SDL.h: no gamepad, but DAO_INPUT_SCRIPT.)

// ------------------------------------------------------------------ launcher (none)

namespace dao
{
    // Settings' default update channel comes from the launcher's release list on Windows;
    // there is no updater here.
    UpdateChannel DefaultUpdateChannel() { return UpdateChannel{}; }
}

// ------------------------------------------------------------------ audio output (none)

namespace dao::apu
{
    bool InitializeAudioOutput()
    {
        LOGI("Audio output: none (headless build); XMA is decoded and dropped");
        return false;
    }
}
