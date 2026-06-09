// ============================================================
//  DashboardHtml.cpp  -  Eingebettete Dashboard-Seite
//  AUTO-GENERIERT aus web/index.html. Zum Anpassen entweder
//  diese Datei direkt editieren oder web/index.html aendern
//  und neu einbetten (siehe README, Abschnitt "Dashboard").
// ============================================================
#include "DashboardHtml.h"

const char* DASHBOARD_HTML = R"EVORLHTML(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>EvoRL · Trainings-Monitor</title>
<style>
  :root{
    --ink:#0a0e13;        /* Hintergrund          */
    --panel:#121922;      /* Kartenflaeche        */
    --panel-2:#0e151d;    /* tiefer gesetzt       */
    --line:#1f2b38;       /* Rahmen / Raster      */
    --txt:#dfe7ef;        /* Primaertext          */
    --muted:#6b7d90;      /* Sekundaertext        */
    --amber:#f5a623;      /* Hauptsignal          */
    --teal:#2dd4bf;       /* Zweitsignal          */
    --rose:#f43f5e;       /* Warnung / Inject     */
    --grid-radius:14px;
  }
  *{box-sizing:border-box}
  html,body{margin:0;height:100%}
  body{
    background:
      radial-gradient(1200px 600px at 80% -10%, #15202c 0%, transparent 60%),
      var(--ink);
    color:var(--txt);
    font-family:"Inter","Segoe UI",system-ui,sans-serif;
    -webkit-font-smoothing:antialiased;
  }
  .mono{font-family:"JetBrains Mono","SFMono-Regular",ui-monospace,Menlo,Consolas,monospace}

  header{
    display:flex;align-items:baseline;gap:16px;flex-wrap:wrap;
    padding:22px 28px;border-bottom:1px solid var(--line);
  }
  header h1{
    margin:0;font-size:20px;font-weight:600;letter-spacing:.02em;
  }
  header h1 .dot{color:var(--amber)}
  header .sub{color:var(--muted);font-size:13px}
  .status-pill{
    margin-left:auto;display:inline-flex;align-items:center;gap:9px;
    padding:7px 14px;border:1px solid var(--line);border-radius:999px;
    font-size:12.5px;letter-spacing:.04em;text-transform:uppercase;color:var(--muted);
  }
  .status-pill .led{width:9px;height:9px;border-radius:50%;background:var(--muted)}
  .status-pill.live .led{background:var(--teal);box-shadow:0 0 0 0 rgba(45,212,191,.5);animation:pulse 1.6s infinite}
  .status-pill.live{color:var(--teal);border-color:#1c3a37}
  .status-pill.done .led{background:var(--amber)}
  .status-pill.done{color:var(--amber);border-color:#3a3017}
  @keyframes pulse{0%{box-shadow:0 0 0 0 rgba(45,212,191,.45)}70%{box-shadow:0 0 0 8px rgba(45,212,191,0)}100%{box-shadow:0 0 0 0 rgba(45,212,191,0)}}

  main{padding:24px 28px 48px;max-width:1280px;margin:0 auto}

  /* Generations-Fortschritt */
  .progress-wrap{margin-bottom:24px}
  .progress-head{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:8px}
  .progress-head .gen{font-size:15px}
  .progress-head .gen b{font-size:22px;font-weight:600}
  .progress-head .pct{color:var(--muted);font-size:13px}
  .bar{height:6px;background:var(--panel-2);border-radius:999px;overflow:hidden;border:1px solid var(--line)}
  .bar > span{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--amber),#ffcf6b);transition:width .4s ease}

  /* Kennzahlen-Raster */
  .cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:14px;margin-bottom:26px}
  .card{
    background:linear-gradient(180deg,var(--panel),var(--panel-2));
    border:1px solid var(--line);border-radius:var(--grid-radius);
    padding:16px 18px;position:relative;overflow:hidden;
  }
  .card .label{color:var(--muted);font-size:11.5px;letter-spacing:.07em;text-transform:uppercase}
  .card .value{margin-top:8px;font-size:28px;font-weight:600;line-height:1}
  .card .unit{color:var(--muted);font-size:13px;margin-left:4px;font-weight:400}
  .card.accent .value{color:var(--amber)}
  .card.teal .value{color:var(--teal)}
  .card::after{content:"";position:absolute;inset:0 0 auto 0;height:2px;background:var(--line)}
  .card.accent::after{background:linear-gradient(90deg,var(--amber),transparent)}
  .card.teal::after{background:linear-gradient(90deg,var(--teal),transparent)}

  /* Live-Replay-Arena */
  .arena{background:var(--panel);border:1px solid var(--line);border-radius:var(--grid-radius);padding:16px 18px;margin-bottom:26px}
  .arena-head{display:flex;justify-content:space-between;align-items:baseline;gap:12px;flex-wrap:wrap;margin-bottom:12px}
  .arena-head h2{margin:0;font-size:13.5px;font-weight:600;letter-spacing:.02em}
  .arena-meta{color:var(--muted);font-size:12.5px}
  #arenaCanvas{width:100%;max-width:460px;aspect-ratio:1/1;display:block;margin:0 auto;
    background:var(--panel-2);border:1px solid var(--line);border-radius:8px}
  .arena-legend{display:flex;gap:18px;justify-content:center;margin-top:12px;font-size:12px;color:var(--muted)}
  .arena-legend .sw{display:inline-block;width:11px;height:11px;border-radius:3px;margin-right:6px;vertical-align:middle}
  .arena-legend .agent{background:var(--teal)}
  .arena-legend .food{background:var(--amber);border-radius:50%}
  .arena-legend .wall{background:#2a3744}

  /* Charts */
  .charts{display:grid;grid-template-columns:repeat(3,1fr);gap:18px}
  @media(max-width:980px){.charts{grid-template-columns:1fr 1fr}}
  @media(max-width:640px){.charts{grid-template-columns:1fr}}
  .chart{
    background:var(--panel);border:1px solid var(--line);
    border-radius:var(--grid-radius);padding:16px 18px 10px;
  }
  .chart h2{margin:0 0 2px;font-size:13.5px;font-weight:600;letter-spacing:.02em}
  .chart .legend{display:flex;gap:16px;margin:4px 0 10px;font-size:12px;color:var(--muted)}
  .chart .legend i{display:inline-block;width:10px;height:2px;margin-right:6px;vertical-align:middle}
  canvas{width:100%;height:230px;display:block}

  footer{color:var(--muted);font-size:12px;text-align:center;padding:18px;border-top:1px solid var(--line)}
  .err{color:var(--rose)}
</style>
</head>
<body>
  <header>
    <h1>EvoRL<span class="dot">·</span>Mission Control</h1>
    <span class="sub">Neuroevolution eines GRU-Agenten · Live-Telemetrie</span>
    <span id="status" class="status-pill"><span class="led"></span><span id="statusText">verbinde…</span></span>
  </header>

  <main>
    <section class="progress-wrap">
      <div class="progress-head">
        <div class="gen">Generation <b id="genNow" class="mono">–</b> <span class="pct">/ <span id="genTotal" class="mono">–</span></span></div>
        <div class="pct"><span id="elapsed" class="mono">0s</span> · Curriculum <span id="curr" class="mono">0%</span></div>
      </div>
      <div class="bar"><span id="progressBar"></span></div>
    </section>

    <section class="cards">
      <div class="card accent"><div class="label">Beste Fitness</div><div class="value mono" id="best">–</div></div>
      <div class="card"><div class="label">Ø Fitness</div><div class="value mono" id="avg">–</div></div>
      <div class="card teal"><div class="label">Ø Food / Episode</div><div class="value mono" id="avgFood">–</div></div>
      <div class="card teal"><div class="label">Bestes Food</div><div class="value mono" id="bestFood">–</div></div>
      <div class="card"><div class="label">Global Best</div><div class="value mono" id="gBest">–</div></div>
      <div class="card"><div class="label">Mutation σ</div><div class="value mono" id="mut">–</div></div>
      <div class="card"><div class="label">Diversität</div><div class="value mono" id="div">–</div></div>
      <div class="card"><div class="label">Spezies</div><div class="value mono" id="species">–</div></div>
    </section>

    <section class="arena">
      <div class="arena-head">
        <h2>Bester Agent · Live-Replay</h2>
        <div class="arena-meta mono"><span id="rGen">–</span> · Step <span id="rStep">0</span>/<span id="rLen">0</span> · Food <span id="rFood">0</span></div>
      </div>
      <canvas id="arenaCanvas"></canvas>
      <div class="arena-legend">
        <span><i class="sw agent"></i>Agent</span>
        <span><i class="sw food"></i>Nahrung</span>
        <span><i class="sw wall"></i>Wand / Hindernis</span>
      </div>
    </section>

    <section class="charts">
      <div class="chart">
        <h2>Fitness-Verlauf</h2>
        <div class="legend"><span><i style="background:var(--amber)"></i>Best</span><span><i style="background:var(--teal)"></i>Durchschnitt</span></div>
        <canvas id="fitChart"></canvas>
      </div>
      <div class="chart">
        <h2>Nahrung pro Episode</h2>
        <div class="legend"><span><i style="background:var(--amber)"></i>Best Food</span><span><i style="background:var(--teal)"></i>Ø Food</span></div>
        <canvas id="foodChart"></canvas>
      </div>
      <div class="chart">
        <h2>Effizienz · Schritte / Nahrung</h2>
        <div class="legend"><span><i style="background:var(--amber)"></i>Best</span><span><i style="background:var(--teal)"></i>Ø</span><span style="margin-left:auto;color:var(--muted)">niedriger = besser</span></div>
        <canvas id="spfChart"></canvas>
      </div>
    </section>
  </main>

  <footer>Pollt <span class="mono">/api/status</span> · aktualisiert jede Sekunde · <span id="conn"></span></footer>

<script>
const $ = id => document.getElementById(id);
const fmt = (n,d=1) => (n==null||isNaN(n)) ? "–" : Number(n).toLocaleString("de-DE",{maximumFractionDigits:d});
const fmtTime = s => { s=Math.floor(s); const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),x=s%60;
  return (h?h+"h ":"")+(h||m?m+"m ":"")+x+"s"; };
const getCss = v => getComputedStyle(document.documentElement).getPropertyValue(v).trim();
function roundRect(ctx,x,y,w,h,r){ctx.beginPath();ctx.moveTo(x+r,y);ctx.arcTo(x+w,y,x+w,y+h,r);
  ctx.arcTo(x+w,y+h,x,y+h,r);ctx.arcTo(x,y+h,x,y,r);ctx.arcTo(x,y,x+w,y,r);ctx.closePath();}

// ---- Schlanker Canvas-Linienchart (kein externes Framework) ----
function drawChart(canvas, series){
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth, h = canvas.clientHeight;
  canvas.width = w*dpr; canvas.height = h*dpr;
  const ctx = canvas.getContext("2d"); ctx.scale(dpr,dpr);
  ctx.clearRect(0,0,w,h);
  const padL=46,padR=10,padT=10,padB=22;
  const plotW=w-padL-padR, plotH=h-padT-padB;

  let n=0,min=Infinity,max=-Infinity;
  series.forEach(s=>{ n=Math.max(n,s.data.length);
    s.data.forEach(v=>{ if(v<min)min=v; if(v>max)max=v; }); });
  if(!isFinite(min)){ ctx.fillStyle="#6b7d90"; ctx.font="12px monospace";
    ctx.fillText("Warte auf Daten…",padL,padT+plotH/2); return; }
  if(min===max){ max=min+1; min-=1; }
  const pad=(max-min)*0.08; min-=pad; max+=pad;

  // Raster + Y-Achsen-Beschriftung
  ctx.strokeStyle="#1f2b38"; ctx.fillStyle="#6b7d90";
  ctx.font="11px ui-monospace,monospace"; ctx.lineWidth=1;
  for(let g=0;g<=4;g++){
    const y=padT+plotH*g/4, val=max-(max-min)*g/4;
    ctx.beginPath(); ctx.moveTo(padL,y); ctx.lineTo(w-padR,y); ctx.stroke();
    ctx.fillText(fmt(val,0),4,y+3);
  }
  const X=i=> padL + (n<=1?0:plotW*i/(n-1));
  const Y=v=> padT + plotH*(1-(v-min)/(max-min));

  series.forEach(s=>{
    if(!s.data.length) return;
    ctx.beginPath(); ctx.strokeStyle=s.color; ctx.lineWidth=1.8;
    s.data.forEach((v,i)=>{ const x=X(i),y=Y(v); i?ctx.lineTo(x,y):ctx.moveTo(x,y); });
    ctx.stroke();
    // letzter Punkt markiert
    const li=s.data.length-1;
    ctx.fillStyle=s.color; ctx.beginPath();
    ctx.arc(X(li),Y(s.data[li]),2.6,0,7); ctx.fill();
  });
}

let lastData=null;
function render(d){
  lastData=d;
  // Status
  const pill=$("status"), txt=$("statusText");
  pill.className="status-pill"+(d.finished?" done":(d.running?" live":""));
  txt.textContent = d.finished ? "abgeschlossen" : (d.running ? "läuft" : "wartet");

  $("genNow").textContent=fmt(d.generation,0);
  $("genTotal").textContent=fmt(d.totalGenerations,0);
  const pct = d.totalGenerations? Math.min(100,100*d.generation/d.totalGenerations):0;
  $("progressBar").style.width=pct+"%";
  $("elapsed").textContent=fmtTime(d.elapsedSeconds);
  $("curr").textContent=fmt(d.curriculum*100,0)+"%";

  $("best").textContent=fmt(d.bestFitness,0);
  $("avg").textContent=fmt(d.avgFitness,0);
  $("avgFood").textContent=fmt(d.avgFood,1);
  $("bestFood").textContent=fmt(d.bestFood,0);
  $("gBest").textContent=fmt(d.globalBestFitness,0);
  $("mut").textContent=fmt(d.mutation,3);
  $("div").textContent=fmt(d.diversity,3);
  $("species").textContent=fmt(d.species,0);

  const H=d.history||{};
  drawChart($("fitChart"),[
    {data:H.best||[],color:getCss('--amber')},
    {data:H.avg||[], color:getCss('--teal')},
  ]);
  drawChart($("foodChart"),[
    {data:H.bestFood||[],color:getCss('--amber')},
    {data:H.avgFood||[], color:getCss('--teal')},
  ]);
  drawChart($("spfChart"),[
    {data:H.bestSpf||[],color:getCss('--amber')},
    {data:H.avgSpf||[], color:getCss('--teal')},
  ]);

  loadReplay(d.generation);
}

// ---- Live-Replay des besten Agenten ----
let 





=null, rFrame=0, rEvtPtr=0, rEaten=0, rFoodSet=null, lastReplayGen=-1;

async function loadReplay(gen){
  if(!gen || gen===lastReplayGen) return;        // nur bei neuer Generation neu laden
  try{
    const r=await fetch("/api/replay",{cache:"no-store"});
    const rep=await r.json();
    if(!rep.frames || !rep.frames.length) return;
    replay=rep; lastReplayGen=gen; resetReplay(); drawArena();
  }catch(e){ /* Replay noch nicht verfuegbar */ }
}
function resetReplay(){
  rFrame=0; rEvtPtr=0; rEaten=0;
  rFoodSet=new Set(replay.food0);
}
function stepReplay(){
  if(!replay) return;
  rFrame++;
  if(rFrame>=replay.frames.length){ resetReplay(); drawArena(); return; } // Schleife
  while(rEvtPtr<replay.events.length && replay.events[rEvtPtr][0]===rFrame){
    const e=replay.events[rEvtPtr++];
    if(e[1]===1) rFoodSet.add(e[2]);
    else { rFoodSet.delete(e[2]); rEaten++; }    // Entfernen = gefressen
  }
  drawArena();
}
function drawArena(){
  const cv=$("arenaCanvas"); if(!cv) return;
  const dpr=window.devicePixelRatio||1;
  const size=cv.clientWidth || 460;
  cv.width=size*dpr; cv.height=size*dpr;
  const ctx=cv.getContext("2d"); ctx.scale(dpr,dpr);
  ctx.clearRect(0,0,size,size);
  if(!replay){ ctx.fillStyle="#6b7d90"; ctx.font="12px monospace";
    ctx.fillText("warte auf ersten Agenten…",16,size/2); return; }

  const W=replay.w, cs=size/W;
  ctx.fillStyle="#2a3744";
  for(const idx of replay.walls){ const x=idx%W, y=(idx/W)|0; ctx.fillRect(x*cs, y*cs, cs+0.6, cs+0.6); }
  ctx.fillStyle=getCss("--amber");
  for(const idx of rFoodSet){ const x=idx%W, y=(idx/W)|0;
    ctx.beginPath(); ctx.arc((x+0.5)*cs,(y+0.5)*cs,cs*0.3,0,7); ctx.fill(); }
  const f=replay.frames[rFrame];
  if(f){ ctx.fillStyle=getCss("--teal"); const p=cs*0.14;
    roundRect(ctx, f[0]*cs+p, f[1]*cs+p, cs-2*p, cs-2*p, Math.max(2,cs*0.2)); ctx.fill(); }

  $("rGen").textContent="Gen "+replay.generation;
  $("rStep").textContent=rFrame;
  $("rLen").textContent=replay.frames.length-1;
  $("rFood").textContent=rEaten;
}

async function poll(){
  try{
    const r=await fetch("/api/status",{cache:"no-store"});
    const d=await r.json();
    $("conn").textContent="";
    render(d);
  }catch(e){
    $("conn").innerHTML='<span class="err">keine Verbindung zum Trainings-Backend</span>';
  }
}
window.addEventListener("resize",()=>{ if(lastData) render(lastData); drawArena(); });
poll(); setInterval(poll,1000);
setInterval(stepReplay,45);   // ~22 fps Animation
</script>
</body>
</html>
)EVORLHTML";
