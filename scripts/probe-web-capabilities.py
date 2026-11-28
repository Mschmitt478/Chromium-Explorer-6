#!/usr/bin/env python3
"""Probe the installed Internet Explorer XP Flatpak in a disposable localhost window."""

import json
import subprocess
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

page = b"""<!doctype html><title>Browser capability check</title><h1>Browser capability check</h1>
<script>
(async () => {
  const result = {ua: navigator.userAgent};
  const attempt = async (key, check) => {
    try {
      result[key] = await Promise.race([
        Promise.resolve().then(check),
        new Promise(resolve => setTimeout(() => resolve("timeout"), 5000))
      ]);
    } catch (error) { result[key] = error.name; }
  };
  const video = document.createElement("video");
  const audio = document.createElement("audio");
  result.h264 = video.canPlayType('video/mp4; codecs="avc1.42E01E"');
  result.aac = audio.canPlayType('audio/mp4; codecs="mp4a.40.2"');
  result.vp9 = video.canPlayType('video/webm; codecs="vp09.00.10.08"');
  result.av1 = video.canPlayType('video/mp4; codecs="av01.0.05M.08"');
  result.mediaSource = typeof MediaSource !== "undefined";
  result.webAuthn = typeof PublicKeyCredential !== "undefined";
  result.webRTC = typeof RTCPeerConnection !== "undefined";
  result.screenCaptureAPI = Boolean(navigator.mediaDevices?.getDisplayMedia);
  result.cameraAPI = Boolean(navigator.mediaDevices?.getUserMedia);
  result.webUsbAPI = Boolean(navigator.usb);
  result.webHidAPI = Boolean(navigator.hid);
  result.webSerialAPI = Boolean(navigator.serial);
  result.filePickerAPI = typeof showOpenFilePicker !== "undefined";
  result.notificationAPI = typeof Notification !== "undefined";
  result.pushManagerAPI = typeof PushManager !== "undefined";
  result.pipAPI = document.pictureInPictureEnabled;
  await Promise.all([
    attempt("wasm", async () => Boolean((await WebAssembly.instantiate(
      new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0]))).instance)),
    attempt("webgl2", () => Boolean(document.createElement("canvas").getContext("webgl2"))),
    attempt("webgpuAdapter", async () => navigator.gpu ? Boolean(await navigator.gpu.requestAdapter()) : "API absent"),
    attempt("indexedDB", () => new Promise((resolve, reject) => {
      const request = indexedDB.open("compatibility-probe");
      request.onerror = () => reject(request.error);
      request.onsuccess = () => { request.result.close(); resolve(true); };
    })),
    attempt("cacheStorage", async () => Boolean(await caches.open("compatibility-probe"))),
    attempt("serviceWorker", async () => {
      if (!navigator.serviceWorker) return false;
      await navigator.serviceWorker.register("/sw.js");
      return Boolean(await navigator.serviceWorker.ready);
    }),
    attempt("widevine", async () => Boolean(await navigator.requestMediaKeySystemAccess(
      "com.widevine.alpha", [{initDataTypes: ["webm"],
        videoCapabilities: [{contentType: 'video/webm; codecs="vp9"'}]}])))
  ]);
  await fetch("/results", {method: "POST", body: JSON.stringify(result)});
})();
</script>"""

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/":
            self.reply("text/html; charset=utf-8", page)
        elif self.path == "/sw.js":
            self.reply("text/javascript", b"self.addEventListener('install', () => self.skipWaiting());")
        else:
            self.send_error(404)
    def reply(self, content_type, data):
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def do_POST(self):
        if self.path != "/results":
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 16384:
                self.send_error(400)
                return
            self.server.result = json.loads(self.rfile.read(length))
        except ValueError:
            self.send_error(400)
            return
        self.reply("text/plain", b"ok")
    def log_message(self, fmt, *args):
        pass

def main():
    with HTTPServer(("127.0.0.1", 0), Handler) as server:
        server.result = None
        server.timeout = 1
        process = subprocess.Popen([
            "flatpak", "run", "io.github.Mschmitt478.ChromiumExplorer",
            "--private", f"http://127.0.0.1:{server.server_port}/",
        ])
        try:
            deadline = time.monotonic() + 30
            while server.result is None and process.poll() is None and time.monotonic() < deadline:
                server.handle_request()
            if server.result is None:
                raise SystemExit("No browser capability results received")
            print(json.dumps(server.result, indent=2, sort_keys=True))
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=8)


if __name__ == "__main__":
    main()
