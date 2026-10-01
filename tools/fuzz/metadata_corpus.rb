require 'fileutils'

destination = ARGV.fetch(0) { abort 'Usage: ruby tools/fuzz/metadata_corpus.rb output-directory' }
FileUtils.mkdir_p(destination)
File.binwrite(File.join(destination, 'id3v1'), 'TAG' + 'Title'.ljust(30, "\0") + 'Artist'.ljust(30, "\0") + "\0" * 65)
def syncsafe(value)
  [value >> 21 & 127, value >> 14 & 127, value >> 7 & 127, value & 127].pack('C*')
end
def id3_frame(id, body)
  id.b + syncsafe(body.bytesize) + "\0\0".b + body
end
def riff_chunk(id, body)
  id.b + [body.bytesize].pack('V') + body + (body.bytesize.odd? ? "\0".b : ''.b)
end
[2, 3, 4].each do |version|
  body = "\x00Title".b
  frame = version == 2 ? 'TT2' + [0, 0, body.bytesize].pack('C*') : 'TIT2' + [body.bytesize].pack('N') + "\0\0"
  frame << body
  File.binwrite(File.join(destination, "id3v#{version}"), 'ID3' + [version, 0, 0].pack('C*') + syncsafe(frame.bytesize) + frame)
end
chapter = "intro\0".b + [1234, 2345, 0xffffffff, 0xffffffff].pack('N4') + id3_frame('TIT2', "\x03Chapter".b)
frames = id3_frame('CHAP', chapter) + id3_frame('CTOC', "root\0\x03\x01intro\0".b)
frames << id3_frame('APIC', "\0image/png\0\x03\0\x89PNG".b)
tag = "ID3\x04\0\0".b + syncsafe(frames.bytesize) + frames
File.binwrite(File.join(destination, 'id3_chapters'), tag)
comments = ['TITLE=Example', 'ARTIST=Artist', 'CHAPTER000=00:00:00.000', 'CHAPTER000NAME=Chapter']
opus = 'OpusTags' + [6].pack('V') + 'Lindar' + [comments.size].pack('V')
comments.each { |value| opus << [value.bytesize].pack('V') << value }
File.binwrite(File.join(destination, 'opus_tags'), opus)
wave = riff_chunk('fmt ', [1, 1, 48000, 96000, 2, 16].pack('vvVVvv'))
wave << riff_chunk('LIST', 'INFO' + riff_chunk('INAM', "Title\0"))
wave << riff_chunk('cue ', [1, 7, 12000].pack('V3') + 'data' + [0, 0, 12000].pack('V3'))
adtl = 'adtl' + riff_chunk('labl', [7].pack('V') + "Label\0")
adtl << riff_chunk('note', [7].pack('V') + "Note\0")
adtl << riff_chunk('ltxt', [7, 24000].pack('V2') + 'rgn ' + [0, 0, 0, 1252].pack('v4') + "Description\0")
wave << riff_chunk('LIST', adtl)
wave << riff_chunk('id3 ', tag)
wave << riff_chunk('bext', "\0" * 602)
wave << riff_chunk('data', "\0" * 256)
File.binwrite(File.join(destination, 'wave_tags'), 'RIFF' + [wave.bytesize + 4].pack('V') + 'WAVE' + wave)
%w[tone.wav tone.opus tone.mp3 mcu.opus].each do |name|
  path = File.expand_path("../../tests/audiosamples/#{name}", __dir__)
  FileUtils.cp(path, destination) if File.file?(path)
end
