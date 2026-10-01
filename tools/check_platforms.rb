require 'fileutils'
require 'open3'

root = ENV.fetch('LND_WORKSPACE') { File.expand_path('..', __dir__) }
profile = File.expand_path(ARGV.shift || abort('Usage: ruby tools/check_platforms.rb build-directory [devices metadata http asio]'), root)
groups = ARGV.empty? ? %w[devices metadata http asio] : ARGV
abort 'Unknown platform check' unless (groups - %w[devices metadata http asio]).empty?
header = File.read(File.join(profile, 'generated/lnd_modules.h'))
clang = ENV.fetch('CLANG', 'clang')
out = File.join(profile, 'tests/platforms')
FileUtils.mkdir_p(out)
includes = %w[include . modules tests/platforms/stubs].flat_map { |path| ['-I', File.join(root, path)] }
includes += ['-I', File.join(profile, 'generated')]
common = ['-std=c23', '-DLND_USE_LIBC_ALLOC=1', '-Wall', '-Wextra', '-Werror',
          '-Wno-unused-parameter', '-Wno-missing-field-initializers', *includes]
platforms = {
  'macos-x64' => ['x86_64-apple-macos12', 0],
  'macos-arm64' => ['arm64-apple-macos12', 0],
  'ios-arm64' => ['arm64-apple-ios14', 1],
  'ios-simulator-arm64' => ['arm64-apple-ios14-simulator', 1],
  'ios-simulator-x64' => ['x86_64-apple-ios14-simulator', 1]
}

def run(*args)
  stdout, stderr, status = Open3.capture3(*args)
  print stdout
  warn stderr unless stderr.empty?
  abort "Failed: #{args.join(' ')}" unless status.success? && !stderr.include?('warning:')
end
analyse = lambda do |sources, flags|
  sources.each do |file|
    run(clang, *common, *flags, '-fsyntax-only', file)
    run(clang, *common, *flags, '--analyze', '-Xanalyzer', '-analyzer-output=text', file)
  end
end
compile = lambda do |name, target, ios, sources, flags|
  directory = File.join(out, name)
  FileUtils.mkdir_p(directory)
  sources.each do |file|
    run(clang, *common, *flags, "--target=#{target}", "-DLND_TEST_IOS=#{ios}", '-ffreestanding', '-nostdlibinc',
        '-isystem', File.join(root, 'tests/platforms/stubs/libc'), '-c', file,
        '-o', File.join(directory, file.delete_prefix(root + '/').tr('/\\:', '_') + '.o'))
  end
end
os = %w[-DLND_OS_MODE=1 -DLND_THREADS=1]
no_os = %w[-DLND_OS_MODE=0 -DLND_THREADS=0]
groups.each do |group|
  module_name = group == 'devices' ? 'DEVICES' : group.upcase
  unless header.include?("#define LND_MODULE_#{module_name} 1\n")
    puts "#{group}: skipped (module disabled)"
    next
  end
  case group
  when 'devices'
    codec = File.join(root, 'modules/formats/codecs/audiotoolbox/decoder.c')
    backend = File.join(root, 'modules/io/devices/coreaudio/audiounit.c')
    sources = %w[modules/formats/codecs/mediacodec/decoder.c modules/io/devices/aaudio/aaudio.c].map { |p| File.join(root, p) }
    sources << codec
    %w[macos ios].each do |platform|
      file = File.join(out, "#{platform}.c")
      File.write(file, "#include \"src/platform.h\"\n#undef LND_OS_MACOS\n#undef LND_OS_IOS\n#define LND_OS_MACOS #{platform == 'macos' ? 1 : 0}\n#define LND_OS_IOS #{platform == 'ios' ? 1 : 0}\n#undef LND_MODULE_IOS\n#define LND_MODULE_IOS #{platform == 'ios' ? 1 : 0}\n#include \"io/devices/coreaudio/audiounit.c\"\n")
      sources << file
    end
    analyse.call(sources, os)
    platforms.each do |name, (target, ios)|
      files = [backend, codec]
      files << File.join(root, 'modules/io/devices/ios/session.c') if ios == 1
      compile.call(name, target, ios, files, os)
      if ios == 1
        native = File.join(root, 'modules/io/devices/ios/native.m')
        run(clang, "--target=#{target}", '-fobjc-arc', '-fsyntax-only', '-Wall', '-Wextra', '-Werror', *includes, native)
        run(clang, "--target=#{target}", '-fobjc-arc', '-c', *includes, native, '-o', File.join(out, name, 'session-native.o'))
      end
    end
  when 'metadata'
    sources = (Dir.glob(File.join(root, 'modules/metadata/**/*.c')) +
               %w[modules/utility/text/text.c modules/formats/riff/read.c].map { |p| File.join(root, p) }).sort
    ogg = File.join(out, 'headers/ogg')
    FileUtils.mkdir_p(ogg)
    FileUtils.cp(File.join(profile, 'generated/ogg/ogg/config_types.h'), File.join(ogg, 'os_types.h'))
    flags = no_os + ['-I', File.dirname(ogg), '-I', File.join(root, 'vendor/ogg/include')]
    analyse.call(sources, flags)
    platforms.merge('mcu-armv6m' => ['armv6m-none-eabi', 0]).each do |name, (target, ios)|
      compile.call(name, target, ios, sources, flags)
    end
  when 'http'
    sources = %w[modules/formats/decode/decode.c modules/utility/text/text.c modules/metadata/id3/id3.c modules/formats/mp4/read.c].map { |p| File.join(root, p) } +
              Dir.glob(File.join(root, 'modules/formats/demux/*.c')) +
              Dir.glob(File.join(root, 'modules/network/http/{,http_hls/,http_mp4/,http_packets/,http_file/}*.c'))
    flags = %w[-DLND_HTTP_CURL=0 -DLND_HTTP_AES128=0 -DLND_HTTP_HLS=1 -DLND_HTTP_LL_HLS=1 -DLND_HTTP_ICY=1 -DLND_HTTP_MP4=1]
    analyse.call(sources, flags + no_os)
    platforms.each { |name, (target, ios)| compile.call(name, target, ios, sources, flags + os) }
  when 'asio'
    analyse.call(Dir.glob(File.join(root, 'modules/io/devices/asio/*.c')).sort, os + ['-I', File.join(root, 'vendor')])
  end
  puts "#{group}: host analysis and applicable cross-compilation passed (mock headers, no platform linking)"
end
