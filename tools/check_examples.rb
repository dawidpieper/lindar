require 'fileutils'
require 'open3'
require 'socket'

$stdout.sync = true
root = ENV.fetch('LND_WORKSPACE') { File.file?('CMakeLists.txt') ? Dir.pwd : File.expand_path('..', __dir__) }
build = File.expand_path(ARGV.fetch(0, 'build/debug'), root)
manifest = ARGV[1] ? File.join(build, "examples-#{ARGV[1]}.txt") : Dir[File.join(build, 'examples-*.txt')].then { |files| files.one? ? files.first : nil }
abort 'Usage: ruby tools/check_examples.rb build-directory [configuration]' unless manifest && File.file?(manifest)
examples = Dir[File.join(root, 'examples/*/*/main.c')]
abort 'Expected 30 to 50 examples' unless (30..50).cover?(examples.size)
abort 'Duplicate example names' unless examples.map { |f| File.basename(File.dirname(f)) }.uniq.size == examples.size
abort 'Unexpected example category' unless Dir[File.join(root, 'examples/*')].all? { |f| File.directory?(f) && File.basename(f).match?(/\A(?:mcu|os|os-windows|os-osx|os-linux|os-android|os-ios)\z/) }
abort 'Per-example README found' if Dir[File.join(root, 'examples/**/*')].any? { |f| File.basename(f).match?(/\Areadme(?:\.|\z)/i) }
work = File.join(build, 'examples/results')
config = File.basename(manifest).delete_prefix('examples-').delete_suffix('.txt')
fixture = File.join(build, 'tests', config, 'lindar_vst3_fixture.vst3')
fixture = File.join(build, 'tests/lindar_vst3_fixture.vst3') unless File.exist?(fixture)
FileUtils.mkdir_p(work)
samples = ([0, 8192, 0, -8192] * 200).pack('s<*')
fmt = [1, 1, 8000, 16000, 2, 16].pack('vvVVvv')
info = 'INFOINAM' + [8].pack('V') + "Example\0"
body = 'WAVEfmt ' + [fmt.bytesize].pack('V') + fmt + 'LIST' + [info.bytesize].pack('V') + info + 'data' + [samples.bytesize].pack('V') + samples
wav = 'RIFF' + [body.bytesize].pack('V') + body
input = File.join(work, 'input.wav')
File.binwrite(input, wav)
modules = File.read(File.join(build, 'generated/lnd_modules.h'))
has = ->(name) { modules.match?(/^#define LND_MODULE_#{name.upcase} 1$/) }
args = %w[manual sine_mcu pcm_convert pcm_layout source_memory source_callback transport stream_mcu http_mcu adpcm_mcu opus_mcu
          manual_graph planar graph_mixer graph_splitter graph_channels notifications notifications_realtime slide_autofree varispeed effects
          stretch soundtouch analysis_levels analysis_fft metadata_edit output_callback decode_push monitor].to_h { |name| [name, []] }
args.merge!('render_file' => [File.join(work, 'rendered.wav')], 'record' => [File.join(work, 'recorded.wav'), '0.05', '--null'],
            'convert' => [input, File.join(work, 'converted.wav')], 'file_play' => [input, '--null'], 'metadata' => [input],
            'devices' => ['--null'], 'device_events' => ['--null'], 'device_tone' => ['--null'],
            'midi_synth' => [File.join(root, 'vendor/tinysoundfont/examples/florestan-subset.sf2')],
            'vst3_host' => [fixture])
server = nil
worker = nil
failures = []
passed = skipped = 0
begin
    File.readlines(manifest, chomp: true).reject(&:empty?).each do |exe|
        name = File.basename(exe).delete_suffix('.exe')
        reason = case name
                 when 'asio_duplex' then 'requires an ASIO driver'
                 when 'vst3_host' then 'test VST3 fixture is not built' unless File.exist?(args[name][0])
                 when 'metadata' then 'WAVE metadata parser is disabled' unless has.call('metadata_wave')
                 when 'convert' then 'WAV decoder or encoder is disabled' unless has.call('wav_decoder') && has.call('wav_encoder')
                 when 'file_play', 'httpplay' then 'null backend or WAV decoder is disabled' unless has.call('null') && has.call('wav_decoder')
                 when 'record', 'devices', 'device_events', 'device_tone' then 'null backend is disabled' unless has.call('null')
                 end
        if reason
            puts "SKIP #{name}: #{reason}"
            skipped += 1
            next
        end
        if name == 'httpplay'
            server = TCPServer.new('127.0.0.1', 0)
            args[name] = ['--null', "http://127.0.0.1:#{server.addr[1]}/input.wav"]
            worker = Thread.new do
                loop do
                    client = server.accept
                    begin
                        request = client.gets
                        while (line = client.gets) && line != "\r\n"; end
                        client.write("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: #{wav.bytesize}\r\nConnection: close\r\n\r\n")
                        client.write(wav) unless request&.start_with?('HEAD ')
                    ensure
                        client.close
                    end
                end
            end
        end
        abort "No smoke scenario for #{name}" unless args.key?(name)
        Open3.popen2e(exe, *args[name], chdir: work) do |stdin, output, wait|
            stdin.close
            reader = Thread.new { output.read }
            timed_out = !wait.join(30)
            if timed_out
                Process.kill('KILL', wait.pid)
                wait.join
            end
            text = reader.value
            File.write(File.join(work, "#{name}.log"), text)
            if !timed_out && wait.value.success?
                passed += 1
                puts "PASS #{name}"
            else
                failures << name
                puts "FAIL #{name}: #{timed_out ? 'timeout' : wait.value.exitstatus}\n#{text}"
            end
        end
    end
ensure
    worker&.kill&.join
    server&.close
end
puts "#{examples.size} examples; #{passed} passed, #{skipped} skipped, #{failures.size} failed"
exit(failures.empty? ? 0 : 1)
