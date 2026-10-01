#include "io.h"
#include "lindar_metadata_comments.h"

static const uint32_t crc_table[256] = {
    0x00000000u, 0x04c11db7u, 0x09823b6eu, 0x0d4326d9u, 0x130476dcu, 0x17c56b6bu, 0x1a864db2u, 0x1e475005u, 0x2608edb8u, 0x22c9f00fu, 0x2f8ad6d6u, 0x2b4bcb61u,
    0x350c9b64u, 0x31cd86d3u, 0x3c8ea00au, 0x384fbdbdu, 0x4c11db70u, 0x48d0c6c7u, 0x4593e01eu, 0x4152fda9u, 0x5f15adacu, 0x5bd4b01bu, 0x569796c2u, 0x52568b75u,
    0x6a1936c8u, 0x6ed82b7fu, 0x639b0da6u, 0x675a1011u, 0x791d4014u, 0x7ddc5da3u, 0x709f7b7au, 0x745e66cdu, 0x9823b6e0u, 0x9ce2ab57u, 0x91a18d8eu, 0x95609039u,
    0x8b27c03cu, 0x8fe6dd8bu, 0x82a5fb52u, 0x8664e6e5u, 0xbe2b5b58u, 0xbaea46efu, 0xb7a96036u, 0xb3687d81u, 0xad2f2d84u, 0xa9ee3033u, 0xa4ad16eau, 0xa06c0b5du,
    0xd4326d90u, 0xd0f37027u, 0xddb056feu, 0xd9714b49u, 0xc7361b4cu, 0xc3f706fbu, 0xceb42022u, 0xca753d95u, 0xf23a8028u, 0xf6fb9d9fu, 0xfbb8bb46u, 0xff79a6f1u,
    0xe13ef6f4u, 0xe5ffeb43u, 0xe8bccd9au, 0xec7dd02du, 0x34867077u, 0x30476dc0u, 0x3d044b19u, 0x39c556aeu, 0x278206abu, 0x23431b1cu, 0x2e003dc5u, 0x2ac12072u,
    0x128e9dcfu, 0x164f8078u, 0x1b0ca6a1u, 0x1fcdbb16u, 0x018aeb13u, 0x054bf6a4u, 0x0808d07du, 0x0cc9cdcau, 0x7897ab07u, 0x7c56b6b0u, 0x71159069u, 0x75d48ddeu,
    0x6b93dddbu, 0x6f52c06cu, 0x6211e6b5u, 0x66d0fb02u, 0x5e9f46bfu, 0x5a5e5b08u, 0x571d7dd1u, 0x53dc6066u, 0x4d9b3063u, 0x495a2dd4u, 0x44190b0du, 0x40d816bau,
    0xaca5c697u, 0xa864db20u, 0xa527fdf9u, 0xa1e6e04eu, 0xbfa1b04bu, 0xbb60adfcu, 0xb6238b25u, 0xb2e29692u, 0x8aad2b2fu, 0x8e6c3698u, 0x832f1041u, 0x87ee0df6u,
    0x99a95df3u, 0x9d684044u, 0x902b669du, 0x94ea7b2au, 0xe0b41de7u, 0xe4750050u, 0xe9362689u, 0xedf73b3eu, 0xf3b06b3bu, 0xf771768cu, 0xfa325055u, 0xfef34de2u,
    0xc6bcf05fu, 0xc27dede8u, 0xcf3ecb31u, 0xcbffd686u, 0xd5b88683u, 0xd1799b34u, 0xdc3abdedu, 0xd8fba05au, 0x690ce0eeu, 0x6dcdfd59u, 0x608edb80u, 0x644fc637u,
    0x7a089632u, 0x7ec98b85u, 0x738aad5cu, 0x774bb0ebu, 0x4f040d56u, 0x4bc510e1u, 0x46863638u, 0x42472b8fu, 0x5c007b8au, 0x58c1663du, 0x558240e4u, 0x51435d53u,
    0x251d3b9eu, 0x21dc2629u, 0x2c9f00f0u, 0x285e1d47u, 0x36194d42u, 0x32d850f5u, 0x3f9b762cu, 0x3b5a6b9bu, 0x0315d626u, 0x07d4cb91u, 0x0a97ed48u, 0x0e56f0ffu,
    0x1011a0fau, 0x14d0bd4du, 0x19939b94u, 0x1d528623u, 0xf12f560eu, 0xf5ee4bb9u, 0xf8ad6d60u, 0xfc6c70d7u, 0xe22b20d2u, 0xe6ea3d65u, 0xeba91bbcu, 0xef68060bu,
    0xd727bbb6u, 0xd3e6a601u, 0xdea580d8u, 0xda649d6fu, 0xc423cd6au, 0xc0e2d0ddu, 0xcda1f604u, 0xc960ebb3u, 0xbd3e8d7eu, 0xb9ff90c9u, 0xb4bcb610u, 0xb07daba7u,
    0xae3afba2u, 0xaafbe615u, 0xa7b8c0ccu, 0xa379dd7bu, 0x9b3660c6u, 0x9ff77d71u, 0x92b45ba8u, 0x9675461fu, 0x8832161au, 0x8cf30badu, 0x81b02d74u, 0x857130c3u,
    0x5d8a9099u, 0x594b8d2eu, 0x5408abf7u, 0x50c9b640u, 0x4e8ee645u, 0x4a4ffbf2u, 0x470cdd2bu, 0x43cdc09cu, 0x7b827d21u, 0x7f436096u, 0x7200464fu, 0x76c15bf8u,
    0x68860bfdu, 0x6c47164au, 0x61043093u, 0x65c52d24u, 0x119b4be9u, 0x155a565eu, 0x18197087u, 0x1cd86d30u, 0x029f3d35u, 0x065e2082u, 0x0b1d065bu, 0x0fdc1becu,
    0x3793a651u, 0x3352bbe6u, 0x3e119d3fu, 0x3ad08088u, 0x2497d08du, 0x2056cd3au, 0x2d15ebe3u, 0x29d4f654u, 0xc5a92679u, 0xc1683bceu, 0xcc2b1d17u, 0xc8ea00a0u,
    0xd6ad50a5u, 0xd26c4d12u, 0xdf2f6bcbu, 0xdbee767cu, 0xe3a1cbc1u, 0xe760d676u, 0xea23f0afu, 0xeee2ed18u, 0xf0a5bd1du, 0xf464a0aau, 0xf9278673u, 0xfde69bc4u,
    0x89b8fd09u, 0x8d79e0beu, 0x803ac667u, 0x84fbdbd0u, 0x9abc8bd5u, 0x9e7d9662u, 0x933eb0bbu, 0x97ffad0cu, 0xafb010b1u, 0xab710d06u, 0xa6322bdfu, 0xa2f33668u,
    0xbcb4666du, 0xb8757bdau, 0xb5365d03u, 0xb1f740b4u};

static uint32_t checksum(const uint8_t *p, size_t n) {
    uint32_t crc = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t byte = i >= 22 && i < 26 ? 0 : p[i];
        crc = (crc << 8) ^ crc_table[(crc >> 24) ^ byte];
    }
    return crc;
}

static int32_t page_read(LND_IO *input, uint64_t pos, uint8_t *page, size_t *size) {
    int32_t r = lnd_tag_read_at(input, pos, page, 27);
    if (r) return r;
    if (memcmp(page, "OggS", 4) || page[4] || (page[5] & ~7u)) return LND_ERR_FORMAT;
    size_t segments = page[26];
    r = lnd_tag_read_at(input, pos + 27, page + 27, segments);
    if (r) return r;
    size_t body = 0;
    for (size_t i = 0; i < segments; ++i) body += page[27 + i];
    *size = 27 + segments + body;
    r = lnd_tag_read_at(input, pos + 27 + segments, page + 27 + segments, body);
    if (!r && checksum(page, *size) != lnd_tag_get32(page + 22, false)) r = LND_ERR_FORMAT;
    return r;
}

static int32_t write_tags(LND_IO *output, uint8_t *page, uint32_t serial, uint32_t sequence, const uint8_t *packet, size_t bytes, uint32_t *pages) {
    size_t remaining = bytes;
    *pages = 0;
    bool finished = false;
    while (!finished) {
        size_t segments = remaining / 255 + 1;
        if (segments > 255) segments = 255;
        size_t body = remaining < segments * 255 ? remaining : segments * 255;
        finished = segments * 255 > remaining;
        memset(page, 0, 27);
        memcpy(page, "OggS", 4);
        page[5] = *pages ? 1 : 0;
        if (!finished) memset(page + 6, 255, 8);
        lnd_tag_put32(page + 14, serial, false);
        lnd_tag_put32(page + 18, sequence + *pages, false);
        page[26] = (uint8_t)segments;
        memset(page + 27, 255, segments);
        if (finished) page[27 + segments - 1] = (uint8_t)(remaining % 255);
        if (body) memcpy(page + 27 + segments, packet + bytes - remaining, body);
        size_t size = 27 + segments + body;
        lnd_tag_put32(page + 22, checksum(page, size), false);
        int32_t r = lnd_tag_write(output, page, size);
        if (r) return r;
        ++*pages;
        remaining -= body;
    }
    return LND_OK;
}

int32_t lnd_tag_ogg(LND_METADATA *m, LND_IO *input, LND_IO *output, const LND_METADATA *replacement, uint32_t flags) {
    if ((!m && !replacement) || (output && !replacement)) return LND_ERR_INVALID_ARG;
    uint8_t *page = lnd_alloc(65307);
    if (!page) return LND_ERR_OUT_OF_MEMORY;
    void *new_tags = nullptr;
    size_t new_size = 0;
    int32_t r = replacement ? LND_MetadataOpusCreateBuffer(replacement, flags, &new_tags, &new_size) : LND_OK;
    lnd_tag_buffer packet = {.limit = m ? m->limits.bytes : replacement->limits.bytes};
    uint32_t serial = 0, sequence = 0, old_pages = 0, new_pages = 0, delta = 0;
    bool found = false, tags = false, ended = false;
    uint64_t pos = 0;
    while (!r && pos < LND_IoGetSizeBytes(input)) {
        size_t size;
        r = page_read(input, pos, page, &size);
        if (r) break;
        uint32_t page_serial = lnd_tag_get32(page + 14, false);
        uint32_t page_sequence = lnd_tag_get32(page + 18, false);
        unsigned segments = page[26];
        size_t header = 27 + segments, body = size - header;
        bool selected_head = false;
        if (!found && !(page[5] & 2)) {
            r = LND_METADATA_ERR_NOT_FOUND;
            break;
        }
        if (!found && (page[5] & 2) && body >= 8 && !memcmp(page + header, "OpusHead", 8)) {
            if (page[5] != 2 || body < 19 || page[header + 8] > 15 || !page[header + 9] || !segments || page[27 + segments - 1] == 255 ||
                lnd_tag_get64(page + 6) || page_sequence) {
                r = LND_ERR_FORMAT;
                break;
            }
            for (unsigned i = 0; i + 1 < segments; ++i)
                if (page[27 + i] != 255) r = LND_ERR_FORMAT;
            if (r) break;
            found = true;
            selected_head = true;
            serial = page_serial;
            sequence = page_sequence + 1;
        }
        bool selected = found && !ended && page_serial == serial && !selected_head;
        if (selected && page_sequence != sequence++) {
            r = LND_ERR_FORMAT;
            break;
        }
        if (selected && !tags) {
            if (!segments || (page[5] & 6) || ((page[5] & 1) != (old_pages ? 1 : 0))) {
                r = LND_ERR_FORMAT;
                break;
            }
            bool finish = page[27 + segments - 1] < 255;
            for (unsigned i = 0; i + 1 < segments; ++i)
                if (page[27 + i] != 255) r = LND_ERR_FORMAT;
            uint64_t granule = lnd_tag_get64(page + 6);
            if (r || (finish ? granule != 0 : granule != UINT64_MAX)) {
                r = LND_ERR_FORMAT;
                break;
            }
            lnd_tag_append(&packet, page + header, body);
            ++old_pages;
            if (packet.error) {
                r = packet.error;
                break;
            }
            if (finish) {
                LND_METADATA *check = m ? m : LND_MetadataCreate(&replacement->limits);
                if (!check) {
                    r = LND_ERR_OUT_OF_MEMORY;
                    break;
                }
                r = LND_MetadataOpusRead(check, packet.data, packet.size);
                if (!m) LND_MetadataFree(check);
                if (r) break;
                tags = true;
                if (!output) break;
                r = write_tags(output, page, serial, 1, new_tags, new_size, &new_pages);
                delta = new_pages - old_pages;
            }
        } else {
            if (selected) {
                if (page[5] & 2) {
                    r = LND_ERR_FORMAT;
                    break;
                }
                if (page[5] & 4) ended = true;
                lnd_tag_put32(page + 18, page_sequence + delta, false);
                lnd_tag_put32(page + 22, checksum(page, size), false);
            }
            if (output) r = lnd_tag_write(output, page, size);
        }
        pos += size;
    }
    if (!r && !found)
        r = LND_METADATA_ERR_NOT_FOUND;
    else if (!r && (!tags || (output && !ended)))
        r = LND_ERR_FORMAT;
    lnd_free(packet.data);
    lnd_free(new_tags);
    lnd_free(page);
    return r;
}
