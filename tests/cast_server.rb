require 'socket'
require 'uri'
require 'base64'
servers = 3.times.map { TCPServer.new('127.0.0.1', 0) }
errors = Queue.new
bodies = Queue.new
titles = Queue.new
workers = []
headers = lambda do |socket|
  fields = {}
  while (line = socket.gets) && !line.strip.empty?
    name, value = line.split(':', 2)
    fields[name.downcase] = value.to_s.strip
  end
  fields
end
acceptors = servers.each_with_index.map do |server, type|
  Thread.new do
    loop do
      socket = server.accept
      workers << Thread.new(socket) do |s|
        begin
          first = s.gets.to_s.strip
          if type == 2
            method, target = first.split
            fields = headers.call(s)
            raise 'admin method' unless method == 'GET'
            uri = URI.parse(target)
            query = URI.decode_www_form(uri.query).to_h
            raise 'admin title' unless query['song'] == 'Title & Unicode ł'
            if uri.path == '/admin.cgi'
              raise 'shoutcast password' unless query['pass'] == 'secret'
            else
              raise 'icecast admin auth' unless fields['authorization'] == "Basic #{Base64.strict_encode64('source:secret')}"
              raise 'icecast mount' unless query['mount'] == '/stream'
            end
            titles << query['song']
            s.write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
          else
            if type == 0
              if first != 'secret'
                s.write("invalid password\r\n\r\n")
                next
              end
              s.write("OK2\r\nicy-caps:11\r\n\r\n")
              fields = headers.call(s)
              raise 'shoutcast name' unless fields['icy-name'] == 'Lindar'
            else
              raise 'icecast source' unless first == 'SOURCE /stream HTTP/1.0'
              fields = headers.call(s)
              raise 'icecast auth' unless fields['authorization'] == "Basic #{Base64.strict_encode64('source:secret')}"
              s.write("HTTP/1.0 200 OK\r\n\r\n")
            end
            raise 'content type' unless fields['content-type'] == 'audio/mpeg'
            body = s.read
            if body.bytesize == 40000
              raise 'corrupted bytes' unless body == (0...40000).map { |i| i & 255 }.pack('C*')
            else
              raise 'invalid encoded MP3' unless body.bytesize > 100 && (body.start_with?('ID3') || (body.getbyte(0) == 255 && body.getbyte(1) & 0xe0 == 0xe0))
            end
            bodies << body.bytesize
          end
        rescue => e
          errors << e
        ensure
          s.close
        end
      end
    end
  rescue IOError, Errno::EBADF
  end
end
ok = system(File.expand_path(ARGV.fetch(0)), *servers.map { |s| "http://127.0.0.1:#{s.addr[1]}" })
servers.each(&:close)
acceptors.each(&:join)
workers.each { |w| w.join(5) }
raise errors.pop unless errors.empty?
raise 'missing broadcast or title' unless bodies.size >= 2 && bodies.size == titles.size
exit(ok ? 0 : 1)
