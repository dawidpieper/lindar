require 'fileutils'
require 'open3'
root = ENV.fetch('LND_WORKSPACE') { File.expand_path('..', __dir__) }
profile = File.expand_path(ARGV.fetch(0) { abort 'Usage: ruby tools/check_public_api.rb build-shared' }, root)
work = File.join(profile, 'tests/api')
FileUtils.mkdir_p(work)
install = File.join(work, 'install')
package = File.read(File.join(profile, 'CMakeCache.txt'))[/^LND_PACKAGE:[^=]+=([^\r\n]+)/, 1]
library_name = package && package != 'custom' ? package : 'lindar'
cc = ENV.fetch('CC', 'gcc')
cxx = ENV.fetch('CXX', 'g++')
def run(*args)
  stdout, stderr, status = Open3.capture3(*args)
  abort "#{args.join(' ')}\n#{stdout}#{stderr}" unless status.success?
  stdout
end
FileUtils.rm_f(Dir.glob(File.join(install, 'include/lindar*.h')))
FileUtils.rm_f(File.join(install, 'include/lnd_modules.h'))
run('cmake', '--install', profile, '--prefix', install, '--component', 'Lindar')
[File.join(root, 'include'), File.join(install, 'include')].each do |directory|
  headers = Dir.glob(File.join(directory, '*.h'))
  %w[c cpp].each do |language|
    source = File.join(work, "audit-header.#{language}")
    headers.each do |header|
      File.write(source, "#include \"#{File.basename(header)}\"\n")
      run(language == 'c' ? cc : cxx, language == 'c' ? '-std=c23' : '-std=c++17', '-Wall', '-Wextra', '-Werror',
          '-fsyntax-only', '-I', directory, source)
    end
  end
  puts "#{directory}: #{headers.size * 2} standalone C/C++ header checks"
end
windows = RUBY_PLATFORM.match?(/mswin|mingw/)
ENV['PATH'] = File.join(install, 'bin') + File::PATH_SEPARATOR + ENV.fetch('PATH', '')
ENV['LD_LIBRARY_PATH'] = File.join(install, 'lib') + File::PATH_SEPARATOR + ENV.fetch('LD_LIBRARY_PATH', '')
link_flags = windows ? [] : ['-Xlinker', '-rpath', '-Xlinker', File.join(install, 'lib')]
clients = {}
clients['public_codec'] = ['c'] if %w[lindar_codecs.h lindar_decode.h].all? { |header| File.file?(File.join(install, 'include', header)) }
clients['public_api'] = %w[c cpp] if File.read(File.join(profile, 'CMakeCache.txt')).match?(/^LND_USE_LIBC_ALLOC:BOOL=ON$/)
clients.each do |name, languages|
  languages.each do |language|
    source = File.join(work, "installed_#{name}.#{language}")
    FileUtils.cp(File.join(root, 'tests', "test_#{name}.c"), source)
    binary = File.join(work, "installed_#{name}_#{language}#{windows ? '.exe' : ''}")
    run(language == 'c' ? cc : cxx, language == 'c' ? '-std=c23' : '-std=c++17',
        '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-DLND_SHARED', '-I', File.join(install, 'include'),
        source, '-L', File.join(install, 'lib'), "-l#{library_name}", *link_flags, '-o', binary)
    run(binary)
    puts "#{name}/#{language}: compiled against installed headers, linked and executed with installed shared library"
  end
end
headers = Dir.glob(File.join(install, 'include/lindar*.h'))
source = File.join(work, 'installed_exports.c')
includes = headers.map { |header| "#include \"#{File.basename(header)}\"" }.join("\n")
File.write(source, includes)
preprocessed = run(cc, '-std=c23', '-E', '-P', '-I', File.join(install, 'include'), source)
symbols = preprocessed.scan(/\b(LND_\w+)\s*\(/).flatten.uniq.sort
entries = symbols.map { |name| "    (void (*)(void))#{name}," }.join("\n")
File.write(source, includes + "\nvoid (*volatile exports[])(void) = {\n#{entries}\n};\nint main(void) { return exports[0] ? 0 : 1; }\n")
binary = File.join(work, "installed_exports#{windows ? '.exe' : ''}")
run(cc, '-std=c23', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-DLND_SHARED', '-I', File.join(install, 'include'),
    source, '-L', File.join(install, 'lib'), "-l#{library_name}", *link_flags, '-o', binary)
run(binary)
puts "Public exports: #{symbols.size} installed function declarations linked and loaded"
