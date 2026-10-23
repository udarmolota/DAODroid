// A Vulkan loader on another driver than the phone's own (a Turnip build): a private
// copy of the system libvulkan.so in a linker namespace of ours, behind
// libvkdriverhook.so, which hands the copy our driver when it looks for the phone's. The
// copy keeps doing what the loader does on Android (the HAL entry point, the swapchain on
// top of the driver), so the driver only has to be one the system loader could load. After
// ValDroid's driver.c (the author's own launcher).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// The private loader library (exports the Vulkan API, vkGetInstanceProcAddr included), or
// null, with the reason logged, if anything failed: the phone's own driver is then the one
// to use. driver_path: an absolute path to a Vulkan HAL driver (vulkan.<board>.so or a
// Turnip build); hook_dir: the directory holding libvkdriverhook.so (the app's native
// library directory).
void* dao_open_private_vulkan_loader(const char* driver_path, const char* hook_dir);

#ifdef __cplusplus
}
#endif
