// Headless build: the slice of SDL3's API that input/input.cpp uses, with no SDL behind it.
// SDL_Init fails, so input.cpp runs with no gamepad and keeps its scripted presses
// (DAO_INPUT_SCRIPT), which is what the phone needs from adb. Phase 4 links the real SDL3.
#pragma once

#include <cstdint>
#include <cstdlib>

using SDL_JoystickID = uint32_t;
struct SDL_Gamepad;

enum SDL_GamepadButton
{
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    SDL_GAMEPAD_BUTTON_TOUCHPAD,
};

enum SDL_GamepadAxis
{
    SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
    SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
};

constexpr uint32_t SDL_INIT_GAMEPAD = 0x2000;
constexpr uint32_t SDL_INIT_HAPTIC = 0x1000;
constexpr uint32_t SDL_EVENT_GAMEPAD_ADDED = 0x650;
constexpr uint32_t SDL_EVENT_GAMEPAD_REMOVED = 0x651;
#define SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"

struct SDL_GamepadDeviceEvent { SDL_JoystickID which; };
union SDL_Event
{
    uint32_t type;
    struct { uint32_t type; SDL_GamepadDeviceEvent gdevice; } padded;
    SDL_GamepadDeviceEvent gdevice;
};

inline bool SDL_Init(uint32_t) { return false; }
inline const char* SDL_GetError() { return "no SDL in the headless build"; }
inline bool SDL_SetHint(const char*, const char*) { return true; }
inline bool SDL_PollEvent(SDL_Event*) { return false; }
inline SDL_Gamepad* SDL_OpenGamepad(SDL_JoystickID) { return nullptr; }
inline void SDL_CloseGamepad(SDL_Gamepad*) {}
inline SDL_JoystickID SDL_GetGamepadID(SDL_Gamepad*) { return 0; }
inline SDL_JoystickID* SDL_GetGamepads(int* count) { if (count) *count = 0; return nullptr; }
inline void SDL_free(void* p) { free(p); }
inline bool SDL_GetGamepadButton(SDL_Gamepad*, SDL_GamepadButton) { return false; }
inline int16_t SDL_GetGamepadAxis(SDL_Gamepad*, SDL_GamepadAxis) { return 0; }
inline const char* SDL_GetGamepadName(SDL_Gamepad*) { return nullptr; }
inline int SDL_GetGamepadType(SDL_Gamepad*) { return 0; }
inline bool SDL_SetGamepadLED(SDL_Gamepad*, uint8_t, uint8_t, uint8_t) { return false; }
inline bool SDL_RumbleGamepad(SDL_Gamepad*, uint16_t, uint16_t, uint32_t) { return false; }
