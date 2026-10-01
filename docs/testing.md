# Development tools

## CI

GitHub Actions runs on Windows x64 (`windows-2025`), Linux x64 (`ubuntu-24.04`) and macOS ARM64 (`macos-15`). Each native job builds shared quanta once, then runs CTest, installed C/C++ clients, Ruby FFI and examples. A separate Linux ASan/UBSan job covers core, graph, DSP, effects, PCM, WAV/ADPCM, metadata and custom HTTP. Diagnostic reports fail the job.

PRs also check module/package definitions, Ruby syntax, workflow syntax and API documentation. Cortex-M0/M33 run in QEMU when core/MCU/build files change. Documentation-only changes skip native builds. Use the `result` job as the required check.

At 02:23 UTC, after a commit within the last 24 hours, CI additionally tests all four packages with static/shared linking, static quanta and all 14 custom profiles with GCC 14, and AVR. The GCC job also regenerates every audio fixture into an empty directory and reruns CTest against it. Tags matching `v*` and manual runs with `full` selected use the same scope. Push triggers cover `main` and `master`.

Compiler cache is capped at 256 MB per native job. FFmpeg/OpenSSL build directories are cached by gitlinks, build configuration, compiler and runner image; only the default branch saves caches. Older runs of the same PR/ref are cancelled. Packages, documentation and logs expire after one day. PRs upload reports and documentation; other native runs also upload shared-library packages, with all four packages in full runs. Static builds are tested but not distributed. No releases are published automatically.

Workflow: `.github/workflows/ci.yml`; shared commands: `tools/ci/run.rb`; pinned tool downloads: `tools/ci/download.rb`. `LND_CI_BUILD` selects an existing build directory for local runs; `LND_CI_OUTPUT` selects the report/package directory. Windows packages include compiler runtime DLLs; Linux packages require the system ALSA/GStreamer runtime.

## Tests

```sh
cmake --preset lindar_quanta -DLND_BUILD_TESTS=ON -DLND_BUILD_CXX_TESTS=ON
cmake --build --preset lindar_quanta --parallel
ctest --preset lindar_quanta --output-on-failure
```

| Command | Coverage |
| --- | --- |
| `ruby tools/check_modules.rb` | Layout, providers and dependency ordering. |
| `ruby tools/check_build_profiles.rb` | Build profiles, including C-only, SoundTouch, Bungee and scalar; optional profile names restrict builds, `--resolution-only` skips them. |
| `ruby tools/check_platforms.rb build-directory [devices metadata http asio]` | Clang analysis and cross-compilation with mock SDK headers. |
| `ruby tools/check_examples.rb build-directory [configuration]` | Examples with null devices, local HTTP and the test VST3 plugin; logs in `examples/results`. |
| `ruby tools/check_public_api.rb build-shared` | Installed shared-library clients and standalone C/C++ headers. |
| `ruby tools/check_ffi.rb path-to-library` | Ruby Fiddle calls. |

Build/API runners use `CC` and `CXX`. `LND_WORKSPACE` selects a local source alias where needed for shared filesystems. `tests/test_public_api.c` is shared by CMake's C/C++ clients and the installed-library runner. Runtime checks stay in C; Ruby drives builds, fixture generation and local servers.

Audio inputs are versioned in `tests/audiosamples`. Test binaries are written to `<build>/tests` and examples to `<build>/examples`. Building tests copies the audio inputs to `<build>/tests/audiosamples`, including when an individual test target is built. CTest runs in `<build>/tests`; generated output stays outside the source tree. No media tools are required for a normal clone/build/test cycle.

To regenerate the complete audio fixture set from synthetic PCM into an empty directory:

```sh
cmake --build build/lindar_quanta --target aac_tone
ruby tools/generate_samples.rb --aac-tone build/lindar_quanta/aac_tone all
cmake --preset lindar_quanta -DLND_TEST_AUDIO_DIR="$PWD/build/tests/audiosamples"
cmake --build --preset lindar_quanta --parallel
ctest --preset lindar_quanta --output-on-failure
```

Append `.exe` to `aac_tone` on Windows. Regeneration requires FFmpeg with LAME, Vorbis, Opus and Speex encoders, SoX, `speexdec`, and GStreamer with `avenc_alac`, `qtmux` and `jpegenc`. Override tools with `FFMPEG`, `SPEEXDEC`, `SOX`, `GST_LAUNCH` and `AAC_TONE`. `--output` changes the default `build/tests/audiosamples`; individual groups are `base`, `adpcm`, `extensions`, `bounds`, `he-aac`, `http` and `system`. HTTP/system generation creates its own base inputs. Encoded bytes may differ between tool versions; CI checks decoded results. `ruby tools/check_adpcm.rb <build>/tests/codec-results` compares encoder output with FFmpeg. HTTP fixture details: [tests/audiosamples/HTTP.md](../tests/audiosamples/HTTP.md).

Harnesses live in `tools/fuzz`; `ruby tools/fuzz/metadata_corpus.rb output-directory` generates metadata seeds. `LND_BUILD_FUZZERS=ON` requires Clang, UNIX, static linking and the libc allocator. `LND_FUZZ_SANITIZERS` defaults to `address,undefined`; `LND_FUZZ_CODEC` selects the decoder. Instrument dependencies for MemorySanitizer/ThreadSanitizer builds. Recover-mode UBSan diagnostics can coexist with a successful CTest result.

## Benchmarks

`tools/bench` contains the benchmarks. `LND_BUILD_BENCHMARKS=ON` enables targets for selected modules: `lindar_bench_kernels`, `lindar_bench_sinc`, `lindar_bench_runtime`, planar, effects, audit and control. At least graph, SIMD or sinc assembly must be enabled.

## Microcontrollers

`tools/check_qemu.rb` builds native reference tests, firmware and [bare_metal](../examples/mcu/bare_metal/main.c), then compares results. Profiles use core and queue without OS, workers or a default allocator.

| Profile | QEMU target |
| --- | --- |
| `m33` | mps2-an505 / Cortex-M33, soft-float |
| `m0` | microbit / Cortex-M0 |
| `avr` | mega2560 / ATmega2560 |

Requires Ruby, CMake, Ninja, a native C23 compiler, QEMU and GCC 14+ cross-compilers with newlib (ARM) or avr-libc. Cross-compiler and size tools must be in PATH.

```sh
ruby tools/check_qemu.rb --boards m33,m0
ruby tools/check_qemu.rb --boards avr --build /tmp/lindar-qemu
```

Default: all profiles. `--timeout` limits execution. Tool overrides: `CMAKE`, `NINJA`, `QEMU_ARM`, `QEMU_AVR`, `ARM_SIZE`, `AVR_SIZE`; `CC` selects the native compiler. On WSL, use a WSL-local build directory if the source share cannot support CMake's filesystem operations.

Results, logs, ELF images and maps go in the build directory. Memory checks enforce static image/RAM budgets and stack reserves, not peak stack use. Firmware fixtures also check failure reporting, hangs and ARM HardFault. QEMU does not validate audio peripherals or real-time deadlines.

`ruby tools/check_mcu.rb [--queue|--adpcm|--opus]` instead uses Clang/LLVM to check Cortex-M0+/M33 relocatable objects and runtime symbols without execution. The Opus profile still requires libc allocation.

## API reference

Requires Ruby 3.1+, `rexml`, CMake 3.21+ and Doxygen 1.10+. No compiler or Graphviz is needed for the standalone command:

```sh
ruby tools/docs.rb
```

Output: `build/docs/html/index.html`. `--output` changes the destination; `--cmake`/`--doxygen` or `CMAKE`/`DOXYGEN` select tools. `LND_BUILD_DOCS=ON` adds the CMake target `lindar_docs`, writing `<build>/docs`.
