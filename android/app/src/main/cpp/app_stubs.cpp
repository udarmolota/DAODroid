// What the runtime needs from the parts the app doesn't build yet, as no-ops: the ImGui
// overlays over the game (the on-screen keyboard, notices, photo mode, cheats) and the
// launcher's update channel. The overlays come once the presenter's UI drawers are wired
// to the SDL window (docs/PHASE5_runbook.md, open items).
#include "stdafx.h"

#include "settings.h"
#include "ui/cheats.h"
#include "ui/keyboard.h"
#include "ui/menu_movies.h"
#include "ui/notice.h"
#include "ui/photo_mode.h"

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

    // No keyboard overlay yet: the default text is the answer, as the console's keyboard
    // stub gave.
    std::optional<std::u16string> RunKeyboard(const KeyboardRequest& request)
    {
        LOGI("Keyboard: not available yet, answering with the default text");
        return request.text;
    }
}

namespace dao
{
    // Settings' default update channel comes from the launcher's release list on Windows;
    // there is no updater here.
    UpdateChannel DefaultUpdateChannel() { return UpdateChannel{}; }
}
