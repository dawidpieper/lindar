require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'rbconfig'

$stdout.sync = true
ROOT = File.expand_path(ENV.fetch('LND_WORKSPACE', '../..'), __dir__)
OUT = File.expand_path(ENV.fetch('LND_CI_OUTPUT', 'build/ci'), ROOT)
BUILD = File.expand_path(ENV.fetch('LND_CI_BUILD', 'native'), OUT)
JOBS = ENV.fetch('CMAKE_BUILD_PARALLEL_LEVEL', '2')
WINDOWS = RbConfig::CONFIG['host_os'].match?(/mingw|mswin/)
MACOS = RbConfig::CONFIG['host_os'].include?('darwin')
PACKAGES = %w[linda lindar_pitya lindar lindar_quanta].freeze

Dir.chdir(ROOT)
FileUtils.mkdir_p(OUT)

def run(*command, log: nil)
  puts command.join(' ')
  output, status = Open3.capture2e(*command)
  File.write(File.join(OUT, "#{log}.log"), output) if log
  abort output unless status.success?
  print output unless log
  output
end

def ruby(script, *args, log: nil)
  run(RbConfig.ruby, File.join(ROOT, 'tools', script), *args, log: log)
end

def output(values)
  text = values.map { |key, value| "#{key}=#{value}\n" }.join
  print text
  File.write(ENV['GITHUB_OUTPUT'], text, mode: 'a') if ENV['GITHUB_OUTPUT']
end

def configure(package, shared, *extra)
  run('cmake', '-S', ROOT, '-B', BUILD, '-G', ENV.fetch('CMAKE_GENERATOR', 'Ninja'),
      "-DLND_PACKAGE=#{package}", "-DLND_SHARED=#{shared ? 'ON' : 'OFF'}", '-DCMAKE_BUILD_TYPE=Release',
      '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', '-DLND_BUILD_TESTS=ON', '-DLND_BUILD_CXX_TESTS=ON',
      '-DLND_BUILD_EXAMPLES=ON', '-DLND_LTO=OFF', '-DLND_FFMPEG_BUNDLED=ON', '-DLND_FFMPEG_FLAGS=--enable-pic',
      "-DLND_OPENSSL_JOBS=#{JOBS}", *extra, log: 'configure')
end

def test_build(name)
  run('cmake', '--build', BUILD, '--parallel', JOBS, log: "#{name}-build")
  run('ctest', '--test-dir', BUILD, '--output-on-failure', '--parallel', JOBS,
      '--output-junit', File.join(OUT, "#{name}-tests.xml"), log: "#{name}-tests")
end

def shared_library(package, directory = BUILD)
  File.join(directory, WINDOWS ? "#{package}.dll" : MACOS ? "lib#{package}.dylib" : "lib#{package}.so")
end

def check_clients(package)
  ruby('check_public_api.rb', BUILD, log: "#{package}-api")
  ruby('check_ffi.rb', shared_library(package), log: "#{package}-ffi") unless package == 'linda'
end

def package_library(package)
  stage = File.join(OUT, 'packages', package)
  abort "Package directory already exists: #{stage}" if File.exist?(stage)
  run('cmake', '--install', BUILD, '--prefix', stage, '--component', 'Lindar', log: "#{package}-install")
  binary = shared_library(package, File.join(stage, WINDOWS ? 'bin' : 'lib'))
  abort "Missing installed library: #{binary}" unless File.file?(binary)
  if WINDOWS
    run('cmake', "-DLND_LIBRARY=#{binary}", "-DLND_RUNTIME_DIR=#{ENV.fetch('LND_RUNTIME_DIR', File.dirname(ENV.fetch('CC', 'clang')))}",
        '-P', File.join(ROOT, 'tools/ci/runtime.cmake'), log: "#{package}-runtime")
  end
  if WINDOWS
    licences = File.expand_path('../share/licenses', ENV.fetch('LND_RUNTIME_DIR'))
    FileUtils.cp_r(licences, File.join(stage, 'share/licenses/toolchain'))
  end
  licences = File.join(stage, 'share/licenses/vendor')
  Dir.glob('vendor/{*,bungee/submodules/*}/{LICENSE*,LICENCE*,COPYING*,COPYRIGHT*,NOTICE*}').select { |path| File.file?(path) }.each do |path|
    target = File.join(licences, path.delete_prefix('vendor/'))
    FileUtils.mkdir_p(File.dirname(target))
    FileUtils.cp(path, target)
  end
  FileUtils.mkdir_p(licences)
  FileUtils.cp('vendor/VERSIONS.md', licences)
  FileUtils.cp('vendor/cacert.LICENSE', licences)
  revision = ENV.fetch('GITHUB_SHA') { run('git', 'rev-parse', 'HEAD').strip }
  File.write(File.join(stage, 'build.txt'), "revision=#{revision}\npackage=#{package}\nplatform=#{RUBY_PLATFORM}\n" +
             File.readlines(File.join(BUILD, 'CMakeCache.txt')).grep(/^(?:LND_|CMAKE_(?:C_COMPILER|CXX_COMPILER|BUILD_TYPE|SYSTEM|OSX))/).join)
  archive = File.join(OUT, "#{package}-#{ENV.fetch('LND_CI_PLATFORM', RUBY_PLATFORM)}.tar.gz")
  run('cmake', '-E', 'chdir', File.dirname(stage), 'cmake', '-E', 'tar', 'czf', archive, package, log: "#{package}-archive")
  File.write(archive + '.sha256', "#{Digest::SHA256.file(archive).hexdigest}  #{File.basename(archive)}\n")
end

case ARGV.shift
when 'plan'
  event = JSON.parse(File.read(ENV.fetch('GITHUB_EVENT_PATH')))
  kind = ENV.fetch('GITHUB_EVENT_NAME')
  full = kind == 'schedule' || ENV.fetch('GITHUB_REF', '').start_with?('refs/tags/') || event.dig('inputs', 'full').to_s == 'true'
  recent = kind != 'schedule' || Time.now.to_i - run('git', 'show', '-s', '--format=%ct', 'HEAD').to_i < 86_400
  base = kind == 'pull_request' ? event.dig('pull_request', 'base', 'sha') : event['before']
  files = nil
  if !full && base && base.match?(/\A[0-9a-f]{40}\z/) && base != '0' * 40
    _, status = Open3.capture2e('git', 'cat-file', '-e', "#{base}^{commit}")
    run('git', 'fetch', '--no-tags', '--depth=1', 'origin', base) unless status.success?
    files = run('git', 'diff', '--name-only', '-z', base, 'HEAD').split("\0")
    run('git', '-c', 'core.whitespace=cr-at-eol', 'diff', '--check', base, 'HEAD')
  else
    run('git', '-c', 'core.whitespace=cr-at-eol', 'show', '--format=', '--check', 'HEAD')
  end
  native = recent && (full || !files || files.any? { |path| !path.match?(%r{\A(?:docs/|(?:readme|README)\.md\z|LICENSE\z)}) })
  docs = recent && (full || !files || files.any? { |path| path.match?(%r{\A(?:include/|docs/|examples/|modules/.*/module\.cmake|cmake/|tools/docs\.rb|tools/ci/|\.github/|CMakeLists\.txt|(?:readme|README)\.md)}) })
  mcu = recent && (full || !files || files.any? { |path| path.match?(%r{\A(?:src/|include/lindar(?:_queue)?\.h|modules/pcm/queue/|tests/mcu/|examples/mcu/|tools/(?:ci/|qemu/|check_mcu|check_qemu)|cmake/|\.github/|CMake)}) })
  output(native: native, docs: docs, mcu: mcu, full: full && recent)
when 'check'
  Dir.glob('{tools,tests}/**/*.rb').each { |path| run(RbConfig.ruby, '-c', path) }
  run(RbConfig.ruby, 'tests/test_ci.rb')
  ruby('check_modules.rb', log: 'modules')
  run('cmake', "-DLND_SOURCE_DIR=#{ROOT}", "-DLND_TEST_DIR=#{OUT}/tests/packages", '-DLND_TEST_GENERATOR=Ninja',
      '-P', 'tests/test_packages.cmake', log: 'packages')
when 'cache-key'
  inputs = Dir.glob('{CMakeLists.txt,cmake/vendor.cmake,cmake/vendor/**/*.cmake,modules/**/setup.cmake,modules/formats/ffmpeg/{build.sh.in,decoders.txt,encoders.txt},tools/ci/*,.github/actions/build-env/*,.gitmodules}').select { |path| File.file?(path) }.sort
  digest = Digest::SHA256.new
  inputs.each { |path| digest << path << File.binread(path) }
  digest << run('git', 'ls-files', '--stage', 'vendor')
  %w[CC CXX].each { |key| digest << run(ENV.fetch(key), '--version') }
  digest << ENV.fetch('ImageVersion', '') << ENV.fetch('LND_CI_PLATFORM', RUBY_PLATFORM) << ROOT
  output(key: "native-v1-#{digest.hexdigest}")
when 'native'
  if ENV['LND_CI_PLATFORM']
    expected = ENV['LND_CI_PLATFORM'].end_with?('arm64') ? /\A(?:arm64|aarch64)/ : /\A(?:x86_64|x64)/
    abort 'Ruby architecture does not match the CI target' unless RbConfig::CONFIG['host_cpu'].match?(expected)
    abort 'Compiler architecture does not match the CI target' unless run(ENV.fetch('CC'), '-dumpmachine').match?(expected)
  end
  configure('lindar_quanta', true)
  test_build('lindar_quanta-shared')
  check_clients('lindar_quanta')
  ruby('check_examples.rb', BUILD, log: 'examples')
when 'packages'
  package_library('lindar_quanta')
  if ARGV.delete('--full')
    PACKAGES.each do |package|
      [false, true].each do |shared|
        next if package == 'lindar_quanta' && shared
        configure(package, shared)
        test_build("#{package}-#{shared ? 'shared' : 'static'}")
        next unless shared
        check_clients(package)
        package_library(package)
      end
    end
  end
when 'profiles'
  configure('lindar_quanta', false)
  test_build('gcc-quanta')
  run('cmake', '--build', BUILD, '--target', 'aac_tone', '--parallel', JOBS, log: 'aac-generator')
  samples = File.join(OUT, 'regenerated/tests/audiosamples')
  abort "Regeneration directory already exists: #{samples}" if File.exist?(samples)
  ruby('generate_samples.rb', '--output', samples, '--aac-tone', File.join(BUILD, 'aac_tone'), 'all', log: 'generate-audiosamples')
  expected = Dir.glob('tests/audiosamples/**/*').select { |path| File.file?(path) && !path.end_with?('.md') }
  missing = expected.reject { |path| File.size?(File.join(samples, path.delete_prefix('tests/audiosamples/'))) }
  abort "Missing regenerated audio files: #{missing.join(', ')}" unless missing.empty?
  configure('lindar_quanta', false, "-DLND_TEST_AUDIO_DIR=#{samples}")
  test_build('regenerated')
  ruby('check_build_profiles.rb', log: 'profiles')
when 'sanitise'
  flags = '-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
  modules = %w[files wav adpcm graph queue stream slide autofree notify analysis dsp effects decode http
               metadata_id3v1 metadata_id3v2 metadata_comments metadata_wave metadata_io null]
  run('cmake', '-S', ROOT, '-B', BUILD, '-G', ENV.fetch('CMAKE_GENERATOR', 'Ninja'), '-DCMAKE_BUILD_TYPE=Debug', '-DLND_MINIMAL=ON',
      '-DLND_OS_MODE=ON', '-DLND_THREADS=ON', '-DLND_SHARED=OFF', '-DLND_LTO=OFF',
      '-DLND_BUILD_TESTS=ON', '-DLND_BUILD_EXAMPLES=OFF', '-DLND_HTTP_TRANSPORT=CUSTOM', '-DLND_HTTP_AES128=OFF',
      "-DCMAKE_C_FLAGS=#{flags}", "-DCMAKE_CXX_FLAGS=#{flags}", "-DCMAKE_EXE_LINKER_FLAGS=#{flags}",
      *modules.map { |name| "-DLND_MODULE_#{name.upcase}=ON" }, log: 'sanitise-configure')
  test_build('sanitise')
else
  abort 'Usage: ruby tools/ci/run.rb plan|check|cache-key|native|packages [--full]|profiles|sanitise'
end
