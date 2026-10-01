#include "module.hpp"
#include "bridge.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include <filesystem>
#include <map>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#else
#include <dlfcn.h>
#endif
namespace lndvst {
static void *symbol(void *handle, const char *name) {
#ifdef _WIN32
    return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle), name));
#elif defined(__APPLE__)
    CFStringRef key = CFStringCreateWithCString(nullptr, name, kCFStringEncodingUTF8);
    void *result = reinterpret_cast<void *>(CFBundleGetFunctionPointerForName(static_cast<CFBundleRef>(handle), key));
    CFRelease(key);
    return result;
#else
    return dlsym(handle, name);
#endif
}
static std::filesystem::path binary(std::filesystem::path path) {
#ifndef __APPLE__
    if (std::filesystem::is_directory(path)) {
#if defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
        const char *arch = "arm64-win";
#elif defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
        const char *arch = "x86_64-win";
#elif defined(_WIN32)
        const char *arch = "x86-win";
#elif defined(__aarch64__)
        const char *arch = "aarch64-linux";
#else
        const char *arch = "x86_64-linux";
#endif
#ifdef _WIN32
        const char *extension = ".vst3";
#else
        const char *extension = ".so";
#endif
        path = path / "Contents" / arch / (path.stem().u8string() + extension);
    }
#endif
    return std::filesystem::canonical(path);
}
Module::Module(const std::string &name) {
    try {
        auto path = binary(std::filesystem::u8path(name));
#ifdef _WIN32
        handle = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#elif defined(__APPLE__)
        auto text = path.u8string();
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8 *>(text.data()), text.size(), true);
        handle = url ? CFBundleCreate(nullptr, url) : nullptr;
        if (url) CFRelease(url);
        if (handle && !CFBundleLoadExecutable(static_cast<CFBundleRef>(handle))) {
            CFRelease(handle);
            handle = nullptr;
        }
#else
        handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        if (!handle) return;
#ifdef _WIN32
        auto entry = reinterpret_cast<bool (*)()>(symbol(handle, "InitDll"));
        entered = !entry || entry();
#elif defined(__APPLE__)
        auto entry = reinterpret_cast<bool (*)(void *)>(symbol(handle, "bundleEntry"));
        entered = !entry || entry(handle);
#else
        auto entry = reinterpret_cast<bool (*)(void *)>(symbol(handle, "ModuleEntry"));
        entered = !entry || entry(handle);
#endif
        if (!entered) return;
        auto get = reinterpret_cast<Steinberg::IPluginFactory *(PLUGIN_API *)()>(symbol(handle, "GetPluginFactory"));
        factory = get ? get() : nullptr;
        if (!factory) return;
        Steinberg::PFactoryInfo f{};
        factory->getFactoryInfo(&f);
        int count = factory->countClasses();
        if (count < 0 || count > 65536) return;
        for (int i = 0; i < count; ++i) {
            Steinberg::PClassInfo c{};
            if (factory->getClassInfo(i, &c) != Steinberg::kResultOk || std::strncmp(c.category, kVstAudioEffectClass, sizeof c.category)) continue;
            LND_VST3_CLASS info{};
            std::memcpy(info.id, c.cid, 16);
            std::snprintf(info.name, sizeof info.name, "%.*s", static_cast<int>(sizeof c.name), c.name);
            std::snprintf(info.vendor, sizeof info.vendor, "%.*s", static_cast<int>(sizeof f.vendor), f.vendor);
            classes.push_back(info);
        }
    } catch (...) {
        close();
        throw;
    }
}
Module::~Module() { close(); }
void Module::close() {
    if (factory) factory->release();
    if (!handle) return;
    if (entered) {
#ifdef _WIN32
        auto exit = reinterpret_cast<bool (*)()>(symbol(handle, "ExitDll"));
#elif defined(__APPLE__)
        auto exit = reinterpret_cast<bool (*)()>(symbol(handle, "bundleExit"));
#else
        auto exit = reinterpret_cast<bool (*)()>(symbol(handle, "ModuleExit"));
#endif
        if (exit) exit();
    }
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#elif defined(__APPLE__)
    CFBundleUnloadExecutable(static_cast<CFBundleRef>(handle));
    CFRelease(handle);
#else
    dlclose(handle);
#endif
}
std::shared_ptr<Module> module(const char *path) {
    if (!path || !*path) return {};
    static std::mutex mutex;
    static std::map<std::string, std::weak_ptr<Module>> loaded;
    std::lock_guard<std::mutex> lock(mutex);
    auto key = binary(std::filesystem::u8path(path)).u8string();
    auto existing = loaded[key].lock();
    if (existing) return existing;
    auto result = std::make_shared<Module>(key);
    if (!result->factory || result->classes.empty()) return {};
    loaded[key] = result;
    return result;
}
}
extern "C" int32_t lnd_vst3_scan(const char *path, uint32_t index, LND_VST3_CLASS *info, uint32_t *count) {
    try {
        auto m = lndvst::module(path);
        if (!m) return LND_ERR_FORMAT;
        if (count) *count = static_cast<uint32_t>(m->classes.size());
        if (info) {
            if (index >= m->classes.size()) return LND_ERR_INVALID_ARG;
            *info = m->classes[index];
        }
        return LND_OK;
    } catch (const std::bad_alloc &) {
        return LND_ERR_OUT_OF_MEMORY;
    } catch (...) {
        return LND_ERR_IO;
    }
}
