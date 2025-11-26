import http.server
import socketserver
import urllib.request
import urllib.error
import sys

PORT = 8080

class ProxyHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        feeds = {
            "/cbc": "https://www.cbc.ca/webfeed/rss/rss-topstories",
            "/bbc": "https://feeds.bbci.co.uk/news/rss.xml",
            "/wea": "https://weather.gc.ca/rss/weather/45.403_-71.901_e.xml"
        }

        if self.path in feeds:
            target_url = feeds[self.path]
            print(f"Request from {self.client_address[0]}...")
            print(f" -> Fetching: {target_url}")

            try:
                # FAKE A BROWSER to avoid being blocked
                headers = {
                    'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36'
                }

                req = urllib.request.Request(target_url, headers=headers)

                # Set a timeout (5 seconds) so it doesn't hang forever
                with urllib.request.urlopen(req, timeout=5) as response:
                    data = response.read()

                self.send_response(200)
                self.send_header('Content-type', 'application/rss+xml')
                self.end_headers()
                self.wfile.write(data)
                print(" -> Success! Sent to client.\n")

            except urllib.error.HTTPError as e:
                print(f" -> HTTP Error: {e.code} {e.reason}")
                self.send_error(e.code, f"Upstream Error: {e.reason}")
            except Exception as e:
                print(f" -> Error: {e}")
                self.send_error(500, f"Proxy Error: {e}")
        else:
            self.send_error(404, "Feed not configured in proxy")

print(f"RSS Proxy listening on 0.0.0.0:{PORT}")
# Allow address reuse to prevent "Address already in use" errors on restart
socketserver.TCPServer.allow_reuse_address = True

with socketserver.TCPServer(("0.0.0.0", PORT), ProxyHandler) as httpd:
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping proxy.")
        httpd.server_close()