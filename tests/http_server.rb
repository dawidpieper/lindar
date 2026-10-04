require 'socket'
require 'openssl'
require 'tmpdir'
require 'fileutils'

binary = File.expand_path(ARGV.fetch(0))
samples = File.expand_path('audiosamples')
key = OpenSSL::PKey::RSA.new(2048)
cert = OpenSSL::X509::Certificate.new
cert.version = 2
cert.serial = 1
cert.subject = cert.issuer = OpenSSL::X509::Name.parse('/CN=Lindar HTTP tests')
cert.public_key = key.public_key
cert.not_before = Time.now - 3600
cert.not_after = Time.now + 86400
factory = OpenSSL::X509::ExtensionFactory.new
factory.subject_certificate = factory.issuer_certificate = cert
cert.add_extension(factory.create_extension('basicConstraints', 'CA:TRUE', true))
cert.add_extension(factory.create_extension('subjectAltName', 'IP:127.0.0.1'))
cert.sign(key, OpenSSL::Digest.new('SHA256'))
context = OpenSSL::SSL::SSLContext.new
context.cert = cert
context.key = key
context.min_version = OpenSSL::SSL::TLS1_2_VERSION
plain = TCPServer.new('127.0.0.1', 0)
cross = TCPServer.new('127.0.0.1', 0)
secure_tcp = TCPServer.new('127.0.0.1', 0)
secure = OpenSSL::SSL::SSLServer.new(secure_tcp, context)
base = "http://127.0.0.1:#{plain.addr[1]}"
other = "http://127.0.0.1:#{cross.addr[1]}"
tls = "https://127.0.0.1:#{secure_tcp.addr[1]}"
requests = Hash.new(0)
mutex = Mutex.new
errors = Queue.new
workers = []
aes_key = (0...16).to_a.pack('C*')
large = File.binread(File.join(samples, 'tone.m4a'))
at = 0
seen_mdat = false
loop do
  size, type = large.byteslice(at, 8).unpack('Na4')
  if type == 'moov'
    raise 'Expected trailing moov' unless seen_mdat
    large.insert(at, [8 * 1024 * 1024, 'free'].pack('Na4') + "\0" * (8 * 1024 * 1024 - 8))
    break
  end
  seen_mdat ||= type == 'mdat'
  raise 'Invalid fixture' if size < 8 || at + size >= large.bytesize
  at += size
end
serve = lambda do |socket, origin|
  loop do
    line = socket.gets
    break unless line
    method, target = line.split
    raise 'method' unless method == 'GET'
    path = target.split('?').first
    headers = {}
    while (header = socket.gets) && header != "\r\n"
      name, value = header.split(':', 2)
      headers[name.downcase] = value.to_s.strip
    end
    agent = path == '/ua' ? 'LindarTest/2' : path == '/no-agent' ? nil : 'Lindar'
    raise "User-Agent mismatch at #{path}" unless headers['user-agent'] == agent
    if path == '/cross-target'
      raise 'Credentials crossed origin' if headers.key?('authorization') || headers.key?('x-test')
    elsif !headers.key?('authorization')
      raise "Authorization missing at #{path}" unless path == '/stall' || path == '/slow'
    end
    count = mutex.synchronize { requests[path] += 1 }
    response = lambda do |status, body, extra = {}|
      fields = {'Content-Length' => body.bytesize.to_s, 'Connection' => 'keep-alive'}.merge(extra)
      socket.write("HTTP/1.1 #{status}\r\n" + fields.map { |k,v| "#{k}: #{v}\r\n" }.join + "\r\n" + body)
    end
    case path
    when '/ua', '/no-agent'
      response.call('200 OK', File.binread(File.join(samples, 'tone.wav')))
    when '/cookie'
      response.call('302 Found', '', {'Location' => '/cookie-target', 'Set-Cookie' => 'test=ok; Path=/; HttpOnly'})
    when '/cookie-target'
      raise 'Cookie lost' unless headers['cookie'].to_s.include?('test=ok')
      response.call('200 OK', File.binread(File.join(samples, 'tone.wav')))
    when '/cross'
      response.call('302 Found', '', {'Location' => other + '/cross-target'})
    when '/cross-target'
      response.call('200 OK', File.binread(File.join(samples, 'tone.wav')))
    when '/retry'
      if count.odd?
        response.call('503 Unavailable', '', {'Retry-After' => '0'})
      else
        response.call('200 OK', File.binread(File.join(samples, 'tone.wav')))
      end
    when '/icy'
      audio = File.binread(File.join(samples, 'tone.mp3'))
      body = ''.b
      audio.bytes.each_slice(4096).with_index do |bytes, index|
        body << bytes.pack('C*')
        next unless bytes.length == 4096
        title = "StreamTitle='Track #{index}';"
        units = (title.bytesize + 15) / 16
        body << units.chr << title.ljust(units * 16, "\0")
      end
      response.call('200 OK', body, {'icy-metaint' => '4096', 'icy-name' => 'Lindar Test Radio'})
    when '/chunked', '/chunked-opus', '/chunked-file'
      socket.write("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")
      fixture = path == '/chunked-opus' ? 'tone.opus' : path == '/chunked-file' ? 'tone.aiff' : 'tone.mp3'
      File.binread(File.join(samples, fixture)).bytes.each_slice(path == '/chunked-opus' ? 7 : 997) do |bytes|
        chunk = bytes.pack('C*')
        socket.write("#{chunk.bytesize.to_s(16)}\r\n#{chunk}\r\n")
      end
      socket.write("0\r\n\r\n")
    when '/no-etag.opus'
      raise 'Unexpected range without validator' if headers['range'] || headers['if-range']
      response.call('200 OK', File.binread(File.join(samples, 'tone.opus')), {'Accept-Ranges' => 'bytes'})
    when '/large-file', '/no-file-cache', '/file-limit', '/bad-cache-dir'
      body = File.binread(File.join(samples, 'tone.aiff'))
      if path == '/large-file'
        padding = 8 * 1024 * 1024
        body.insert(12, ['JUNK', padding].pack('a4N') + "\0" * padding)
        body[4, 4] = [body.bytesize - 8].pack('N')
      end
      response.call('200 OK', body, {'Content-Type' => 'application/octet-stream'})
    when '/missing'
      response.call('404 Not Found', '')
    when '/resume', '/changed', '/resume-twice', '/file-resume'
      body = File.binread(File.join(samples, path == '/file-resume' ? 'tone.aiff' : 'tone.wav'))
      if headers['range']
        if ['/resume-twice', '/file-resume'].include?(path) && count % 3 == 2
          response.call('503 Unavailable', '')
          next
        end
        start = headers['range'].match(/bytes=(\d+)-/)[1].to_i
        raise 'Resume offset' unless start == 10000 && headers['if-range'] == '"stable"'
        tag = path == '/changed' ? '"changed"' : '"stable"'
        response.call('206 Partial Content', body.byteslice(start..), {'ETag' => tag,
                      'Content-Range' => "bytes #{start}-#{body.bytesize - 1}/#{body.bytesize}"})
      else
        socket.write("HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nETag: \"stable\"\r\nContent-Length: #{body.bytesize}\r\n\r\n" + body.byteslice(0, 10000))
        break
      end
    when '/truncated'
      body = File.binread(File.join(samples, 'tone.wav'))
      socket.write("HTTP/1.1 200 OK\r\nContent-Length: #{body.bytesize}\r\n\r\n" + body.byteslice(0, 10000))
      break
    when '/slow'
      socket.write("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")
      audio = File.binread(File.join(samples, 'tone.mp3'))
      audio.bytes.each_slice(8000) do |bytes|
        chunk = bytes.pack('C*')
        socket.write("#{chunk.bytesize.to_s(16)}\r\n#{chunk}\r\n")
        sleep 0.05
      end
      socket.write("0\r\n\r\n")
    when '/stall'
      sleep 0.2
      break
    when '/hls-master.m3u8'
      response.call('200 OK', "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=128000,CODECS=\"mp4a.40.2\"\nhls-ts/index.m3u8\n", {'Content-Type' => 'application/vnd.apple.mpegurl'})
    when '/hls-aes/index.m3u8'
      body = File.binread(File.join(samples, 'hls-ts/index.m3u8'))
      body.sub!("#EXT-X-MEDIA-SEQUENCE:0", "#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-KEY:METHOD=AES-128,URI=\"key\"")
      response.call('200 OK', body, {'Content-Type' => 'application/vnd.apple.mpegurl'})
    when '/hls-aes/key'
      response.call('200 OK', aes_key)
    when %r{\A/hls-aes/seg(\d+)\.ts\z}
      index = Regexp.last_match(1).to_i
      cipher = OpenSSL::Cipher.new('aes-128-cbc')
      cipher.encrypt
      cipher.key = aes_key
      cipher.iv = [0, index].pack('Q>Q>')
      body = File.binread(File.join(samples, "hls-ts/seg#{index}.ts"))
      response.call('200 OK', cipher.update(body) + cipher.final)
    else
      relative = path.delete_prefix('/')
      raise 'Unsafe path' if relative.split('/').include?('..')
      file = File.join(samples, relative)
      if File.file?(file) || path == '/large.m4a'
        body = path == '/large.m4a' ? large : File.binread(file)
        fields = {'Accept-Ranges' => 'bytes', 'ETag' => '"stable"',
                  'Content-Type' => path.end_with?('.m3u8') ? 'application/vnd.apple.mpegurl' : 'application/octet-stream'}
        if headers['range']
          first, last = headers['range'].match(/bytes=(\d+)-(\d*)/).captures
          first = first.to_i
          last = last.empty? ? body.bytesize - 1 : [last.to_i, body.bytesize - 1].min
          raise 'Invalid range' if first > last || headers['if-range'] != '"stable"'
          fields['Content-Range'] = "bytes #{first}-#{last}/#{body.bytesize}"
          response.call('206 Partial Content', body.byteslice(first..last), fields)
        else
          response.call('200 OK', body, fields)
        end
      else
        response.call('404 Not Found', '')
      end
    end
  end
rescue IOError, SystemCallError, OpenSSL::SSL::SSLError
ensure
  socket.close rescue nil
end
acceptors = [[plain, base], [cross, other], [secure, tls]].map do |server, origin|
  Thread.new do
    loop do
      socket = server.accept
      workers << Thread.new(socket) do |s|
        begin
          serve.call(s, origin)
        rescue => e
          errors << e.full_message
        end
      end
    rescue OpenSSL::SSL::SSLError
      next
    rescue SystemCallError
      break if server == secure ? secure_tcp.closed? : server.closed?
    rescue IOError
      break
    end
  end
end
result = false
Dir.mktmpdir('lindar-http-') do |directory|
  ca = File.join(directory, 'ca.pem')
  File.binwrite(ca, cert.to_pem)
  result = system(binary, base, tls, ca)
end
[plain, cross, secure_tcp].each(&:close)
acceptors.each(&:join)
workers.each { |thread| thread.join(1) }
until errors.empty?
  warn errors.pop
  result = false
end
exit(result ? 0 : 1)
