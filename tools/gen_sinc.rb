ROOT = File.expand_path('..', __dir__)
TAPS = [8, 16, 32].freeze
STRIDES = [1, 2, 4, 8, 0].freeze

def function(lines, name, body, arm:)
  sym = "LND_SYMBOL(#{name})"
  lines.concat(['.p2align 4', ".globl #{sym}", '#if !defined(_WIN32) && !defined(__APPLE__)',
    ".type #{sym}, #{arm ? '%' : '@'}function", ".hidden #{sym}", '#elif defined(__APPLE__)', ".private_extern #{sym}", '#endif', "#{sym}:"])
  lines.concat(body.map { |line| "    #{line}" })
  lines.concat(['#if !defined(_WIN32) && !defined(__APPLE__)', ".size #{sym}, .-#{sym}", '#endif'])
end

def header(arm:)
  ['#ifdef __APPLE__', '#define LND_SYMBOL(x) _##x', '#else', '#define LND_SYMBOL(x) x', '#endif',
    *(arm ? [] : ['.intel_syntax noprefix']), '.text']
end

def arm_dot(taps, stride)
  a = []
  if stride == 0
    a.concat(['uxtw x3, w3', 'lsl x3, x3, #2', 'lsl x4, x3, #1', 'add x5, x4, x3'])
  end
  groups = taps / 4
  if stride == 1
    (0...groups).step(2) do |g|
      a.concat(["ldp q0, q1, [x0, ##{g * 16}]", "ldp q2, q3, [x1, ##{g * 16}]",
        "fmul v#{16 + g}.4s, v0.4s, v2.4s", "fmul v#{17 + g}.4s, v1.4s, v3.4s"])
    end
  else
    groups.times do |g|
      if stride == 2
        if g == groups - 1
          a.concat(["ldur q0, [x0, ##{g * 32 - 4}]", "ldur q1, [x0, ##{g * 32 + 12}]", 'uzp2 v0.4s, v0.4s, v1.4s'])
        else
          a.concat(["ldp q0, q1, [x0, ##{g * 32}]", 'uzp1 v0.4s, v0.4s, v1.4s'])
        end
      else
        4.times do |lane|
          address = stride == 0 ? ['[x0]', '[x0, x3]', '[x0, x4]', '[x0, x5]'][lane] : "[x0, ##{(g * 4 + lane) * stride * 4}]"
          a << "ldr s#{lane}, #{address}"
        end
        a.concat(['zip1 v0.2s, v0.2s, v1.2s', 'zip1 v2.2s, v2.2s, v3.2s', 'zip1 v0.2d, v0.2d, v2.2d'])
        a << 'add x0, x0, x3, lsl #2' if stride == 0 && g != groups - 1
      end
      a.concat(["ldr q4, [x1, ##{g * 16}]", "fmul v#{16 + g}.4s, v0.4s, v4.4s"])
    end
  end
  width = groups / 2
  while width > 0
    width.times { |i| a << "fadd v#{16 + i}.4s, v#{16 + i}.4s, v#{16 + i + width}.4s" }
    width /= 2
  end
  a.concat(['dup v1.2d, v16.d[1]', 'fadd v0.2s, v16.2s, v1.2s', 'faddp s0, v0.2s', 'ret'])
end

def arm_tree(a, reg, first, step, count, channels, channel)
  shape = channels == 2 ? '2s' : '4s'
  if count == 1
    a << "ldr #{channels == 2 ? 'd' : 'q'}#{reg}, [x0, ##{(first * channels + channel) * 4}]"
    a << "fmul v#{reg}.#{shape}, v#{reg}.#{shape}, v#{16 + first / 4}.s[#{first % 4}]"
  else
    arm_tree(a, reg, first, step * 2, count / 2, channels, channel)
    arm_tree(a, reg + 1, first + step, step * 2, count / 2, channels, channel)
    a << "fadd v#{reg}.#{shape}, v#{reg}.#{shape}, v#{reg + 1}.#{shape}"
  end
end

def arm_tree_pair(a, reg, first, step, count, channels = 8, channel = 0)
  if count == 1
    a << "ldp q#{reg}, q#{24 + reg}, [x0, ##{(first * channels + channel) * 4}]"
    [reg, reg + 24].each { |r| a << "fmul v#{r}.4s, v#{r}.4s, v#{16 + first / 4}.s[#{first % 4}]" }
  else
    arm_tree_pair(a, reg, first, step * 2, count / 2, channels, channel)
    arm_tree_pair(a, reg + 1, first + step, step * 2, count / 2, channels, channel)
    [reg, reg + 24].each { |r| a << "fadd v#{r}.4s, v#{r}.4s, v#{r + 1}.4s" }
  end
end

def arm_frame_four(taps)
  a = []
  (0...taps / 4).step(2) { |g| a << "ldp q#{16 + g}, q#{17 + g}, [x1, ##{g * 16}]" }
  groups = taps / 4
  (0...groups).step(2) do |i|
    4.times do |g|
      t = i + g * groups
      a << "ldp q#{24 + g * 2}, q#{25 + g * 2}, [x0, ##{t * 16}]"
    end
    4.times do |g|
      2.times do |lane|
        t = i + g * groups + lane
        r = 24 + g * 2 + lane
        a << "fmul v#{r}.4s, v#{r}.4s, v#{16 + t / 4}.s[#{t % 4}]"
      end
    end
    2.times do |lane|
      a.concat(["fadd v#{24 + lane}.4s, v#{24 + lane}.4s, v#{28 + lane}.4s",
        "fadd v#{26 + lane}.4s, v#{26 + lane}.4s, v#{30 + lane}.4s",
        "fadd v#{i + lane}.4s, v#{24 + lane}.4s, v#{26 + lane}.4s"])
    end
  end
  n = groups / 2
  while n > 0
    n.times { |i| a << "fadd v#{i}.4s, v#{i}.4s, v#{i + n}.4s" }
    n /= 2
  end
  a.concat(['str q0, [x2]', 'ret'])
end

def arm_frame_stereo16
  a = ['ldp q16, q17, [x1]', 'ldp q18, q19, [x1, #32]']
  regs = (0...8).to_a + (24...32).to_a
  (0...16).step(2) { |i| a << "ldp d#{regs[i]}, d#{regs[i + 1]}, [x0, ##{i * 8}]" }
  16.times { |i| a << "fmul v#{regs[i]}.2s, v#{regs[i]}.2s, v#{16 + i / 4}.s[#{i % 4}]" }
  [8, 4, 2, 1].each { |n| n.times { |i| a << "fadd v#{regs[i]}.2s, v#{regs[i]}.2s, v#{regs[i + n]}.2s" } }
  a.concat(['str d0, [x2]', 'ret'])
end

def arm_frame(taps, channels)
  return arm_frame_stereo16 if taps == 16 && channels == 2
  if taps == 8 && channels == 2
    return ['ldp q0, q1, [x1]',
      'ldp q4, q5, [x0]',
      'ldp q6, q7, [x0, #32]',
      'zip1 v2.4s, v0.4s, v0.4s',
      'zip2 v0.4s, v0.4s, v0.4s',
      'zip1 v3.4s, v1.4s, v1.4s',
      'zip2 v1.4s, v1.4s, v1.4s',
      'fmul v2.4s, v4.4s, v2.4s',
      'fmul v0.4s, v0.4s, v5.4s',
      'fmul v3.4s, v6.4s, v3.4s',
      'fmul v1.4s, v1.4s, v7.4s',
      'fadd v2.4s, v2.4s, v3.4s',
      'fadd v0.4s, v0.4s, v1.4s',
      'fadd v0.4s, v2.4s, v0.4s',
      'ext v1.16b, v0.16b, v0.16b, #8',
      'fadd v0.2s, v0.2s, v1.2s',
      'str d0, [x2]',
      'ret']
  end
  return arm_frame_four(taps) if channels == 4 && taps == 32
  a = []
  if channels == 2
    groups = taps / 2
    (0...groups).step(4) do |g|
      a.concat(["ldp q0, q1, [x1, ##{g * 8}]", "ldp q4, q5, [x0, ##{g * 16}]", "ldp q6, q7, [x0, ##{g * 16 + 32}]",
        'zip1 v2.4s, v0.4s, v0.4s', 'zip2 v0.4s, v0.4s, v0.4s',
        'zip1 v3.4s, v1.4s, v1.4s', 'zip2 v1.4s, v1.4s, v1.4s',
        "fmul v#{16 + g}.4s, v4.4s, v2.4s", "fmul v#{17 + g}.4s, v5.4s, v0.4s",
        "fmul v#{18 + g}.4s, v6.4s, v3.4s", "fmul v#{19 + g}.4s, v7.4s, v1.4s"])
    end
    width = groups / 2
    while width > 0
      width.times { |i| a << "fadd v#{16 + i}.4s, v#{16 + i}.4s, v#{16 + i + width}.4s" }
      width /= 2
    end
    a.concat(['ext v1.16b, v16.16b, v16.16b, #8', 'fadd v0.2s, v16.2s, v1.2s', 'str d0, [x2]'])
  elsif taps == 8 && channels == 4
    a << 'ldp q16, q17, [x1]'
    4.times { |g| a << "ldp q#{g * 2}, q#{g * 2 + 1}, [x0, ##{g * 32}]" }
    8.times { |i| a << "fmul v#{i}.4s, v#{i}.4s, v#{16 + i / 4}.s[#{i % 4}]" }
    [4, 2, 1].each { |n| n.times { |i| a << "fadd v#{i}.4s, v#{i}.4s, v#{i + n}.4s" } }
    a << 'str q0, [x2]'
  else
    (0...taps / 4).step(2) { |g| a << "ldp q#{16 + g}, q#{17 + g}, [x1, ##{g * 16}]" }
    if channels == 8
      arm_tree_pair(a, 0, 0, 1, taps)
      a << 'stp q0, q24, [x2]'
    else
      arm_tree(a, 0, 0, 1, taps, channels, 0)
      a << 'str q0, [x2]'
    end
  end
  a << 'ret'
end


def arm_dynamic_tree(a, reg, first, step, count, width)
  if count == 1
    address = '[x0]'
    if first > 0
      if first & (first - 1) == 0
        a << "add x9, x0, x5, lsl ##{Math.log2(first).to_i}"
      else
        a.concat(["mov x9, ##{first}", 'madd x9, x5, x9, x0'])
      end
      address = '[x9]'
    end
    if width == 8
      a << "ldp q#{reg}, q#{24 + reg}, #{address}"
      [reg, reg + 24].each { |r| a << "fmul v#{r}.4s, v#{r}.4s, v#{16 + first / 4}.s[#{first % 4}]" }
    elsif width == 4
      a.concat(["ldr q#{reg}, #{address}", "fmul v#{reg}.4s, v#{reg}.4s, v#{16 + first / 4}.s[#{first % 4}]"])
    else
      a.concat(["ldr s#{reg}, #{address}", "fmul s#{reg}, s#{reg}, v#{16 + first / 4}.s[#{first % 4}]"])
    end
  else
    arm_dynamic_tree(a, reg, first, step * 2, count / 2, width)
    arm_dynamic_tree(a, reg + 1, first + step, step * 2, count / 2, width)
    if width == 1
      a << "fadd s#{reg}, s#{reg}, s#{reg + 1}"
    else
      a << "fadd v#{reg}.4s, v#{reg}.4s, v#{reg + 1}.4s"
      a << "fadd v#{24 + reg}.4s, v#{24 + reg}.4s, v#{25 + reg}.4s" if width == 8
    end
  end
end

def arm_frame_dynamic(taps)
  a = ['uxtw x5, w4', 'lsl x5, x5, #2']
  (0...taps / 4).step(2) { |g| a << "ldp q#{16 + g}, q#{17 + g}, [x1, ##{g * 16}]" }
  [8, 4, 1].each do |width|
    label = ".Larm_frame_#{taps}_#{width}"
    a.concat(["cmp w4, ##{width}", "b.lo #{label}_end", "#{label}:"])
    arm_dynamic_tree(a, 0, 0, 1, taps, width)
    a << (width == 8 ? 'stp q0, q24, [x2]' : width == 4 ? 'str q0, [x2]' : 'str s0, [x2]')
    a.concat(["add x0, x0, ##{width * 4}", "add x2, x2, ##{width * 4}", "sub w4, w4, ##{width}",
      "cmp w4, ##{width}", "b.hs #{label}", "#{label}_end:"])
  end
  a << 'ret'
end

def tree_ops(reg, first, step, count)
  return [["LEAF", reg, first]] if count == 1
  tree_ops(reg, first, step * 2, count / 2) +
    tree_ops(reg + 1, first + step, step * 2, count / 2) + [["ADD", reg, reg + 1]]
end

tree = ["#pragma once"]
[4, *TAPS].each do |taps|
  ops = tree_ops(0, 0, 1, taps).map { |op, r, i| "#{op}(#{r}, #{i})" }
  tree << "#define LND_SINC_TREE_#{taps}(LEAF, ADD) " + ops.join(" " + 92.chr + 10.chr + "    ")
end
tree << '#define LND_SINC_TREE(T, LEAF, ADD) LND_SINC_TREE_##T(LEAF, ADD)'
File.write(File.join(ROOT, 'modules/pcm/audio/sinc_tree.h'), tree.join("\n") + "\n")

arm = header(arm: true)
TAPS.each do |taps|
  STRIDES.each { |stride| function(arm, "lnd_sinc_arm64_dot_#{taps}_#{stride}", arm_dot(taps, stride), arm: true) }
  [2, 4, 8].each { |channels| function(arm, "lnd_sinc_arm64_frame_#{taps}_#{channels}", arm_frame(taps, channels), arm: true) }
  function(arm, "lnd_sinc_arm64_frame_#{taps}_0", arm_frame_dynamic(taps), arm: true)
end
[16, 32].each do |channels|
  body = ['ldp q16, q17, [x1]']
  (0...channels).step(8) do |channel|
    arm_tree_pair(body, 0, 0, 1, 8, channels, channel)
    body << "stp q0, q24, [x2, ##{channel * 4}]"
  end
  function(arm, "lnd_sinc_arm64_frame_8_#{channels}", body + ['ret'], arm: true)
end
arm.concat(['#if !defined(_WIN32) && !defined(__APPLE__)', '.section .note.GNU-stack,"",%progbits', '#endif'])
File.write(File.join(ROOT, 'modules/pcm/sinc_asm/arm64.S'), arm.join("\n") + "\n")

def xscaled_base(a, tap, third = false)
  base = 'LND_SRC'
  terms = [[8, 'r10', 8], [4, 'r10', 4], [2, 'r10', 2], [1, 'r10', 1]]
  terms += [[24, 'rax', 8], [12, 'rax', 4], [6, 'rax', 2], [3, 'rax', 1]] if third
  terms.sort_by { |value, _, _| -value }.each do |value, reg, scale|
    while tap >= value
      index = scale == 1 ? reg : "#{reg} * #{scale}"
      a << "lea r11, [#{base} + #{index}]"
      base = 'r11'
      tap -= value
    end
  end
  base
end

def xaddr(a, tap, stride, extra = 0)
  base = stride == 0 ? xscaled_base(a, tap) : 'LND_SRC'
  offset = stride == 0 ? extra : tap * stride * 4 + extra
  "[#{base}#{offset == 0 ? '' : " + #{offset}"}]"
end

def xgather4(a, reg, tap, taps, stride, vex, temp1 = 4, temp2 = 5)
  v = vex ? 'v' : ''
  if stride == 1
    a << "#{v}movups xmm#{reg}, #{xaddr(a, tap, stride)}"
  elsif stride == 2
    odd = tap == taps - 4
    base = tap * 8 - (odd ? 4 : 0)
    a << "#{v}movups xmm#{reg}, [LND_SRC + #{base}]"
    if vex
      a << "vshufps xmm#{reg}, xmm#{reg}, [LND_SRC + #{base + 16}], #{odd ? '0xdd' : '0x88'}"
    else
      a << "movups xmm#{temp1}, [LND_SRC + #{base + 16}]"
      a << "shufps xmm#{reg}, xmm#{temp1}, #{odd ? '0xdd' : '0x88'}"
    end
  else
    if stride == 0
      base = xscaled_base(a, tap, true)
      addresses = ["[#{base}]", "[#{base} + r10]", "[#{base} + r10 * 2]", "[#{base} + rax]"]
    else
      addresses = 4.times.map { |i| "[LND_SRC + #{(tap + i) * stride * 4}]" }
    end
    a << "#{v}movss xmm#{reg}, #{addresses[0]}"
    a << "#{v}movss xmm#{temp1}, #{addresses[1]}"
    a << (vex ? "vunpcklps xmm#{reg}, xmm#{reg}, xmm#{temp1}" : "unpcklps xmm#{reg}, xmm#{temp1}")
    a << "#{v}movss xmm#{temp1}, #{addresses[2]}"
    a << "#{v}movss xmm#{temp2}, #{addresses[3]}"
    a << (vex ? "vunpcklps xmm#{temp1}, xmm#{temp1}, xmm#{temp2}" : "unpcklps xmm#{temp1}, xmm#{temp2}")
    a << (vex ? "vmovlhps xmm#{reg}, xmm#{reg}, xmm#{temp1}" : "movlhps xmm#{reg}, xmm#{temp1}")
  end
end

def xdot_leaf(a, reg, group, taps, stride, width)
  tap = group * width
  vector = width == 4 ? 'xmm' : width == 8 ? 'ymm' : 'zmm'
  if width == 4
    xgather4(a, reg, tap, taps, stride, false)
    a.concat(["movups xmm4, [LND_H + #{tap * 4}]", "mulps xmm#{reg}, xmm4"])
  elsif width == 8
    if stride == 1
      a << "vmovups ymm#{reg}, [LND_SRC + #{tap * 4}]"
    else
      xgather4(a, reg, tap, taps, stride, true)
      xgather4(a, 3, tap + 4, taps, stride, true)
      a << "vinsertf128 ymm#{reg}, ymm#{reg}, xmm3, 1"
    end
    a << "vmulps ymm#{reg}, ymm#{reg}, [LND_H + #{tap * 4}]"
  else
    if stride == 1
      a << "vmovups zmm#{reg}, [LND_SRC + #{tap * 4}]"
    elsif stride == 2
      a << "vmovups zmm#{reg}, [LND_SRC + #{tap * 8}]"
      a << "vpermt2ps zmm#{reg}, zmm4, [LND_SRC + #{tap * 8 + 60}]"
    else
      a.concat(['mov eax, 65535', 'kmovw k1, eax'])
      if stride == 0
        base = xscaled_base(a, tap)
        a << "vgatherdps zmm#{reg}{k1}, [#{base} + zmm4 * 4]"
      else
        a << "vgatherdps zmm#{reg}{k1}, [LND_SRC + zmm4 * 4 + #{tap * stride * 4}]"
      end
    end
    a << "vmulps zmm#{reg}, zmm#{reg}, [LND_H + #{tap * 4}]"
  end
end

def xdot(taps, stride, width)
  a = []
  if stride == 0
    a << 'mov r10d, LND_STEP'
    if width == 16
      a.concat(['vpbroadcastd zmm4, r10d', 'vpmulld zmm4, zmm4, [rip + .Lindex1]'])
    end
    a << 'shl r10, 2'
    a << 'lea rax, [r10 + r10 * 2]' if width != 16
  elsif width == 16 && stride != 1
    a << "vmovdqu32 zmm4, [rip + .Lindex#{stride}]"
  end
  vector = width == 4 ? 'xmm' : width == 8 ? 'ymm' : 'zmm'
  tree_ops(0, 0, 1, taps / width).each do |op, reg, n|
    if op == 'LEAF'
      xdot_leaf(a, reg, n, taps, stride, width)
    else
      a << (width == 4 ? "addps xmm#{reg}, xmm#{n}" : "vaddps #{vector}#{reg}, #{vector}#{reg}, #{vector}#{n}")
    end
  end
  if width == 16
    a.concat(['vextractf64x4 ymm4, zmm0, 1', 'vaddps ymm0, ymm0, ymm4'])
  end
  if width >= 8
    a.concat(['vextractf128 xmm4, ymm0, 1', 'vaddps xmm0, xmm0, xmm4',
      'vmovhlps xmm4, xmm0, xmm0', 'vaddps xmm0, xmm0, xmm4', 'vshufps xmm4, xmm0, xmm0, 0x55', 'vaddss xmm0, xmm0, xmm4', 'vzeroupper'])
  else
    a.concat(['movhlps xmm4, xmm0', 'addps xmm0, xmm4', 'movaps xmm4, xmm0', 'shufps xmm4, xmm4, 0x55', 'addss xmm0, xmm4'])
  end
  a << 'ret'
end

def xframe_tree(a, taps, channels, channel, width, vex)
  previous = 0
  vector = width == 1 || width == 4 ? 'xmm' : width == 8 ? 'ymm' : 'zmm'
  tree_ops(0, 0, 1, taps).each do |op, reg, n|
    if op == 'LEAF'
      if channels == 0
        if n < taps / 2
          delta = n - previous
          if delta > 0
            raise 'unexpected sinc tree order' unless delta == taps / 4
            a << 'add LND_SRC, r11'
          elsif delta < 0
            [8, 4, 2, 1].each do |scale|
              a << "lea LND_SRC, [LND_SRC + r10#{scale == 1 ? '' : " * #{scale}"}]" if (-delta & scale) != 0
            end
          end
          previous = n
          source = '[LND_SRC]'
        else
          raise 'unexpected sinc tree pair' unless n == previous + taps / 2
          source = '[LND_SRC + rax]'
        end
      else
        source = xaddr(a, n, channels, channel * 4)
      end
      if width == 1
        a << "#{vex ? 'v' : ''}movss xmm#{reg}, #{source}"
        a << (vex ? "vmulss xmm#{reg}, xmm#{reg}, [LND_H + #{n * 4}]" : "mulss xmm#{reg}, [LND_H + #{n * 4}]")
      elsif vex
        a << "vbroadcastss #{width == 4 ? 'ymm' : vector}#{reg}, [LND_H + #{n * 4}]"
        a << "vmulps #{vector}#{reg}, #{vector}#{reg}, #{source}"
      else
        a.concat(["movss xmm#{reg}, [LND_H + #{n * 4}]", "shufps xmm#{reg}, xmm#{reg}, 0",
          "movups xmm6, #{source}", "mulps xmm#{reg}, xmm6"])
      end
    else
      suffix = width == 1 ? 'ss' : 'ps'
      a << (vex ? "vadd#{suffix} #{vector}#{reg}, #{vector}#{reg}, #{vector}#{n}" : "add#{suffix} xmm#{reg}, xmm#{n}")
    end
  end
  a.concat(['sub LND_SRC, rax', 'sub LND_SRC, r10']) if channels == 0
end

def xframe_stereo(a, taps, width)
  tree_ops(0, 0, 1, taps * 2 / width).each do |op, reg, n|
    vector = width == 4 ? 'xmm' : 'ymm'
    if op == 'LEAF'
      if width == 4
        a.concat(["movq xmm5, [LND_H + #{n * 8}]", 'unpcklps xmm5, xmm5',
          "movups xmm#{reg}, [LND_SRC + #{n * 16}]", "mulps xmm#{reg}, xmm5"])
      else
        a.concat(["vmovups xmm4, [LND_H + #{n * 16}]", 'vunpcklps xmm5, xmm4, xmm4', 'vunpckhps xmm4, xmm4, xmm4',
          'vinsertf128 ymm5, ymm5, xmm4, 1', "vmulps ymm#{reg}, ymm5, [LND_SRC + #{n * 32}]"])
      end
    else
      a << (width == 4 ? "addps xmm#{reg}, xmm#{n}" : "vaddps ymm#{reg}, ymm#{reg}, ymm#{n}")
    end
  end
  if width == 4
    a.concat(['movhlps xmm5, xmm0', 'addps xmm0, xmm5', 'movq [LND_OUT], xmm0'])
  else
    a.concat(['vextractf128 xmm4, ymm0, 1', 'vaddps xmm0, xmm0, xmm4', 'vmovhlps xmm4, xmm0, xmm0',
      'vaddps xmm0, xmm0, xmm4', 'vmovq [LND_OUT], xmm0'])
  end
end

def xframe(taps, channels, width, name)
  a = []
  save = width == 4 && channels != 2
  if save
    a.concat(['#ifdef _WIN32', ".seh_proc LND_SYMBOL(#{name})", '#endif'])
  end
  if channels == 0
    a.concat(['#ifdef _WIN32', 'mov r9d, DWORD PTR [rsp + 40]', '#else', 'mov r9d, r8d', '#endif',
      'mov r10d, r9d', 'shl r10, 2', "lea r11, [r10 * #{taps / 4}]", 'lea rax, [r11 + r11]', 'neg r10'])
  end
  if save
    a.concat(['#ifdef _WIN32', 'sub rsp, 24', '.seh_stackalloc 24', 'movaps [rsp], xmm6', '.seh_savexmm xmm6, 0', '.seh_endprologue', '#endif'])
  end
  if channels == 2
    xframe_stereo(a, taps, width)
  elsif channels > 0
    block = [channels, width].min
    (0...channels).step(block) do |channel|
      xframe_tree(a, taps, channels, channel, block, width != 4)
      a << "#{width == 4 ? '' : 'v'}movups [LND_OUT + #{channel * 4}], #{block == 4 ? 'xmm' : 'ymm'}0"
    end
  else
    [width, *(width == 16 ? [8, 4] : width == 8 ? [4] : []), 1].each do |block|
      label = ".L#{name}_#{block}"
      a.concat(["cmp r9d, #{block}", "jb #{label}_end", "#{label}:"])
      xframe_tree(a, taps, 0, 0, block, width != 4)
      vector = block <= 4 ? 'xmm' : block == 8 ? 'ymm' : 'zmm'
      a << "#{width == 4 ? '' : 'v'}mov#{block == 1 ? 'ss' : 'ups'} [LND_OUT], #{vector}0"
      a.concat(["add LND_SRC, #{block * 4}", "add LND_OUT, #{block * 4}", "sub r9d, #{block}",
        "cmp r9d, #{block}", "jae #{label}", "#{label}_end:"])
    end
  end
  a << 'vzeroupper' if width != 4
  if save
    a.concat(['#ifdef _WIN32', 'movaps xmm6, [rsp]', 'add rsp, 24', '#endif'])
  end
  a << 'ret'
  a.concat(['#ifdef _WIN32', '.seh_endproc', '#endif']) if save
  a
end

x64 = header(arm: false)
x64.concat(['#ifdef _WIN32', '#define LND_SRC rcx', '#define LND_H rdx', '#define LND_OUT r8', '#define LND_STEP r9d', '#else',
  '#define LND_SRC rdi', '#define LND_H rsi', '#define LND_OUT rdx', '#define LND_STEP ecx', '#endif'])
[['sse2', 4], ['avx', 8], ['avx512', 16]].each do |level, width|
  TAPS.each do |taps|
    if taps >= width
      STRIDES.each do |stride|
        name = "lnd_sinc_x64_#{level}_dot_#{taps}_#{stride}"
        function(x64, name, xdot(taps, stride, width), arm: false)
      end
    end
    (width == 16 ? [0] : [2, 4, 8, 0]).each do |channels|
      name = "lnd_sinc_x64_#{level}_frame_#{taps}_#{channels}"
      function(x64, name, xframe(taps, channels, width, name), arm: false)
    end
  end
end
x64.concat(['#ifdef _WIN32', '.section .rdata,"dr"', '#elif defined(__APPLE__)', '.section __TEXT,__const', '#else', '.section .rodata', '#endif'])
{1 => (0...16).to_a, 2 => ((0...8).map { |i| i * 2 } + (0...8).map { |i| 17 + i * 2 }), 4 => (0...16).map { |i| i * 4 }, 8 => (0...16).map { |i| i * 8 }}.each do |stride, indices|
  x64.concat(['.p2align 6', ".Lindex#{stride}:", '.long ' + indices.join(', ')])
end
x64.concat(['#if !defined(_WIN32) && !defined(__APPLE__)', '.section .note.GNU-stack,"",@progbits', '#endif'])
File.write(File.join(ROOT, 'modules/pcm/sinc_asm/x64.S'), x64.join("\n") + "\n")
