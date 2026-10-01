require 'fileutils'
require 'open3'
root = ENV.fetch('LND_WORKSPACE') { File.expand_path('..', __dir__) }
out = File.join(root, 'build/audit-profiles')
FileUtils.mkdir_p(out)
cc = ENV.fetch('CC', 'gcc')
cxx = ENV.fetch('CXX', 'g++')
base = ['-G', 'Ninja', "-DCMAKE_C_COMPILER=#{cc}", "-DCMAKE_CXX_COMPILER=#{cxx}", '-DCMAKE_BUILD_TYPE=Release',
        '-DLND_MINIMAL=ON', '-DLND_OS_MODE=OFF', '-DLND_THREADS=OFF', '-DLND_LTO=OFF',
        '-DLND_BUILD_TESTS=ON', '-DLND_BUILD_EXAMPLES=ON']
def run(log, *args, success: true)
  stdout, stderr, status = Open3.capture3(*args)
  File.write(log, stdout + stderr)
  abort "#{args.join(' ')}\n#{stdout}\n#{stderr}" unless status.success? == success
  stdout + stderr
end
profiles = {
  'minimal' => ['-DLND_USE_LIBC_ALLOC=OFF'],
  'graph-pcm' => ['-DLND_MODULE_GRAPH=ON', '-DLND_MODULE_CODECS=OFF', '-DLND_MODULE_IO=OFF'],
  'graph-codecs' => ['-DLND_MODULE_GRAPH=ON', '-DLND_MODULE_CODECS=ON'],
  'devices-null' => ['-DLND_OS_MODE=ON', '-DLND_THREADS=ON', '-DLND_MODULE_DEVICES=ON', '-DLND_MODULE_NULL=ON',
                     '-DLND_MODULE_CODECS=OFF', '-DLND_MODULE_IO=OFF'],
  'file-decoder' => ['-DLND_OS_MODE=ON', '-DLND_MODULE_FILES=ON', '-DLND_MODULE_WAV=ON', '-DLND_BUILD_ENCODERS=OFF'],
  'file-encoder' => ['-DLND_OS_MODE=ON', '-DLND_MODULE_FILES=ON', '-DLND_MODULE_WAV=ON', '-DLND_BUILD_DECODERS=OFF'],
  'midi' => ['-DLND_MODULE_MIDI=ON'],
  'speex' => ['-DLND_MODULE_SPEEX=ON', '-DLND_MODULE_DECODE=ON'],
  'metadata' => ['-DLND_MODULE_METADATA_ID3V2=ON', '-DLND_MODULE_METADATA_IO=ON'],
  'http-custom' => ['-DLND_MODULE_HTTP=ON', '-DLND_HTTP_TRANSPORT=CUSTOM', '-DLND_MODULE_WAV=ON', '-DLND_BUILD_ENCODERS=OFF',
                    '-DLND_HTTP_AES128=OFF']
}
profiles.merge!(
  'c-only' => ['-DLND_MODULE_SLIDE=ON', '-DCMAKE_CXX_COMPILER=deliberately-unavailable-cxx'],
  'soundtouch' => ['-DLND_MODULE_SLIDE=ON', '-DLND_MODULE_SOUNDTOUCH=ON', '-DLND_MODULE_BUNGEE=OFF'],
  'bungee' => ['-DLND_MODULE_SLIDE=ON', '-DLND_MODULE_BUNGEE=ON', '-DLND_MODULE_SOUNDTOUCH=OFF'],
  'stretch-scalar' => ['-DLND_MODULE_SLIDE=ON', '-DLND_MODULE_BUNGEE=ON', '-DLND_MODULE_SOUNDTOUCH=ON', '-DLND_BUNGEE_SIMD=OFF']
)
resolution_only = ARGV.delete('--resolution-only')
abort 'Usage: ruby tools/check_build_profiles.rb [--resolution-only] [profile ...]' unless (ARGV - profiles.keys).empty?
profiles.select! { |name, _| ARGV.include?(name) } unless ARGV.empty?
profiles.clear if resolution_only
profiles.each do |name, args|
  dir = File.join(out, name)
  run(File.join(out, "#{name}-configure.log"), 'cmake', '--fresh', '-S', root, '-B', dir, *base, *args)
  run(File.join(out, "#{name}-build.log"), 'cmake', '--build', dir, '-j', '4')
  text = run(File.join(out, "#{name}-test.log"), 'ctest', '--test-dir', dir, '--output-on-failure', '-j', '4')
  install = File.join(dir, 'tests/install')
  FileUtils.rm_f(Dir.glob(File.join(install, 'include/lindar*.h')))
  FileUtils.rm_f(File.join(install, 'include/lnd_modules.h'))
  run(File.join(out, "#{name}-install.log"), 'cmake', '--install', dir, '--prefix', install, '--component', 'Lindar')
  headers = Dir.glob(File.join(install, 'include/*.h'))
  %w[c cpp].each do |language|
    source = File.join(dir, "tests/header.#{language}")
    headers.each do |header|
      File.write(source, "#include \"#{File.basename(header)}\"\n")
      run(File.join(out, "#{name}-headers.log"), language == 'c' ? cc : cxx, language == 'c' ? '-std=c23' : '-std=c++17',
          '-Wall', '-Wextra', '-Werror', '-fsyntax-only', '-I', File.join(install, 'include'), source)
    end
  end
  if name == 'c-only'
    abort 'C++ was enabled in the C-only profile' if Dir.glob(File.join(dir, 'CMakeFiles/*/CMakeCXXCompiler.cmake')).any?
    abort 'Vendor leaked into C-only build' if File.read(File.join(dir, 'build.ninja')).match?(/\.dir\/vendor\/(?:bungee|soundtouch)\//)
  end
  if name == 'minimal'
    abort 'Minimal installation contains module headers' unless headers.map { |path| File.basename(path) }.sort == %w[lindar.h lnd_modules.h]
    header = File.read(File.join(dir, 'generated/lnd_modules.h'))
    abort 'Minimal base depends on modules' unless header.include?("#define LND_MODULES_COUNT 0\n")
    abort 'Core is exposed as an extension' if header.match?(/^#define LND_MODULE_CORE /)
    run(File.join(out, 'minimal-render.log'), File.join(dir, "examples/sine_mcu#{RUBY_PLATFORM.match?(/mswin|mingw/) ? '.exe' : ''}"))
  end
  if %w[graph-pcm devices-null].include?(name)
    header = File.read(File.join(dir, 'generated/lnd_modules.h'))
    %w[CODECS IO].each { |mod| abort "#{name}: unnecessary #{mod}" unless header.include?("#define LND_MODULE_#{mod} 0\n") }
    public_headers = File.read(File.join(dir, 'generated/public-headers.txt'))
    abort "#{name}: unnecessary codec/I/O headers" if public_headers.match?(/lindar_(?:codecs|io)\.h/)
  end
  abort 'Graph leaked into file-only profile' if name.start_with?('file-') && File.read(File.join(dir, 'generated/lnd_modules.h')).include?("#define LND_MODULE_GRAPH 1\n")
  puts "#{name}: built, tested, installed; #{headers.size * 2} standalone header checks; #{text[/\d+% tests passed[^\n]*/]}"
end
dir = File.join(out, 'http-cache')
run(File.join(out, 'cache-configure.log'), 'cmake', '--fresh', '-S', root, '-B', dir, *base,
    '-DLND_MODULE_HTTP=ON', '-DLND_HTTP_TRANSPORT=CUSTOM', '-DLND_HTTP_AES128=OFF')
[
 ['-DLND_MODULE_HTTP_HLS=OFF', { 'HTTP_HLS' => 0, 'HTTP_CURL' => 0 }],
 ['-DLND_MODULE_HTTP_HLS=ON', { 'HTTP_HLS' => 1 }],
 ['-DLND_OS_MODE=ON', '-DLND_HTTP_TRANSPORT=CURL', { 'HTTP_CURL' => 1 }],
 ['-DLND_HTTP_TRANSPORT=CUSTOM', { 'HTTP_CURL' => 0 }],
 ['-DLND_HTTP_HLS=OFF', '-DLND_MODULE_HTTP_HLS=AUTO', { 'HTTP_HLS' => 0 }]
].each_with_index do |entry, index|
  values = entry.pop
  run(File.join(out, "cache-#{index}.log"), 'cmake', '-S', root, '-B', dir, *entry)
  header = File.read(File.join(dir, 'generated/lnd_modules.h'))
  values.each { |key, value| abort "Cache mismatch: #{key}" unless header.include?("#define LND_MODULE_#{key} #{value}\n") }
end
{
 'disabled-dependency' => ['-DLND_MODULE_GRAPH=ON', '-DLND_MODULE_BUFFERS=OFF'],
 'os-required' => ['-DLND_MODULE_HTTP_CURL=ON'],
 'threads-required' => ['-DLND_OS_MODE=ON', '-DLND_MODULE_DEVICES=ON'],
 'wrong-platform' => ['-DLND_OS_MODE=ON', '-DLND_THREADS=ON', "-DLND_MODULE_#{RUBY_PLATFORM =~ /mswin|mingw/ ? 'ALSA' : 'WASAPI'}=ON"]
}.each do |name, args|
  dir = File.join(out, name)
  text = run(File.join(out, "#{name}.log"), 'cmake', '-S', root, '-B', dir, *base, *args, success: false)
  abort "Not a module diagnostic: #{name}" unless text.include?('Module ')
  abort "Vendor materialized before validation: #{name}" if Dir.exist?(File.join(dir, 'vendor'))
end
puts 'Reused cache, migration aliases, dependency and capability rejection passed'

order_source = File.join(out, 'order-source')
FileUtils.mkdir_p(order_source)
File.write(File.join(order_source, 'CMakeLists.txt'), <<~CMAKE)
  cmake_minimum_required(VERSION 3.21)
  project(lindar_order C)
  set(LND_MODULE_DEFAULT ON)
  set(LND_OS_MODE OFF)
  set(LND_THREADS OFF)
  set(LND_BUILD_DECODERS ON)
  set(LND_BUILD_ENCODERS ON)
  set(LND_MODULE_HTTP ON CACHE STRING "")
  set(LND_HTTP_TRANSPORT CUSTOM CACHE STRING "")
  include("#{root}/cmake/modules.cmake")
  include("#{root}/cmake/options.cmake")
  file(GLOB_RECURSE manifests "#{root}/modules/*/module.cmake")
  list(SORT manifests)
  if(REVERSE)
      list(REVERSE manifests)
  endif()
  foreach(manifest ${manifests})
      include(${manifest})
  endforeach()
  lnd_resolve_modules()
  list(SORT LND_MODULE_KNOWN)
  set(result "")
  foreach(name ${LND_MODULE_KNOWN})
      string(TOUPPER ${name} upper)
      string(APPEND result "${name}=${LND_MODULE_${upper}};${LND_MODULE_${upper}_DECODER};${LND_MODULE_${upper}_ENCODER}\\n")
  endforeach()
  list(SORT LND_MODULE_REQUIREMENTS)
  string(APPEND result "${LND_MODULE_REQUIREMENTS}\\n")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/resolved.txt" "${result}")
CMAKE
%w[OFF ON].each do |reverse|
  run(File.join(out, "order-#{reverse}.log"), 'cmake', '-S', order_source, '-B', File.join(out, "order-#{reverse}"),
      '-G', 'Ninja', "-DCMAKE_C_COMPILER=#{cc}", "-DREVERSE=#{reverse}")
end
abort 'Manifest order changed the resolved configuration' unless File.read(File.join(out, 'order-OFF/resolved.txt')) == File.read(File.join(out, 'order-ON/resolved.txt'))
puts 'All module declarations resolve identically in forward and reverse order'

