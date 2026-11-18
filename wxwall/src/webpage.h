#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WxWall</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#667085;--line:#dfe3ea;--acc:#2563eb;--vfr:#16a34a;--mvfr:#2563eb;--ifr:#dc2626;--lifr:#c026d3}
@media (prefers-color-scheme:dark){:root{--bg:#0f1115;--card:#181b22;--fg:#e6e8ee;--mut:#98a2b3;--line:#2a2f3a;--acc:#60a5fa}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:640px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 16px;display:flex;align-items:center;gap:10px}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--mut);margin:0 0 12px}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:14px}
label{display:block;font-weight:600;margin:12px 0 4px}label:first-of-type{margin-top:0}
.hint{color:var(--mut);font-size:13px;font-weight:400}
input[type=text],input[type=number],select{width:100%;padding:9px 10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg);font:inherit}
input[type=range]{width:100%}
.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.checks{display:grid;grid-template-columns:1fr 1fr;gap:6px}.checks label{font-weight:400;margin:0;display:flex;gap:8px;align-items:center}
button{font:inherit;font-weight:600;padding:9px 14px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--fg);cursor:pointer}
button.primary{background:var(--acc);border-color:var(--acc);color:#fff}
.actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:14px}
.chip{font-size:12px;font-weight:700;padding:2px 8px;border-radius:99px;color:#fff;background:#888}
.VFR{background:var(--vfr)}.MVFR{background:var(--mvfr)}.IFR{background:var(--ifr)}.LIFR{background:var(--lifr)}
pre{white-space:pre-wrap;word-break:break-word;background:var(--bg);border:1px solid var(--line);border-radius:8px;padding:10px;margin:0 0 10px;font:13px ui-monospace,Menlo,monospace}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;margin:0;font-size:14px}dt{color:var(--mut)}dd{margin:0}
.search{display:flex;gap:8px}.search input{flex:1}#fcResults{display:grid;gap:6px;margin-top:8px}#fcResults button{text-align:left;font-weight:400}
.err{color:var(--ifr)}#msg{min-height:1.4em;margin-top:10px;font-weight:600}
</style></head><body><main>
<h1>WxWall <span id="cat" class="chip">—</span></h1>

<section><h2>Now</h2>
<pre id="raw">Loading…</pre>
<dl id="status"></dl>
</section>

<section><h2>Nearby flights</h2>
<label for="adsbUrl">PiAware URL <span class="hint">— your receiver's aircraft.json; leave blank to use adsb.lol around the forecast location</span></label>
<input id="adsbUrl" type="text" placeholder="http://192.168.1.50/skyaware/data/aircraft.json">
<div class="row">
<div><label for="radiusNm">Radius (NM)</label><input id="radiusNm" type="number" min="1" max="100"></div>
<div><label for="maxFlights">Flights shown</label><input id="maxFlights" type="number" min="1" max="8"></div>
</div>
<div class="row">
<div><label for="minAlt">Min altitude (ft)</label><input id="minAlt" type="number" min="0" max="60000" step="100"></div>
<div><label for="maxAlt">Max altitude (ft)</label><input id="maxAlt" type="number" min="0" max="60000" step="1000"></div>
</div>
<p class="hint">Min altitude 0 includes aircraft on the ground.</p>
<label for="aeroKey">FlightAware AeroAPI key <span class="hint" id="aeroKeyState"></span></label>
<div class="search"><input id="aeroKey" type="password" autocomplete="off" placeholder="Paste a key to set or replace it"><button id="aeroClear">Clear</button></div>
<label for="aeroBudget">AeroAPI lookups per month <span class="hint">— $0.005 each; the $10 feeder credit covers 2,000. Used only when the free adsbdb lookup has no plausible route, paced across the month.</span></label>
<input id="aeroBudget" type="number" min="0" max="60000" step="100">
</section>

<section><h2>Airports</h2>
<label for="airports">Airports <span class="hint">— each gets its own METAR page; the first also gets the TAF timeline (up to 10)</span></label>
<input id="airports" type="text" placeholder="KPAO, KSJC, KSFO" autocapitalize="characters">
</section>

<section><h2>Forecast location</h2>
<p id="fcCurrent" class="hint"></p>
<label for="fcQuery">City or ZIP code</label>
<div class="search"><input id="fcQuery" type="text" placeholder="Palo Alto or 94301"><button id="fcSearch">Search</button></div>
<div id="fcResults"></div>
</section>

<section><h2>Display</h2>
<label>Pages</label>
<div class="checks">
<label><input type="checkbox" id="pg_flights"> Nearby flights</label>
<label><input type="checkbox" id="pg_metar"> METAR (each airport)</label>
<label><input type="checkbox" id="pg_taf"> TAF timeline</label>
<label><input type="checkbox" id="pg_outlook"> Forecast</label>
</div>
<div class="row">
<div><label for="pageSecs">Seconds per page</label><input id="pageSecs" type="number" min="3" max="120"></div>
<div><label for="celsius">Forecast temps</label><select id="celsius"><option value="0">°F</option><option value="1">°C</option></select></div>
</div>
<label for="bright">Brightness <span class="hint" id="brightVal"></span></label>
<input id="bright" type="range" min="1" max="255">
<div class="row">
<div><label for="nightStart">Night starts</label><select id="nightStart"></select></div>
<div><label for="nightEnd">Night ends</label><select id="nightEnd"></select></div>
</div>
<label for="nightBright">Night brightness <span class="hint" id="nightVal"></span></label>
<input id="nightBright" type="range" min="0" max="255">
<p class="hint">Set start and end to the same hour to turn night dimming off. Night brightness 0 turns the panel off.</p>
</section>

<section><h2>Time</h2>
<div class="row">
<div><label for="clock">Clock</label><select id="clock"><option value="0">Local 12h</option><option value="1">Local 24h</option><option value="2">Zulu</option></select></div>
<div><label for="tzSel">Time zone</label><select id="tzSel"></select></div>
</div>
<div id="tzCustomWrap" hidden><label for="tz">POSIX TZ string</label><input id="tz" type="text"></div>
</section>

<section><h2>Advanced</h2>
<label for="api">Tachbase API URL</label>
<input id="api" type="text">
<div class="actions">
<button id="refresh">Refresh weather now</button>
<button id="restart">Restart</button>
<button id="forget">Forget Wi-Fi</button>
</div>
<label for="fw" style="margin-top:16px">Firmware update <span class="hint">— upload firmware.bin</span></label>
<form id="fwForm"><input id="fw" type="file" accept=".bin"> <button>Upload</button></form>
</section>

<div class="actions"><button class="primary" id="save">Save settings</button></div>
<div id="msg"></div>
</main>
<script>
const $=id=>document.getElementById(id);
const TZ=[["Pacific","PST8PDT,M3.2.0,M11.1.0",["America/Los_Angeles","America/Vancouver"]],
["Mountain","MST7MDT,M3.2.0,M11.1.0",["America/Denver","America/Boise","America/Edmonton"]],
["Arizona","MST7",["America/Phoenix"]],
["Central","CST6CDT,M3.2.0,M11.1.0",["America/Chicago","America/Winnipeg"]],
["Eastern","EST5EDT,M3.2.0,M11.1.0",["America/New_York","America/Detroit","America/Toronto","America/Indiana/Indianapolis"]],
["Alaska","AKST9AKDT,M3.2.0,M11.1.0",["America/Anchorage"]],
["Hawaii","HST10",["Pacific/Honolulu"]],["UTC","UTC0",["UTC","Etc/UTC"]]];
TZ.forEach(([n,v])=>$("tzSel").add(new Option(n,v)));$("tzSel").add(new Option("Custom…","custom"));
for(let h=0;h<24;h++){const t=(h%12||12)+(h<12?" AM":" PM");$("nightStart").add(new Option(t,h));$("nightEnd").add(new Option(t,h));}
$("tzSel").onchange=()=>{$("tzCustomWrap").hidden=$("tzSel").value!=="custom"};
const pct=v=>Math.round(v/2.55)+"%";
let bt;$("bright").oninput=e=>{$("brightVal").textContent=pct(e.target.value);clearTimeout(bt);bt=setTimeout(()=>fetch("/api/brightness?v="+e.target.value,{method:"POST"}),150)};
$("nightBright").oninput=e=>$("nightVal").textContent=pct(e.target.value);
let fc=null,aeroClear=false;
function showFc(){$("fcCurrent").textContent=fc?"Forecast for "+fc.label+" ("+(+fc.lat).toFixed(2)+", "+(+fc.lon).toFixed(2)+")":"No location set. The forecast page is hidden until you pick one."}
function pickFc(r){fc=r;showFc();$("fcResults").innerHTML="";msg("Location selected. Click Save settings.")}
async function fcSearch(){
 const q=$("fcQuery").value.trim();if(!q)return;const zip=/^\d{5}$/.test(q);$("fcResults").textContent="Searching…";
 try{const j=await(await fetch("https://geocoding-api.open-meteo.com/v1/search?count=6&language=en&name="+encodeURIComponent(q)+(zip?"&countryCode=US":""))).json();
  const rs=(j.results||[]).map(r=>({name:r.name,lat:r.latitude,lon:r.longitude,
   label:[r.name,r.admin1,r.country_code!=="US"?r.country:null].filter(Boolean).join(", ")+(zip?" "+q:"")}));
  $("fcResults").innerHTML="";if(!rs.length){$("fcResults").textContent="No matches.";return}
  if(rs.length===1){pickFc(rs[0]);return}
  rs.forEach(r=>{const b=document.createElement("button");b.textContent=r.label;b.onclick=()=>pickFc(r);$("fcResults").append(b)});
 }catch(e){$("fcResults").textContent="Search failed. Is this device online?"}}
$("aeroClear").onclick=()=>{aeroClear=true;$("aeroKey").value="";$("aeroKeyState").textContent="— will be cleared when you save"};
$("fcSearch").onclick=fcSearch;$("fcQuery").onkeydown=e=>{if(e.key==="Enter")fcSearch()};
const msg=(t,bad)=>{$("msg").textContent=t;$("msg").className=bad?"err":""};
const ago=s=>s<0?"never":s<90?s+"s ago":Math.round(s/60)+" min ago";
function fill(s){
 $("airports").value=s.airports;const F=s.flights;$("adsbUrl").value=F.adsbUrl;$("radiusNm").value=F.radiusNm;$("maxFlights").value=F.maxFlights;
 $("minAlt").value=F.minAlt;$("maxAlt").value=F.maxAlt;$("aeroBudget").value=F.aeroBudget;$("aeroKey").value="";aeroClear=false;
 $("aeroKeyState").textContent=F.aeroKeySet?"— a key is saved":"— not set; routes come from adsbdb (free)";$("pg_flights").checked=s.pages.flights;fc=s.forecast&&s.forecast.lat!=null?s.forecast:null;showFc();
 $("pg_metar").checked=s.pages.metar;$("pg_taf").checked=s.pages.taf;$("pg_outlook").checked=s.pages.outlook;
 $("pageSecs").value=s.pageSecs;$("celsius").value=s.celsius?1:0;
 $("bright").value=s.bright;$("brightVal").textContent=pct(s.bright);
 $("nightBright").value=s.nightBright;$("nightVal").textContent=pct(s.nightBright);
 $("nightStart").value=s.nightStart;$("nightEnd").value=s.nightEnd;$("clock").value=s.clock;$("api").value=s.api;
 let tz=s.tz;
 if(!s.configured){const z=Intl.DateTimeFormat().resolvedOptions().timeZone;const m=TZ.find(t=>t[2].includes(z));if(m)tz=m[1];}
 if(TZ.some(t=>t[1]===tz)){$("tzSel").value=tz;$("tz").value=tz}else{$("tzSel").value="custom";$("tz").value=tz}
 $("tzCustomWrap").hidden=$("tzSel").value!=="custom";
}
function status(st){
 const h=st.primary,first=(st.settings.airports||"").split(",")[0];$("raw").textContent=h&&h.raw?h.raw:(first?"No METAR yet for "+first:"Add airports below.");
 const c=$("cat");c.textContent=h&&h.cat?h.cat:"—";c.className="chip "+(h&&h.cat||"");
 const fl=st.flights,flTxt=st.settings.pages.flights?(fl.count+" nearby · center: "+fl.center+" · "+ago(fl.age)+(fl.err?" — "+fl.err:"")):"off";
 const rows=[["Flights",flTxt],["AeroAPI",st.settings.flights.aeroKeySet?(fl.aeroToday+" today (pace "+fl.aeroAllowedToday+"/day) · "+fl.aeroMonth+" of "+st.settings.flights.aeroBudget+" this month (≈$"+(fl.aeroMonth*0.005).toFixed(2)+")"+(fl.aeroErr?" — "+fl.aeroErr:"")):"no key"],["METAR",ago(st.metarAge)+(st.metarErr?" — "+st.metarErr:"")],["TAF",ago(st.tafAge)+(st.tafErr?" — "+st.tafErr:"")],
  ["Forecast",(st.settings.forecast.name?st.settings.forecast.name+" · "+ago(st.outlookAge):"no location")+(st.outlookErr?" — "+st.outlookErr:"")],["Address",st.ip+" · wxwall.local"],["Firmware",st.version+" · up "+Math.round(st.uptime/60)+" min · "+Math.round(st.heap/1024)+" KB free"]];
 $("status").innerHTML="";rows.forEach(([k,v])=>{const dt=document.createElement("dt");dt.textContent=k;const dd=document.createElement("dd");dd.textContent=v;$("status").append(dt,dd)});
}
async function load(first){try{const st=await (await fetch("/api/state")).json();if(first)fill(st.settings);status(st)}catch(e){if(first)msg("Could not reach the display",1)}}
$("save").onclick=async()=>{
 const tz=$("tzSel").value==="custom"?$("tz").value:$("tzSel").value;
 const flb={adsbUrl:$("adsbUrl").value,radiusNm:+$("radiusNm").value,maxFlights:+$("maxFlights").value,minAlt:+$("minAlt").value,
  maxAlt:+$("maxAlt").value,aeroBudget:+$("aeroBudget").value};if($("aeroKey").value.trim())flb.aeroKey=$("aeroKey").value.trim();if(aeroClear)flb.aeroKeyClear=true;
 const body={flights:flb,airports:$("airports").value,forecast:fc||{},tz,clock:+$("clock").value,bright:+$("bright").value,nightBright:+$("nightBright").value,
  nightStart:+$("nightStart").value,nightEnd:+$("nightEnd").value,pageSecs:+$("pageSecs").value,celsius:$("celsius").value==="1",api:$("api").value,
  pages:{flights:$("pg_flights").checked,metar:$("pg_metar").checked,taf:$("pg_taf").checked,outlook:$("pg_outlook").checked}};
 const r=await fetch("/api/settings",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)});
 const j=await r.json();if(!r.ok){msg(j.error||"Save failed",1);return}
 fill(j.settings);msg("Saved. Fetching weather…");setTimeout(()=>load(),6000);
};
const post=(u,t)=>fetch(u,{method:"POST"}).then(()=>msg(t));
$("refresh").onclick=()=>{post("/api/refresh","Refreshing…");setTimeout(()=>load(),6000)};
$("restart").onclick=()=>confirm("Restart the display?")&&post("/api/restart","Restarting…");
$("forget").onclick=()=>confirm("Forget Wi-Fi? The display will start its setup network (WxWall-Setup).")&&post("/api/forget-wifi","Wi-Fi cleared. Join WxWall-Setup to reconfigure.");
$("fwForm").onsubmit=async e=>{e.preventDefault();const f=$("fw").files[0];if(!f)return;msg("Uploading "+f.name+"…");
 const fd=new FormData();fd.append("firmware",f);const r=await fetch("/update",{method:"POST",body:fd});const t=await r.text();msg(t,!r.ok);if(r.ok)setTimeout(()=>location.reload(),15000)};
load(true);setInterval(load,15000);
</script></body></html>)HTML";
