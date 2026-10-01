values = (0..256).map { |n| (Math.sin(n * Math::PI / 512) * 32767).round }
path = File.expand_path('../examples/mcu/sine_mcu/sine_table.h', __dir__)
File.write(path, "#pragma once\n\n#include <stdint.h>\n\nstatic const int16_t sine_quarter[257] = {\n" + values.each_slice(16).map { |row| '    ' + row.join(', ') + ',' }.join("\n") + "\n};\n")
