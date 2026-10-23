#include "vulkan_private_loader.h"

#include <android/log.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "android_linker_ns.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "DAODroid", __VA_ARGS__)

void* dao_open_private_vulkan_loader(const char* driver_path, const char* hook_dir)
{
    if (!driver_path || driver_path[0] != '/')
    {
        LOG("vulkan loader: '%s' is not an absolute path", driver_path ? driver_path : "");
        return NULL;
    }
    if (!linkernsbypass_load_status())
    {
        LOG("vulkan loader: linker namespaces are not available on this phone");
        return NULL;
    }

    // Search path: the hook's directory and the driver's directory.
    char search[2 * PATH_MAX + 2];
    const char* driver_slash = strrchr(driver_path, '/');
    snprintf(search, sizeof(search), "%s:%.*s", hook_dir ? hook_dir : "", (int)(driver_slash - driver_path),
        driver_path);

    struct android_namespace_t* ns =
        android_create_namespace("daodroid-vulkan", search, search, ANDROID_NAMESPACE_TYPE_SHARED, NULL, NULL);
    if (!ns)
    {
        LOG("vulkan loader: android_create_namespace failed");
        return NULL;
    }
    void* hook = linkernsbypass_namespace_dlopen("libvkdriverhook.so", RTLD_GLOBAL, ns);
    void (*hook_set)(void*, void*) = hook ? (void (*)(void*, void*))dlsym(hook, "vkdriverhook_set") : NULL;
    void* libdl = dlopen("libdl.so", RTLD_LAZY);
    void* loader_dlopen_ext = libdl ? dlsym(libdl, "__loader_android_dlopen_ext") : NULL;
    if (!hook_set || !loader_dlopen_ext)
    {
        LOG("vulkan loader: hook unavailable (%s)", hook ? "no __loader_android_dlopen_ext" : dlerror());
        return NULL;
    }

    void* driver = linkernsbypass_namespace_dlopen(driver_path, RTLD_LOCAL | RTLD_NOW, ns);
    if (!driver)
    {
        // A driver may need system libraries the app's namespace does not see (libsync, for one).
        LOG("vulkan loader: %s did not load (%s); retrying with the system libraries visible", driver_path,
            dlerror());
        linkernsbypass_link_namespace_to_default_all_libs(ns);
        driver = linkernsbypass_namespace_dlopen(driver_path, RTLD_LOCAL | RTLD_NOW, ns);
    }
    if (!driver)
    {
        LOG("vulkan loader: %s did not load: %s", driver_path, dlerror());
        return NULL;
    }
    if (!dlsym(driver, "HMI"))
    {
        LOG("vulkan loader: %s has no HMI entry point: not an Android Vulkan HAL driver", driver_path);
        return NULL;
    }
    hook_set(loader_dlopen_ext, driver);

    void* loader = linkernsbypass_namespace_dlopen_unique("/system/lib64/libvulkan.so", NULL, RTLD_GLOBAL | RTLD_NOW, ns);
    if (!loader)
    {
        // The loader too can need vendor libraries.
        LOG("vulkan loader: a private copy of libvulkan.so did not load (%s); retrying with the system libraries visible",
            dlerror());
        linkernsbypass_link_namespace_to_default_all_libs(ns);
        loader = linkernsbypass_namespace_dlopen_unique("/system/lib64/libvulkan.so", NULL, RTLD_GLOBAL | RTLD_NOW, ns);
    }
    if (!loader)
    {
        // Then the vendor's libraries, where the phone's own driver and its helpers live.
        struct android_namespace_t* vendor = android_get_exported_namespace ? android_get_exported_namespace("sphal") : NULL;
        LOG("vulkan loader: still no libvulkan.so (%s); retrying with the vendor libraries visible%s", dlerror(),
            vendor ? "" : " (no sphal namespace)");
        if (vendor && android_link_namespaces_all_libs && android_link_namespaces_all_libs(ns, vendor))
            loader = linkernsbypass_namespace_dlopen_unique("/system/lib64/libvulkan.so", NULL, RTLD_GLOBAL | RTLD_NOW, ns);
    }
    if (!loader)
    {
        LOG("vulkan loader: a private copy of libvulkan.so did not load: %s", dlerror());
        return NULL;
    }
    LOG("vulkan loader: %s, through a private libvulkan.so %p", driver_path, loader);
    return loader;
}
