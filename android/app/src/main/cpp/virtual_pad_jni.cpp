// The on-screen gamepad's state from the app's VirtualPadView (com.daodroid.VirtualPad),
// handed to the input unit, which merges it with a physical pad.
#include "stdafx.h"

#include <jni.h>

#include "input/input.h"
#include "xenia/gpu/d3d12/dao_frame_trace.h"

extern "C" JNIEXPORT void JNICALL Java_com_daodroid_VirtualPad_nativeSetState(JNIEnv*, jclass, jint buttons,
    jfloat leftX, jfloat leftY, jfloat rightX, jfloat rightY, jfloat leftTrigger, jfloat rightTrigger)
{
    dao::input::SetVirtualPad(uint16_t(buttons), leftX, leftY, rightX, rightY, leftTrigger, rightTrigger);
}

// Frames the game presented so far (Xenia swaps), for the performance overlay.
extern "C" JNIEXPORT jlong JNICALL Java_com_daodroid_GameActivity_nativeGetPresentedFrames(JNIEnv*, jclass)
{
    return jlong(xe::gpu::dao_trace::SwapCount());
}
