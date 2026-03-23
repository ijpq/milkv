from flask import Flask, render_template_string, jsonify, send_from_directory
import subprocess
import os
import signal
from datetime import datetime
from queue import Queue
import threading
import configparser

app = Flask(__name__)
REC_DIR = '/mnt/data/recordings'
RTSP_URL = 'rtsp://127.0.0.1/h264'

# 录像状态
recording_process = None
current_file = None
upload_queue = Queue()
upload_log_file = "/mnt/data/logs/upload.log"

def log_upload(msg):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    try:
        with open(upload_log_file, "a") as f:
            f.write(f"[{timestamp}] {msg}\n")
    except:
        pass

def upload_worker():
    while True:
        mp4_path = upload_queue.get()
        if mp4_path is None:
            break
        _upload_with_retry(mp4_path)
        upload_queue.task_done()

def _upload_with_retry(mp4_path, max_retries=3):
    config = configparser.ConfigParser()
    config.read('/etc/camera/config.ini')
    remote = config.get('record', 'remote', fallback='nas:recordings')
    filename = os.path.basename(mp4_path)
    
    log_upload(f"Starting upload: {filename} -> {remote}")
    
    for attempt in range(1, max_retries + 1):
        try:
            ret = subprocess.run([
                'rclone', 'copy',
                '--config', '/root/.config/rclone/rclone.conf',
                mp4_path, remote
            ], capture_output=True, timeout=300)
            
            if ret.returncode == 0:
                log_upload(f"Upload successful: {filename}")
                os.remove(mp4_path)
                log_upload(f"Deleted local file: {filename}")
                return
            else:
                log_upload(f"Upload attempt {attempt} failed: {ret.stderr.decode()}")
        except Exception as e:
            log_upload(f"Upload attempt {attempt} error: {e}")
        
        if attempt < max_retries:
            import time
            time.sleep(5)
    
    log_upload(f"Upload failed after {max_retries} attempts: {filename}")

# 启动上传线程
upload_thread = threading.Thread(target=upload_worker, daemon=True)
upload_thread.start()

HTML = '''
<!DOCTYPE html>
<html>
<head>
    <title>Milk-V Duo S 监控 (硬件编码)</title>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 20px; background: #f0f0f0; }
        .container { max-width: 900px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; }
        h1 { color: #333; }
        .status { margin: 20px 0; padding: 15px; background: #e8f5e9; border-radius: 4px; }
        .info { margin: 10px 0; color: #666; }
        .rtsp { background: #f5f5f5; padding: 10px; border-radius: 4px; font-family: monospace; margin: 10px 0; font-size: 16px; }
        .controls { margin: 20px 0; }
        .btn { padding: 12px 24px; font-size: 16px; border: none; border-radius: 4px; cursor: pointer; margin-right: 10px; }
        .btn-start { background: #4CAF50; color: white; }
        .btn-stop { background: #f44336; color: white; }
        .btn:disabled { background: #ccc; cursor: not-allowed; }
        .recording-indicator { display: inline-block; width: 12px; height: 12px; background: red; border-radius: 50%; animation: blink 1s infinite; margin-right: 8px; }
        @keyframes blink { 0%, 50% { opacity: 1; } 51%, 100% { opacity: 0; } }
        table { width: 100%; border-collapse: collapse; margin: 20px 0; }
        th, td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background: #f5f5f5; }
        a { color: #1976d2; text-decoration: none; }
        a:hover { text-decoration: underline; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📹 Milk-V Duo S 监控 (H.264 硬件编码)</h1>
        
        <div class="status">
            <h3>✅ 系统状态</h3>
            <div id="status">加载中...</div>
        </div>
        
        <div class="controls">
            <h3>🎥 录像控制</h3>
            <button id="startBtn" class="btn btn-start" onclick="startRecording()">开始录像</button>
            <button id="stopBtn" class="btn btn-stop" onclick="stopRecording()" disabled>停止录像</button>
            <div id="recordStatus" style="margin-top: 10px;"></div>
        </div>
        
        <h3>📡 RTSP 实时流</h3>
        <div class="rtsp">rtsp://192.168.31.219/h264</div>
        <p class="info">使用 VLC 播放器打开上面的地址观看实时画面</p>
        
        <h3>📼 录像文件</h3>
        <div id="recordings">加载中...</div>
    </div>
    
    <script>
        let isRecording = false;
        
        function updateStatus() {
            fetch('/api/status')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('status').innerHTML = 
                        '<div class="info">CPU: ' + data.cpu + '%</div>' +
                        '<div class="info">内存: ' + data.mem + '%</div>' +
                        '<div class="info">录像总数: ' + data.recordings + ' 个</div>';
                    
                    isRecording = data.recording;
                    updateButtons();
                    
                    if (isRecording) {
                        document.getElementById('recordStatus').innerHTML = 
                            '<span class="recording-indicator"></span>录像中: ' + data.current_file;
                    } else {
                        document.getElementById('recordStatus').innerHTML = '';
                    }
                });
        }
        
        function updateButtons() {
            document.getElementById('startBtn').disabled = isRecording;
            document.getElementById('stopBtn').disabled = !isRecording;
        }
        
        function startRecording() {
            fetch('/api/record/start', {method: 'POST'})
                .then(r => r.json())
                .then(data => {
                    if (data.status === 'started') {
                        alert('录像已开始');
                        updateStatus();
                    } else {
                        alert('启动失败: ' + data.message);
                    }
                });
        }
        
        function stopRecording() {
            if (confirm('确定停止录像？')) {
                fetch('/api/record/stop', {method: 'POST'})
                    .then(r => r.json())
                    .then(data => {
                        if (data.status === 'stopped') {
                            alert('录像已停止，正在上传...');
                            updateStatus();
                            updateRecordings();
                        } else {
                            alert('停止失败: ' + data.message);
                        }
                    });
            }
        }
        
        function updateRecordings() {
            fetch('/api/recordings')
                .then(r => r.json())
                .then(data => {
                    let html = '<table><tr><th>文件名</th><th>大小</th><th>时间</th></tr>';
                    data.files.forEach(f => {
                        html += '<tr><td><a href="/recordings/' + f.name + '">' + f.name + '</a></td>' +
                                '<td>' + f.size + '</td><td>' + f.time + '</td></tr>';
                    });
                    html += '</table>';
                    document.getElementById('recordings').innerHTML = html;
                });
        }
        
        updateStatus();
        updateRecordings();
        setInterval(updateStatus, 2000);
        setInterval(updateRecordings, 10000);
    </script>
</body>
</html>
'''

@app.route('/')
def index():
    return render_template_string(HTML)

@app.route('/api/record/start', methods=['POST'])
def record_start():
    global recording_process, current_file
    
    if recording_process is not None:
        return jsonify({"status": "error", "message": "Already recording"}), 400
    
    current_file = f"rec_{datetime.now().strftime('%Y%m%d_%H%M%S')}.mp4"
    filepath = os.path.join(REC_DIR, current_file)
    
    try:
        recording_process = subprocess.Popen([
            'ffmpeg', '-rtsp_transport', 'tcp', '-i', RTSP_URL,
            '-c', 'copy', '-f', 'mp4',
            '-movflags', '+frag_keyframe+empty_moov+default_base_moof',
            filepath
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
        return jsonify({"status": "started", "file": current_file})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/record/stop', methods=['POST'])
def record_stop():
    global recording_process, current_file
    
    if recording_process is None:
        return jsonify({"status": "error", "message": "Not recording"}), 400
    
    try:
        recording_process.send_signal(signal.SIGINT)
        recording_process.wait(timeout=10)
        
        filepath = os.path.join(REC_DIR, current_file)
        filename = current_file
        
        recording_process = None
        current_file = None
        
        # 添加到上传队列
        upload_queue.put(filepath)
        
        return jsonify({"status": "stopped", "file": filename})
    except Exception as e:
        recording_process = None
        current_file = None
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/status')
def status():
    try:
        cpu = subprocess.check_output("top -bn1 | grep 'CPU:' | awk '{print $2}'", shell=True).decode().strip().replace('%', '')
        mem = subprocess.check_output("free | grep Mem | awk '{print int($3/$2 * 100)}'", shell=True).decode().strip()
        rec_count = len([f for f in os.listdir(REC_DIR) if f.endswith('.mp4')])
        
        return jsonify({
            'cpu': cpu,
            'mem': mem,
            'recordings': rec_count,
            'recording': recording_process is not None,
            'current_file': current_file if current_file else ''
        })
    except:
        return jsonify({'cpu': '0', 'mem': '0', 'recordings': 0, 'recording': False, 'current_file': ''})

@app.route('/api/recordings')
def recordings():
    try:
        files = []
        for f in sorted(os.listdir(REC_DIR), reverse=True)[:20]:
            if f.endswith('.mp4'):
                path = os.path.join(REC_DIR, f)
                size = os.path.getsize(path)
                size_str = f"{size/1024/1024:.1f} MB" if size > 1024*1024 else f"{size/1024:.1f} KB"
                mtime = datetime.fromtimestamp(os.path.getmtime(path)).strftime('%H:%M:%S')
                files.append({'name': f, 'size': size_str, 'time': mtime})
        return jsonify({'files': files})
    except:
        return jsonify({'files': []})

@app.route('/recordings/<filename>')
def download(filename):
    return send_from_directory(REC_DIR, filename)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080)
