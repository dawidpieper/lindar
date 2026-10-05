# Lindar 1.0 release checklist

> **Provisional requirements**
>
> This document defines the current requirements for publishing Lindar 1.0.
> They may be expanded, reduced or otherwise changed at any time, without prior notice.

Status as of **5 October 2026**.

## Current status

**Done**: complete. **Partial**: some checks remain. **Pending**: not yet complete.
Physical checks use real hardware. Codec conversion covers every decoder and encoder supported on the target platform.

| Platform | Automated tests | Basic examples on hardware | Codec conversion |
| --- | --- | --- | --- |
| Windows x64 | Done | Partial | Done |
| Windows ARM64 | Done | Done | Done |
| Windows x86 | Done | Pending | Pending |
| Linux x64 | Done | Pending | Done |
| Linux ARM64 | Done | Partial | Done |
| Linux x86 | Pending | Pending | Pending |
| Linux ARM32 | Pending | Pending | Pending |
| macOS x64 | Pending | Pending | Pending |
| macOS ARM64 | Done | Done | Done |
| Android ARM64 | Done | Partial | Done |
| iOS | Done | Done | Done |
| Cortex-M0 | Done | Pending | N/A |
| Cortex-M33 | Done | Done | N/A |
| ATmega2560 | Done | Partial | N/A |

Automated test status covers all supported variants of each platform. See [test runners and tooling](testing.md).

### Integration and listening checks

These checks apply to every platform on which the feature is supported. Passing automated tests does not complete them.

- [ ] Fully test VST3 with production plugins.
- [ ] Fully test ASIO with hardware and drivers on Windows.
- [ ] Fully test MIDI with varied files and sound banks.
- [ ] Complete listening tests for all effects.
- [ ] Test broadcasting.

### Error handling and benchmarks

- [ ] Confirm complete error and exception handling, including resource cleanup when processing malformed files and streams.
- [ ] Collect and publish consolidated benchmarks.

### Loading and processing

- [ ] Review a consistent lazy-loading model for HTTP and filesystem sources.
- [ ] Review and optimise `sinc-asm`, particularly AVX-512, checking numerical results and throughput.

### Allocation and configuration

- [ ] Review libc allocator settings for the intended workloads.
- [ ] Define macros to simplify configuration where useful.

### Devices and recording

- [x] Check that mobile recording settings cover the intended use cases.
- [ ] Consider an optional Lindar module for managing recording permissions.
- [ ] Extend the ASIO API where justified by missing use cases.

### Formats and media

- [ ] Add encoders for further useful formats, including FLAC.
- [ ] Add audio CD support.
- [ ] Recognise audio streams in video files.

### Plugin support

- [ ] Define and document an API for dynamically loaded external Lindar plugins.
- [ ] Add Audio Unit (AU) hosting on macOS.
- [ ] Add CLAP hosting.

## Companion bindings

Separate deliverables from the C library, intended to be ready around the 1.0 release. Integration choices remain provisional.

- [ ] Ruby bindings; consider full native integration as a gem, without FFI.
- [ ] Python bindings, provisionally through FFI.
- [ ] C# bindings, provisionally through FFI.

Bindings for other languages are planned for later.
