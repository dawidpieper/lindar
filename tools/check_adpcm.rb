require 'open3'

root = ARGV.fetch(0) { abort 'Usage: ruby tools/check_adpcm.rb build-directory/tests/codec-results' }
files = Dir[File.join(root, 'lindar-*.wav')]
abort 'Run the ADPCM encoder tests first.' if files.empty?
files.each do |wave|
  reference = wave.sub('.wav', '-ffmpeg.s16')
  output, status = Open3.capture2e(ENV.fetch('FFMPEG', 'ffmpeg'), '-v', 'error', '-y', '-i', wave, '-f', 's16le', reference)
  abort output unless status.success?
  expected = File.binread(wave.sub('.wav', '.s16'))
  actual = File.binread(reference)
  abort "Decoded PCM differs: #{wave}" unless actual.start_with?(expected)
  puts "#{File.basename(wave)}: #{expected.bytesize / 2} valid samples match FFmpeg"
end
