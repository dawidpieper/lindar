#include "asio.h"
#include "src/alloc.h"

#include <objbase.h>
#include <string.h>
#include <wchar.h>

static char *lnd_asio_utf8(const WCHAR *text) {
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0)
        return nullptr;
    char *result = lnd_alloc((size_t)count);
    if (!result)
        return nullptr;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result, count, nullptr, nullptr)) {
        lnd_free(result);
        return nullptr;
    }
    return result;
}

static uint64_t lnd_asio_hash(uint64_t hash, const void *memory, size_t bytes) {
    const uint8_t *p = memory;
    for (size_t i = 0; i < bytes; i++)
        hash = (hash ^ p[i]) * UINT64_C(1099511628211);
    return hash;
}

int32_t lnd_asio_registry_enumerate(lnd_asio_registration_proc proc, void *user, uint64_t *fingerprint) {
    HKEY root;
    LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ, &root);
    uint64_t hash = UINT64_C(14695981039346656037);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
        if (fingerprint)
            *fingerprint = hash;
        return LND_OK;
    }
    if (status != ERROR_SUCCESS)
        return LND_ERR_EXTERNAL;
    WCHAR name[256], clsid[40];
    int32_t result = LND_OK;
    for (DWORD index = 0;; index++) {
        DWORD size = LND_COUNTOF(name);
        status = RegEnumKeyExW(root, index, name, &size, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status != ERROR_SUCCESS) {
            result = LND_ERR_EXTERNAL;
            break;
        }
        DWORD bytes = sizeof clsid;
        if (RegGetValueW(root, name, L"CLSID", RRF_RT_REG_SZ, nullptr, clsid, &bytes) != ERROR_SUCCESS)
            continue;
        clsid[LND_COUNTOF(clsid) - 1] = 0;
        GUID id;
        if (FAILED(CLSIDFromString(clsid, &id)))
            continue;
        WCHAR canonical[40];
        if (!StringFromGUID2(&id, canonical, LND_COUNTOF(canonical)))
            continue;
        char key[40];
        if (!WideCharToMultiByte(CP_UTF8, 0, canonical, -1, key, sizeof key, nullptr, nullptr))
            continue;
        hash = lnd_asio_hash(hash, &id, sizeof id);
        hash = lnd_asio_hash(hash, name, size * sizeof(WCHAR));
        bytes = 0;
        status = RegGetValueW(root, name, L"Description", RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
        WCHAR *description = nullptr;
        if (status == ERROR_SUCCESS && bytes >= sizeof(WCHAR) && bytes <= 65536) {
            description = lnd_alloc((size_t)bytes + sizeof(WCHAR));
            if (!description) {
                result = LND_ERR_OUT_OF_MEMORY;
                break;
            }
            DWORD capacity = bytes;
            if (RegGetValueW(root, name, L"Description", RRF_RT_REG_SZ, nullptr, description, &capacity) != ERROR_SUCCESS) {
                lnd_free(description);
                description = nullptr;
            } else
                description[capacity / sizeof(WCHAR)] = 0;
        }
        char *label = lnd_asio_utf8(description && description[0] ? description : name);
        lnd_free(description);
        if (!label) {
            result = LND_ERR_OUT_OF_MEMORY;
            break;
        }
        hash = lnd_asio_hash(hash, label, strlen(label));
        if (proc)
            result = proc(user, &id, key, label);
        lnd_free(label);
        if (result != LND_OK)
            break;
    }
    RegCloseKey(root);
    if (result == LND_OK && fingerprint)
        *fingerprint = hash;
    return result;
}

int32_t lnd_asio_driver_load(const GUID *id, lnd_asio_abi **out) {
    *out = nullptr;
    HRESULT result = CoCreateInstance(id, nullptr, CLSCTX_INPROC_SERVER, id, (void **)out);
    if (FAILED(result))
        return result == E_OUTOFMEMORY ? LND_ERR_OUT_OF_MEMORY : LND_ERR_NO_DEVICE;
    return *out ? LND_OK : LND_ERR_EXTERNAL;
}

void lnd_asio_driver_unload(lnd_asio_abi *driver) {
    if (driver)
        driver->vt->release(driver);
}
