#include "host.hpp"
namespace lndvst {
static uint64_t get(const uint8_t *p, unsigned bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i)
        value |= static_cast<uint64_t>(p[i]) << (i * 8);
    return value;
}
static void put(std::vector<uint8_t> &v, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
        v.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
static void append(std::vector<uint8_t> &v, const void *data, size_t bytes) {
    if (bytes) v.insert(v.end(), static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + bytes);
}
int32_t Host::save(void *data, size_t capacity, size_t *bytes) {
    int32_t flushed = process(nullptr, 0, 0);
    if (flushed) return flushed;
    Memory comp, cont;
    if (component->getState(&comp) != kResultOk) return LND_ERR_EXTERNAL;
    bool controller_state = controller && controller->getState(&cont) == kResultOk;
    uint64_t list = 48 + comp.data.size() + cont.data.size();
    std::vector<uint8_t> preset;
    preset.reserve(static_cast<size_t>(list) + 48);
    append(preset, "VST3", 4);
    put(preset, 1, 4);
    FUID id = FUID::fromTUID(reinterpret_cast<const char *>(plugin.id));
    char text[33]{};
    id.toString(text);
    append(preset, text, 32);
    put(preset, list, 8);
    append(preset, comp.data.data(), comp.data.size());
    append(preset, cont.data.data(), cont.data.size());
    append(preset, "List", 4);
    put(preset, controller_state ? 2 : 1, 4);
    append(preset, "Comp", 4);
    put(preset, 48, 8);
    put(preset, comp.data.size(), 8);
    if (controller_state) {
        append(preset, "Cont", 4);
        put(preset, 48 + comp.data.size(), 8);
        put(preset, cont.data.size(), 8);
    }
    *bytes = preset.size();
    if (!data && !capacity) return LND_OK;
    if (!data || capacity < preset.size()) return LND_ERR_INVALID_ARG;
    std::memcpy(data, preset.data(), preset.size());
    return LND_OK;
}
int32_t Host::load(const void *data, size_t bytes) {
    if (!data || bytes < 56 || bytes > Memory::limit * 2 + 128) return LND_ERR_INVALID_ARG;
    const auto *p = static_cast<const uint8_t *>(data);
    if (std::memcmp(p, "VST3", 4) || get(p + 4, 4) != 1) return LND_ERR_FORMAT;
    char text[33]{};
    std::memcpy(text, p + 8, 32);
    FUID id;
    if (!id.fromString(text) || std::memcmp(id.toTUID(), plugin.id, 16)) return LND_ERR_FORMAT;
    uint64_t list = get(p + 40, 8);
    if (list < 48 || list > bytes - 8 || std::memcmp(p + list, "List", 4)) return LND_ERR_FORMAT;
    uint64_t count = get(p + list + 4, 4);
    if (count > (bytes - list - 8) / 20) return LND_ERR_FORMAT;
    Memory comp, cont;
    bool has_comp = false, has_cont = false;
    for (uint64_t i = 0; i < count; ++i) {
        const uint8_t *entry = p + list + 8 + i * 20;
        uint64_t start = get(entry + 4, 8), size = get(entry + 12, 8);
        if (start < 48 || start > list || size > list - start || size > Memory::limit) return LND_ERR_FORMAT;
        if (!std::memcmp(entry, "Comp", 4)) {
            if (has_comp) return LND_ERR_FORMAT;
            has_comp = true;
            comp.data.assign(p + start, p + start + size);
        } else if (!std::memcmp(entry, "Cont", 4)) {
            if (has_cont) return LND_ERR_FORMAT;
            has_cont = true;
            cont.data.assign(p + start, p + start + size);
        }
    }
    if (!has_comp) return LND_ERR_FORMAT;
    Memory previous_comp, previous_cont;
    bool backup_comp = component->getState(&previous_comp) == kResultOk;
    bool backup_cont = controller && controller->getState(&previous_cont) == kResultOk;
    if (processing) {
        processor->setProcessing(false);
        processing = false;
    }
    if (active) {
        component->setActive(false);
        active = false;
    }
    bool ok = component->setState(&comp) == kResultOk;
    if (ok && controller) {
        comp.position = 0;
        auto r = controller->setComponentState(&comp);
        ok = r == kResultOk || r == kNotImplemented;
        if (ok && has_cont) ok = controller->setState(&cont) == kResultOk;
    }
    if (!ok && backup_comp) {
        previous_comp.position = 0;
        component->setState(&previous_comp);
        if (controller) {
            previous_comp.position = 0;
            controller->setComponentState(&previous_comp);
            if (backup_cont) {
                previous_cont.position = 0;
                controller->setState(&previous_cont);
            }
        }
    }
    int32_t r = reset();
    if (r) return r;
    dirty = ok;
    return ok ? LND_OK : LND_ERR_EXTERNAL;
}
}
