# Lindar

Library for Integrated Native Digital Audio Runtime.

A modular C audio library, developed for [Elten](https://github.com/dawidpieper/elten3) and other audio work at [Prowadnica Foundation](https://prowadnica.org).

> **Public preview**
>
> Public API, default config, module boundaries and package contents may change before 1.0.
> Not all target configurations have been fully tested yet.

## Features

- Native playback and recording on Windows, Linux, macOS, Android and iOS.
- Seeking, looping, live PCM queues, mixing, routing, channel mapping, resampling and planar/interleaved PCM conversion.
- WAV/ADPCM, MP3, AAC, FLAC, Opus and Vorbis decoding/encoding; AIFF and Speex decoding; additional formats through system codecs and FFmpeg.
- Metadata and chapters; ID3, Ogg Opus and WAVE retagging.
- Filters, delay, reverb, modulation, dynamics, tempo/pitch control and parameter automation.
- Peak/RMS metering, FFT analysis, playback notifications and render-time profiling.
- HTTP/HTTPS, internet radio, HLS and Icecast/SHOUTcast broadcasting.
- VST3 effects and editors; MIDI-file synthesis with SF2 banks.

## Idea

Lindar uses the same source and playback APIs in MCU firmware and desktop or mobile applications. The core can render PCM without an OS or heap; devices, codecs, networking and processing are selected at build time.

Allocations can use libc or application callbacks, and core objects support caller-provided storage. The application can pull PCM itself or let a device drive playback. Lindar's worker threads are optional. The C API uses opaque resource handles and supports FFI.

[Architecture](docs/architecture.md) explains the audio types, graph and rendering model.

## Packages

Package names and contents are provisional for the preview and may change before release. Each name identifies a CMake preset and library target.

| Package | Contents |
| --- | --- |
| `linda` | Core only; no OS, workers or default allocator. |
| `lindar_pitya` | Graph, devices, files, queues and WAV/ADPCM. |
| `lindar` | Extended codecs, metadata, DSP, analysis, HTTP and SoundTouch. |
| `lindar_quanta` | Adds Bungee, ASIO, effects, MIDI, VST3 and FFmpeg. |

To distinguish builds, it is recommended to retain the package names in binary filenames. For custom builds, use a name such as `lindar_custom`, `lindar_xyz` or another `lindar_*` variant.

## Example

With the `lindar` package: play `music.mp3` through a stereo mixer at half gain. Press Enter to stop.

```c
#include "lindar.h"
#include "lindar_devices.h"
#include "lindar_files.h"
#include "lindar_graph.h"
#include <stdio.h>

int main(void) {
    if (LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0) != LND_OK ||
        LND_LibraryInit() != LND_OK) return 1;

    LND_SOURCE *source = LND_SourceCreateFile("music.mp3", 0, nullptr);
    LND_SOUND *sound = source ? LND_SourceEnsureSound(source, nullptr) : nullptr;
    LND_NODE *mixer = LND_NodeCreateMixer(2, 48000, 0);
    LND_DEVICE_INSTANCE *device = LND_DeviceInstanceOpen(LND_DEVICE_DEFAULT_OUTPUT);
    int result = 1;

    if (sound && mixer && device &&
        LND_SoundSetOutput(sound, mixer) == LND_OK &&
        LND_NodeSetParam(mixer, LND_PARAM_GAIN, 0.5f) == LND_OK &&
        LND_NodeConnect(mixer, LND_DeviceInstanceGetNode(device)) == LND_OK &&
        LND_SoundPlay(sound) == LND_OK) {
        puts("Playing music.mp3. Press Enter to stop.");
        getchar();
        result = 0;
    }

    LND_LibraryFree();
    return result;
}
```

More examples: [examples/](examples).

## Documentation

- [1.0 release checklist](docs/todo.md)
- [Security policy](docs/SECURITY.md)
- [Build and installation](docs/build.md)
- [Architecture and module development](docs/architecture.md)
- [Tests, benchmarks, MCU emulation and documentation tools](docs/testing.md)

API behaviour and constraints are documented in [include/](include). Generate the reference, module/package tables and example index with `ruby tools/docs.rb`; open `build/docs/html/index.html`. [Requirements](docs/testing.md#api-reference).

## Contributing

Thank you for your interest in Lindar and for helping improve it.

See [how to contribute and accept the CLA](docs/contributing.md).

## Licence

All original project content is licensed under the [GNU Lesser General Public License, version 2.1 only](LICENSE) (`LGPL-2.1-only`). Third-party code and assets retain their own licences.