require 'open3'

ROOT = File.expand_path('..', __dir__)

def git_output(*args)
  output, error, status = Open3.capture3('git', '-C', ROOT, *args)
  abort error unless status.success?
  output
end

def git(*args)
  system('git', '-C', ROOT, *args) || abort('Could not initialise vendor submodules')
end

if ARGV.any? { |arg| %w[-h --help].include?(arg) }
  puts 'Usage: ruby tools/vendor.rb [dependency ...]'
  puts 'Initialise the recorded commits; no dependency versions are advanced.'
  exit
end

paths = git_output('config', '--file', '.gitmodules', '--get-regexp', '^submodule\..*\.path$')
        .lines.map { |line| line.split(' ', 2).last.strip }
names = paths.to_h { |path| [File.basename(path), path] }
selected = ARGV.empty? ? names.keys : ARGV.uniq
unknown = selected - names.keys
abort "Unknown dependencies: #{unknown.join(', ')}; choose #{names.keys.join(', ')}" unless unknown.empty?
releases = git_output('config', '--file', '.gitmodules', '--get-regexp', '^submodule\..*\.release$')
           .lines.to_h { |line| line.strip.split(' ', 2) }
git('submodule', 'update', '--init', '--checkout', '--depth', '1', '--', *selected.map { |name| names.fetch(name) })
selected.each do |name|
  path = names.fetch(name)
  release = releases["submodule.#{path}.release"]
  next unless release
  ref = "refs/tags/#{release}^{commit}"
  _, _, status = Open3.capture3('git', '-C', File.join(ROOT, path), 'rev-parse', '--verify', '--quiet', ref)
  git('-C', path, 'fetch', '--depth', '1', 'origin', 'tag', release) unless status.success?
end
if selected.include?('bungee')
  git('-C', names.fetch('bungee'), 'submodule', 'update', '--init', '--recursive', '--checkout', '--depth', '1')
end
