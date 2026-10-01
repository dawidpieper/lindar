require 'digest'
require 'fileutils'

TOOLS = {
  'arm' => ['https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v15.2.1-1.1/xpack-arm-none-eabi-gcc-15.2.1-1.1-linux-x64.tar.gz',
            'da6a49ad4003944b823c6c93702a8787c922ab34bd7e918ec0eaf6933a9b1ff6', 1],
  'avr' => ['https://github.com/ZakKemble/avr-gcc-build/releases/download/v16.1.0-1/avr-gcc-16.1.0-x64-linux.tar.bz2',
            '8621ecc6514df50202b58b23b0f8f72f0e535ec35ee40426194e9a15c57692f6', 1],
  'doxygen' => ['https://github.com/doxygen/doxygen/releases/download/Release_1_18_0/doxygen-1.18.0.linux.bin.tar.gz',
                '14fa81bdc34171edb5f1f02b1d60e74802f0439b77fa44e592565d517d72df90', 1],
  'actionlint' => ['https://github.com/rhysd/actionlint/releases/download/v1.7.12/actionlint_1.7.12_linux_amd64.tar.gz',
                  '8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8', 0]
}.freeze
abort "Usage: ruby tools/ci/download.rb #{TOOLS.keys.join('|')} [...]" if ARGV.empty? || !(ARGV - TOOLS.keys).empty?
ARGV.each do |name|
  url, sha256, strip = TOOLS.fetch(name)
  directory = File.expand_path("../../build/ci-tools/#{name}", __dir__)
  marker = File.join(directory, '.sha256')
  unless File.file?(marker) && File.read(marker) == sha256
    FileUtils.mkdir_p(directory)
    archive = directory + '.archive'
    abort "Download failed: #{name}" unless system('curl', '--fail', '--location', '--retry', '3', '--silent', '--show-error', url, '--output', archive)
    abort "Checksum mismatch: #{name}" unless Digest::SHA256.file(archive).hexdigest == sha256
    abort "Extraction failed: #{name}" unless system('tar', '-xf', archive, '-C', directory, "--strip-components=#{strip}")
    File.unlink(archive)
    File.write(marker, sha256)
  end
  path = name == 'actionlint' ? directory : File.join(directory, 'bin')
  puts path
  File.write(ENV['GITHUB_PATH'], path + "\n", mode: 'a') if ENV['GITHUB_PATH']
end
