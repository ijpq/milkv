#!/usr/bin/env python3
import os
import glob
import time
import socket
import threading
import subprocess
import configparser
from queue import Queue
from flask import Flask, Response, render_template_string, jsonify

app = Flask(__name__)

SOCKET_PATH   = "/tmp/camera_daemon.sock"
RECORD_DIR    = "/mnt/data/recordings"

# 上传队列和日志
upload_queue = Queue()
upload_log_file = "/mnt/data/logs/upload.log"

def log_upload(msg):
    """记录上传日志"""
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    try:
        with open(upload_log_file, "a") as f:
            f.write(f"[{timestamp}] {msg}\n")
    except:
        pass
    print(f"[upload] {msg}")

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

def upload_worker():
    """后台上传工作线程"""
    log_upload("Upload worker started")
    while True:
        mjpeg_path = upload_queue.get()
        if mjpeg_path is None:
            break
        _upload_with_retry(mjpeg_path)
        upload_queue.task_done()

def _upload_with_retry(mjpeg_path, max_retries=3):
    """带重试机制的上传函数"""
    config = configparser.ConfigParser()
    config.read('/etc/camera/config.ini')
    remote = config.get('record', 'remote', fallback='nas:recordings')
    filename = os.path.basename(mjpeg_path)
    
    log_upload(f"Starting upload: {filename} -> {remote}")
    
    for attempt in range(1, max_retries + 1):
        try:
            # 使用 copy 而不是 move（保留本地文件直到确认上传成功）
            ret = subprocess.run([
                'rclone', 
                '--config', '/root/.config/rclone/rclone.conf',
                'copy',  # 改为 copy，上传成功后手动删除
                mjpeg_path, 
                remote,
                '--progress', 
                '--stats-one-line',
                '--timeout', '300s',        # 5分钟超时
                '--retries', '3',           # rclone 内部重试
                '--low-level-retries', '10' # 底层重试
            ], capture_output=True, text=True, timeout=600, 
               env={'HOME': '/root', 'PATH': '/usr/bin:/bin'})
            
            if ret.returncode == 0:
                log_upload(f"✓ Upload success (attempt {attempt}): {filename}")
                
                # 上传成功后删除本地文件
                try:
                    os.remove(mjpeg_path)
                    log_upload(f"✓ Deleted local file: {filename}")
                except Exception as e:
                    log_upload(f"✗ Failed to delete: {filename}, error: {e}")
                
                return True
            else:
                error_msg = ret.stderr.strip() if ret.stderr else "Unknown error"
                log_upload(f"✗ Upload failed (attempt {attempt}/{max_retries}): {error_msg}")
                
                if attempt < max_retries:
                    wait_time = 5 * attempt  # 递增等待：5s, 10s, 15s
                    log_upload(f"  Retrying in {wait_time} seconds...")
                    time.sleep(wait_time)
        
        except subprocess.TimeoutExpired:
            log_upload(f"✗ Upload timeout (attempt {attempt}/{max_retries}): {filename}")
            if attempt < max_retries:
                time.sleep(5 * attempt)
        
        except Exception as e:
            log_upload(f"✗ Upload exception (attempt {attempt}/{max_retries}): {e}")
            if attempt < max_retries:
                time.sleep(5 * attempt)
    
    # 所有重试都失败
    log_upload(f"✗✗✗ UPLOAD FAILED after {max_retries} attempts: {filename}")
    log_upload(f"  Local file preserved: {mjpeg_path}")
    return False

def _postprocess(mjpeg_path, frame_count):
    """将文件加入上传队列"""
    log_upload(f"Queued for upload: {os.path.basename(mjpeg_path)} ({frame_count} frames)")
    upload_queue.put(mjpeg_path)

@app.route("/")
def index():
    return render_template_string(HTML_PAGE)

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
        parts = resp[8:].rsplit(':', 1)
        mjpeg_path = parts[0]
        frame_count = int(parts[1]) if len(parts) > 1 else 0
        _postprocess(mjpeg_path, frame_count)
        return jsonify({"status": "stopped",
                        "processing": os.path.basename(mjpeg_path)})
    return jsonify({"status": "error", "message": resp}), 400

@app.route("/api/status")
def status():
    resp = _send_command("status")
    local_mjpeg = [os.path.basename(f)
                   for f in sorted(glob.glob(f"{RECORD_DIR}/*.mjpeg"))]
    return jsonify({
        "daemon":      resp,
        "recording":   resp.startswith("RECORDING:"),
        "local_files": local_mjpeg,
    })

@app.route("/api/logs/daemon")
def logs_daemon():
    try:
        with open('/mnt/data/logs/camera_daemon.log', 'r') as f:
            lines = f.readlines()
        return jsonify({"lines": lines[-100:]})
    except Exception as e:
        return jsonify({"lines": [str(e)]})

@app.route("/api/logs/web")
def logs_web():
    try:
        with open('/mnt/data/logs/camera_web.log', 'r') as f:
            lines = f.readlines()
        return jsonify({"lines": lines[-100:]})
    except Exception as e:
        return jsonify({"lines": [str(e)]})

HTML_PAGE = """<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Duo S 监控</title>
<style>
  :root{--bg:#0f1117;--accent:#e53;--text:#e8e8e8;--muted:#666}
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
  <img id="video" src="http://192.168.31.219:8554" alt="实时画面">
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
async function loadLog(type) {
  const r = await fetch('/api/logs/' + type);
  const d = await r.json();
  const box = document.getElementById('log-box');
  box.textContent = d.lines.join('');
  box.scrollTop = box.scrollHeight;
}
function clearLog() {
  document.getElementById('log-box').textContent = '';
}
async function startRec(){
  document.getElementById('btn-start').disabled=true;
  const r=await fetch('/api/record/start',{method:'POST'});
  const d=await r.json();
  if(d.status!=='started'){
    alert('启动失败: '+d.message);
    document.getElementById('btn-start').disabled=false;
  }
  pollStatus();
}
async function stopRec(){
  document.getElementById('btn-stop').disabled=true;
  const r=await fetch('/api/record/stop',{method:'POST'});
  const d=await r.json();
  if(d.status==='stopped')
    document.getElementById('status-info').textContent='正在上传: '+d.processing;
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
<div style="margin-top:24px;width:100%;max-width:800px;">
  <div style="display:flex;gap:10px;margin-bottom:8px;">
    <button onclick="loadLog('daemon')" style="padding:6px 16px;background:#222;color:#aaa;border:1px solid #444;border-radius:6px;cursor:pointer;">Daemon 日志</button>
    <button onclick="loadLog('web')" style="padding:6px 16px;background:#222;color:#aaa;border:1px solid #444;border-radius:6px;cursor:pointer;">Web 日志</button>
    <button onclick="clearLog()" style="padding:6px 16px;background:#222;color:#aaa;border:1px solid #444;border-radius:6px;cursor:pointer;">清空</button>
  </div>
  <pre id="log-box" style="background:#0a0a0a;color:#0f0;font-size:.75rem;padding:12px;border-radius:8px;height:200px;overflow-y:auto;white-space:pre-wrap;word-break:break-all;"></pre>
</div>
</body>
</html>"""

if __name__ == "__main__":
    # 启动上传工作线程
    threading.Thread(target=upload_worker, daemon=True).start()
    log_upload("=== App started ===")
    app.run(host="0.0.0.0", port=8080, threaded=True)
