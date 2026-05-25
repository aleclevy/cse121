from http.server import HTTPServer, BaseHTTPRequestHandler

class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length).decode('utf-8')
            print(f"[POST] {self.path} — Body: {body}")
                                            
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"OK")

        def log_message(self, format, *args):
            pass  # suppress default access logs since we handle it ourselves

                                                                                
if __name__ == "__main__":
                                                                                        server = HTTPServer(("0.0.0.0", 1234), Handler)
                                                                                        print("Server listening on port 1234...")
                                                                                        server.serve_forever()
