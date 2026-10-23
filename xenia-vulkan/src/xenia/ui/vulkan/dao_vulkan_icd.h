// DAO: a Vulkan driver loaded directly (DAO_VULKAN_ICD), with no system loader:
// a plain ICD through vk_icdGetInstanceProcAddr, or an Android Vulkan HAL
// module (a Turnip build for Android) through its HMI entry point. Without the
// loader there are no layers and, on Android, no VK_KHR_surface: for off-screen
// runs (DAO_VULKAN_NO_SURFACE, android/runtime).
#ifndef XENIA_UI_VULKAN_DAO_VULKAN_ICD_H_
#define XENIA_UI_VULKAN_DAO_VULKAN_ICD_H_

#include "xenia/ui/vulkan/vulkan_api.h"

namespace xe {
namespace ui {
namespace vulkan {
namespace dao_icd {

// The driver's vkGetInstanceProcAddr, from a library opened with dlopen; null if
// it is neither kind of driver.
PFN_vkGetInstanceProcAddr GetInstanceProcAddr(void* library);

// A Vulkan loader library the host opened itself (the app's private copy of the
// system loader on another driver): used instead of dlopen("libvulkan.so") and
// never closed by the instance. Set before the graphics system is created.
void SetLoaderLibrary(void* library);
void* GetLoaderLibrary();

// For a driver without the loader's layer enumeration: no layers.
VKAPI_ATTR VkResult VKAPI_CALL
NoLayers(uint32_t* property_count, VkLayerProperties* properties);

}  // namespace dao_icd
}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_DAO_VULKAN_ICD_H_
