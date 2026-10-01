require 'fileutils'
require 'json'
require 'open3'
require 'optparse'
require 'pathname'
require 'rexml/document'

$stdout.sync = true

ROOT = File.expand_path('..', __dir__)
PACKAGES = %w[linda lindar_pitya lindar lindar_quanta].freeze
PLATFORMS = {'WINDOWS' => 'Windows', 'LINUX' => 'Linux', 'APPLE' => 'macOS', 'IOS' => 'iOS', 'ANDROID' => 'Android API 28+'}.freeze

options = {output: File.join(ROOT, 'build/docs'), cmake: ENV.fetch('CMAKE', 'cmake'), doxygen: ENV.fetch('DOXYGEN', 'doxygen')}
parser = OptionParser.new do |args|
  args.banner = 'Usage: ruby tools/docs.rb [options]'
  args.on('--output DIRECTORY', 'Output directory (default: build/docs)') { |v| options[:output] = File.expand_path(v) }
  args.on('--cmake EXECUTABLE', 'CMake executable') { |v| options[:cmake] = v }
  args.on('--doxygen EXECUTABLE', 'Doxygen executable (1.10 or later)') { |v| options[:doxygen] = v }
  args.on('-h', '--help', 'Show this help') { puts args; exit }
end
begin
  parser.parse!
rescue OptionParser::ParseError => error
  abort error.message
end
abort parser.to_s unless ARGV.empty?

def run(*command)
  stdout, stderr, status = Open3.capture3(*command, chdir: ROOT)
  raise "#{command.first} failed (#{status.exitstatus}):\n#{stdout}#{stderr}" unless status.success?
  stdout
end

def reference(name)
  name.match?(/\A[A-Z_]+\z/) ? "`LND_#{name}`" : "@ref lnd_#{name} \"#{name}\""
end

def references(names)
  names.empty? ? '-' : names.map { |name| reference(name) }.join(', ')
end

def page(pages, name, text)
  pages[name] = "/**\n#{text}\n*/\n"
end

def quote(value)
  '"' + value.to_s.tr('\\', '/').gsub('"', '\\"') + '"'
end

def xml_text(element)
  return '' unless element
  REXML::XPath.match(element, './/text()').map(&:value).join.strip
end

def verify_api(output, headers)
  index = REXML::Document.new(File.read(File.join(output, 'xml/index.xml')))
  counts = Hash.new(0)
  names = []
  index.elements.each('doxygenindex/compound') do |compound|
    next unless %w[file struct].include?(compound.attributes['kind'])
    document = REXML::Document.new(File.read(File.join(output, 'xml', "#{compound.attributes['refid']}.xml")))
    document.elements.each('doxygen/compounddef/sectiondef/memberdef') do |member|
      type = xml_text(member.elements['type'])
      function = member.attributes['kind'] == 'function'
      callback = %w[typedef variable].include?(member.attributes['kind']) && type.match?(/\(\*\)?\z/)
      next unless function || callback
      name = member.elements['name'].text
      names << name if function
      if function
        params = member.get_elements('param').filter_map { |param| param.elements['declname']&.text }
      else
        arguments = member.elements['argsstring'].text.delete_prefix(')').delete_prefix('(').delete_suffix(')')
        params = arguments == 'void' ? [] : arguments.split(',').map { |param| param[/\w+\s*\z/].strip }
        type = type.sub(/\(\*\)?\z/, '').strip
      end
      documented = REXML::XPath.match(member, './/parameterlist[@kind="param"]//parametername').map { |param| xml_text(param) }
      raise "#{name}: parameter documentation differs (#{params.inspect} / #{documented.inspect})" unless params == documented
      returns = REXML::XPath.match(member, './/simplesect[@kind="return"]')
      expected = type == 'void' ? 0 : 1
      raise "#{name}: expected #{expected} return description(s)" unless returns.size == expected
      raise "#{name}: missing summary" if xml_text(member.elements['briefdescription']).empty?
      counts[function ? :functions : :callbacks] += 1
    end
  end
  declarations = headers.map { |path| File.read(path) }.join("\n").gsub(%r{/\*.*?\*/}m, '')
  expected_names = declarations.scan(/LND_API\s+(?!extern\b)[^;]*?\b(LND_\w+)\s*\(/).flatten.sort
  raise 'Doxygen did not include every public function' unless names.sort == expected_names
  expected_callbacks = declarations.scan(/\(\*\w+\)\([^;]*\);/).size
  raise 'Doxygen did not include every callback' unless counts[:callbacks] == expected_callbacks
  puts "Verified #{counts[:functions]} functions and #{counts[:callbacks]} callbacks: parameters and returns complete."
end

begin
  version = run(options[:doxygen], '--version')[/\d+\.\d+(?:\.\d+)?/]
  raise 'Doxygen 1.10 or later is required' unless version && (version.split('.').map(&:to_i) <=> [1, 10, 0]) >= 0
  output = options[:output]
  generated = File.join(output, 'input')
  FileUtils.mkdir_p(generated)
  pages = {}
  puts 'Resolving module and package declarations with CMake...'
  packages = {}
  modules = nil
  PACKAGES.each do |package|
    packages[package] = {}
    PLATFORMS.each_key do |platform|
      metadata = File.join(generated, "#{package}-#{platform.downcase}.json")
      run(options[:cmake], "-DLND_PACKAGE=#{package}", "-DLND_DOCS_PLATFORM=#{platform}",
          "-DLND_DOCS_METADATA=#{metadata}", '-P', File.join(ROOT, 'cmake/docs_metadata.cmake'))
      catalogue = JSON.parse(File.read(metadata))
      modules ||= catalogue
      packages[package][platform] = catalogue.select { |_, data| data.fetch('enabled') }.keys.sort
    end
  end
  headers = Dir.glob(File.join(ROOT, 'include/*.h')).sort
  examples = Dir.glob(File.join(ROOT, 'examples/*/*/main.c')).sort
  declared_headers = ['lindar.h'] + modules.values.flat_map { |data| data.fetch('header') }
  missing = headers.map { |path| File.basename(path) } - declared_headers
  raise "Headers without a module: #{missing.join(', ')}" unless missing.empty?

  page(pages, 'index', <<~TEXT)
    @mainpage Lindar
    - @ref lnd_core "Core API"
    - @ref lnd_modules "Module dependencies"
    - @ref lnd_packages "Package contents"
    - @ref lnd_examples "Examples"

    All #{headers.size} public headers are included; API availability depends on the build.
  TEXT

  categories = modules.group_by { |_, data| data.fetch('directory').split('/')[1] }.sort.to_h
  page(pages, 'modules', <<~TEXT)
    @defgroup lnd_modules Modules
    Dependencies from module.cmake; codec-direction dependencies apply only when enabled.
  TEXT
  page(pages, 'core', "@defgroup lnd_core Core API\nAlways built from src/. Public interface: @ref lindar.h.\n")
  categories.each do |category, entries|
    page(pages, "category_#{category}", "@defgroup lnd_category_#{category} #{category}\n@ingroup lnd_modules\n")
    entries.sort.each do |name, data|
      details = ["@defgroup lnd_#{name} #{name}", "@ingroup lnd_category_#{category}",
                 "Source directory: `#{data.fetch('directory')}`.", '',
                 "Public header: #{data['header'].empty? ? 'none; uses an existing interface' : "@ref #{data['header'].first}"}.", '',
                 '| Property | Value |', '| --- | --- |',
                 "| Requires | #{references(data['requires'])} |"]
      %w[decoder encoder].each do |kind|
        next unless data[kind] == ['TRUE']
        dependencies = [kind == 'decoder' ? 'codecs' : 'output'] + data["#{kind}_requires"]
        details << "| #{kind.capitalize} requires | #{references(dependencies.uniq)} |"
      end
      details << "| Requires one of | #{references(data['requires_one_of'])} |" unless data['requires_one_of'].empty?
      data['requires_if'].each do |pair|
        condition, dependency = pair.split(':', 2)
        details << "| When #{reference(condition)} is enabled | #{reference(dependency)} |"
      end
      unless data['requires_provider'].empty?
        provider = data['requires_provider'].first
        candidates = modules.select { |_, entry| entry['provides'].include?(provider) }.keys.sort
        details << "| Provider: #{provider} | #{references(candidates)} |"
      end
      %w[provides provider_kind platform min_api available default_with languages libs].each do |field|
        next if data[field].empty?
        details << "| #{field.tr('_', ' ').capitalize} | #{data[field].map { |value| "`#{value}`" }.join(', ')} |"
      end
      details << '| Internal | Enabled through dependencies |' if data['internal'] == ['TRUE']
      if name == 'http'
        details << '| HTTP transport | LND_HTTP_TRANSPORT selects CURL or CUSTOM; CURL adds http_curl |'
      end
      page(pages, "module_#{name}", details.join("\n"))
    end
  end
  header_docs = headers.map do |path|
    header = File.basename(path)
    owner = header == 'lindar.h' ? 'core' : modules.find { |_, data| data['header'].include?(header) }.first
    "/** @file #{header}\n@brief Public #{owner} API.\n@ingroup lnd_#{owner}\n*/"
  end
  pages['headers'] = header_docs.join("\n")

  package_text = ["@page lnd_packages Packages",
                  'Resolved module sets, assuming required external dependencies. Android uses API 28.', '']
  packages.each do |name, platforms|
    package_text += ["@section package_#{name} #{name}", '',
                     "Preset and library name: `#{name}`. Windows shared builds produce `#{name}.dll`.", '',
                     "| Module | #{PLATFORMS.values.join(' | ')} |", "| --- | #{Array.new(PLATFORMS.size, '---').join(' | ')} |"]
    included = platforms.values.flatten.uniq.sort
    if included.empty?
      package_text << "| Core only | #{Array.new(PLATFORMS.size, 'yes').join(' | ')} |"
    else
      included.each do |mod|
        package_text << "| #{reference(mod)} | #{platforms.values.map { |list| list.include?(mod) ? 'yes' : '-' }.join(' | ')} |"
      end
    end
    package_text << ''
  end
  page(pages, 'packages', package_text.join("\n"))

  example_index = ["@page lnd_examples Examples",
                   'Targets match directory names; bare_metal uses tools/check_qemu.rb.', '']
  examples.group_by { |path| Pathname.new(path).relative_path_from(Pathname.new(ROOT)).each_filename.to_a[1] }.sort.each do |category, paths|
    example_index << "@section examples_#{category.tr('-', '_')} #{category}"
    paths.each do |path|
      relative = Pathname.new(path).relative_path_from(Pathname.new(File.join(ROOT, 'examples'))).to_s.tr('\\', '/')
      name = File.basename(File.dirname(path))
      id = "example_#{category.tr('-', '_')}_#{name}"
      example_index << "- @subpage #{id}"
      used_headers = File.read(path).scan(/#include\s+"(lindar[^\"]*\.h)"/).flatten
      siblings = Dir.glob(File.join(File.dirname(path), '*.{c,h}')).sort.reject { |file| file == path || File.basename(file) =~ /sample|table/ }
      text = ["@page #{id} #{category}/#{name}", "Source: `examples/#{relative}`.", '',
              "Public headers used directly: #{used_headers.empty? ? 'none; the program uses local helpers' : used_headers.map { |header| "@ref #{header}" }.join(', ')}.", '', "@include{lineno} #{relative}"]
      siblings.each do |file|
        rel = Pathname.new(file).relative_path_from(Pathname.new(File.join(ROOT, 'examples'))).to_s.tr('\\', '/')
        text += ['', "@include{lineno} #{rel}"]
      end
      page(pages, id, text.join("\n"))
    end
  end
  page(pages, 'examples', example_index.join("\n"))

  inputs = pages.map do |name, text|
    path = File.join(generated, "#{name}.dox")
    File.write(path, text)
    path
  end
  config = {
    'PROJECT_NAME' => quote('Lindar'), 'PROJECT_BRIEF' => quote('Native audio runtime'),
    'OUTPUT_DIRECTORY' => quote(output), 'OUTPUT_LANGUAGE' => 'English',
    'INPUT' => (headers + inputs.sort).map { |path| quote(path) }.join(' '),
    'EXAMPLE_PATH' => quote(File.join(ROOT, 'examples')), 'EXAMPLE_RECURSIVE' => 'YES',
    'EXAMPLE_PATTERNS' => '*.c *.h', 'OPTIMIZE_OUTPUT_FOR_C' => 'YES',
    'JAVADOC_AUTOBRIEF' => 'YES', 'MULTILINE_CPP_IS_BRIEF' => 'YES',
    'FULL_PATH_NAMES' => 'YES', 'STRIP_FROM_PATH' => quote(ROOT),
    'STRIP_FROM_INC_PATH' => quote(File.join(ROOT, 'include')),
    'ENABLE_PREPROCESSING' => 'YES', 'MACRO_EXPANSION' => 'YES', 'EXPAND_ONLY_PREDEF' => 'YES',
    'PREDEFINED' => 'LND_API= __ANDROID__=1', 'SEARCH_INCLUDES' => 'NO',
    'GENERATE_HTML' => 'YES', 'GENERATE_TREEVIEW' => 'YES', 'HTML_DYNAMIC_MENUS' => 'NO',
    'HTML_COLORSTYLE' => 'LIGHT', 'TIMESTAMP' => 'NO',
    'GENERATE_LATEX' => 'NO', 'GENERATE_XML' => 'YES', 'XML_PROGRAMLISTING' => 'NO',
    'SOURCE_BROWSER' => 'YES', 'INLINE_SOURCES' => 'NO', 'VERBATIM_HEADERS' => 'YES',
    'HAVE_DOT' => 'NO', 'QUIET' => 'YES', 'WARNINGS' => 'YES',
    'WARN_IF_UNDOCUMENTED' => 'YES', 'WARN_IF_DOC_ERROR' => 'YES',
    'WARN_IF_INCOMPLETE_DOC' => 'YES', 'WARN_NO_PARAMDOC' => 'YES', 'WARN_IF_UNDOC_ENUM_VAL' => 'YES',
    'WARN_AS_ERROR' => 'FAIL_ON_WARNINGS', 'WARN_LOGFILE' => quote(File.join(output, 'warnings.log'))
  }
  doxyfile = File.join(output, 'Doxyfile')
  File.write(doxyfile, config.map { |key, value| "#{key} = #{value}" }.join("\n") + "\n")
  puts "Generating HTML with Doxygen #{version}..."
  begin
    run(options[:doxygen], doxyfile)
  rescue RuntimeError => error
    warn File.read(File.join(output, 'warnings.log')) if File.file?(File.join(output, 'warnings.log'))
    raise error
  end
  verify_api(output, headers)
  puts "Documentation: #{File.join(output, 'html/index.html')}"
rescue Errno::ENOENT, RuntimeError, JSON::ParserError => error
  abort error.message
end
