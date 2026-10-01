# Dependency revisions

| Directory | Project | Release | Commit | Licence | Use |
| --- | --- | --- | --- | --- | --- |
| `bungee` | [Bungee](https://github.com/bungee-audio-stretch/bungee) | [v2.4.30](https://github.com/bungee-audio-stretch/bungee/releases/tag/v2.4.30) | `8cb6977d0c1a1b411ac320493b3c7f5182ed2d22` | MPL 2.0 | Time stretching; Eigen and PFFFT are pinned by Bungee. |
| `c-ares` | [c-ares](https://github.com/c-ares/c-ares) | [v1.34.8](https://github.com/c-ares/c-ares/releases/tag/v1.34.8) | `c7a3138dcfe3bb0eaaf10c0c24c36dc66dc790ab` | MIT | Asynchronous DNS for curl. |
| `cacert` | [Mozilla CA bundle](https://github.com/bagder/ca-bundle) | 2026-09-03 | `ab325adf04921579c89f77559fce20f964695ca9` | MPL 2.0 | Published bundle dated 2026-09-03, from curl's backup repository. |
| `curl` | [curl](https://github.com/curl/curl) | [curl-8_22_0](https://github.com/curl/curl/releases/tag/curl-8_22_0) | `01346829096c61b372692f6dc43ffa778c6caccd` | curl licence | HTTP/HTTPS transport. |
| `cwasio` | [cwASIO](https://github.com/s13n/cwASIO) | commit | `421ac19b6ecf2c70bf58e024f1d574a36000ca1f` | MIT | ASIO types; explicit commit, no upstream releases. |
| `fdk-aac` | [FDK AAC](https://github.com/mstorsjo/fdk-aac) | [v2.0.3](https://github.com/mstorsjo/fdk-aac/releases/tag/v2.0.3) | `716f4394641d53f0d79c9ddac3fa93b03a49f278` | Fraunhofer FDK AAC (NOTICE) | AAC decoding and encoding. |
| `ffmpeg` | [FFmpeg](https://github.com/FFmpeg/FFmpeg) | [n9.0.2](https://github.com/FFmpeg/FFmpeg/releases/tag/n9.0.2) | `946fcce07b6dcd0331c8cc609192aeff5e1924f8` | LGPL 2.1+ with the default build | Audio codecs, containers and streaming. |
| `flac` | [FLAC](https://github.com/xiph/flac) | [1.5.0](https://github.com/xiph/flac/releases/tag/1.5.0) | `1507800de4b70e21be71f38caa0d9079d0bc6e45` | BSD 3-Clause for libFLAC | FLAC decoding and encoding. |
| `lame` | [LAME](https://github.com/enzo1982/lame) | [RELEASE__4_0](https://github.com/enzo1982/lame/releases/tag/RELEASE__4_0) | `5a2d347159f9e6e441ee436407949e122ae802dc` | LGPL 2.0+ | MP3 encoding; release mirror maintained by Robert Kausch. |
| `minimp3` | [minimp3](https://github.com/lieff/minimp3) | commit | `ea99364f61c14656440e8d77e9c233ccf3124633` | CC0 1.0 | MP3 decoding; explicit commit, no upstream releases. |
| `ogg` | [libogg](https://github.com/xiph/ogg) | [v1.3.6](https://github.com/xiph/ogg/releases/tag/v1.3.6) | `be05b13e98b048f0b5a0f5fa8ce514d56db5f822` | BSD 3-Clause | Ogg framing. |
| `openssl` | [OpenSSL](https://github.com/openssl/openssl) | [openssl-4.0.3](https://github.com/openssl/openssl/releases/tag/openssl-4.0.3) | `af1775b60dfa141a4ad762585052cabeb9f37e9e` | Apache 2.0 | Optional curl TLS backend. |
| `opus` | [Opus](https://github.com/xiph/opus) | [v1.6.1](https://github.com/xiph/opus/releases/tag/v1.6.1) | `22244de5a79bd1d6d623c32e72bf1954b56235be` | BSD 3-Clause | Opus decoding and encoding. |
| `opusfile` | [opusfile](https://github.com/xiph/opusfile) | [v0.12](https://github.com/xiph/opusfile/releases/tag/v0.12) | `a55c164e9891a9326188b7d4d216ec9a88373739` | BSD 3-Clause | Ogg Opus file decoding. |
| `soundtouch` | [SoundTouch](https://codeberg.org/soundtouch/soundtouch) | [2.4.1](https://codeberg.org/soundtouch/soundtouch/src/tag/2.4.1) | `0047e0b1ecfceb041348579119bf79b73a322a3a` | LGPL 2.1+ | Tempo, pitch and rate processing. |
| `speex` | [Speex](https://github.com/xiph/speex) | [Speex-1.2.1](https://github.com/xiph/speex/releases/tag/Speex-1.2.1) | `5dceaaf3e23ee7fd17c80cb5f02a838fd6c18e01` | BSD 3-Clause | Speex decoding and encoding. |
| `tinysoundfont` | [TinySoundFont](https://github.com/schellingb/TinySoundFont) | commit | `853a0a171759f1ddba0de1442133a75912bbeffa` | MIT (TSF), zlib (TML) | SoundFont and MIDI; explicit commit, no upstream releases. |
| `vorbis` | [Vorbis](https://github.com/xiph/vorbis) | [v1.3.7](https://github.com/xiph/vorbis/releases/tag/v1.3.7) | `0657aee69dec8508a0011f47f3b69d7538e9d262` | BSD 3-Clause | Vorbis decoding and encoding. |
| `vst3_pluginterfaces` | [VST3 interfaces](https://github.com/steinbergmedia/vst3_pluginterfaces) | [v3.8.1_build_84](https://github.com/steinbergmedia/vst3_pluginterfaces/releases/tag/v3.8.1_build_84) | `4f547e8e102b47de4a8b8aaf343c73b700786372` | MIT | VST3 host interfaces. |

Bungee's release pins its own dependencies. Keep these gitlinks unchanged when using that release:

| Directory under `bungee` | Origin | Commit | Licence |
| --- | --- | --- | --- |
| `submodules/eigen` | [Eigen](https://gitlab.com/libeigen/eigen) | `c29c800126982c561e8d0b9255dc65474cd98de3` | MPL 2.0; see COPYING.README for other files. |
| `submodules/pffft` | [PFFFT](https://bitbucket.org/jpommier/pffft) | `02fe7715a5bf8bfd914681c53429600f94e0f536` | BSD-style; FFTPACK public domain. |
| `submodules/cxxopts` | [cxxopts](https://github.com/jarro2783/cxxopts) | `4bf61f08697b110d9e3991864650a405b3dd515d` | MIT; not built by Lindar. |

Source selection and compiler settings belong to [cmake/vendor.cmake](../cmake/vendor.cmake), its helpers and module setup files. Bungee and SoundTouch use existing build-copy adapters. The Opusfile adapter excludes stdio convenience entry points in builds without an OS; the upstream checkout stays unchanged. VST3's build copy excludes Git metadata.

See [dependency maintenance](../docs/build.md#dependencies) for initialisation and manual updates. Upstream licence files are authoritative. The CA bundle includes its Mozilla source data and retains a separate [MPL 2.0 licence](cacert.LICENSE). Application redistribution must satisfy the licences of the selected libraries, including any applicable relinking requirements.
