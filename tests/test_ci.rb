require 'fileutils'
require 'json'
require 'open3'
require 'tmpdir'
require 'rbconfig'

runner = File.expand_path('../tools/ci/run.rb', __dir__)
checks = 0
Dir.mktmpdir('lindar-ci-') do |root|
  command = lambda do |*args|
    text, status = Open3.capture2e(*args, chdir: root)
    raise text unless status.success?
    text.strip
  end
  command.call('git', 'init', '-b', 'main')
  command.call('git', 'config', 'user.name', 'Lindar test')
  command.call('git', 'config', 'user.email', 'test@example.invalid')
  FileUtils.mkdir_p(File.join(root, 'src'))
  File.write(File.join(root, 'README.md'), "Lindar\n")
  File.write(File.join(root, 'src/sample.c'), "int sample;\n")
  command.call('git', 'add', 'README.md', 'src/sample.c')
  command.call('git', 'commit', '-m', 'Initial fixture')
  baseline = command.call('git', 'rev-parse', 'HEAD')
  commit = lambda do |path|
    FileUtils.mkdir_p(File.dirname(File.join(root, path)))
    File.write(File.join(root, path), "fixture\n", mode: 'a')
    command.call('git', 'add', path)
    command.call('git', 'commit', '-m', 'Change fixture')
  end
  plan = lambda do |kind, event, expected, ref = 'refs/heads/main'|
    event_file = File.join(root, 'event.json')
    output_file = File.join(root, 'outputs.txt')
    File.write(event_file, JSON.generate(event))
    File.write(output_file, '')
    env = {'LND_WORKSPACE' => root, 'GITHUB_EVENT_NAME' => kind, 'GITHUB_EVENT_PATH' => event_file,
           'GITHUB_OUTPUT' => output_file, 'GITHUB_REF' => ref}
    text, status = Open3.capture2e(env, RbConfig.ruby, runner, 'plan')
    raise text unless status.success?
    actual = File.readlines(output_file, chomp: true).to_h { |line| line.split('=', 2) }
    raise "#{kind}: #{actual.inspect}, expected #{expected.inspect}" unless actual == expected.transform_values(&:to_s)
    checks += 1
  end
  commit.call('README.md')
  plan.call('pull_request', {'pull_request' => {'base' => {'sha' => baseline}}}, {native: false, docs: true, mcu: false, full: false}.transform_keys(&:to_s))
  baseline = command.call('git', 'rev-parse', 'HEAD')
  commit.call('src/sample.c')
  plan.call('push', {'before' => baseline}, {native: true, docs: false, mcu: true, full: false}.transform_keys(&:to_s))
  baseline = command.call('git', 'rev-parse', 'HEAD')
  commit.call('modules/processing/dsp/sample.c')
  plan.call('push', {'before' => baseline}, {native: true, docs: false, mcu: false, full: false}.transform_keys(&:to_s))
  all = {native: true, docs: true, mcu: true, full: true}.transform_keys(&:to_s)
  plan.call('workflow_dispatch', {'inputs' => {'full' => true}}, all)
  plan.call('push', {'before' => baseline}, all, 'refs/tags/v0.1')
  plan.call('schedule', {}, all)
  old = (Time.now - 172_800).strftime('%Y-%m-%dT%H:%M:%S%z')
  text, status = Open3.capture2e({'GIT_COMMITTER_DATE' => old}, 'git', 'commit', '--amend', '--no-edit', '--date', old, chdir: root)
  raise text unless status.success?
  plan.call('schedule', {}, {native: false, docs: false, mcu: false, full: false}.transform_keys(&:to_s))
end
puts "CI selection: #{checks} scenarios passed"
