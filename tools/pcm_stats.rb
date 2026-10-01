#!/usr/bin/env ruby

file, format, channels, rate = ARGV[0], ARGV[1] || "f32", (ARGV[2] || 2).to_i, (ARGV[3] || 48000).to_i
abort "usage: pcm_stats.rb file [f32|f64|s16|s32] [channels] [rate]" unless file

data = File.binread(file)
samples =
  case format
  when "f32" then data.unpack("e*")
  when "f64" then data.unpack("E*")
  when "s16" then data.unpack("s<*").map { |v| v / 32768.0 }
  when "s32" then data.unpack("l<*").map { |v| v / 2147483648.0 }
  else abort "unknown format #{format}"
  end

frames = samples.size / channels
puts "frames=#{frames} duration=#{'%.3f' % (frames.to_f / rate)}s channels=#{channels} rate=#{rate} format=#{format}"

first_active = (0...frames).find { |f| channels.times.any? { |c| samples[f * channels + c].abs > 1e-4 } }
last_active = (0...frames).reverse_each.find { |f| channels.times.any? { |c| samples[f * channels + c].abs > 1e-4 } }
puts "active: #{first_active.inspect}..#{last_active.inspect} (#{first_active && last_active ? '%.3f' % ((last_active - first_active + 1).to_f / rate) : '0'}s)"

lo = first_active || 0
hi = last_active || -1
channels.times do |c|
  ch = (lo..hi).map { |f| samples[f * channels + c] }
  frames = ch.size
  sum = ch.sum { |v| v * v }
  rms = frames.zero? ? 0.0 : Math.sqrt(sum / frames)
  peak = ch.map(&:abs).max || 0.0
  dc = frames.zero? ? 0.0 : ch.sum / frames
  crossings = (1...frames).select { |i| ch[i - 1] < 0 && ch[i] >= 0 }
  freq = crossings.size > 2 ? (crossings.size - 1) * rate.to_f / (crossings.last - crossings.first) : 0.0
  dbfs = rms > 0 ? 20 * Math.log10(rms) : -Float::INFINITY
  puts format("ch%d: rms=%.4f (%.1f dBFS) peak=%.4f dc=%.5f freq=%.2f Hz", c, rms, dbfs, peak, dc, freq)
end
