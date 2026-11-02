// xLume + Sentry - web page (served from flash)
#pragma once
#include <Arduino.h>

static const char kIndexHtml[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>xLume + Sentry</title>
<style>
:root{--bg:#0b0e13;--panel:#141a22;--panel2:#1c2430;--line:#2a3442;--text:#e8eef4;--dim:#8a96a6;--accent:#5ed6c9;--bad:#f85149;--good:#3fb950}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{padding:18px 16px 6px;max-width:1100px;margin:auto;display:flex;align-items:baseline;gap:10px;flex-wrap:wrap}
h1{margin:0;font-size:26px}h1 span{color:var(--accent);font-weight:500;font-size:18px}
.sub{color:var(--dim);font-size:12px}
main{max-width:1100px;margin:auto;padding:8px 16px 32px;display:grid;gap:14px;grid-template-columns:minmax(0,1fr) minmax(0,1fr)}
@media(max-width:760px){main{grid-template-columns:minmax(0,1fr)}}
.card{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:16px}
.card h2{margin:0 0 12px;font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:var(--accent)}
.wide{grid-column:1/-1}
.temps{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}
.t{background:var(--panel2);border-radius:10px;padding:10px 4px;text-align:center}.t b{display:block;font-size:clamp(16px,4.2vw,22px);white-space:nowrap}.t small{color:var(--dim)}
.kv{display:grid;grid-template-columns:110px 1fr;gap:6px 10px;margin-top:12px;font-size:14px}.kv div:nth-child(odd){color:var(--dim)}
label{display:block;color:var(--dim);font-size:13px;margin:10px 0 4px}
select,input[type=number],input[type=password]{width:100%;background:var(--panel2);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:8px}
input[type=range]{width:100%;accent-color:var(--accent)}
.row{display:flex;gap:10px;align-items:center}.row>*{flex:1;min-width:0}.row.wrap{flex-wrap:wrap}.row.wrap>input{flex:1 1 200px}
input[type=color]{width:100%;height:42px;border:1px solid var(--line);border-radius:8px;background:var(--panel2);padding:3px}
button{background:var(--panel2);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:8px 12px;cursor:pointer;font:inherit}
button.primary{background:var(--accent);color:#06231f;border-color:var(--accent);font-weight:600}
.strip{display:flex;gap:6px;margin:6px 0 2px;flex-wrap:wrap}.dot{width:18px;height:18px;border-radius:50%;background:#222;box-shadow:0 0 8px currentColor}
#console{background:#0f1319;border:1px solid var(--line);border-radius:10px;padding:10px;height:260px;overflow-y:auto;font:12px/1.45 ui-monospace,Consolas,monospace;white-space:pre-wrap}
.seg{display:inline-flex;border:1px solid var(--line);border-radius:8px;overflow:hidden}.seg button{border:0;border-radius:0}.seg button.on{background:var(--accent);color:#06231f}
progress{width:100%;height:12px}
.muted{color:var(--dim);font-size:12px}
</style></head><body>
<header><h1>xLume <span>+ Sentry</span></h1><div class="sub" id="fw"></div></header>
<main>
<section class="card"><h2>Console</h2>
 <div class="temps"><div class="t"><small>CPU</small><b id="tc">--</b></div><div class="t"><small>GPU</small><b id="tg">--</b></div><div class="t"><small>eDRAM</small><b id="te">--</b></div><div class="t"><small>Mobo</small><b id="tm">--</b></div></div>
 <div class="kv"><div>Now playing</div><div id="game">-</div><div>Developer</div><div id="dev">-</div><div>Title ID</div><div id="tid">-</div><div>Xbox IP</div><div id="xip">-</div><div>Last data</div><div id="seen">-</div><div>SD card</div><div id="sd">-</div></div>
 <label>Screen</label><div class="seg"><button id="mDef" onclick="setMode(0)">Status</button><button id="mUart" onclick="setMode(1)">UART log</button></div>
</section>
<section class="card"><h2>Lighting</h2>
 <div class="strip" id="strip"></div>
 <label>Effect</label><select id="effect" onchange="send({effect:this.value})"></select>
 <div class="row"><div><label>Color</label><input type="color" id="c1" oninput="later({c1:this.value.slice(1)})"></div>
 <div><label>Color 2</label><input type="color" id="c2" oninput="later({c2:this.value.slice(1)})"></div></div>
 <label>Brightness <span id="bv"></span></label><input type="range" id="bri" min="0" max="255" oninput="bv.textContent=Math.round(this.value/2.55)+'%';later({bri:this.value})">
 <label>Speed <span id="sv"></span></label><input type="range" id="spd" min="1" max="255" oninput="sv.textContent=this.value;later({spd:this.value})">
 <div class="row"><div><label>LED count</label><input type="number" id="cnt" min="1" max="300"></div><div><label>&nbsp;</label><button onclick="send({count:cnt.value})">Set count</button></div></div>
 <div class="row" style="margin-top:12px"><button onclick="send({effect:0})">Static</button><button onclick="send({effect:4})">LEDs off</button></div>
 <p class="muted">Changes apply instantly and are saved on the board. The console plugin may change the look again when you start another game.</p>
</section>
<section class="card wide"><h2>UART output</h2>
 <div id="console"></div>
 <div class="row" style="margin-top:10px;flex:0"><label style="margin:0;flex:0 0 auto"><input type="checkbox" id="auto" checked> Autoscroll</label><button style="flex:0 0 auto" onclick="document.getElementById('console').textContent=''">Clear output</button></div>
</section>
<section class="card wide"><h2>Firmware update</h2>
 <p class="muted">Upload the .bin from Arduino IDE (Sketch &rsaquo; Export Compiled Binary). One file updates both the lighting and the Sentry screen.</p>
 <div class="row wrap"><input type="file" id="bin" accept=".bin"><input type="password" id="pw" placeholder="Update password"><button class="primary" style="flex:0 0 auto" onclick="upload()">Upload</button></div>
 <progress id="prog" max="100" value="0" style="margin-top:10px"></progress><div class="muted" id="upmsg"></div>
</section>
</main>
<script>
const FX=["Static Color","Static Gradient","Static Pulse","Static Twinkle","LEDs Off","Color Wave","Color Cycle","Dynamic Pulse","Dynamic Twinkle","Fire","Confetti","Sparkle","Theater Chase","Rainbow Chase","Larson Scanner","Comet","Meteor Rain","Color Wipe","Two-Color Gradient","Two-Color Chase","Strobe","Police Lights","Candle Flicker"];
const $=id=>document.getElementById(id);
FX.forEach((n,i)=>{const o=document.createElement('option');o.value=i;o.textContent=n;$('effect').appendChild(o)});
for(let i=0;i<12;i++){const d=document.createElement('div');d.className='dot';$('strip').appendChild(d)}
let editing=0,pend={},timer=null;
function q(o){return Object.keys(o).map(k=>k+'='+encodeURIComponent(o[k])).join('&')}
function send(o){editing=Date.now();fetch('/api/led?'+q(o))}
function later(o){Object.assign(pend,o);editing=Date.now();clearTimeout(timer);timer=setTimeout(()=>{send(pend);pend={}},120)}
function setMode(u){fetch('/api/mode?uart='+u).then(poll)}
const hex=a=>'#'+a.map(v=>v.toString(16).padStart(2,'0')).join('');
function t(v){return v==null?'--':v.toFixed(1)+'°'}
function poll(){fetch('/api/status').then(r=>r.json()).then(s=>{
 $('fw').textContent='firmware '+s.fw+'  ·  '+s.net;
 if(s.temps){$('tc').textContent=t(s.temps[0]);$('tg').textContent=t(s.temps[1]);$('te').textContent=t(s.temps[2]);$('tm').textContent=t(s.temps[3])}
 $('game').textContent=s.game||'-';$('dev').textContent=s.dev||'-';$('tid').textContent=s.titleId||'-';$('xip').textContent=s.xboxIp||'-';
 $('seen').textContent=s.lastLineAgo<0?'nothing yet':(s.lastLineAgo<2?'just now':s.lastLineAgo+' s ago');$('sd').textContent=s.sd?'OK':'not found';
 $('mDef').className=s.uart?'':'on';$('mUart').className=s.uart?'on':'';
 const l=s.led;[...$('strip').children].forEach((d,i)=>{const c=hex(l.preview[i]);d.style.background=c;d.style.color=c});
 if(Date.now()-editing>1500){$('effect').value=l.effect;$('c1').value=hex(l.c1);$('c2').value=hex(l.c2);$('bri').value=l.bri;$('bv').textContent=Math.round(l.bri/2.55)+'%';$('spd').value=l.spd;$('sv').textContent=l.spd;if(document.activeElement!==$('cnt'))$('cnt').value=l.count}
}).catch(()=>{})}
function logPoll(){fetch('/serialdata').then(r=>r.text()).then(x=>{if(x&&x!=='No data available'){const c=$('console');c.textContent+=x;if(c.textContent.length>60000)c.textContent=c.textContent.slice(-40000);if($('auto').checked)c.scrollTop=c.scrollHeight}}).catch(()=>{})}
function upload(){const f=$('bin').files[0];if(!f){$('upmsg').textContent='Choose a .bin file first.';return}
 const x=new XMLHttpRequest(),fd=new FormData();fd.append('firmware',f,f.name);
 x.upload.onprogress=e=>{if(e.lengthComputable)$('prog').value=e.loaded*100/e.total};
 x.onload=()=>{$('upmsg').textContent=x.status==200?'Done - the board is restarting.':'Failed: '+x.responseText};
 x.onerror=()=>{$('upmsg').textContent='Connection lost (normal if it finished and restarted).'};
 x.open('POST','/update');x.setRequestHeader('X-OTA-Password',$('pw').value);x.send(fd);$('upmsg').textContent='Uploading...'}
poll();setInterval(poll,1000);setInterval(logPoll,250);
</script></body></html>)HTML";
