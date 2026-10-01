#pragma once
#include "bridge.h"
#include "module.hpp"
#include "objects.hpp"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/gui/iplugview.h"
#include <unordered_map>
#include <thread>
namespace lndvst {
struct Parameter {
    ParameterInfo info{};
    std::atomic<double> value{0};
    std::atomic<bool> input{false};
    std::atomic<bool> output{false};
};
struct Host final : IHostApplication, IComponentHandler, IPlugFrame {
    std::shared_ptr<Module> module;
    IComponent *component = nullptr;
    IAudioProcessor *processor = nullptr;
    IEditController *controller = nullptr;
    IUnitInfo *units = nullptr;
    IConnectionPoint *component_connection = nullptr, *controller_connection = nullptr;
    IPlugView *view = nullptr;
    bool component_initialized = false, controller_initialized = false, active = false, processing = false, attached = false;
    bool bypass = false, dirty = false;
    std::atomic<bool> pending_parameters{false};
    std::atomic<int32> restart{0};
    std::atomic<uint32> refs{1};
    LND_VST3_OPTIONS options{};
    LND_VST3_CLASS plugin{};
    std::thread::id ui_thread = std::this_thread::get_id();
    std::unique_ptr<Parameter[]> parameters;
    int32 parameter_count = 0;
    std::unordered_map<ParamID, Parameter *> ids;
    Changes input_changes, output_changes;
    Events events;
    ProcessContext context{};
    std::vector<float> input, output;
    std::vector<float *> input_planes, output_planes;
    std::vector<AudioBusBuffers> input_buses, output_buses;
    uint32 tail = 0, latency = 0;
    LND_VST3_RESIZE_PROC resize = nullptr;
    void *resize_user = nullptr;
    tresult PLUGIN_API queryInterface(const TUID id, void **out) override;
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }
    tresult PLUGIN_API getName(String128 name) override;
    tresult PLUGIN_API createInstance(TUID cid, TUID id, void **out) override;
    tresult PLUGIN_API beginEdit(ParamID id) override { return ids.count(id) ? kResultOk : kInvalidArgument; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override;
    tresult PLUGIN_API endEdit(ParamID id) override { return beginEdit(id); }
    tresult PLUGIN_API restartComponent(int32 flags) override {
        restart.fetch_or(flags);
        return kResultOk;
    }
    tresult PLUGIN_API resizeView(IPlugView *view, ViewRect *size) override;
    int32_t initialize(const LND_VST3_OPTIONS &o);
    int32_t configure();
    void refresh();
    int32_t reset();
    int32_t process(const LND_PCM *pcm, size_t start, uint32_t frames);
    int32_t action(int32_t action, lnd_vst3_args &args);
    int32_t save(void *data, size_t capacity, size_t *bytes);
    int32_t load(const void *data, size_t bytes);
    int32_t set(ParamID id, double value);
    void editor_close();
    ~Host();
};
}
