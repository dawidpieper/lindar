#define INIT_CLASS_IID
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/gui/iplugview.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdlib>
using namespace Steinberg;
using namespace Steinberg::Vst;
static const FUID class_id(0x527BAAAF, 0x7B934EF9, 0xB5550001, 0xA0202026);
static bool same(const TUID id, const FUID &wanted) { return !std::memcmp(id, wanted.toTUID(), 16); }
static void text(String128 out, const char *value) {
    std::fill_n(out, 128, 0);
    for (unsigned i = 0; value[i] && i < 127; ++i)
        out[i] = value[i];
}
class View final : public IPlugView {
    std::atomic<uint32> refs{1};
    IPlugFrame *frame = nullptr;
    ViewRect size{0, 0, 320, 200};

  public:
    tresult PLUGIN_API queryInterface(const TUID id, void **out) override {
        *out = nullptr;
        if (!same(id, IPlugView::iid) && !same(id, FUnknown::iid)) return kNoInterface;
        *out = this;
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override {
        auto n = --refs;
        if (!n) delete this;
        return n;
    }
    ~View() {
        if (frame) frame->release();
    }
    tresult PLUGIN_API isPlatformTypeSupported(FIDString) override { return kResultOk; }
    tresult PLUGIN_API attached(void *parent, FIDString type) override { return parent ? kResultOk : kResultFalse; }
    tresult PLUGIN_API removed() override { return kResultOk; }
    tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API getSize(ViewRect *out) override {
        *out = size;
        return kResultOk;
    }
    tresult PLUGIN_API onSize(ViewRect *in) override {
        size = *in;
        return kResultOk;
    }
    tresult PLUGIN_API onFocus(TBool) override { return kResultOk; }
    tresult PLUGIN_API setFrame(IPlugFrame *f) override {
        if (f) f->addRef();
        if (frame) frame->release();
        frame = f;
        return kResultOk;
    }
    tresult PLUGIN_API canResize() override { return kResultTrue; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect *r) override { return r->getWidth() >= 100 ? kResultOk : kResultFalse; }
};
class Plugin final : public IComponent, public IAudioProcessor, public IEditController, public IUnitInfo {
    std::atomic<uint32> refs{1};
    FUnknown *host = nullptr;
    IComponentHandler *handler = nullptr;
    double value = 0.5, audio_value = 0.5, program = 0;
    int32 channels = 2, block = 0;
    bool active = false, running = false;

  public:
    tresult PLUGIN_API queryInterface(const TUID id, void **out) override {
        *out = nullptr;
        if (same(id, IComponent::iid) || same(id, FUnknown::iid))
            *out = static_cast<IComponent *>(this);
        else if (same(id, IAudioProcessor::iid))
            *out = static_cast<IAudioProcessor *>(this);
        else if (same(id, IEditController::iid))
            *out = static_cast<IEditController *>(this);
        else if (same(id, IUnitInfo::iid))
            *out = static_cast<IUnitInfo *>(this);
        else
            return kNoInterface;
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override {
        auto n = --refs;
        if (!n) delete this;
        return n;
    }
    tresult PLUGIN_API initialize(FUnknown *context) override {
        if (host || !context) return kResultFalse;
        IHostApplication *app = nullptr;
        if (context->queryInterface(IHostApplication::iid.toTUID(), reinterpret_cast<void **>(&app))) return kResultFalse;
        TUID id;
        IMessage::iid.toTUID(id);
        IMessage *message = nullptr;
        tresult r = app->createInstance(id, id, reinterpret_cast<void **>(&message));
        if (message) {
            message->setMessageID("test");
            message->getAttributes()->setInt("value", 17);
            int64 n = 0;
            message->getAttributes()->getInt("value", n);
            if (n != 17) r = kResultFalse;
            message->release();
        }
        app->release();
        if (r) return r;
        host = context;
        host->addRef();
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override {
        if (host) host->release();
        host = nullptr;
        return kResultOk;
    }
    tresult PLUGIN_API getControllerClassId(TUID) override { return kResultFalse; }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection) override { return type == kAudio ? 1 : 0; }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo &info) override {
        if (type != kAudio || index) return kInvalidArgument;
        info = {};
        info.mediaType = type;
        info.direction = dir;
        info.channelCount = channels;
        info.busType = kMain;
        info.flags = BusInfo::kDefaultActive;
        text(info.name, "Audio");
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo &, RoutingInfo &) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive(TBool enabled) override {
        active = enabled;
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream *s) override {
        double v;
        int32 n = 0;
        if (s->read(&v, sizeof v, &n) || n != sizeof v || v < 0 || v > 1) return kResultFalse;
        value = audio_value = v;
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream *s) override {
        int32 n;
        return s->write(&value, sizeof value, &n);
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *in, int32 ni, SpeakerArrangement *out, int32 no) override {
        if (ni != 1 || no != 1 || in[0] != out[0] || (in[0] != 3 && in[0] != (uint64(1) << 19))) return kResultFalse;
        channels = in[0] == 3 ? 2 : 1;
        return kResultOk;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection, int32, SpeakerArrangement &out) override {
        out = channels == 2 ? 3 : uint64(1) << 19;
        return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultOk : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API setupProcessing(ProcessSetup &s) override {
        block = s.maxSamplesPerBlock;
        return active ? kResultFalse : kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool enabled) override {
        running = enabled;
        return kResultOk;
    }
    tresult PLUGIN_API process(ProcessData &d) override {
        if (!running || !active || d.numSamples > block || d.numInputs != 1 || d.numOutputs != 1 || !d.processContext) return kResultFalse;
        for (int32 i = 0; d.inputParameterChanges && i < d.inputParameterChanges->getParameterCount(); ++i) {
            auto q = d.inputParameterChanges->getParameterData(i);
            int32 offset;
            double v;
            if (q->getPoint(q->getPointCount() - 1, offset, v) == kResultOk) audio_value = q->getParameterId() == 10 ? v : v < 0.5 ? 0.25 : 0.75;
        }
        for (int32 c = 0; c < channels; ++c)
            for (int32 f = 0; f < d.numSamples; ++f)
                d.outputs[0].channelBuffers32[c][f] = d.inputs[0].channelBuffers32[c][f] * static_cast<float>(audio_value);
        return kResultOk;
    }
    uint32 PLUGIN_API getTailSamples() override { return 32; }
    tresult PLUGIN_API setComponentState(IBStream *s) override { return setState(s); }
    int32 PLUGIN_API getParameterCount() override { return 2; }
    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo &p) override {
        if (index < 0 || index > 1) return kInvalidArgument;
        p = {};
        p.id = index ? 20 : 10;
        p.unitId = 0;
        p.stepCount = index ? 1 : 0;
        p.defaultNormalizedValue = index ? 0 : 0.5;
        p.flags = index ? ParameterInfo::kIsProgramChange : ParameterInfo::kCanAutomate;
        text(p.title, index ? "Program" : "Gain");
        text(p.units, "%");
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue v, String128 out) override {
        char b[32];
        std::snprintf(b, sizeof b, "%.1f", v * 100);
        text(out, b);
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID, TChar *in, ParamValue &v) override {
        char b[128]{};
        for (unsigned i = 0; i < 127 && in[i]; ++i)
            b[i] = static_cast<char>(in[i]);
        v = std::atof(b) / 100;
        return kResultOk;
    }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue v) override { return v * 100; }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue v) override { return v / 100; }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override { return id == 10 ? value : program; }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) override {
        if (id == 10)
            value = v;
        else if (id == 20) {
            program = v;
            value = v < 0.5 ? 0.25 : 0.75;
        } else
            return kInvalidArgument;
        return kResultOk;
    }
    tresult PLUGIN_API setComponentHandler(IComponentHandler *h) override {
        if (h) h->addRef();
        if (handler) handler->release();
        handler = h;
        return kResultOk;
    }
    IPlugView *PLUGIN_API createView(FIDString) override { return new View; }
    int32 PLUGIN_API getUnitCount() override { return 1; }
    tresult PLUGIN_API getUnitInfo(int32 index, UnitInfo &info) override {
        if (index) return kInvalidArgument;
        info = {};
        info.id = 0;
        info.parentUnitId = -1;
        info.programListId = 7;
        text(info.name, "Root");
        return kResultOk;
    }
    int32 PLUGIN_API getProgramListCount() override { return 1; }
    tresult PLUGIN_API getProgramListInfo(int32 index, ProgramListInfo &info) override {
        if (index) return kInvalidArgument;
        info = {};
        info.id = 7;
        info.programCount = 2;
        text(info.name, "Factory");
        return kResultOk;
    }
    tresult PLUGIN_API getProgramName(ProgramListID id, int32 index, String128 name) override {
        if (id != 7 || index < 0 || index > 1) return kInvalidArgument;
        text(name, index ? "Loud" : "Quiet");
        return kResultOk;
    }
    tresult PLUGIN_API getProgramInfo(ProgramListID, int32, Steinberg::Vst::CString, String128) override { return kResultFalse; }
    tresult PLUGIN_API hasProgramPitchNames(ProgramListID, int32) override { return kResultFalse; }
    tresult PLUGIN_API getProgramPitchName(ProgramListID, int32, int16, String128) override { return kResultFalse; }
    UnitID PLUGIN_API getSelectedUnit() override { return 0; }
    tresult PLUGIN_API selectUnit(UnitID unit) override { return unit ? kResultFalse : kResultOk; }
    tresult PLUGIN_API getUnitByBus(MediaType, BusDirection, int32, int32, UnitID &) override { return kResultFalse; }
    tresult PLUGIN_API setUnitProgramData(int32, int32, IBStream *) override { return kResultFalse; }
};
class Factory final : public IPluginFactory {
    std::atomic<uint32> refs{1};

  public:
    tresult PLUGIN_API queryInterface(const TUID id, void **out) override {
        *out = nullptr;
        if (!same(id, IPluginFactory::iid) && !same(id, FUnknown::iid)) return kNoInterface;
        *out = this;
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override {
        auto n = --refs;
        if (!n) delete this;
        return n;
    }
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo *info) override {
        *info = PFactoryInfo("Lindar test", "", "", 0);
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override { return 1; }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo *info) override {
        if (index) return kInvalidArgument;
        *info = PClassInfo(class_id.toTUID(), PClassInfo::kManyInstances, kVstAudioEffectClass, "Gain test");
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void **out) override {
        *out = nullptr;
        if (std::memcmp(cid, class_id.toTUID(), 16)) return kInvalidArgument;
        auto p = new Plugin;
        auto r = p->queryInterface(iid, out);
        p->release();
        return r;
    }
};
#ifdef _WIN32
#define TEST_EXPORT extern "C" __declspec(dllexport)
#else
#define TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif
TEST_EXPORT IPluginFactory *PLUGIN_API GetPluginFactory() { return new Factory; }
TEST_EXPORT bool InitDll() { return true; }
TEST_EXPORT bool ExitDll() { return true; }
TEST_EXPORT bool ModuleEntry(void *) { return true; }
TEST_EXPORT bool ModuleExit() { return true; }
#ifdef __APPLE__
TEST_EXPORT bool bundleEntry(void *) { return true; }
TEST_EXPORT bool bundleExit() { return true; }
#endif
