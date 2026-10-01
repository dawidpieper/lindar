# Build and installation

Requires C23 and CMake 3.21+. Presets require CMake 3.25+ and Ninja. FDK AAC, SoundTouch and VST3 need C++; Bungee needs C++20. iOS also needs Objective-C.

## Packages

Initialise dependencies for `lindar` or `lindar_quanta`; `linda` and `lindar_pitya` need none:

```sh
ruby tools/vendor.rb
cmake --preset lindar_quanta -DLND_SHARED=ON
cmake --build --preset lindar_quanta --parallel
cmake --install build/lindar_quanta --prefix install/lindar_quanta --component Lindar
```

[Package contents](../readme.md#packages). Without presets, set `LND_PACKAGE` to the package name. Libraries are static unless `LND_SHARED=ON`.

## Custom builds

Fixed packages override module selectors, codec directions, OS, threads and allocator settings. Use `LND_PACKAGE=custom` (the default) for independent selection. Switching back to `custom` retains cached values.

| Option | Selection |
| --- | --- |
| `LND_MINIMAL=ON` | Disable optional modules by default. |
| `LND_MODULE_<NAME>=AUTO/ON/OFF` | AUTO permits dependency resolution to enable a module; explicit OFF is enforced. |
| `LND_OS_MODE`, `LND_THREADS` | OS integration and workers. |
| `LND_USE_LIBC_ALLOC=OFF` | No default Lindar allocator; vendor allocations are unaffected. |
| `LND_BUILD_DECODERS`, `LND_BUILD_ENCODERS` | Enable a codec direction globally. |
| `LND_MODULE_<FORMAT>_DECODER/ENCODER` | Enable a direction for one format. |

For example, file decoding without graph or devices:

```sh
cmake -S . -B build/file -DLND_MINIMAL=ON -DLND_OS_MODE=ON -DLND_THREADS=OFF -DLND_MODULE_FILES=ON -DLND_MODULE_WAV=ON -DLND_BUILD_ENCODERS=OFF
```

MCU presets: `minimal`, `adpcm-mcu`, `opus-mcu`. Cross-compilation and execution are covered under [MCU checks](testing.md#microcontrollers).

For custom MCU networking, set `LND_HTTP_TRANSPORT=CUSTOM`, `LND_OS_MODE=OFF`, `LND_THREADS=OFF` and `LND_MODULE_HTTP=ON`. An allocator is required; [http_mcu](../examples/mcu/http_mcu/main.c) supplies a nonblocking transport.

## Dependencies

For selected dependencies, run `ruby tools/vendor.rb opus opusfile ogg`. Initialisation uses recorded gitlinks; builds do not download dependencies.

To update a submodule, check out a stable release tag or an explicit commit for projects without releases. Record the new gitlink, its `commit`/`release` values in `.gitmodules` and [vendor/VERSIONS.md](../vendor/VERSIONS.md) together.

## External tools

| Target | Requirements |
| --- | --- |
| Windows | 32-bit ASIO needs Clang/GCC for its C bridge; MSVC is unsupported for that configuration. |
| Linux | ALSA development package; `lindar`/`lindar_quanta` also need GStreamer app/audio 1.14+ development packages. |
| Android | API 26 for AAudio, API 28 for MediaCodec. |

CURL uses vendored curl and c-ares. `LND_CURL_TLS` defaults to SCHANNEL on Windows, OPENSSL elsewhere. HLS AES-128 needs OpenSSL libcrypto even with Schannel.

OpenSSL requires Perl and make: MSYS Perl/make for MinGW, developer-shell nmake for MSVC. Overrides are `LND_OPENSSL_PERL`, `LND_OPENSSL_MAKE` and `LND_OPENSSL_TARGET`. `LND_OPENSSL_JOBS` controls its internal parallelism; `LND_OPENSSL_ASM` controls assembler kernels.

`LND_FFMPEG_BUNDLED=ON` links vendored static FFmpeg archives and is the quanta default. It requires `sh`, `make` and Unix tools including `cmp`. OFF selects installed FFmpeg through CMake discovery, including `CMAKE_PREFIX_PATH`.

## Linking

With `add_subdirectory`, set `LND_PACKAGE` before adding Lindar and link its target; `lindar` also aliases the selected target. Transitive static vendor/platform libraries are carried by the CMake target, not merged into the Lindar archive.

The `Lindar` install component contains the library, selected public headers and generated `lnd_modules.h`. There is no installed `find_package` configuration. Shared-library clients define `LND_SHARED` for import declarations. External shared vendors and compiler runtimes must also be deployed when used.

## Examples

`LND_BUILD_EXAMPLES=ON` builds examples supported by the selected modules. Target names match directories under [examples](../examples), except `bare_metal`, which uses the QEMU runner.

```sh
cmake --preset lindar_pitya -DLND_BUILD_EXAMPLES=ON
cmake --build --preset lindar_pitya --target render_file file_play
./build/lindar_pitya/examples/render_file build/lindar_pitya/tone.wav
./build/lindar_pitya/examples/file_play build/lindar_pitya/tone.wav
```

The generated reference contains the example index and sources. [tools/check_examples.rb](../tools/check_examples.rb) runs supported examples with local fixtures and null devices.
