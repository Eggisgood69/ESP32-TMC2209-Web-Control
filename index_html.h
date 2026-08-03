#ifndef INDEX_HTML_H
#define INDEX_HTML_H

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'><title>TMC2209 Control Panel</title>
<style>
:root {
  --bg-dark: #09090b;
  --card-bg: rgba(24, 24, 27, 0.65);
  --border-light: rgba(255, 255, 255, 0.08);
  --accent-cyan: #00f2fe;
  --accent-blue: #4facfe;
  --accent-green: #2ed553;
  --accent-purple: #a855f7;
  --accent-red: #ff453a;
}

body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  background: var(--bg-dark);
  background-image: 
    radial-gradient(at 0% 0%, rgba(0, 242, 254, 0.12) 0px, transparent 50%),
    radial-gradient(at 100% 100%, rgba(46, 213, 83, 0.1) 0px, transparent 50%);
  color: #f4f4f5; margin: 0; padding: 20px 15px;
  display: flex; justify-content: center; align-items: center; min-height: 100vh;
  box-sizing: border-box;
}

.card {
  background: var(--card-bg);
  backdrop-filter: blur(30px); -webkit-backdrop-filter: blur(30px);
  width: 100%; max-width: 400px; padding: 24px;
  border-radius: 28px;
  border: 1px solid var(--border-light);
  box-shadow: 0 30px 60px rgba(0, 0, 0, 0.6), inset 0 1px 0 rgba(255, 255, 255, 0.1);
  box-sizing: border-box;
  overflow: hidden; /* 防卡片外溢 */
}

h2 {
  font-size: 22px; font-weight: 700; margin: 0 0 15px 0; text-align: center;
  letter-spacing: -0.5px;
  background: linear-gradient(135deg, #ffffff 30%, #a1a1aa 100%);
  -webkit-background-clip: text; -webkit-text-fill-color: transparent;
}

.row { display: flex; justify-content: space-between; align-items: center; margin: 10px 0; }
.label-text { font-size: 13px; color: #71717a; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 8px; display: block;}

/* 效能診斷網格 (Diagnostic Grid) */
.diag-grid {
  display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-bottom: 18px;
}
.diag-item {
  background: rgba(0, 0, 0, 0.3); border: 1px solid var(--border-light);
  padding: 8px 4px; border-radius: 12px; text-align: center;
}
.diag-title { font-size: 10px; color: #71717a; font-weight: 600; text-transform: uppercase; margin-bottom: 4px; }
.diag-val { font-size: 11px; font-weight: 700; }
.status-on { color: var(--accent-green); }
.status-off { color: #52525b; }
.mode-stealth { color: var(--accent-cyan); }
.mode-spread { color: var(--accent-purple); }

/* 主控制按鈕 */
button { width: 100%; padding: 16px; margin: 10px 0; font-size: 17px; font-weight: 600; border-radius: 18px; border: none; color: white; cursor: pointer; transition: all 0.25s ease; }
.btn-start { background: linear-gradient(135deg, #2ed553, #10b981); box-shadow: 0 8px 25px rgba(46, 213, 83, 0.25); }
.btn-stop { background: linear-gradient(135deg, #ff453a, #f43f5e); box-shadow: 0 8px 25px rgba(255, 69, 58, 0.25); }

/* 現代切換開關 */
.toggle-group { display: flex; background: rgba(0, 0, 0, 0.4); padding: 4px; border-radius: 16px; border: 1px solid var(--border-light); margin-bottom: 18px; }
.toggle-btn {
  flex: 1; padding: 12px; font-size: 14px; font-weight: 600; color: #a1a1aa; border: none; background: transparent; border-radius: 12px; cursor: pointer; transition: 0.25s; text-align: center; margin: 0;
}
.toggle-btn.active { background: rgba(255, 255, 255, 0.1); color: #ffffff; border: 1px solid rgba(255,255,255,0.1); }

.rpm-display { font-size: 52px; font-weight: 800; text-align: center; margin: 5px 0; background: linear-gradient(135deg, var(--accent-cyan), var(--accent-blue)); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
.rpm-unit { font-size: 16px; color: #71717a; font-weight: 500; }

.slider { -webkit-appearance: none; width: 100%; height: 6px; border-radius: 3px; background: rgba(255, 255, 255, 0.1); outline: none; margin: 20px 0 25px 0; }
.slider::-webkit-slider-thumb {
  -webkit-appearance: none; width: 26px; height: 26px; border-radius: 50%; background: #ffffff; cursor: pointer;
  box-shadow: 0 0 15px var(--accent-cyan); border: 2px solid var(--accent-cyan); transition: transform 0.15s ease;
}

/* 時間輸入欄位 (嚴格三分防跑版) */
.timer-container { 
  display: flex; 
  gap: 8px; 
  width: 100%; 
  box-sizing: border-box; 
}

.timer-container input[type='number'] {
  flex: 1 1 0;
  width: 0;
  min-width: 0;
  box-sizing: border-box;
  padding: 10px 2px;
  font-size: 14px;
  font-weight: 600;
  background: rgba(0, 0, 0, 0.3);
  color: #ffffff;
  border: 1px solid var(--border-light);
  border-radius: 12px;
  text-align: center;
  outline: none;
  transition: 0.25s;
}

.timer-container input[type='number']:focus { 
  border-color: var(--accent-cyan); 
  background: rgba(0,0,0,0.5); 
}

.status-badge { padding: 4px 12px; border-radius: 20px; font-weight: 600; font-size: 13px; }
.badge-run { background: rgba(46, 213, 83, 0.15); color: var(--accent-green); border: 1px solid rgba(46, 213, 83, 0.3); }
.badge-stop { background: rgba(255, 69, 58, 0.15); color: var(--accent-red); border: 1px solid rgba(255, 69, 58, 0.3); }
.temp-box { background: rgba(255,255,255,0.02); color: var(--accent-green); padding: 12px; border-radius: 16px; font-size: 14px; font-weight: 600; text-align: center; border: 1px solid rgba(46,213,83,0.2); margin-bottom: 15px; }
.ms-badge { background: rgba(255,255,255,0.05); color: var(--accent-cyan); border: 1px solid rgba(0,242,254,0.2); padding: 2px 10px; border-radius: 8px; font-size: 13px; }
hr { border: 0; border-top: 1px solid var(--border-light); margin: 20px 0; }
</style>

<script>
var lastState = null;
function send(p, v){ var x = new XMLHttpRequest(); x.open('GET', '/api?' + p + '=' + v, true); x.send(); } 

function setDirection(dir) {
  document.getElementById('btn_cw').classList.toggle('active', dir === 1);
  document.getElementById('btn_ccw').classList.toggle('active', dir === -1);
  send('dir', dir);
}

function triggerAction(action){
  var url = '/' + action;
  if(action === 'start'){
    var currentRpm = document.getElementById('rpm_slider').value;
    url += '?rpm=' + currentRpm;
  }
  var x = new XMLHttpRequest();
  x.open('GET', url, true);
  x.send();
}

if(!!window.EventSource){
  var s = new EventSource('/live-stream');
  s.addEventListener('message', function(e){
    var o = JSON.parse(e.data);
    document.getElementById('t_disp').innerText = o.timer;
    document.getElementById('temp_disp').innerText = o.temp;
    document.getElementById('ms_disp').innerText = "1 / " + o.microstep;
    
    // 即時更新診斷燈號區塊
    var intpEl = document.getElementById('diag_intp');
    intpEl.innerText = o.intpol ? "256 ON" : "OFF";
    intpEl.className = "diag-val " + (o.intpol ? "status-on" : "status-off");

    var coolEl = document.getElementById('diag_cool');
    coolEl.innerText = o.coolstep ? "ACTIVE" : "OFF";
    coolEl.className = "diag-val " + (o.coolstep ? "status-on" : "status-off");

    var modeEl = document.getElementById('diag_mode');
    modeEl.innerText = o.mode;
    modeEl.className = "diag-val " + (o.mode === "StealthChop" ? "mode-stealth" : "mode-spread");
    
    if(lastState !== null && o.running !== lastState){ location.reload(); }
    lastState = o.running;
  }, false);
}
</script></head><body>

<div class='card'>
  <h2>TMC2209 Control</h2>
  <div id='temp_disp' class='temp-box'>🟢 Temp Normal (< 120&deg;C)</div>

  <!-- 效能診斷燈號 -->
  <div class='diag-grid'>
    <div class='diag-item'>
      <div class='diag-title'>Interpolation</div>
      <div id='diag_intp' class='diag-val status-on'>256 ON</div>
    </div>
    <div class='diag-item'>
      <div class='diag-title'>CoolStep</div>
      <div id='diag_cool' class='diag-val status-on'>ACTIVE</div>
    </div>
    <div class='diag-item'>
      <div class='diag-title'>Drive Mode</div>
      <div id='diag_mode' class='diag-val mode-stealth'>StealthChop</div>
    </div>
  </div>
  
  <div class='row'><span style='color:#a1a1aa; font-size:14px;'>Motor Status</span><span id='status_box_placeholder'></span></div>
  <div class='row'><span style='color:#a1a1aa; font-size:14px;'>Microstep Gear</span><span id='ms_disp' class='ms-badge'>1 / 16</span></div>
  <div class='row'><span style='color:#a1a1aa; font-size:14px;'>Timer Remaining</span><span id='t_disp' style='color:#ffb703; font-weight:600;'>Continuous</span></div>

  <div id='action_btn_area'></div>

  <hr>

  <span class='label-text'>Speed Control</span>
  <div class='rpm-display'><span id='rpm_txt'>60</span> <span class='rpm-unit'>RPM</span></div>
  <input type='range' id='rpm_slider' class='slider' min='1' max='450' step='1' value='60' 
         oninput='document.getElementById("rpm_txt").innerText=this.value' 
         onchange='send("rpm",this.value)'>

  <span class='label-text'>Rotation Direction</span>
  <div class='toggle-group'>
    <button id='btn_cw' class='toggle-btn active' onclick='setDirection(1)'>Clockwise (CW)</button>
    <button id='btn_ccw' class='toggle-btn' onclick='setDirection(-1)'>Counter-CW (CCW)</button>
  </div>

  <span class='label-text'>Auto Shutdown Timer</span>
  <div class='timer-container'>
    <input type='number' placeholder='Hr' onchange='send("t_h",this.value)'>
    <input type='number' placeholder='Min' onchange='send("t_m",this.value)'>
    <input type='number' placeholder='Sec' onchange='send("t_s",this.value)'>
  </div>
</div>

</body></html>
)rawliteral";

#endif