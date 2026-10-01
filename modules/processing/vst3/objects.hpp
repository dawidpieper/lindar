#pragma once
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <stdexcept>
#include "bridge.h"

#include <string>
#include <vector>
namespace lndvst {
using namespace Steinberg;
using namespace Steinberg::Vst;
inline bool same_iid(const TUID a, const FUID &b) { return !std::memcmp(a, b.toTUID(), 16); }
template <class I, bool Owned = true> class Object : public I {
    std::atomic<uint32> refs{1};

  public:
    virtual ~Object() = default;
    tresult PLUGIN_API queryInterface(const TUID id, void **out) override {
        if (!out) return kInvalidArgument;
        *out = nullptr;
        if (!same_iid(id, I::iid) && !same_iid(id, FUnknown::iid)) return kNoInterface;
        *out = static_cast<I *>(this);
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override {
        auto n = --refs;
        if constexpr (Owned) {
            if (!n) delete this;
        }
        return n;
    }
};
inline void utf8(const TChar *src, char *dst, size_t capacity) {
    if (lnd_vst3_utf8(reinterpret_cast<const uint16_t *>(src), dst, capacity)) throw std::length_error("text");
}
inline std::u16string utf16(const char *text) {
    char16_t buffer[128];
    int32_t size = lnd_vst3_utf16(text, reinterpret_cast<uint16_t *>(buffer), 128);
    if (size < 0) throw std::length_error("text");
    return std::u16string(buffer, static_cast<size_t>(size));
}
class Attributes final : public Object<IAttributeList> {
    std::map<std::string, int64> ints;
    std::map<std::string, double> floats;
    std::map<std::string, std::u16string> strings;
    std::map<std::string, std::vector<uint8_t>> binary;

  public:
    tresult PLUGIN_API setInt(AttrID id, int64 value) override {
        if (!id) return kInvalidArgument;
        try {
            ints[id] = value;
            return kResultOk;
        } catch (...) {
            return kOutOfMemory;
        }
    }
    tresult PLUGIN_API getInt(AttrID id, int64 &value) override {
        if (!id) return kInvalidArgument;
        auto p = ints.find(id);
        if (p == ints.end()) return kResultFalse;
        value = p->second;
        return kResultOk;
    }
    tresult PLUGIN_API setFloat(AttrID id, double value) override {
        if (!id) return kInvalidArgument;
        try {
            floats[id] = value;
            return kResultOk;
        } catch (...) {
            return kOutOfMemory;
        }
    }
    tresult PLUGIN_API getFloat(AttrID id, double &value) override {
        if (!id) return kInvalidArgument;
        auto p = floats.find(id);
        if (p == floats.end()) return kResultFalse;
        value = p->second;
        return kResultOk;
    }
    tresult PLUGIN_API setString(AttrID id, const TChar *value) override {
        if (!id || !value) return kInvalidArgument;
        try {
            strings[id] = reinterpret_cast<const char16_t *>(value);
            return kResultOk;
        } catch (...) {
            return kOutOfMemory;
        }
    }
    tresult PLUGIN_API getString(AttrID id, TChar *value, uint32 size) override {
        if (!id || !value || size < 2) return kInvalidArgument;
        auto p = strings.find(id);
        if (p == strings.end()) return kResultFalse;
        size_t n = std::min(p->second.size(), static_cast<size_t>(size / 2 - 1));
        std::memcpy(value, p->second.data(), n * 2);
        value[n] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API setBinary(AttrID id, const void *data, uint32 size) override {
        if (!id || (!data && size) || size > 64u * 1024 * 1024) return kInvalidArgument;
        try {
            auto &v = binary[id];
            v.resize(size);
            if (size) std::memcpy(v.data(), data, size);
            return kResultOk;
        } catch (...) {
            return kOutOfMemory;
        }
    }
    tresult PLUGIN_API getBinary(AttrID id, const void *&data, uint32 &size) override {
        if (!id) return kInvalidArgument;
        auto p = binary.find(id);
        if (p == binary.end()) return kResultFalse;
        data = p->second.data();
        size = static_cast<uint32>(p->second.size());
        return kResultOk;
    }
};
class Message final : public Object<IMessage> {
    std::string id;
    Attributes *attributes = new Attributes;

  public:
    ~Message() override { attributes->release(); }
    FIDString PLUGIN_API getMessageID() override { return id.c_str(); }
    void PLUGIN_API setMessageID(FIDString value) override {
        try {
            id = value ? value : "";
        } catch (...) {
        }
    }
    IAttributeList *PLUGIN_API getAttributes() override { return attributes; }
};
class Memory final : public Object<IBStream, false> {
  public:
    std::vector<uint8_t> data;
    uint64_t position = 0;
    static constexpr size_t limit = 64u * 1024 * 1024;
    tresult PLUGIN_API read(void *buffer, int32 bytes, int32 *read) override {
        if (read) *read = 0;
        if (bytes < 0 || (!buffer && bytes)) return kInvalidArgument;
        size_t n = position < data.size() ? std::min(static_cast<size_t>(bytes), data.size() - static_cast<size_t>(position)) : 0;
        if (n) std::memcpy(buffer, data.data() + position, n);
        position += n;
        if (read) *read = static_cast<int32>(n);
        return n || !bytes ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API write(void *buffer, int32 bytes, int32 *written) override {
        if (written) *written = 0;
        if (bytes < 0 || (!buffer && bytes) || position > limit || static_cast<size_t>(bytes) > limit - position) return kInvalidArgument;
        try {
            if (data.size() < position + bytes) data.resize(static_cast<size_t>(position) + bytes);
            if (bytes) std::memcpy(data.data() + position, buffer, bytes);
            position += bytes;
            if (written) *written = bytes;
            return kResultOk;
        } catch (...) {
            return kOutOfMemory;
        }
    }
    tresult PLUGIN_API seek(int64 offset, int32 mode, int64 *result) override {
        if (mode < kIBSeekSet || mode > kIBSeekEnd) return kInvalidArgument;
        int64 base = mode == kIBSeekCur ? static_cast<int64>(position) : mode == kIBSeekEnd ? static_cast<int64>(data.size()) : 0;
        if (offset < -base || offset > static_cast<int64>(limit) - base) return kInvalidArgument;
        position = static_cast<uint64_t>(base + offset);
        if (result) *result = static_cast<int64>(position);
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64 *pos) override {
        if (!pos) return kInvalidArgument;
        *pos = static_cast<int64>(position);
        return kResultOk;
    }
};
class Queue final : public Object<IParamValueQueue, false> {
  public:
    ParamID id = 0;
    int32 count = 0;
    struct Point {
        int32 offset;
        double value;
    } points[64]{};
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return count; }
    tresult PLUGIN_API getPoint(int32 index, int32 &offset, ParamValue &value) override {
        if (index < 0 || index >= count) return kInvalidArgument;
        offset = points[index].offset;
        value = points[index].value;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 offset, ParamValue value, int32 &index) override {
        if (offset < 0 || count == 64) return kResultFalse;
        index = count;
        points[count++] = {offset, value};
        return kResultOk;
    }
};
class Changes final : public Object<IParameterChanges, false> {
  public:
    std::unique_ptr<Queue[]> queues;
    int32 count = 0, capacity = 0;
    void allocate(int32 n) {
        queues = std::make_unique<Queue[]>(n);
        capacity = n;
        count = 0;
    }
    void clear() {
        for (int32 i = 0; i < count; ++i)
            queues[i].count = 0;
        count = 0;
    }
    int32 PLUGIN_API getParameterCount() override { return count; }
    IParamValueQueue *PLUGIN_API getParameterData(int32 index) override { return index >= 0 && index < count ? &queues[index] : nullptr; }
    IParamValueQueue *PLUGIN_API addParameterData(const ParamID &id, int32 &index) override {
        for (int32 i = 0; i < count; ++i)
            if (queues[i].id == id) {
                index = i;
                return &queues[i];
            }
        if (count == capacity) return nullptr;
        index = count;
        queues[count].id = id;
        queues[count].count = 0;
        return &queues[count++];
    }
};
class Events final : public Object<IEventList, false> {
  public:
    int32 PLUGIN_API getEventCount() override { return 0; }
    tresult PLUGIN_API getEvent(int32 index, Event &event) override { return kResultFalse; }
    tresult PLUGIN_API addEvent(Event &event) override { return kResultFalse; }
};
}
