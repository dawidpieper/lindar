require 'fileutils'
require 'open3'

root = ENV.fetch('LND_WORKSPACE') { File.expand_path('..', __dir__) }
out = File.join(root, 'build/module-contracts-src')
source = File.join(out, 'source')
FileUtils.mkdir_p(source)
File.binwrite(File.join(source, 'CMakeLists.txt'), <<~CMAKE)
  cmake_minimum_required(VERSION 3.21)
  project(module_contracts NONE)
  set(LND_MODULE_DEFAULT OFF)
  option(LND_OS_MODE "" OFF)
  option(LND_THREADS "" OFF)
  option(LND_BUILD_DECODERS "" ON)
  option(LND_BUILD_ENCODERS "" ON)
  set(LND_HTTP_TRANSPORT CUSTOM)
  set(LND_HTTP_AES128 OFF CACHE BOOL "")
  include("#{root}/cmake/modules.cmake")
  include("#{root}/cmake/options.cmake")
  if(TEST_PLATFORM)
      foreach(platform WINDOWS APPLE IOS ANDROID LINUX)
          set(LND_PLATFORM_${platform} OFF)
      endforeach()
      set(LND_PLATFORM_${TEST_PLATFORM} ON)
    if(LND_PLATFORM_IOS)
        set(LND_PLATFORM_APPLE ON)
    endif()
      set(LND_ANDROID_API 26)
  endif()
  file(GLOB_RECURSE manifests "#{root}/modules/*/module.cmake")
  list(SORT manifests)
  if(REVERSE)
      list(REVERSE manifests)
  endif()
  foreach(manifest ${manifests})
      include(${manifest})
  endforeach()
  lnd_validate_module_layout("#{root}/modules")
  lnd_resolve_modules()
  list(SORT LND_MODULE_KNOWN)
  set(result "")
  foreach(name ${LND_MODULE_KNOWN})
      string(TOUPPER ${name} upper)
      string(APPEND result "${name}=${LND_MODULE_${upper}}\\n")
  endforeach()
  list(SORT LND_MODULE_REQUIREMENTS)
  string(APPEND result "${LND_MODULE_REQUIREMENTS}\\n")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/resolved.txt" "${result}")
CMAKE
cases = {
  'minimal' => [{}, [], %w[audio text graph]],
  'graph' => [{ 'GRAPH' => 'ON', 'CODECS' => 'OFF', 'IO' => 'OFF' }, %w[graph audio buffers pcm_float], %w[codecs io]],
  'decoder' => [{ 'AAC' => 'ON', 'AAC_ENCODER' => 'OFF' }, %w[aac mp4 codecs], %w[output audio]],
  'encoder' => [{ 'WAV' => 'ON', 'WAV_DECODER' => 'OFF' }, %w[wav output audio], %w[riff codecs]],
  'stretch' => [{ 'STRETCH' => 'ON' }, %w[stretch soundtouch graph audio], %w[bungee]],
  'soundtouch' => [{ 'SOUNDTOUCH' => 'ON' }, %w[stretch soundtouch], %w[bungee]],
  'bungee' => [{ 'BUNGEE' => 'ON' }, %w[stretch bungee], %w[soundtouch]],
  'fallback' => [{ 'STRETCH' => 'ON', 'SOUNDTOUCH' => 'OFF' }, %w[stretch bungee], %w[soundtouch]],
  'both' => [{ 'SOUNDTOUCH' => 'ON', 'BUNGEE' => 'ON' }, %w[stretch soundtouch bungee], []],
  'metadata' => [{ 'METADATA_IO' => 'ON' }, %w[metadata_io metadata io text], %w[riff]],
  'metadata-wave' => [{ 'METADATA_IO' => 'ON', 'METADATA_WAVE' => 'ON' }, %w[metadata_io metadata_wave riff], []],
  'metadata-opus-alias' => [{ 'METADATA_OPUS' => 'ON' }, %w[metadata_comments metadata text], %w[ogg]],
  'http' => [{ 'HTTP' => 'ON' }, %w[http http_hls http_mp4 http_packets demux mp4 audio], %w[http_curl]],
  'no-algorithm' => [{ 'STRETCH' => 'ON', 'SOUNDTOUCH' => 'OFF', 'BUNGEE' => 'OFF' }, 'requires one of'],
  'disabled-contract' => [{ 'SOUNDTOUCH' => 'ON', 'STRETCH' => 'OFF' }, 'requires stretch'],
  'disabled-dependency' => [{ 'GRAPH' => 'ON', 'BUFFERS' => 'OFF' }, 'requires buffers']
}
backends = %w[wasapi alsa aaudio coreaudio asio null]
device_options = { 'OS_MODE' => 'ON', 'THREADS' => 'ON', 'DEVICES' => 'ON', 'TEST_PLATFORM' => 'WINDOWS' }
{ 'WINDOWS' => 'wasapi', 'LINUX' => 'alsa', 'ANDROID' => 'aaudio', 'APPLE' => 'coreaudio', 'IOS' => 'coreaudio' }.each do |platform, backend|
  cases["devices-#{platform}"] = [device_options.merge('TEST_PLATFORM' => platform), %W[devices graph #{backend}], backends - [backend]]
end
cases['android-config'] = [device_options.merge('TEST_PLATFORM' => 'ANDROID', 'AAUDIO' => 'ON'), %w[aaudio android], %w[ios]]
cases['ios-config'] = [device_options.merge('TEST_PLATFORM' => 'IOS', 'COREAUDIO' => 'ON'), %w[coreaudio ios], %w[android aaudio]]
cases['android-config-off'] = [device_options.merge('TEST_PLATFORM' => 'ANDROID', 'AAUDIO' => 'ON', 'ANDROID' => 'OFF'), 'requires android']
cases['ios-config-off'] = [device_options.merge('TEST_PLATFORM' => 'IOS', 'COREAUDIO' => 'ON', 'IOS' => 'OFF'), 'requires ios']
%w[asio null].each do |backend|

  cases["devices-explicit-#{backend}"] = [device_options.merge(backend.upcase => 'ON'), %W[devices #{backend}], backends - [backend]]
end
disabled_backends = backends.to_h { |backend| [backend.upcase, 'OFF'] }
cases['devices-null-fallback'] = [device_options.merge(disabled_backends).merge('NULL' => 'AUTO'), %w[devices null], backends - ['null']]
cases['devices-no-backend'] = [device_options.merge(disabled_backends), 'requires one of']
cases.each do |name, (options, enabled, disabled)|
  results = %w[OFF ON].map do |reverse|
    dir = File.join(out, "#{name}-#{reverse}")
    args = options.map do |key, value|
      prefix = key == 'TEST_PLATFORM' ? '' : %w[OS_MODE THREADS].include?(key) ? 'LND_' : 'LND_MODULE_'
      "-D#{prefix}#{key}=#{value}"
    end
    stdout, stderr, status = Open3.capture3('cmake', '--fresh', '-S', source, '-B', dir, '-G', 'Ninja', "-DREVERSE=#{reverse}", *args)
    log = stdout + stderr
    File.binwrite(File.join(out, "#{name}-#{reverse}.log"), log)
    if enabled.is_a?(String)
      abort "#{name}: expected #{enabled}\n#{log}" if status.success? || !log.include?(enabled)
      next
    end
    abort "#{name}: #{log}" unless status.success?
    result = File.read(File.join(dir, 'resolved.txt'))
    abort 'Core entered the module resolver' if result.match?(/^core=/)
    abort 'Minimal profile enabled an extension' if name == 'minimal' && result.match?(/=ON$/)
    enabled.each { |mod| abort "#{name}: missing #{mod}" unless result.include?("#{mod}=ON\n") }
    disabled.each { |mod| abort "#{name}: unexpected #{mod}" unless result.include?("#{mod}=OFF\n") }
    result
  end
  abort "#{name}: resolution depends on declaration order" unless results[0] == results[1]
  puts "#{name}: passed in both declaration orders"
end

%w[category orphan category-source category-header root-source duplicate duplicate-name name-mismatch foreign-source].each do |name|
  dir = File.join(out, name, 'source')
  modules = File.join(dir, 'modules')
  module_path = name.start_with?('category') ? 'group/nested/sample' : 'sample'
  FileUtils.mkdir_p(File.join(modules, module_path))
  manifest = "lnd_add_module(sample INTERNAL DEFAULT ON)\n"
  expected = nil
  extra_include = ''
  case name
  when 'orphan'
    FileUtils.mkdir_p(File.join(modules, 'loose'))
    expected = 'Module category must not be empty'
  when 'category-source', 'category-header'
    File.binwrite(File.join(modules, 'group', name.end_with?('source') ? 'loose.c' : 'loose.h'), '')
    expected = 'Module category may only contain directories'
  when 'root-source'
    File.binwrite(File.join(modules, 'loose.c'), '')
    expected = 'Module category may only contain directories'
  when 'duplicate'
    manifest += "lnd_add_module(extra)\n"
    expected = 'Each directory declares exactly one module'
  when 'duplicate-name'
    other = File.join(modules, 'other/sample')
    FileUtils.mkdir_p(other)
    File.binwrite(File.join(other, 'module.cmake'), manifest)
    extra_include = "include(\"${CMAKE_CURRENT_SOURCE_DIR}/modules/other/sample/module.cmake\")"
    expected = 'Duplicate module sample'
  when 'name-mismatch'
    manifest = "lnd_add_module(different INTERNAL DEFAULT ON)\n"
    expected = 'Module directory must match its name different'
  when 'foreign-source'
    manifest = "lnd_add_module(sample INTERNAL DEFAULT ON SOURCES ../foreign.c)\n"
    File.binwrite(File.join(modules, 'foreign.c'), '')
    expected = 'must own its source'
  end
  File.binwrite(File.join(modules, module_path, 'module.cmake'), manifest)
  File.binwrite(File.join(dir, 'CMakeLists.txt'), <<~CMAKE)
    cmake_minimum_required(VERSION 3.21)
    project(layout_contract NONE)
    include("#{root}/cmake/modules.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/modules/#{module_path}/module.cmake")
    #{extra_include}
    lnd_validate_module_layout("${CMAKE_CURRENT_SOURCE_DIR}/modules")
  CMAKE
  stdout, stderr, status = Open3.capture3('cmake', '--fresh', '-S', dir, '-B', File.join(out, name, 'build'), '-G', 'Ninja')
  log = stdout + stderr
  File.binwrite(File.join(out, "#{name}.log"), log)
  if expected
    abort "#{name}: #{log}" if status.success? || !log.include?(expected)
  else
    abort "#{name}: #{log}" unless status.success?
  end
  puts "#{name}: #{expected ? 'rejected' : 'accepted'}"
end
