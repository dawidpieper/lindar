# HTTP test fixtures

The HTTP/HLS fixtures derive from generated WAV, AAC, Opus and FLAC tones.

Run `ruby tools/generate_samples.rb http` to generate into `build/tests/audiosamples`. This recreates the base inputs, then writes the ALAC/AC3 references and MPEG-TS/fMP4 HLS sets using FFmpeg. The HLS files copy encoded packets, so comparing HLS output with the corresponding file decoder tests packet boundaries, timestamps and trim without introducing a second lossy encode.

The Ruby HTTP server generates its local TLS certificate, AES-128 key and encrypted segments at test time. It also inserts an 8 MiB free box before a trailing MP4 moov, preserving audio offsets. The range test verifies that the client does not download that padding, even with a 4 KiB compressed-input limit. All tests run against local or in-memory transports.
