#pragma once
#include "pluginterfaces/base/ipluginbase.h"
#include "lindar_vst3.h"
#include <memory>
#include <string>
#include <vector>
namespace lndvst {
struct Module {
    void *handle = nullptr;
    Steinberg::IPluginFactory *factory = nullptr;
    std::vector<LND_VST3_CLASS> classes;
    bool entered = false;
    explicit Module(const std::string &path);
    ~Module();
    void close();
};
std::shared_ptr<Module> module(const char *path);
}
