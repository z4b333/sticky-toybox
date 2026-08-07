from http.server import BaseHTTPRequestHandler, HTTPServer
import urllib.parse, json
CAPTURED = []
class H(BaseHTTPRequestHandler):
    def log_message(self,*a): pass
    def do_GET(self):
        if self.path == '/items':
            body = b"Alice\nBob\nCharlie\n"
            self.send_response(200); self.send_header('Content-Type','text/plain')
            self.send_header('Content-Length',str(len(body))); self.end_headers(); self.wfile.write(body)
        else:
            body = open('/tmp/picker_page.html','rb').read()
            self.send_response(200); self.send_header('Content-Type','text/html')
            self.send_header('Content-Length',str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_POST(self):
        n = int(self.headers.get('Content-Length',0))
        raw = self.rfile.read(n).decode()
        data = urllib.parse.parse_qs(raw).get('data',[''])[0]
        CAPTURED.append(data)
        open('/tmp/captured.txt','w').write(data)
        # mimic the device: first 10 lines, each cut to 20 chars
        items = [l.strip()[:20] for l in data.split('\n') if l.strip()][:10]
        body = json.dumps({"n":len(items)}).encode()
        self.send_response(200); self.send_header('Content-Type','application/json')
        self.send_header('Content-Length',str(len(body))); self.end_headers(); self.wfile.write(body)
HTTPServer(('127.0.0.1',8099), H).serve_forever()
