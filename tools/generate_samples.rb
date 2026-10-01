require 'fileutils'
require 'open3'
require 'optparse'

$stdout.sync = true
root = ENV.fetch('LND_WORKSPACE') { File.expand_path('..', __dir__) }
samples = File.join(root, 'build/tests/audiosamples')
aac_tone = ENV['AAC_TONE']
groups = %w[base adpcm extensions bounds he-aac http system]
parser = OptionParser.new do |p|
  p.banner = 'Usage: ruby tools/generate_samples.rb [--output directory] [--aac-tone executable] all|base|adpcm|extensions|bounds|he-aac|http|system [...]'
  p.on('--output DIRECTORY') { |path| samples = File.expand_path(path) }
  p.on('--aac-tone EXECUTABLE') { |path| aac_tone = File.expand_path(path) }
end
begin
  parser.parse!
rescue OptionParser::ParseError => error
  abort error.message
end
abort parser.to_s unless (ARGV - groups - ['all']).empty?
selected = ARGV.empty? || ARGV.include?('all') ? groups : ARGV
selected += ['base'] if (selected & %w[http system]).any?
if selected.include?('he-aac')
  abort 'HE-AAC requires --aac-tone executable; build the aac_tone CMake target first.' unless aac_tone
  aac_tone = File.expand_path(aac_tone)
  abort "Missing HE-AAC generator: #{aac_tone}" unless File.file?(aac_tone)
end
FileUtils.mkdir_p(samples)
ffmpeg = ENV.fetch('FFMPEG', 'ffmpeg')

def run(*args)
  output, status = Open3.capture2e(*args)
  abort "#{args.join(' ')}\n#{output}" unless status.success?
end
convert = ->(*args) { run(ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', *args) }
groups.select { |group| selected.include?(group) }.each do |group|
  Dir.chdir(samples) do
    case group
    when 'base'
      pcm = Array.new(88_200 * 2) do |i|
        frame, channel = i.divmod(2)
        (Math.sin(frame * 2 * Math::PI * (channel.zero? ? 440 : 660) / 44100) * 16383.5).round
      end.pack('s<*')
      format = [1, 2, 44100, 176400, 4, 16].pack('vvVVvv')
      wave = 'WAVEfmt ' + [format.bytesize].pack('V') + format + 'data' + [pcm.bytesize].pack('V') + pcm
      File.binwrite('tone.wav', 'RIFF' + [wave.bytesize].pack('V') + wave)
      # Noise substitution makes AAC seek comparisons depend on decoder history.
      aac = %w[-c:a aac -b:a 128k -aac_pns 0]
      {
        'tone.aiff' => %w[-c:a pcm_s16be],
        'tone.mp3' => %w[-c:a libmp3lame -b:a 192k],
        'tone.ogg' => %w[-c:a libvorbis -q:a 5],
        'tone.opus' => %w[-c:a libopus -b:a 96k],
        'tone.flac' => %w[-c:a flac],
        'tone.aac' => aac,
        'tone.m4a' => aac,
        'tone.wma' => %w[-c:a wmav2 -b:a 192k]
      }.each { |name, options| convert.call('-i', 'tone.wav', *options, name) }
      convert.call('-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=0.0626875',
                   '-c:a', 'libopus', '-b:a', '24k', 'mcu.opus')
    when 'bounds'
      FileUtils.mkdir_p('bounds')
      {'mono' => '1', 'stereo' => '2'}.each do |name, channels|
        {'aac' => 'aac', 'spx' => 'libspeex'}.each do |extension, codec|
          convert.call('-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=16000:duration=0.2',
                       '-ac', channels, '-c:a', codec, "bounds/#{name}.#{extension}")
        end
      end
    when 'he-aac'
      run(aac_tone, 'tone.he.aac')
    when 'adpcm'
      FileUtils.mkdir_p('adpcm')
      %w[ima ms].each do |variant|
        [1, 2].each do |channels|
          name = "adpcm/#{variant}#{channels}"
          pcm = Array.new(1003 * channels) do |i|
            frame, channel = i.divmod(channels)
            (Math.sin(frame * 2 * Math::PI * (channel == 0 ? 440 : 733) / 16000) * 22000).round
          end.pack('s<*')
          File.binwrite("#{name}-input.s16", pcm)
          codec = variant == 'ima' ? 'adpcm_ima_wav' : 'adpcm_ms'
          convert.call('-f', 's16le', '-ar', '16000', '-ac', channels.to_s, '-i', "#{name}-input.s16",
                       '-c:a', codec, '-block_size', (128 * channels).to_s, '-bitexact', "#{name}.wav")
          if variant == 'ima'
            run(ENV.fetch('SOX', 'sox'), "#{name}.wav", '-t', 'raw', '-e', 'signed-integer', '-b', '16', '-L', "#{name}-reference.s16")
          else
            convert.call('-i', "#{name}.wav", '-f', 's16le', "#{name}-reference.s16")
          end
        end
      end
    when 'extensions'
      tags = %w[-metadata title=Lindar -metadata artist=Test]
      chapters = %w[-metadata album=Album -metadata CHAPTER001=00:00:01.250 -metadata CHAPTER001NAME=Chapter]
      convert.call('-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=16000:duration=3', '-c:a', 'libspeex', *tags, 'tone.spx')
      convert.call('-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=4', '-c:a', 'libopus', *tags,
                   '-cluster_time_limit', '100', 'tone.webm')
      convert.call('-i', 'tone.webm', '-c:a', 'flac', 'tone.mka')
      convert.call('-i', 'tone.webm', '-c:a', 'wmav2', '-f', 'asf', 'stream.wma')
      {'ogg' => 'libvorbis', 'flac' => 'flac', 'opus' => 'libopus'}.each do |extension, codec|
        convert.call('-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=4',
                     '-c:a', codec, *tags, *chapters, "comments.#{extension}")
      end
      run(ENV.fetch('SPEEXDEC', 'speexdec'), '--quiet', 'tone.spx', 'speex-reference.wav')
    when 'http'
      convert.call('-i', 'tone.wav', '-c:a', 'alac', 'tone.http-alac.m4a')
      convert.call('-i', 'tone.wav', '-c:a', 'ac3', '-b:a', '192k', 'tone.http.ac3')
      {
        'hls-ts' => ['tone.aac', 'mpegts', 'ts'],
        'hls-mp4' => ['tone.aac', 'fmp4', 'm4s'],
        'hls-opus' => ['tone.opus', 'fmp4', 'm4s'],
        'hls-flac' => ['tone.flac', 'fmp4', 'm4s'],
        'hls-alac' => ['tone.http-alac.m4a', 'fmp4', 'm4s'],
        'hls-ac3' => ['tone.http.ac3', 'mpegts', 'ts']
      }.each do |directory, (input, type, extension)|
        FileUtils.mkdir_p(directory)
        FileUtils.rm_f(Dir.glob("#{directory}/seg*.{ts,m4s}") + ["#{directory}/init.mp4"])
        filter = input == 'tone.aac' && type == 'fmp4' ? ['-bsf:a', 'aac_adtstoasc'] : []
        convert.call('-i', input, '-map', '0:a:0', '-c:a', 'copy', '-strict', 'experimental', *filter,
                     '-hls_segment_options', 'strict=experimental', '-f', 'hls', '-hls_time', '0.6',
                     '-hls_playlist_type', 'vod', '-hls_segment_type', type,
                     '-hls_segment_filename', "#{directory}/seg%d.#{extension}", "#{directory}/index.m3u8")
      end
    when 'system'
      source = ['filesrc', 'location=tone.wav', '!', 'wavparse', '!', 'audioconvert']
      commands = [
        [*source, '!', 'avenc_alac', '!', 'qtmux', '!', 'filesink', 'location=tone.alac.m4a'],
        ['avimux', 'name=mux', '!', 'filesink', 'location=tone.video.avi',
         *source, '!', 'queue', '!', 'mux.', 'videotestsrc', 'num-buffers=10', '!',
         'video/x-raw,width=16,height=16,framerate=10/1', '!', 'jpegenc', '!', 'queue', '!', 'mux.']
      ]
      commands.each { |command| run(ENV.fetch('GST_LAUNCH', 'gst-launch-1.0'), '-q', *command) }
    end
  end
  puts "#{group}: fixtures generated in #{samples}"
end
