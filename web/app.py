#!/usr/bin/env python3
import os
import glob
import time
import socket
import struct
import threading
import subprocess
from flask import Flask, Response, render_template_string, jsonify

app = Flask(__name__)

SOCKET_PATH   = "/tmp/camera_daemon.sock"
PREVIEW_PIPE  = "/tmp/camera_preview.pipe"
RECORD_DIR    = "/mnt/data/recordings"
RCLONE_REMOTE = "nas:recordings"
FFMPEG_BIN    = "/usr/bin/ffmpeg"

_latest_frame    = None
_frame_lock      = threading.Lock()
_preview_thread  = None
_preview_running = False

def _read_preview_pipe():
    global _latest_frame, _preview_running
    _preview_running = True
    while _preview_running:
        try:
            fd = open(PREVIEW_PIPE, 'rb')
            print("[preview] Pipe connected")
            while _preview_running:
                header = fd.read(4)
                if len(header) < 4:
                    break
                length = struct.unpack('<I', header)[0]
                if length == 0 or length > 10 * 1024 * 1024:
                    break
                data = b''
                remaining = length
                while remaining > 0:
                    chunk = fd.read(remaining)
                    if not chunk:
                        break
                    data += chunk
                    remaining -= len(chunk)
                if len(data) == length:
                    with _frame_lock:
                        _latest_frame = data
            fd.close()
        except Exception as e:
            print(f"[preview] Pipe error: {e}")
            time.sleep(1)

def _start_preview_thread():
    global _preview_thread
    if _preview_thread is None or not _preview_thread.is_alive():
        _preview_thread = threading.Thread(target=_read_preview_pipe, daemon=True)
        _preview_thread.start()

def _send_command(cmd):
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(5)
        s.connect(SOCKET_PATH)
        s.sendall(cmd.encode())
        resp = s.recv(512).decode().strip()
        s.close()
        return resp
    except Exception as e:
        return f"ERR:socket_{e}"

def _postprocess(h264_path):
    mp4_path = h264_path.replace('.h264', '.mp4')
    def _run():
        print(f"[post] Muxing {h264_path} -> {mp4_path}")
        ret = subprocess.run([
            FFMPEG_BIN, '-y',
            '-framerate', '25',
            '-i', h264_path,
            '-c:v', 'copy',
            '-movflags', '+faststart',
            mp4_path
        ], capture_output=True)
        if ret.returncode != 0:
            print(f"[post] ffmpeg error: {ret.stderr.decode()}")
            return
        try:
            os.remove(h264_path)
        except OSError:
            pass
        print(f"[post] Uploading {mp4_path} -> {RCLONE_REMOTE}")
        ret = subprocess.run([
            'rclone', 'move', mp4_path, RCLONE_REMOTE,
            '--progress', '--stats-one-line'
        ], capture_output=True)
        if ret.returncode == 0:
            print(f"[post] Upload success")
        else:
            print(f"[post] Upload failed: {ret.stderr.decode()}")
    threading.Thread(target=_run, daemon=True).start()

@app.route("/")
def index():
    return render_template_string(HTML_PAGE)

@app.route("/stream")
def stream():
    _start_preview_thread()
    def generate():
        while True:
            with _frame_lock:
                frame = _latest_frame
            if frame:
                yield (b"--frame\r\nContent-Type: image/jpeg\r\n\r\n" + frame + b"\r\n")
            time.sleep(0.04)
    return Response(generate(), mimetype="multipart/x-mixed-replace; boundary=frame")

@app.route("/api/record/start", methods=["POST"])
def record_start():
    resp = _send_command("start")
    if resp.startswith("OK:"):
        return jsonify({"status": "started", "file": os.path.basename(resp[3:])})
    return jsonify({"status": "error", "message": resp}), 400

@app.route("/api/record/stop", methods=["POST"])
def record_stop():
    resp = _send_command("stop")
    if resp.startswith("STOPPED:"):
        h264_path = resp[8:]
        _postprocess(h264_path)
        return jsonify({"status": "stopped", "processing": os.path.basename(h264_path)})
    return jsonify({"status": "error", "message": resp}), 400

@app.route("/api/status")
def status():
    resp = _send_command("status")
    local_mp4  = [os.path.basename(f) for f in sorted(glob.glob(f"{RECORD_DIR}/*.mp4"))]
    local_h264 = [os.path.basename(f) for f in sorted(glob.glob(f"{RECORD_DIR}/*.h264"))]
    return jsonify({
        "daemon":    resp,
        "recording": resp.startswith("RECORDING:"),
        "local_mp4": local_mp4,
        "local_h264": local_h264,
    })

HTML_PAGE = """<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Duo S 监控</title>
<style>
  :root{--bg:#0f1117;--card:#1a1d27;--accent:#e53;--text:#e8e8e8;--muted:#666}
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);color:var(--text);font-family:'Segoe UI',sans-serif;
       display:flex;flex-direction:column;align-items:center;padding:20px}
  h1{margin:16px 0 20px;font-size:1.1rem;color:var(--muted);letter-spacing:.1em;text-transform:uppercase}
  #video-wrap{position:relative;width:100%;max-width:800px;background:#000;
              border-radius:10px;overflow:hidden;aspect-ratio:16/9}
  #video{width:100%;height:100%;object-fit:contain;display:block}
  #overlay{position:absolute;top:12px;left:14px;display:flex;align-items:center;gap:8px}
  .dot{width:10px;height:10px;border-radius:50%;background:var(--muted)}
  .dot.rec{background:var(--accent);animation:blink 1s infinite}
  @keyframes blink{50%{opacity:.25}}
  #rec-label{font-size:.8rem;color:#fff;font-weight:600;text-shadow:0 1px 3px rgba(0,0,0,.8)}
  .controls{margin-top:18px;display:flex;gap:14px}
  button{padding:11px 32px;border:none;border-radius:8px;font-size:.95rem;
         cursor:pointer;font-weight:700;transition:opacity .15s}
  button:hover{opacity:.85}
  button:disabled{opacity:.35;cursor:not-allowed}
  #btn-start{background:var(--accent);color:#fff}
  #btn-stop{background:#333;color:#aaa;border:1px solid #444}
  #btn-stop.active{background:#444;color:#fff;border-color:#666}
  .info{margin-top:14px;font-size:.82rem;color:var(--muted);text-align:center}
</style>
</head>
<body>
<h1>📷 Milk-V Duo S 监控</h1>
<div id="video-wrap">
  <img id="video" src="/stream" alt="实时画面">
  <div id="overlay">
    <div class="dot" id="dot"></div>
    <span id="rec-label"></span>
  </div>
</div>
<div class="controls">
  <button id="btn-start" onclick="startRec()">● 开始录像</button>
  <button id="btn-stop"  onclick="stopRec()">■ 停止录像</button>
</div>
<div class="info" id="status-info">状态：待机中</div>
<script>
async function startRec(){
  document.getElementById('btn-start').disabled=true;
  const r=await fetch('/api/record/start',{method:'POST'});
  const d=await r.json();
  if(d.status!=='started'){alert('启动失败: '+d.message);document.getElementById('btn-start').disabled=false;}
  pollStatus();
}
async function stopRec(){
  document.getElementById('btn-stop').disabled=true;
  const r=await fetch('/api/record/stop',{method:'POST'});
  const d=await r.json();
  if(d.status==='stopped')document.getElementById('status-info').textContent='正在封装并上传: '+d.processing;
  pollStatus();
}
async function pollStatus(){
  try{
    const d=await(await fetch('/api/status')).json();
    const dot=document.getElementById('dot');
    const label=document.getElementById('rec-label');
    const info=document.getElementById('status-info');
    const btnS=document.getElementById('btn-start');
    const btnX=document.getElementById('btn-stop');
    if(d.recording){
      dot.className='dot rec';label.textContent='REC';
      btnS.disabled=true;btnX.disabled=false;btnX.classList.add('active');
      info.textContent='录像中';
    }else{
      dot.className='dot';label.textContent='';
      btnS.disabled=false;btnX.disabled=false;btnX.classList.remove('active');
      if(!info.textContent.includes('上传'))info.textContent='待机中';
    }
  }catch(e){console.error(e);}
}
setInterval(pollStatus,3000);
pollStatus();
</script>
</body>
</html>"""

if __name__ == "__main__":
    _start_preview_thread()
    app.run(host="0.0.0.0", port=8080, threaded=True)