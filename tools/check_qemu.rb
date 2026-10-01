require 'fileutils'
require 'json'
require 'open3'
require 'optparse'

$stdout.sync = true
root = ENV.fetch('LND_WORKSPACE') { File.expand_path('..', __dir__) }
options = {boards: %w[m33 m0 avr], build: File.join(root, 'build/qemu'), timeout: 20.0}
parser = OptionParser.new do |p|
    p.banner = 'Usage: ruby tools/check_qemu.rb [--boards m33,m0,avr] [--build directory]'
    p.on('--boards LIST', Array) { |v| options[:boards] = v }
    p.on('--build DIRECTORY') { |v| options[:build] = File.expand_path(v) }
    p.on('--timeout SECONDS', Float) { |v| options[:timeout] = v }
end
begin
    parser.parse!
rescue OptionParser::ParseError => error
    abort error.message
end
abort parser.to_s unless ARGV.empty? && !options[:boards].empty? && (options[:boards] - %w[m33 m0 avr]).empty? && options[:timeout].positive? && options[:timeout].finite?
FileUtils.mkdir_p(options[:build])

class Execution
    attr_reader :output, :status, :timed_out

    def initialize(command, timeout:, finish_on_marker: false)
        @output = +''
        @timed_out = false
        grouped = !RUBY_PLATFORM.match?(/mingw|mswin/)
        Open3.popen2e(*command, **(grouped ? {pgroup: true} : {})) do |input, output, wait|
            input.close
            reader = Thread.new do
                while (chunk = output.readpartial(4096))
                    @output << chunk
                    raise 'Process output exceeded 1 MiB' if @output.bytesize > 1_048_576
                end
            rescue EOFError
            end
            deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + timeout
            begin
                while wait.alive?
                    break if finish_on_marker && @output.match?(/^LND_EXIT [0-9a-f]{8}\r?\n/)
                    reader.value unless reader.alive?
                    if Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline
                        @timed_out = true
                        break
                    end
                    wait.join(0.01)
                end
            ensure
                if wait.alive?
                    Process.kill('TERM', grouped ? -wait.pid : wait.pid)
                    Process.kill('KILL', grouped ? -wait.pid : wait.pid) unless wait.join(1)
                end
                @status = wait.value
                reader.value
            end
        end
    end

    def exit_code
        values = @output.scan(/^LND_EXIT ([0-9a-f]{8})\r?\n/).flatten
        values.one? ? values.first.to_i(16) : nil
    end

    def cases
        rows = @output.scan(/^LND_CASE (\w+) ([0-9a-f]{8})\r?\n/)
        raise 'Repeated case result' unless rows.map(&:first).uniq.size == rows.size
        rows.to_h
    end
end

cmake = ENV.fetch('CMAKE', 'cmake')
qemu_arm = ENV.fetch('QEMU_ARM', 'qemu-system-arm')
qemu_avr = ENV.fetch('QEMU_AVR', 'qemu-system-avr')
report = {}
reference = {}
%w[host].concat(options[:boards].uniq).each do |board|
    dir = File.join(options[:build], board)
    FileUtils.mkdir_p(dir)
    config = [cmake, '-S', File.join(root, 'tools/qemu'), '-B', dir, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=MinSizeRel', "-DLND_QEMU_BOARD=#{board}"]
    config << "-DCMAKE_TOOLCHAIN_FILE=#{File.join(root, 'tools/qemu/toolchain.cmake')}" unless board == 'host'
    config << "-DCMAKE_MAKE_PROGRAM=#{ENV['NINJA']}" if ENV['NINJA']
    [['configure', config], ['build', [cmake, '--build', dir, '--parallel', '4']]].each do |name, command|
        run = Execution.new(command, timeout: 180)
        File.write(File.join(dir, "#{name}.log"), run.output)
        abort "#{board} #{name} failed:\n#{run.output}" if run.timed_out || !run.status.success?
    end
    extension = board == 'host' ? (RUBY_PLATFORM.match?(/mingw|mswin/) ? '.exe' : '') : '.elf'
    command_for = lambda do |name|
        firmware = File.join(dir, "qemu_#{name}#{extension}")
        if board == 'host'
            [firmware]
        elsif board == 'avr'
            [qemu_avr, '-machine', 'mega2560', '-bios', firmware, '-display', 'none', '-monitor', 'none', '-serial', 'stdio', '-no-reboot']
        else
            [qemu_arm, '-machine', board == 'm33' ? 'mps2-an505' : 'microbit', '-kernel', firmware, '-display', 'none', '-monitor', 'none',
             '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-no-reboot']
        end
    end
    results = {}
    unless board == 'host'
        size_tool = ENV.fetch(board == 'avr' ? 'AVR_SIZE' : 'ARM_SIZE', board == 'avr' ? 'avr-size' : 'arm-none-eabi-size')
        rom, ram, stack = {'m33' => [524288, 32768, 4096], 'm0' => [262144, 16384, 4096], 'avr' => [262144, 8192, 2048]}.fetch(board)
        results[:memory] = {}
        %w[tests example].each do |name|
            size = Execution.new([size_tool, File.join(dir, "qemu_#{name}.elf")], timeout: 10)
            File.write(File.join(dir, "#{name}-size.log"), size.output)
            columns = size.output.lines.last.to_s.split
            abort "Cannot read #{board}/#{name} size" unless size.status.success? && columns.take(3).size == 3 && columns.take(3).all? { |v| v.match?(/\A\d+\z/) }
            text, data, bss = columns.take(3).map(&:to_i)
            abort "#{board}/#{name} exceeds memory budget" if text + data > rom || data + bss + stack > ram
            results[:memory][name] = {rom: text + data, static_ram: data + bss, stack_reserve: stack, ram_limit: ram}
            puts "#{board}/#{name}: ROM #{text + data}, static RAM #{data + bss}, stack reserve #{stack} bytes"
        end
    end

    %w[tests example failure hang fault].each do |name|
        next if name == 'fault' && %w[host avr].include?(board)
        timeout = name == 'hang' ? 0.5 : options[:timeout]
        run = Execution.new(command_for.call(name), timeout: timeout, finish_on_marker: board == 'avr')
        File.write(File.join(dir, "#{name}.log"), run.output)
        expected_exit = name == 'failure' ? 7 : name == 'fault' ? 99 : 0
        valid = name == 'hang' ? run.timed_out && run.exit_code.nil? : !run.timed_out && run.exit_code == expected_exit
        valid &&= run.status.exitstatus == expected_exit unless board == 'avr' || name == 'hang'
        if %w[tests example].include?(name)
            cases = run.cases
            if board == 'host'
                expected_keys = name == 'tests' ? %w[pcm pcm_wide queue transport transport_planar] : %w[cycles sine]
                valid &&= cases.keys.sort == expected_keys.sort
                reference[name] = cases
            else
                valid &&= cases == reference[name]
            end
        end
        valid &&= run.output.include?("LND_FAULT\n") if name == 'fault'
        abort "FAIL #{board}/#{name}: exit=#{run.exit_code.inspect}, timeout=#{run.timed_out}\n#{run.output}" unless valid
        puts "PASS #{board}/#{name}"
        results[name] = {exit: run.exit_code, cases: run.cases, timed_out: run.timed_out}
    end
    report[board] = results
end
File.write(File.join(options[:build], 'results.json'), JSON.pretty_generate(report) + "\n")
puts "QEMU: #{options[:boards].uniq.join(', ')} matched native PCM; failure, fault and timeout handling checked"
