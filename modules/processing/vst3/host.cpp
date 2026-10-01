#include "host.hpp"
extern "C" {
#include "src/pcm.h"
}
#include <cmath>
#include <cstdio>
#include <limits>
namespace lndvst {
tresult PLUGIN_API Host::queryInterface(const TUID id, void **out) {
    if (!out) return kInvalidArgument;
    *out = nullptr;
    if (same_iid(id, IHostApplication::iid) || same_iid(id, FUnknown::iid))
        *out = static_cast<IHostApplication *>(this);
    else if (same_iid(id, IComponentHandler::iid))
        *out = static_cast<IComponentHandler *>(this);
    else if (same_iid(id, IPlugFrame::iid))
        *out = static_cast<IPlugFrame *>(this);
    else
        return kNoInterface;
    addRef();
    return kResultOk;
}
tresult PLUGIN_API Host::getName(String128 name) {
    if (!name) return kInvalidArgument;
    std::fill_n(name, 128, 0);
    const char *text = "Lindar";
    for (unsigned i = 0; text[i]; ++i)
        name[i] = text[i];
    return kResultOk;
}
tresult PLUGIN_API Host::createInstance(TUID cid, TUID id, void **out) {
    if (!out) return kInvalidArgument;
    *out = nullptr;
    try {
        if (same_iid(cid, IMessage::iid) && same_iid(id, IMessage::iid))
            *out = static_cast<IMessage *>(new Message);
        else if (same_iid(cid, IAttributeList::iid) && same_iid(id, IAttributeList::iid))
            *out = static_cast<IAttributeList *>(new Attributes);
        else
            return kNoInterface;
        return kResultOk;
    } catch (...) {
        return kOutOfMemory;
    }
}
tresult PLUGIN_API Host::performEdit(ParamID id, ParamValue value) {
    auto it = ids.find(id);
    if (it == ids.end() || !std::isfinite(value) || value < 0 || value > 1) return kInvalidArgument;
    if (it->second->info.flags & ParameterInfo::kIsProgramChange) {
        for (int32 i = 0; i < parameter_count; ++i) {
            parameters[i].input.store(false);
            parameters[i].value.store(controller->getParamNormalized(parameters[i].info.id));
        }
    }
    it->second->value.store(value);
    it->second->input.store(true);
    pending_parameters.store(true);
    dirty = true;
    return kResultOk;
}
tresult PLUGIN_API Host::resizeView(IPlugView *sender, ViewRect *size) {
    if (sender != view || !size || size->getWidth() < 1 || size->getHeight() < 1 || !resize) return kResultFalse;
    if (resize(resize_user, static_cast<uint32_t>(size->getWidth()), static_cast<uint32_t>(size->getHeight())) != LND_OK) return kResultFalse;
    return view->onSize(size);
}
void Host::editor_close() {
    if (!view) return;
    if (attached) view->removed();
    attached = false;
    view->setFrame(nullptr);
    view->release();
    view = nullptr;
    resize = nullptr;
    resize_user = nullptr;
}
Host::~Host() {
    editor_close();
    if (processing) processor->setProcessing(false);
    if (active) component->setActive(false);
    if (component_connection && controller_connection) {
        component_connection->disconnect(controller_connection);
        controller_connection->disconnect(component_connection);
    }
    if (component_connection) component_connection->release();
    if (controller_connection) controller_connection->release();
    if (units) units->release();
    if (controller) {
        controller->setComponentHandler(nullptr);
        if (controller_initialized) controller->terminate();
        controller->release();
    }
    if (processor) processor->release();
    if (component) {
        if (component_initialized) component->terminate();
        component->release();
    }
}
void Host::refresh() {
    int32 count = controller ? controller->getParameterCount() : 0;
    if (count < 0 || count > 8192) throw std::length_error("parameters");
    auto next = std::make_unique<Parameter[]>(count);
    std::unordered_map<ParamID, Parameter *> lookup;
    for (int32 i = 0; i < count; ++i) {
        if (controller->getParameterInfo(i, next[i].info) != kResultOk || !lookup.emplace(next[i].info.id, &next[i]).second)
            throw std::invalid_argument("parameter");
        next[i].value.store(controller->getParamNormalized(next[i].info.id));
    }
    input_changes.allocate(count);
    output_changes.allocate(count);
    ids = std::move(lookup);
    parameters = std::move(next);
    parameter_count = count;
    pending_parameters.store(false);
    latency = processor->getLatencySamples();
    tail = processor->getTailSamples();
}
int32_t Host::configure() {
    int32 inputs = component->getBusCount(kAudio, kInput), outputs = component->getBusCount(kAudio, kOutput);
    if (inputs < 1 || inputs > 16 || outputs < 1 || outputs > 16 || processor->canProcessSampleSize(kSample32) != kResultOk) return LND_ERR_UNSUPPORTED;
    std::vector<SpeakerArrangement> in(inputs), out(outputs);
    for (int32 i = 0; i < inputs; ++i)
        if (processor->getBusArrangement(kInput, i, in[i]) != kResultOk) return LND_ERR_UNSUPPORTED;
    for (int32 i = 0; i < outputs; ++i)
        if (processor->getBusArrangement(kOutput, i, out[i]) != kResultOk) return LND_ERR_UNSUPPORTED;
    SpeakerArrangement arrangement = options.channels == 1 ? SpeakerArr::kMono : options.channels == 2 ? SpeakerArr::kStereo : in[0];
    if (SpeakerArr::getChannelCount(arrangement) != static_cast<int32>(options.channels)) return LND_ERR_UNSUPPORTED;
    in[0] = out[0] = arrangement;
    if (processor->setBusArrangements(in.data(), inputs, out.data(), outputs) != kResultOk) return LND_ERR_UNSUPPORTED;
    input_buses.resize(inputs);
    output_buses.resize(outputs);
    size_t in_channels = 0, out_channels = 0;
    for (int direction = 0; direction < 2; ++direction) {
        auto &buses = direction == 0 ? input_buses : output_buses;
        size_t &channels = direction == 0 ? in_channels : out_channels;
        for (int32 i = 0; i < static_cast<int32>(buses.size()); ++i) {
            BusInfo info{};
            if (component->getBusInfo(kAudio, direction == 0 ? kInput : kOutput, i, info) != kResultOk || info.channelCount < 0 || info.channelCount > 64)
                return LND_ERR_UNSUPPORTED;
            if (i == 0 && static_cast<uint32>(info.channelCount) != options.channels) return LND_ERR_UNSUPPORTED;
            if (component->activateBus(kAudio, direction == 0 ? kInput : kOutput, i, i == 0) != kResultOk && i == 0) return LND_ERR_UNSUPPORTED;
            buses[i].numChannels = info.channelCount;
            channels += info.channelCount;
        }
        int32 events = component->getBusCount(kEvent, direction == 0 ? kInput : kOutput);
        if (events < 0 || events > 64) return LND_ERR_FORMAT;
        for (int32 i = 0; i < events; ++i)
            component->activateBus(kEvent, direction == 0 ? kInput : kOutput, i, false);
    }
    if (in_channels + out_channels > 256) return LND_ERR_UNSUPPORTED;
    input.assign(in_channels * options.block_frames, 0);
    output.assign(out_channels * options.block_frames, 0);
    input_planes.resize(in_channels);
    output_planes.resize(out_channels);
    for (size_t i = 0; i < in_channels; ++i)
        input_planes[i] = input.data() + i * options.block_frames;
    for (size_t i = 0; i < out_channels; ++i)
        output_planes[i] = output.data() + i * options.block_frames;
    size_t at = 0;
    for (auto &b : input_buses) {
        b.channelBuffers32 = input_planes.data() + at;
        at += b.numChannels;
    }
    at = 0;
    for (auto &b : output_buses) {
        b.channelBuffers32 = output_planes.data() + at;
        at += b.numChannels;
    }
    ProcessSetup setup{};
    setup.processMode = options.offline ? kOffline : kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = static_cast<int32>(options.block_frames);
    setup.sampleRate = options.sample_rate_hz;
    if (processor->setupProcessing(setup) != kResultOk) return LND_ERR_EXTERNAL;
    context.sampleRate = options.sample_rate_hz;
    return LND_OK;
}
int32_t Host::initialize(const LND_VST3_OPTIONS &o) {
    options = o;
    options.path = nullptr;
    module = lndvst::module(o.path);
    if (!module) return LND_ERR_FORMAT;
    bool specified = false;
    for (auto b : o.class_id)
        specified |= b != 0;
    auto found = module->classes.begin();
    if (specified) found = std::find_if(found, module->classes.end(), [&](const auto &c) { return !std::memcmp(c.id, o.class_id, 16); });
    if (found == module->classes.end()) return LND_ERR_INVALID_ARG;
    plugin = *found;
    if (module->factory->createInstance(reinterpret_cast<const char *>(plugin.id), IComponent::iid.toTUID(), reinterpret_cast<void **>(&component)) !=
            kResultOk ||
        !component)
        return LND_ERR_FORMAT;
    component->setIoMode(kAdvanced);
    if (component->initialize(static_cast<IHostApplication *>(this)) != kResultOk) return LND_ERR_EXTERNAL;
    component_initialized = true;
    if (component->queryInterface(IAudioProcessor::iid.toTUID(), reinterpret_cast<void **>(&processor)) != kResultOk || !processor) return LND_ERR_UNSUPPORTED;
    component->queryInterface(IEditController::iid.toTUID(), reinterpret_cast<void **>(&controller));
    if (!controller) {
        TUID id{};
        if (component->getControllerClassId(id) == kResultOk) {
            if (module->factory->createInstance(id, IEditController::iid.toTUID(), reinterpret_cast<void **>(&controller)) != kResultOk || !controller)
                return LND_ERR_EXTERNAL;
            if (controller->initialize(static_cast<IHostApplication *>(this)) != kResultOk) return LND_ERR_EXTERNAL;
            controller_initialized = true;
        }
    }
    if (controller) {
        if (controller->setComponentHandler(this) != kResultOk) return LND_ERR_EXTERNAL;
        controller->queryInterface(IUnitInfo::iid.toTUID(), reinterpret_cast<void **>(&units));
        if (controller_initialized) {
            component->queryInterface(IConnectionPoint::iid.toTUID(), reinterpret_cast<void **>(&component_connection));
            controller->queryInterface(IConnectionPoint::iid.toTUID(), reinterpret_cast<void **>(&controller_connection));
            if (component_connection && controller_connection) {
                component_connection->connect(controller_connection);
                controller_connection->connect(component_connection);
            }
        }
        Memory state;
        if (component->getState(&state) == kResultOk) {
            state.position = 0;
            controller->setComponentState(&state);
        }
    }
    int32_t r = configure();
    if (r) return r;
    refresh();
    if (component->setActive(true) != kResultOk) return LND_ERR_EXTERNAL;
    active = true;
    if (processor->setProcessing(true) != kResultOk) return LND_ERR_EXTERNAL;
    processing = true;
    context.state = ProcessContext::kPlaying;
    return LND_OK;
}
int32_t Host::reset() {
    if (processing) {
        processor->setProcessing(false);
        processing = false;
    }
    if (active) {
        component->setActive(false);
        active = false;
    }
    int32_t r = configure();
    if (r) return r;
    if (component->setActive(true) != kResultOk) return LND_ERR_EXTERNAL;
    active = true;
    if (processor->setProcessing(true) != kResultOk) return LND_ERR_EXTERNAL;
    processing = true;
    refresh();
    return LND_OK;
}
int32_t Host::set(ParamID id, double value) {
    auto it = ids.find(id);
    if (!controller || it == ids.end() || !std::isfinite(value) || value < 0 || value > 1 || (it->second->info.flags & ParameterInfo::kIsReadOnly))
        return LND_ERR_INVALID_ARG;
    if (controller->setParamNormalized(id, value) != kResultOk) return LND_ERR_EXTERNAL;
    return performEdit(id, value) == kResultOk ? LND_OK : LND_ERR_EXTERNAL;
}
int32_t Host::process(const LND_PCM *pcm, size_t start, uint32_t frames) {
    if (!processing) return LND_ERR_STATE;
    if (bypass && frames) {
        if (context.state & ProcessContext::kPlaying) {
            context.projectTimeSamples += frames;
            if (context.state & ProcessContext::kProjectTimeMusicValid) context.projectTimeMusic += frames * context.tempo / (60 * context.sampleRate);
        }
        context.continousTimeSamples += frames;
        return LND_OK;
    }

    for (uint32_t offset = 0;;) {
        uint32_t n = std::min(frames - offset, options.block_frames);
        bool direct = n && pcm->format == LND_FORMAT_F32 && pcm->layout == LND_LAYOUT_PLANAR && lnd_pcm_stride(pcm) == sizeof(float);
        for (uint32_t c = 0; direct && c < options.channels; ++c)
            direct = reinterpret_cast<uintptr_t>(lnd_pcm_at(pcm, c, start + offset)) % alignof(float) == 0;
        for (uint32_t c = 0; c < options.channels; ++c)
            input_planes[c] = direct ? reinterpret_cast<float *>(lnd_pcm_at(pcm, c, start + offset)) : input.data() + c * options.block_frames;
        LND_PCM stage{};
        void *planes[32];
        stage.planes = planes;
        stage.frames = n;
        stage.channels = options.channels;
        stage.format = LND_FORMAT_F32;
        stage.layout = LND_LAYOUT_PLANAR;
        if (n && !direct) {
            for (uint32_t c = 0; c < options.channels; ++c) planes[c] = input_planes[c];
            int32_t result = lnd_pcm_convert(&stage, 0, pcm, start + offset, n);
            if (result) return result;
        }
        for (auto &b : input_buses)
            b.silenceFlags = 0;
        for (auto &b : output_buses)
            b.silenceFlags = 0;
        input_changes.clear();
        output_changes.clear();
        if (pending_parameters.load() && pending_parameters.exchange(false)) {
            for (int phase = 0; phase < 2; ++phase)
                for (int32 i = 0; i < parameter_count; ++i) {
                    auto &p = parameters[i];
                    if (((p.info.flags & ParameterInfo::kIsProgramChange) != 0) != (phase == 0) || !p.input.load() || !p.input.exchange(false)) continue;
                    int32 index = 0;
                    auto q = input_changes.addParameterData(p.info.id, index);
                    q->addPoint(0, p.value.load(), index);
                }
        }
        ProcessData data{};
        data.processMode = options.offline ? kOffline : kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = static_cast<int32>(n);
        data.numInputs = static_cast<int32>(input_buses.size());
        data.numOutputs = static_cast<int32>(output_buses.size());
        data.inputs = input_buses.data();
        data.outputs = output_buses.data();
        data.inputParameterChanges = &input_changes;
        data.outputParameterChanges = &output_changes;
        data.inputEvents = data.outputEvents = &events;
        data.processContext = &context;
        if (processor->process(data) != kResultOk) return LND_ERR_EXTERNAL;
        for (uint32_t c = 0; c < options.channels; ++c) {
            bool silent = (output_buses[0].silenceFlags & (uint64_t(1) << c)) != 0;
            for (uint32_t f = 0; f < n; ++f) {
                float sample = silent ? 0 : output_planes[c][f];
                if (!std::isfinite(sample)) return LND_ERR_EXTERNAL;
                output_planes[c][f] = sample;
            }
        }
        if (n) {
            for (uint32_t c = 0; c < options.channels; ++c) planes[c] = output_planes[c];
            int32_t result = lnd_pcm_convert(pcm, start + offset, &stage, 0, n);
            if (result) return result;
        }
        for (int32 i = 0; i < output_changes.count; ++i) {
            auto &q = output_changes.queues[i];
            auto it = ids.find(q.id);
            if (it != ids.end() && q.count) {
                auto value = q.points[q.count - 1].value;
                if (std::isfinite(value) && value >= 0 && value <= 1) {
                    it->second->value.store(value);
                    it->second->output.store(true);
                }
            }
        }
        if (context.state & ProcessContext::kPlaying) {
            context.projectTimeSamples += n;
            if (context.state & ProcessContext::kProjectTimeMusicValid) context.projectTimeMusic += n * context.tempo / (60 * context.sampleRate);
        }
        context.continousTimeSamples += n;
        offset += n;
        if (offset >= frames) break;
    }
    return LND_OK;
}
int32_t Host::action(int32_t action, lnd_vst3_args &a) {
    if (std::this_thread::get_id() != ui_thread) return LND_ERR_BUSY;
    switch (action) {
    case VST_INFO: {
        auto &info = *static_cast<LND_VST3_INFO *>(a.data);
        info = {};
        info.plugin = plugin;
        info.parameter_count = static_cast<uint32_t>(parameter_count);
        info.program_list_count = units ? static_cast<uint32_t>(std::max(units->getProgramListCount(), 0)) : 0;
        info.latency_frames = latency;
        info.tail_frames = tail;
        info.bypass = bypass;
        info.editor_open = attached;
        info.dirty = dirty;
        return LND_OK;
    }
    case VST_PARAMETER: {
        if (a.index >= static_cast<uint32_t>(parameter_count)) return LND_ERR_INVALID_ARG;
        auto &p = parameters[a.index].info;
        auto &out = *static_cast<LND_VST3_PARAMETER *>(a.data);
        out = {};
        out.id = p.id;
        out.unit_id = p.unitId;
        out.steps = p.stepCount;
        out.default_value = p.defaultNormalizedValue;
        if (p.flags & ParameterInfo::kCanAutomate) out.flags |= LND_VST3_AUTOMATABLE;
        if (p.flags & ParameterInfo::kIsReadOnly) out.flags |= LND_VST3_READ_ONLY;
        if (p.flags & ParameterInfo::kIsProgramChange) out.flags |= LND_VST3_PROGRAM;
        if (p.flags & ParameterInfo::kIsBypass) out.flags |= LND_VST3_BYPASS;
        utf8(p.title, out.name, sizeof out.name);
        utf8(p.units, out.units, sizeof out.units);
        return LND_OK;
    }
    case VST_GET: {
        auto p = ids.find(a.id);
        if (p == ids.end()) return LND_ERR_INVALID_ARG;
        *static_cast<double *>(a.data) = p->second->value.load();
        return LND_OK;
    }
    case VST_SET:
        return set(a.id, a.value);
    case VST_FORMAT: {
        if (!controller || !ids.count(a.id) || !std::isfinite(a.value) || a.value < 0 || a.value > 1) return LND_ERR_INVALID_ARG;
        String128 text{};
        if (controller->getParamStringByValue(a.id, a.value, text) != kResultOk) return LND_ERR_UNSUPPORTED;
        utf8(text, static_cast<char *>(a.data), a.bytes);
        return LND_OK;
    }
    case VST_PARSE: {
        if (!controller || !ids.count(a.id)) return LND_ERR_INVALID_ARG;
        auto text = utf16(static_cast<const char *>(a.input));
        double value = 0;
        if (text.size() >= 128 || controller->getParamValueByString(a.id, reinterpret_cast<TChar *>(text.data()), value) != kResultOk ||
            !std::isfinite(value) || value < 0 || value > 1)
            return LND_ERR_INVALID_ARG;
        *static_cast<double *>(a.data) = value;
        return LND_OK;
    }
    case VST_BYPASS:
        bypass = a.id != 0;
        return LND_OK;
    case VST_PROGRAM_LIST: {
        if (!units || a.index >= static_cast<uint32_t>(std::max(units->getProgramListCount(), 0))) return LND_ERR_INVALID_ARG;
        ProgramListInfo p{};
        if (units->getProgramListInfo(static_cast<int32>(a.index), p) != kResultOk || p.programCount < 0) return LND_ERR_EXTERNAL;
        auto &out = *static_cast<LND_VST3_PROGRAM_LIST *>(a.data);
        out = {};
        out.id = p.id;
        out.count = static_cast<uint32_t>(p.programCount);
        utf8(p.name, out.name, sizeof out.name);
        return LND_OK;
    }
    case VST_PROGRAM_NAME: {
        if (!units || a.index > INT32_MAX) return LND_ERR_INVALID_ARG;
        String128 text{};
        if (units->getProgramName(static_cast<ProgramListID>(a.id), static_cast<int32>(a.index), text) != kResultOk) return LND_ERR_INVALID_ARG;
        utf8(text, static_cast<char *>(a.data), a.bytes);
        return LND_OK;
    }
    case VST_PROGRAM: {
        if (!units) return LND_ERR_UNSUPPORTED;
        int32 count = units->getUnitCount();
        if (count < 0 || count > 65536) return LND_ERR_FORMAT;
        for (int32 i = 0; i < count; ++i) {
            UnitInfo unit{};
            if (units->getUnitInfo(i, unit) != kResultOk || unit.programListId != static_cast<int32>(a.id)) continue;
            for (int32 j = 0; j < parameter_count; ++j) {
                auto &p = parameters[j].info;
                if (p.unitId == unit.id && (p.flags & ParameterInfo::kIsProgramChange)) {
                    if (p.stepCount < 0 || a.index > static_cast<uint32_t>(p.stepCount)) return LND_ERR_INVALID_ARG;
                    return set(p.id, p.stepCount ? static_cast<double>(a.index) / p.stepCount : 0);
                }
            }
        }
        return LND_ERR_UNSUPPORTED;
    }
    case VST_SAVE:
        return save(a.data, a.bytes, a.size);
    case VST_LOAD:
        return load(a.input, a.bytes);
    case VST_TRANSPORT: {
        const auto &t = *static_cast<const LND_VST3_TRANSPORT *>(a.input);
        if (t.position_frames < 0 || !std::isfinite(t.tempo_bpm) || t.tempo_bpm <= 0 || t.tempo_bpm > 1000 || t.numerator < 1 || t.denominator < 1 ||
            (t.denominator & (t.denominator - 1)))
            return LND_ERR_INVALID_ARG;
        context.state = ProcessContext::kTempoValid | ProcessContext::kTimeSigValid | ProcessContext::kProjectTimeMusicValid | ProcessContext::kContTimeValid;
        if (t.playing) context.state |= ProcessContext::kPlaying;
        context.projectTimeSamples = t.position_frames;
        context.tempo = t.tempo_bpm;
        context.projectTimeMusic = static_cast<double>(t.position_frames) * t.tempo_bpm / (60 * context.sampleRate);
        context.timeSigNumerator = t.numerator;
        context.timeSigDenominator = t.denominator;
        return LND_OK;
    }
    case VST_RESET:
        return reset();
    case VST_DISPATCH: {
        int32 flags = restart.exchange(0);
        if (flags & (kReloadComponent | kIoChanged)) {
            int32_t r = reset();
            if (r) return r;
        } else if (flags)
            refresh();
        if (controller)
            for (int32 i = 0; i < parameter_count; ++i)
                if (parameters[i].output.load() && parameters[i].output.exchange(false)) controller->setParamNormalized(parameters[i].info.id, parameters[i].value.load());
        return LND_OK;
    }
    case VST_EDITOR_OPEN: {
        if (!controller || attached) return LND_ERR_STATE;
        const char *platform = a.platform == LND_VST3_HWND     ? kPlatformTypeHWND
                               : a.platform == LND_VST3_NSVIEW ? kPlatformTypeNSView
                               : a.platform == LND_VST3_X11    ? kPlatformTypeX11EmbedWindowID
                                                               : nullptr;
        if (!platform || !a.parent) return LND_ERR_INVALID_ARG;
        view = controller->createView(ViewType::kEditor);
        if (!view) return LND_ERR_UNSUPPORTED;
        if (view->isPlatformTypeSupported(platform) != kResultOk) {
            editor_close();
            return LND_ERR_UNSUPPORTED;
        }
        resize = a.resize;
        resize_user = a.user;
        view->setFrame(this);
        if (view->attached(a.parent, platform) != kResultOk) {
            editor_close();
            return LND_ERR_EXTERNAL;
        }
        attached = true;
        return LND_OK;
    }
    case VST_EDITOR_CLOSE:
        editor_close();
        return LND_OK;
    case VST_EDITOR_SIZE: {
        if (!view) return LND_ERR_STATE;
        ViewRect size{};
        if (view->getSize(&size) != kResultOk || size.getWidth() < 1 || size.getHeight() < 1) return LND_ERR_EXTERNAL;
        auto out = static_cast<uint32_t *>(a.data);
        out[0] = static_cast<uint32_t>(size.getWidth());
        out[1] = static_cast<uint32_t>(size.getHeight());
        return LND_OK;
    }
    case VST_EDITOR_RESIZE: {
        if (!view || view->canResize() != kResultTrue) return LND_ERR_UNSUPPORTED;
        if (!a.id || !a.index || a.id > INT32_MAX || a.index > INT32_MAX) return LND_ERR_INVALID_ARG;
        ViewRect size(0, 0, static_cast<int32>(a.id), static_cast<int32>(a.index));
        view->checkSizeConstraint(&size);
        return view->onSize(&size) == kResultOk ? LND_OK : LND_ERR_EXTERNAL;
    }
    }
    return LND_ERR_INVALID_ARG;
}
}
extern "C" int32_t lnd_vst3_create(const LND_VST3_OPTIONS *options, void **engine) {
    *engine = nullptr;
    try {
        auto host = std::make_unique<lndvst::Host>();
        int32_t result = host->initialize(*options);
        if (!result) *engine = host.release();
        return result;
    } catch (const std::bad_alloc &) {
        return LND_ERR_OUT_OF_MEMORY;
    } catch (...) {
        return LND_ERR_EXTERNAL;
    }
}
extern "C" void lnd_vst3_destroy(void *engine) {
    try {
        delete static_cast<lndvst::Host *>(engine);
    } catch (...) {
    }
}
extern "C" int32_t lnd_vst3_process(void *engine, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    try {
        return static_cast<lndvst::Host *>(engine)->process(pcm, offset, frames);
    } catch (...) {
        return LND_ERR_EXTERNAL;
    }
}
extern "C" int32_t lnd_vst3_action(void *engine, int32_t action, lnd_vst3_args *args) {
    try {
        return static_cast<lndvst::Host *>(engine)->action(action, *args);
    } catch (const std::bad_alloc &) {
        return LND_ERR_OUT_OF_MEMORY;
    } catch (const std::length_error &) {
        return LND_ERR_INVALID_ARG;
    } catch (...) {
        return LND_ERR_EXTERNAL;
    }
}
extern "C" uint32_t lnd_vst3_tail(void *engine) {
    auto s = static_cast<lndvst::Host *>(engine);
    if (s->bypass) return 0;
    uint64_t frames = static_cast<uint64_t>(s->tail) + s->latency;
    return static_cast<uint32_t>(std::min(frames, static_cast<uint64_t>(UINT32_MAX)));
}
