require 'fileutils'
require 'open3'

root = ENV.fetch('LND_WORKSPACE') { File.file?(File.join(Dir.pwd, 'CMakeLists.txt')) ? Dir.pwd : File.expand_path('..', __dir__) }
with_opus = ARGV.delete('--opus')
with_adpcm = ARGV.delete('--adpcm')
with_queue = ARGV.delete('--queue')
abort 'Choose --queue, --opus or --adpcm' if with_queue && (with_opus || with_adpcm)
abort 'Choose --opus or --adpcm' if with_opus && with_adpcm
with_file = with_opus || with_adpcm
build = File.join(root, 'build', with_opus ? 'mcu-opus-check' : with_adpcm ? 'mcu-adpcm-check' : with_queue ? 'mcu-queue-check' : 'mcu-check')
headers = File.join(build, 'headers')
FileUtils.mkdir_p(headers)
File.write(File.join(headers, 'string.h'), <<'C')
#pragma once
#include <stddef.h>
void *memcpy(void *restrict dst, const void *restrict src, size_t bytes);
void *memmove(void *dst, const void *src, size_t bytes);
void *memset(void *dst, int value, size_t bytes);
size_t strlen(const char *text);
size_t strcspn(const char *text, const char *reject);
int memcmp(const void *a, const void *b, size_t bytes);
int strcmp(const char *a, const char *b);
char *strcpy(char *dst, const char *src);
int strncmp(const char *a, const char *b, size_t size);
char *strchr(const char *text, int value);
void *memchr(const void *memory, int value, size_t size);
C

if with_opus
  File.write(File.join(headers, 'errno.h'), "#pragma once\n")
  File.write(File.join(headers, 'stdlib.h'), <<'C')
#pragma once
#include <stddef.h>
void *malloc(size_t bytes);
void *calloc(size_t count, size_t bytes);
void *realloc(void *memory, size_t bytes);
void free(void *memory);
_Noreturn void abort(void);
int abs(int value);
long labs(long value);
int rand(void);
void srand(unsigned seed);
C
  File.write(File.join(headers, 'stdio.h'), <<'C')
#pragma once
#include <stddef.h>
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
typedef struct lnd_test_file FILE;
extern FILE *stderr;
extern FILE *stdout;
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
C
  File.write(File.join(headers, 'alloca.h'), "#pragma once\n#define alloca(bytes) __builtin_alloca(bytes)\n")
  File.write(File.join(headers, 'math.h'), <<'C')
#pragma once
#define M_PI 3.14159265358979323846
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
double fabs(double x);
float fabsf(float x);
double sqrt(double x);
float sqrtf(float x);
double floor(double x);
float floorf(float x);
double ceil(double x);
float ceilf(float x);
double exp(double x);
float expf(float x);
double log(double x);
float logf(float x);
double log10(double x);
float log10f(float x);
double log2(double x);
float log2f(float x);
double sin(double x);
float sinf(float x);
double cos(double x);
float cosf(float x);
double acos(double x);
double atan(double x);
double tanh(double x);
float tanhf(float x);
double pow(double x, double y);
float powf(float x, float y);
long lrint(double x);
long lrintf(float x);
double round(double x);
float roundf(float x);
C
end

def run(*args)
  out, err, status = Open3.capture3(*args)
  abort "#{args.join(' ')}\n#{out}\n#{err}" unless status.success?
  out
end

cmake = ENV.fetch('CMAKE', 'cmake')
clang = ENV.fetch('CLANG', 'clang')
nm = ENV.fetch('LLVM_NM', 'llvm-nm')
linker = ENV.fetch('LD_LLD', 'ld.lld')
%w[cortex-m0plus cortex-m33].each do |cpu|
  output = File.join(build, cpu)
  configure = [cmake, '-S', root, '-B', output, '-G', 'Ninja', '-DCMAKE_SYSTEM_NAME=Generic',
    "-DCMAKE_C_COMPILER=#{clang}", '-DCMAKE_C_COMPILER_TARGET=arm-none-eabi', '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY',
    '-DCMAKE_BUILD_TYPE=Release', '-DLND_MINIMAL=ON', '-DLND_OS_MODE=OFF', '-DLND_THREADS=OFF', '-DLND_USE_LIBC_ALLOC=OFF', '-DLND_BUILD_TESTS=OFF', '-DLND_BUILD_EXAMPLES=OFF',
    '-DLND_LTO=OFF', "-DCMAKE_C_FLAGS=-mcpu=#{cpu} -mthumb -mfloat-abi=soft -ffreestanding -isystem \"#{headers}\""]
  configure.concat(%w[-DLND_MODULE_OPUS=ON -DLND_MODULE_OGG=ON -DOPUS_FIXED_POINT=ON -DOPUS_ENABLE_FLOAT_API=OFF -DOPUS_DISABLE_INTRINSICS=ON -DOPUS_HARDENING=OFF -DOPUS_STACK_PROTECTOR=OFF -DOPUS_FORTIFY_SOURCE=OFF]) if with_opus
  configure << '-DLND_MODULE_ADPCM=ON' if with_adpcm
  configure << '-DLND_MODULE_QUEUE=ON' if with_queue
  configure.concat(%w[-DLND_MODULE_IO=ON -DLND_MODULE_CODECS=ON -DLND_BUILD_ENCODERS=OFF]) if with_file
  configure << "-DCMAKE_MAKE_PROGRAM=#{ENV['NINJA']}" if ENV['NINJA']
  run(*configure)
  run(cmake, '--build', output, '--parallel')
  example = File.join(output, with_file ? 'file_mcu.o' : 'sine_mcu.o')
  run(clang, '--target=arm-none-eabi', "-mcpu=#{cpu}", '-mthumb', '-mfloat-abi=soft', '-ffreestanding', '-std=c23', '-Os',
    '-ffunction-sections', '-fdata-sections', '-DLND_OS_MODE=0', '-DLND_THREADS=0', '-DLND_USE_LIBC_ALLOC=0',
    '-I', File.join(root, 'include'), '-I', File.join(output, 'generated'), '-c', File.join(root, with_file ? 'examples/mcu/file_mcu/file_mcu.c' : 'examples/mcu/sine_mcu/sine_mcu.c'), '-o', example)
  linked = File.join(output, with_file ? 'file_core.o' : 'sine_core.o')
  roots = with_file ? %w[file_mcu_configure file_mcu_init file_mcu_fill file_mcu_free LND_LibraryFree LND_SourceCreateEncodedInput] : %w[sine_mcu_configure sine_mcu_memory_size sine_mcu_init sine_mcu_fill sine_mcu_free LND_LibraryFree]
  roots.concat(%w[LND_QueueInit LND_QueueWritePcm LND_QueueDiscardFrames LND_QueueReset LND_SourceEnd LND_SourceGetStatus]) if with_queue
  link = [linker, '-m', 'armelf', '-r', '--gc-sections'] + roots.flat_map { |symbol| ['-u', symbol] }
  link.concat([example, File.join(output, 'liblindar.a')])
  link.concat([File.join(output, 'liblnd_opusfile.a'), File.join(output, 'liblnd_ogg.a'), File.join(output, 'vendor', 'opus', 'libopus.a')]) if with_opus
  run(*link, '-o', linked)
  undefined = run(nm, '--undefined-only', linked)
  symbols = undefined.lines.map { |line| line.split.last }
  allowed = /\A(?:memcpy|memmove|memset|abs|__clzsi2|__aeabi_(?:mem\w+|[uldi]+div\w*|llsl|llsr|lasr|lmul))\z/
  allowed_file = /\A(?:strlen|strcspn|strcmp|strncmp|strchr|memcmp)\z/
  allowed_opus = /\A(?:malloc|calloc|realloc|free|memchr|qsort)\z/
  unexpected = symbols.reject { |symbol| symbol.match?(allowed) || (with_file && symbol.match?(allowed_file)) || (with_opus && symbol.match?(allowed_opus)) }
  abort "#{cpu}: unexpected runtime symbols: #{unexpected.join(', ')}" unless unexpected.empty?
  File.write(File.join(output, 'runtime-symbols.txt'), undefined)
  puts "#{cpu}: ARM soft-float #{with_opus ? 'Ogg Opus playback' : with_adpcm ? 'ADPCM WAV playback' : with_queue ? 'PCM queue playback' : 'sine playback'} compiled; runtime: #{symbols.join(', ')}"
end
puts 'Relocatable object check only; a target SDK must supply integer division, memory routines, startup and hardware drivers.'
puts 'Ogg Opus requires the target libc allocator for libopusfile/libogg/libopus; core and ADPCM use the application arena.' if with_opus
