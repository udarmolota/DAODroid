#include "xenia/ui/vulkan/dao_vulkan_icd.h"

#include <cstdint>

#include "xenia/base/logging.h"
#include "xenia/base/platform.h"

#if XE_PLATFORM_LINUX
#include <dlfcn.h>
#endif

namespace xe {
namespace ui {
namespace vulkan {
namespace dao_icd {

namespace {

void* s_loader_library = nullptr;

// hardware/hardware.h and hardware/hwvulkan.h layouts (the NDK doesn't ship them).
struct hw_module_methods_t {
  int (*open)(const void* module, const char* id, void** device);
};
struct hw_module_t {
  uint32_t tag;
  uint16_t module_api_version;
  uint16_t hal_api_version;
  const char* id;
  const char* name;
  const char* author;
  hw_module_methods_t* methods;
  void* dso;
  uint32_t reserved[32 - 7];
};
struct hw_device_t {
  uint32_t tag;
  uint32_t version;
  hw_module_t* module;
  uint64_t reserved[12];
  int (*close)(hw_device_t*);
};
struct hwvulkan_device_t {
  hw_device_t common;
  PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;
  PFN_vkCreateInstance CreateInstance;
  PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
};

}  // namespace

PFN_vkGetInstanceProcAddr GetInstanceProcAddr(void* library) {
#if XE_PLATFORM_LINUX
  if (!library) {
    return nullptr;
  }
  if (auto icd_gipa = PFN_vkGetInstanceProcAddr(
          dlsym(library, "vk_icdGetInstanceProcAddr"))) {
    XELOGI("Vulkan ICD: vk_icdGetInstanceProcAddr");
    return icd_gipa;
  }
  auto* module = static_cast<hw_module_t*>(dlsym(library, "HMI"));
  if (!module) {
    XELOGE("Vulkan ICD: neither vk_icdGetInstanceProcAddr nor HMI exported");
    return nullptr;
  }
  XELOGI("Vulkan ICD: Android HAL module '{}' ({}) by {}",
         module->name ? module->name : "?", module->id ? module->id : "?",
         module->author ? module->author : "?");
  void* device = nullptr;
  if (!module->methods || !module->methods->open) {
    XELOGE("Vulkan ICD: the HAL module has no open method");
    return nullptr;
  }
  const int result = module->methods->open(module, "vk0", &device);
  if (result != 0 || !device) {
    XELOGE("Vulkan ICD: opening the HAL device failed ({})", result);
    return nullptr;
  }
  return static_cast<hwvulkan_device_t*>(device)->GetInstanceProcAddr;
#else
  return nullptr;
#endif
}

void SetLoaderLibrary(void* library) { s_loader_library = library; }

void* GetLoaderLibrary() { return s_loader_library; }

VKAPI_ATTR VkResult VKAPI_CALL NoLayers(uint32_t* property_count,
                                        VkLayerProperties* properties) {
  *property_count = 0;
  return VK_SUCCESS;
}

}  // namespace dao_icd
}  // namespace vulkan
}  // namespace ui
}  // namespace xe
