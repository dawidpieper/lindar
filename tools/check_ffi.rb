require 'fiddle/import'

abort 'Usage: ruby tools/check_ffi.rb shared-library' unless ARGV.size == 1
library = Fiddle.dlopen(File.expand_path(ARGV[0]))
api = Module.new do
  extend Fiddle::Importer
  dlload library
  extern 'unsigned int LND_ConfigGetKeyCount()'
  extern 'void *LND_ConfigGetKey(unsigned int)'
  extern 'void *LND_ConfigFindKey(const char *)'
  extern 'const char *LND_ConfigKeyGetName(void *)'
  extern 'int LND_ConfigSet(void *, unsigned long long)'
  extern 'unsigned long long LND_ConfigGet(void *)'
  extern 'int LND_LibraryInit()'
  extern 'void LND_LibraryFree()'
end
available = lambda do |name|
  library[name]
  true
rescue Fiddle::DLError
  false
end
check = ->(value) { raise 'FFI contract failed' unless value }
keys = api.LND_ConfigGetKeyCount.times.map do |index|
  key = api.LND_ConfigGetKey(index)
  name = api.LND_ConfigKeyGetName(key).to_s
  check.call(!key.null? && !name.empty? && api.LND_ConfigFindKey(name).to_i == key.to_i)
  [key, name, api.LND_ConfigGet(key)]
end
check.call(keys.size >= 3 && keys.map { |entry| entry[1] }.uniq.size == keys.size)
check.call(api.LND_ConfigFindKey('missing.key').null?)
check.call(api.LND_ConfigGetKey(keys.size).null?)
check.call(api.LND_ConfigSet(nil, 0) == -1)
mode = api.LND_ConfigFindKey('core.run_mode')
check.call(api.LND_ConfigSet(mode, 2).zero? && api.LND_ConfigGet(mode) == 2)
backend = nil
has_devices = begin
  library['LND_DeviceBackendGetCount']
rescue Fiddle::DLError
  false
end
if has_devices
  api.module_eval do
    extern 'unsigned int LND_DeviceBackendGetCount()'
    extern 'void *LND_DeviceBackendGet(unsigned int)'
    extern 'void *LND_DeviceBackendFind(const char *)'
    extern 'const char *LND_DeviceBackendGetName(void *)'
    extern 'int LND_DeviceSetPreferredBackend(void *)'
    extern 'void *LND_DeviceGetPreferredBackend()'
    extern 'void *LND_DeviceGetActiveBackend()'
    extern 'void *LND_DeviceGetDefaultHandle(int)'
    extern 'unsigned int LND_DeviceGetCount(int)'
  end
check.call(api.LND_DeviceGetActiveBackend.null?)
[0, 1].each do |type|
  symbol = type.zero? ? 'LND_DEVICE_DEFAULT_OUTPUT' : 'LND_DEVICE_DEFAULT_INPUT'
  exported = Fiddle::Pointer.new(library[symbol])[0, Fiddle::SIZEOF_VOIDP].unpack1('J')
  handle = api.LND_DeviceGetDefaultHandle(type)
  check.call(!handle.null? && handle.to_i == exported && api.LND_DeviceGetDefaultHandle(type).to_i == handle.to_i)
end
check.call(api.LND_DeviceGetDefaultHandle(2).null?)
  count = api.LND_DeviceBackendGetCount
  check.call(count.positive?)
  names = count.times.map do |index|
    handle = api.LND_DeviceBackendGet(index)
    name = api.LND_DeviceBackendGetName(handle).to_s
    check.call(api.LND_DeviceBackendFind(name).to_i == handle.to_i)
    name
  end
  check.call(names.uniq.size == count && api.LND_DeviceBackendGet(count).null?)
  check.call(api.LND_DeviceBackendFind('missing').null?)
  check.call(api.LND_DeviceSetPreferredBackend(nil).zero? && api.LND_DeviceGetPreferredBackend.null?)
  backend = api.LND_DeviceBackendFind('null')
  backend = api.LND_DeviceBackendGet(0) if backend.null?
  check.call(api.LND_DeviceSetPreferredBackend(backend).zero? && api.LND_DeviceGetPreferredBackend.to_i == backend.to_i)
  puts "Ruby FFI: #{count} backends (#{names.join(', ')})"
end
stretch = []
if available.call('LND_StretchBackendGetCount')
  api.module_eval do
    extern 'unsigned int LND_StretchBackendGetCount()'
    extern 'void *LND_StretchBackendGet(unsigned int)'
    extern 'void *LND_StretchBackendFind(const char *)'
    extern 'const char *LND_StretchBackendGetName(void *)'
    extern 'int LND_StretchBackendGetPriority(void *)'
    extern 'int LND_StretchBackendSetPriority(void *, int)'
    extern 'void *LND_NodeCreateTempo(unsigned int, unsigned int, float)'
    extern 'void *LND_NodeGetStretchBackend(void *)'
    extern 'int LND_NodeFree(void *)'
  end
  stretch = api.LND_StretchBackendGetCount.times.map do |index|
    handle = api.LND_StretchBackendGet(index)
    name = api.LND_StretchBackendGetName(handle).to_s
    check.call(!name.empty? && api.LND_StretchBackendFind(name).to_i == handle.to_i)
    [handle, api.LND_StretchBackendGetPriority(handle)]
  end
  check.call(api.LND_StretchBackendGet(stretch.size).null? && api.LND_StretchBackendFind('missing').null?)
  check.call(api.LND_StretchBackendSetPriority(stretch.first[0], 1000).zero?) unless stretch.empty?
end
%w[Codec Encoder].each do |kind|
  if available.call("LND_#{kind}GetCount")
    api.module_eval do
      extern "unsigned int LND_#{kind}GetCount()"
      extern "void *LND_#{kind}Get(unsigned int)"
      extern "void *LND_#{kind}Find(const char *)"
      extern "const char *LND_#{kind}GetName(void *)"
      extern "const char *LND_#{kind}GetExtensions(void *)"
      extern "unsigned int LND_#{kind}GetFlags(void *)"
    end
    count = api.public_send("LND_#{kind}GetCount")
    count.times do |index|
      handle = api.public_send("LND_#{kind}Get", index)
      name = api.public_send("LND_#{kind}GetName", handle).to_s
      check.call(!name.empty? && api.public_send("LND_#{kind}Find", name).to_i == handle.to_i)
      api.public_send("LND_#{kind}GetExtensions", handle)
      api.public_send("LND_#{kind}GetFlags", handle)
    end
    check.call(api.public_send("LND_#{kind}Find", 'missing').null?)
    puts "Ruby FFI: #{count} #{kind.downcase} descriptors"
  end
end
api.module_eval do
  extern 'long long LND_SourceRead(void *, void *, int, unsigned long long)'
end
check.call(api.LND_SourceRead(nil, nil, 5, 0) == -1)
if available.call('LND_SoundReadF32')
  api.module_eval { extern 'long long LND_SoundReadF32(void *, void *, unsigned long long)' }
  check.call(api.LND_SoundReadF32(nil, nil, 0) == -1)
end
if available.call('LND_IoRead')
  api.module_eval { extern 'long long LND_IoRead(void *, void *, size_t)' }
  check.call(api.LND_IoRead(nil, nil, 0) == -1)
end
begin
  check.call(api.LND_LibraryInit.zero?)
  check.call(api.LND_ConfigSet(mode, 1) == -2)
  if backend
    check.call(api.LND_DeviceGetActiveBackend.null?)
    api.LND_DeviceGetCount(0)
    check.call(api.LND_DeviceGetActiveBackend.null?)
  end
  unless stretch.empty?
    node = api.LND_NodeCreateTempo(1, 48000, 1.0)
    check.call(!node.null? && api.LND_NodeGetStretchBackend(node).to_i == stretch.first[0].to_i)
    check.call(api.LND_StretchBackendSetPriority(stretch.first[0], 1) == -2)
    check.call(api.LND_NodeFree(node).zero?)
  end
  check.call(api.LND_DeviceSetPreferredBackend(backend) == -2) if backend
ensure
  api.LND_LibraryFree
end
keys.each do |key, name, default|
  check.call(api.LND_ConfigFindKey(name).to_i == key.to_i && api.LND_ConfigGet(key) == default)
end
check.call(api.LND_DeviceGetPreferredBackend.null? && api.LND_DeviceGetActiveBackend.null?) if backend
puts "Ruby FFI: #{keys.size} configuration keys; lookup, identity, set/get, lifecycle and reset passed"
stretch.each { |handle, priority| check.call(api.LND_StretchBackendGetPriority(handle) == priority) }
puts "Ruby FFI: #{stretch.size} stretch backends; discovery, priority, node creation and reset passed" unless stretch.empty?
if backend && api.LND_DeviceBackendGetName(backend).to_s == 'null'
  check.call(api.LND_ConfigSet(mode, 0).zero?)
  check.call(api.LND_ConfigSet(api.LND_ConfigFindKey('devices.auto_open'), 0).zero?)
  check.call(api.LND_DeviceSetPreferredBackend(backend).zero?)
  begin
    result = api.LND_LibraryInit
    if result.zero?
      check.call(api.LND_DeviceGetActiveBackend.to_i == backend.to_i)
      puts 'Ruby FFI: active backend and logical default handles passed'
    else
      check.call(api.LND_DeviceGetActiveBackend.null?)
    end
  ensure
    api.LND_LibraryFree
  end
  check.call(api.LND_DeviceGetActiveBackend.null?)
end
