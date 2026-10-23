package com.daodroid;

/** The on-screen gamepad's state, handed to the game (virtual_pad_jni.cpp). */
final class VirtualPad {
    // Xbox controller button bits, as the game reads them (input/input.h).
    static final int DPAD_UP = 0x0001, DPAD_DOWN = 0x0002, DPAD_LEFT = 0x0004, DPAD_RIGHT = 0x0008;
    static final int START = 0x0010, BACK = 0x0020, LEFT_THUMB = 0x0040, RIGHT_THUMB = 0x0080;
    static final int LB = 0x0100, RB = 0x0200, A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000;

    private VirtualPad() {}

    /** Sticks in -1..1 (Y up), triggers in 0..1. Any thread. */
    static native void nativeSetState(int buttons, float leftX, float leftY, float rightX, float rightY,
                                      float leftTrigger, float rightTrigger);
}
