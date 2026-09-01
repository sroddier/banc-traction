/* Application atelier — traction / fatigue / calib  v1.1
 * file:// = simulation ; AP ESP32 = mesure réelle.
 * Pas de relais : F ≥ 4000 N → alarme « ARRÊTER LA MACHINE ».
 */
(function () {
  "use strict";
  var $ = function (id) { return document.getElementById(id); };
  var fmt = function (n, d) {
    if (n == null || isNaN(n)) return "—";
    return n.toFixed(d).replace(".", ",");
  };

  var chart = new TvmChart($("chart"));
  var fat = new TVM.Fatigue();
  var ws = null;
  var wantLive = true;
  var sim = true;
  var simTimer = null;
  var simT0 = 0;
  var simT = 0;
  var mode = "traction";
  var running = false;
  var broken = false;
  var forceN = 0;
  var dispN = 0;
  var fmaxN = 0;
  var unit = "N";
  var limitN = TVM.LIMIT_DEFAULT;
  var sps = 10;
  var samples = [];
  var cyclesCsv = [];
  var lastFmin = 0;
  var lastFmax = 0;
  var cyc = 0;
  var recT0 = 0;
  var statusCal = false;
  var tared = false;
  var scaleVal = 877;
  var offsetRaw = 0;
  var simBroken = false;
  var overLive = false;
  var overSeen = false;
  var hxOk = true;
  var hasRelay = false;
  var lastRaw = 0;
  var recentN = [];
  var audioCtx = null;
  var lastBeep = 0;
  var csvFmt = "fr";
  var EMA = 0.35;
  var SAMPLE_CAP = 25000;

  function setPill(el, text, cls) {
    el.textContent = text;
    el.className = "pill" + (cls ? " " + cls : "");
  }

  function unlockAudio() {
    if (audioCtx) return;
    try {
      audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    } catch (e) {}
  }

  function beep() {
    if (!audioCtx) return;
    try {
      if (audioCtx.state === "suspended") audioCtx.resume();
      var o = audioCtx.createOscillator();
      var g = audioCtx.createGain();
      o.type = "square";
      o.frequency.value = 880;
      g.gain.value = 0.12;
      o.connect(g);
      g.connect(audioCtx.destination);
      o.start();
      o.stop(audioCtx.currentTime + 0.18);
    } catch (e2) {}
  }

  function showAlarm() {
    var el = $("alarm");
    var live = overLive || !hxOk;
    el.classList.toggle("hidden", !live);
    document.body.classList.toggle("alarm-on", live);
    $("warn-sticky").classList.toggle("hidden", !overSeen || overLive);
    if (!hxOk) {
      el.classList.add("hx");
      $("alarm-title").textContent = "MESURE PERDUE";
      $("alarm-sub").textContent = "ARRÊTER LE TVM au pupitre — plus de signal HX711";
    } else if (overLive) {
      el.classList.remove("hx");
      $("alarm-title").textContent = "ARRÊTER LA MACHINE";
      $("alarm-sub").textContent = "Force ≥ " + Math.round(limitN) + " N — STOP sur le pupitre TVM (pas de relais)";
      var now = performance.now();
      if (now > lastBeep + 700) {
        lastBeep = now;
        beep();
        try { if (navigator.vibrate) navigator.vibrate([180, 80, 180]); } catch (e) {}
      }
    }
  }

  function rollingSd() {
    var n = recentN.length;
    if (n < 8) return NaN;
    var s = 0, s2 = 0, i;
    for (i = 0; i < n; i++) { s += recentN[i]; s2 += recentN[i] * recentN[i]; }
    var mean = s / n;
    var v = s2 / n - mean * mean;
    return v > 0 ? Math.sqrt(v) : 0;
  }

  function showForce() {
    var el = $("force-n");
    el.classList.remove("warn", "break");
    if (broken) el.classList.add("break");
    else if (Math.abs(forceN) >= limitN * 0.9) el.classList.add("warn");
    if (unit === "kg") {
      el.textContent = fmt(TVM.nToKg(dispN), 3);
      $("force-unit").textContent = "kg";
    } else {
      el.textContent = fmt(dispN, 1);
      $("force-unit").textContent = "N";
    }
    $("fmax").textContent = fmt(fmaxN, 1) + " N";
    $("fkg").textContent = fmt(TVM.nToKg(forceN), 3) + " kg";
    $("fcyc").textContent = String(cyc);
    $("fmin").textContent = lastFmin ? fmt(lastFmin, 1) + " N" : "—";
    $("last-fmin").textContent = lastFmin ? fmt(lastFmin, 1) + " N" : "—";
    $("last-fmax").textContent = lastFmax ? fmt(lastFmax, 1) + " N" : "—";
    $("pill-lim").textContent = "Limite " + Math.round(limitN) + " N";
    $("pill-sps").textContent = "SPS " + sps;
    $("raw").textContent = String(lastRaw);
    var dlt = lastRaw - offsetRaw;
    $("delta").textContent = (dlt > 0 ? "+" : "") + String(dlt);
    var ph = $("polarity-hint");
    if (ph) ph.classList.toggle("hidden", !(tared && forceN < -1));
    var sd = rollingSd();
    $("sigma").textContent = isNaN(sd) ? "—" : fmt(sd, 2) + " N";
    $("rec-info").textContent = running
      ? (samples.length + " pts · " + ((performance.now() - recT0) / 1000).toFixed(1) + " s")
      : "arrêté";
    chart.setLimit(limitN);
    showAlarm();
  }

  function setModeUi(m) {
    mode = m;
    document.querySelectorAll(".tabs button").forEach(function (b) {
      b.setAttribute("aria-selected", b.getAttribute("data-mode") === m ? "true" : "false");
    });
    $("view-traction").classList.toggle("on", m === "traction");
    $("view-fatigue").classList.toggle("on", m === "fatigue");
    $("view-calib").classList.toggle("on", m === "calib");
    $("box-cyc").style.opacity = m === "fatigue" ? "1" : "0.45";
    $("box-fmin").style.opacity = m === "fatigue" ? "1" : "0.45";
  }

  function send(s) {
    if (ws && ws.readyState === 1) ws.send(s);
  }

  function capSamples() {
    if (samples.length >= SAMPLE_CAP) samples.splice(0, 5000);
    if (cyclesCsv.length > 20000) cyclesCsv.splice(0, 4000);
  }

  function onSample(o) {
    forceN = +o.N || 0;
    dispN = dispN + EMA * (forceN - dispN);
    if (o.raw != null) lastRaw = +o.raw;
    recentN.push(forceN);
    if (recentN.length > 25) recentN.shift();
    if (o.fmax != null && running) fmaxN = Math.max(fmaxN, +o.fmax);
    if (running && forceN > fmaxN) fmaxN = forceN;
    if (o.cyc != null) cyc = +o.cyc;
    if (o.fmin != null) lastFmin = +o.fmin;
    if (o.sps) sps = +o.sps;
    if (o.hx != null) hxOk = !!o.hx;
    if (Math.abs(forceN) >= limitN) {
      overLive = true;
      overSeen = true;
    } else if (o.over != null) {
      overLive = !!o.over;
    }
    if (overLive) overSeen = true;
    var evt = o.evt || "";
    if (evt === "break") broken = true;
    if (evt === "stop") running = false;
    if (evt === "limit") {
      overLive = true;
      overSeen = true;
    }
    if (evt === "hxfail") hxOk = false;
    if (evt === "cycle") {
      lastFmin = +o.fmin;
      lastFmax = +o.fmax;
      cyclesCsv.push({
        cycle: cyc,
        t: o.t,
        fmin: lastFmin,
        fmax: lastFmax
      });
    }
    if (evt === "peak" && o.fmax != null) lastFmax = +o.fmax;
    chart.push(o.t, forceN, evt);
    if (running) {
      samples.push({
        t: o.t,
        N: forceN,
        kg: o.kg != null ? +o.kg : TVM.nToKg(forceN),
        evt: evt
      });
      capSamples();
    }
    if (chart.pts.length % 2 === 0) chart.draw();
    showForce();
  }

  function onStatus(o) {
    if (o.limit_N != null && o.limit_N !== "") limitN = +o.limit_N;
    if (o.sps) sps = +o.sps;
    if (o.scale != null && o.scale !== "") {
      scaleVal = +o.scale;
      $("scale").value = String(scaleVal.toFixed(3));
    }
    if (o.offset != null) offsetRaw = +o.offset;
    if (o.msg && o.msg.indexOf("Étalonnage OK") === 0) {
      pushCalLog(parseFloat($("ref-kg").value) || 0, scaleVal);
    }
    if (o.unit) unit = o.unit;
    if (o.cal != null) statusCal = !!o.cal;
    if (o.tared != null) tared = !!o.tared;
    if (o.run != null) running = !!o.run;
    if (o.relay != null) {
      hasRelay = !!o.relay;
      $("note-relay").classList.toggle("hidden", hasRelay);
    }
    if (o.over != null) overLive = !!o.over;
    if (o.seen) overSeen = true;
    if (o.hx_ok != null) hxOk = !!o.hx_ok;
    if (o.raw != null) lastRaw = +o.raw;
    if (o.ver) $("pill-ver").textContent = "v" + o.ver;
    if (o.mode && o.mode !== mode) setModeUi(o.mode);
    $("limit").value = String(Math.round(limitN));
    $("unit").value = unit;
    if (o.msg) {
      var extra = "";
      if (!statusCal) extra += " · non étalonné";
      if (!tared) extra += " · tare à vide obligatoire";
      $("status-msg").textContent = o.msg + extra;
    }
    showForce();
  }

  function handleMsg(o) {
    if (!o || !o.type) return;
    if (o.type === "sample") onSample(o);
    else if (o.type === "status") onStatus(o);
  }

  /* ---------- WebSocket (même port 80, chemin /ws) ---------- */
  function connect(i) {
    if (!wantLive) return;
    var urls = TVM.wsUrls(location);
    if (i >= urls.length) {
      startSim("Pas d'ESP32 — simulation. Joindre le Wi-Fi TVM-TRACTION ou ouvrir http://192.168.4.1");
      return;
    }
    try { if (ws) ws.close(); } catch (e) {}
    var url = urls[i];
    var sock;
    try { sock = new WebSocket(url); } catch (e) {
      connect(i + 1);
      return;
    }
    var to = setTimeout(function () {
      try { sock.close(); } catch (e2) {}
      connect(i + 1);
    }, 1800);
    sock.onopen = function () {
      clearTimeout(to);
      ws = sock;
      stopSim();
      sim = false;
      hxOk = true;
      setPill($("pill-link"), "ESP32 " + sock.url.replace(/^ws:\/\//, "").replace(/\/$/, ""), "on");
      $("status-msg").textContent = "Connecté au banc. Tare à vide obligatoire. Pas de relais : STOP au pupitre si F ≥ 4000 N.";
      send(TVM.cmd.getStatus());
    };
    sock.onmessage = function (ev) { handleMsg(TVM.parse(ev.data)); };
    sock.onerror = function () {};
    sock.onclose = function () {
      clearTimeout(to);
      if (ws === sock) ws = null;
      if (wantLive && !sim) setTimeout(function () { connect(0); }, 1500);
    };
  }

  /* ---------- Simulation (même JSON que le firmware) ---------- */
  function stopSim() {
    if (simTimer) { clearInterval(simTimer); simTimer = null; }
  }

  function startSim(msg) {
    stopSim();
    sim = true;
    hxOk = true;
    setPill($("pill-link"), "Simulation", "sim");
    if (msg) $("status-msg").textContent = msg;
    simT0 = performance.now();
    simT = 0;
    simTimer = setInterval(function () {
      simT = performance.now() - simT0;
      var t = simT;
      var N = 0;
      var evt = "";
      var dt = t / 1000;
      if (mode === "fatigue") {
        if (!running) {
          N = 20 + (Math.random() - 0.5) * 4;
        } else {
          var fminS = 80, fmaxS = 900;
          var mean = (fminS + fmaxS) / 2, amp = (fmaxS - fminS) / 2;
          N = mean + amp * Math.sin(2 * Math.PI * 0.45 * dt);
          N += (Math.random() - 0.5) * 8;
          evt = fat.step(N);
          cyc = fat.cyc;
          lastFmin = fat.fmin;
          lastFmax = fat.fmax;
        }
      } else if (mode === "calib") {
        N = (Math.random() - 0.5) * 1.2;
      } else {
        if (!running || simBroken) {
          N = broken ? fmaxN * 0.05 + Math.random() : (Math.random() - 0.5) * 1.5;
        } else {
          var k = 220;
          N = k * dt + 15 * Math.sin(dt * 3) + (Math.random() - 0.5) * 6;
          if (N > 1650 && !simBroken) {
            N = 1650 - (dt - 1650 / k) * 80 + (Math.random() - 0.5) * 10;
            if (N < 1650 * 0.78) {
              simBroken = true;
              broken = true;
              evt = "break";
              running = false;
              N = 40 + Math.random() * 10;
            }
          }
          if (Math.abs(N) >= limitN) {
            evt = "limit";
            overLive = true;
            overSeen = true;
            N = N > 0 ? limitN : -limitN;
          }
        }
      }
      if (N > fmaxN && running) fmaxN = N;
      var obj = {
        type: "sample",
        t: Math.round(t),
        N: N,
        kg: TVM.nToKg(N),
        raw: 0,
        evt: evt,
        cyc: cyc,
        fmin: lastFmin,
        fmax: mode === "fatigue" ? lastFmax : fmaxN,
        over: Math.abs(N) >= limitN,
        hx: true
      };
      handleMsg(obj);
    }, 1000 / 20);
  }

  /* ---------- CSV ---------- */
  function download(name, text) {
    var blob = new Blob(["\uFEFF" + text], { type: "text/csv;charset=utf-8" });
    var a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = name;
    a.click();
    setTimeout(function () { URL.revokeObjectURL(a.href); }, 1500);
  }
  function stamp() {
    var d = new Date();
    var z = function (n) { return (n < 10 ? "0" : "") + n; };
    return d.getFullYear() + z(d.getMonth() + 1) + z(d.getDate()) + "-" + z(d.getHours()) + z(d.getMinutes());
  }
  function csvNum(n, dec) {
    return csvFmt === "fr" ? TVM.csvEscapeFr(n, dec) : TVM.csvEscape(n, dec);
  }
  function csvSep() { return csvFmt === "fr" ? ";" : ","; }
  function csvTraction() {
    var name = ($("name-epro").value || "eprouvette").replace(/[^\w\-]+/g, "_");
    var t0 = samples.length ? samples[0].t : 0;
    var sep = csvSep();
    var lines = ["time_ms" + sep + "force_N" + sep + "force_kg" + sep + "event"];
    for (var i = 0; i < samples.length; i++) {
      var s = samples[i];
      lines.push((s.t - t0) + sep + csvNum(s.N, 2) + sep + csvNum(s.kg, 4) + sep + (s.evt || ""));
    }
    download("traction-" + name + "-" + stamp() + ".csv", lines.join("\r\n"));
  }
  function csvFatigue() {
    var sep = csvSep();
    var lines = ["cycle" + sep + "t_ms" + sep + "Fmin_N" + sep + "Fmax_N"];
    for (var i = 0; i < cyclesCsv.length; i++) {
      var c = cyclesCsv[i];
      lines.push(c.cycle + sep + c.t + sep + csvNum(c.fmin, 2) + sep + csvNum(c.fmax, 2));
    }
    download("fatigue-cycles-" + stamp() + ".csv", lines.join("\r\n"));
  }

  function loadCalLog() {
    try {
      var arr = JSON.parse(localStorage.getItem("tvm-cal-log") || "[]");
      if (!arr.length) {
        $("cal-log").textContent = "Aucun étalonnage enregistré sur cet appareil.";
        return;
      }
      var last = arr[arr.length - 1];
      $("cal-log").textContent = "Dernier étalonnage : " + last.ts + " · " + last.kg + " kg · échelle " + last.scale + " counts/N (" + arr.length + " en mémoire).";
    } catch (e) {
      $("cal-log").textContent = "Journal d'étalonnage indisponible.";
    }
  }
  function pushCalLog(kg, scale) {
    try {
      var arr = JSON.parse(localStorage.getItem("tvm-cal-log") || "[]");
      arr.push({ ts: new Date().toISOString(), kg: kg, scale: scale });
      if (arr.length > 30) arr = arr.slice(-30);
      localStorage.setItem("tvm-cal-log", JSON.stringify(arr));
    } catch (e) {}
    loadCalLog();
  }

  /* ---------- Actions ---------- */
  function doTare() {
    unlockAudio();
    if (sim) {
      forceN = 0;
      dispN = 0;
      tared = true;
      $("status-msg").textContent = "Tare simulation (zéro).";
      handleMsg({ type: "sample", t: Math.round(simT), N: 0, kg: 0, raw: 0, evt: "tare", cyc: cyc, fmin: lastFmin, fmax: fmaxN });
    } else send(TVM.cmd.tare());
  }
  function doStart() {
    unlockAudio();
    running = true;
    broken = false;
    simBroken = false;
    overSeen = false;
    samples = [];
    recT0 = performance.now();
    chart.clear();
    if (sim) { simT0 = performance.now(); simT = 0; }
    if (mode !== "fatigue") { fmaxN = Math.max(0, forceN); }
    if (!sim) send(TVM.cmd.start());
    $("status-msg").textContent = mode === "fatigue"
      ? "Comptage cycles — lancer le Repeat du TVM. Si F ≥ " + Math.round(limitN) + " N : STOP pupitre."
      : "Mesure traction en cours. Si F ≥ " + Math.round(limitN) + " N : ARRÊTER le TVM au pupitre.";
  }
  function doStop() {
    unlockAudio();
    running = false;
    if (sim) {
      handleMsg({ type: "sample", t: Math.round(simT), N: forceN, kg: TVM.nToKg(forceN), raw: 0, evt: "stop", cyc: cyc, fmin: lastFmin, fmax: fmaxN });
      $("status-msg").textContent = "Enregistrement arrêté (simulation). Le TVM se stoppe au pupitre.";
    } else send(TVM.cmd.stop());
  }
  function doReset() {
    if (!confirm("Remettre à zéro la courbe, Fmax et les cycles ?")) return;
    running = false;
    broken = false;
    fmaxN = 0;
    cyc = 0;
    lastFmin = 0;
    lastFmax = 0;
    samples = [];
    cyclesCsv = [];
    overSeen = false;
    overLive = false;
    fat.reset();
    chart.clear();
    if (sim) {
      simT0 = performance.now();
      simT = 0;
      $("status-msg").textContent = "RAZ simulation.";
    } else send(TVM.cmd.reset());
    showForce();
  }
  function doRupture() {
    broken = true;
    running = false;
    var obj = {
      type: "sample", t: Math.round(sim ? simT : performance.now()),
      N: forceN, kg: TVM.nToKg(forceN), raw: lastRaw, evt: "break",
      cyc: cyc, fmin: lastFmin, fmax: fmaxN
    };
    if (sim) handleMsg(obj);
    else {
      send(TVM.cmd.markBreak());
      samples.push({ t: obj.t, N: forceN, kg: obj.kg, evt: "break" });
      chart.push(obj.t, forceN, "break");
      chart.draw();
    }
    $("status-msg").textContent = "Rupture marquée (opérateur).";
    showForce();
  }
  function applyMode(m) {
    setModeUi(m);
    running = false;
    broken = false;
    fmaxN = 0;
    cyc = 0;
    lastFmin = 0;
    lastFmax = 0;
    samples = [];
    cyclesCsv = [];
    overSeen = false;
    fat.reset();
    chart.clear();
    if (!sim) send(TVM.cmd.setMode(m));
    showForce();
  }

  $("tab-traction").onclick = function () { applyMode("traction"); };
  $("tab-fatigue").onclick = function () { applyMode("fatigue"); };
  $("tab-calib").onclick = function () { applyMode("calib"); };

  $("btn-tare").onclick = doTare;
  $("btn-tare2").onclick = doTare;
  $("btn-tare3").onclick = doTare;
  $("btn-start").onclick = doStart;
  $("btn-start2").onclick = doStart;
  $("btn-stop").onclick = doStop;
  $("btn-stop2").onclick = doStop;
  $("btn-reset").onclick = doReset;
  $("btn-reset2").onclick = doReset;
  $("btn-rupture").onclick = doRupture;
  $("btn-csv").onclick = csvTraction;
  $("btn-csv2").onclick = csvFatigue;
  $("btn-png").onclick = function () {
    var name = ($("name-epro").value || "eprouvette").replace(/[^\w\-]+/g, "_");
    chart.exportPng("traction-" + name + "-" + stamp() + ".png");
  };
  $("btn-sim-over").onclick = function () {
    unlockAudio();
    if (!sim) {
      $("status-msg").textContent = "« Simuler F ≥ 4000 N » n'est disponible qu'en simulation.";
      return;
    }
    running = true;
    forceN = limitN + 80;
    dispN = forceN;
    fmaxN = Math.max(fmaxN, forceN);
    handleMsg({
      type: "sample",
      t: Math.round(simT),
      N: forceN,
      kg: TVM.nToKg(forceN),
      raw: 0,
      evt: "limit",
      cyc: cyc,
      fmin: lastFmin,
      fmax: fmaxN,
      over: true,
      hx: true
    });
    $("status-msg").textContent = "Simulation de surcharge — en réel : STOP au pupitre TVM.";
  };

  $("btn-cal").onclick = function () {
    var kg = parseFloat($("ref-kg").value);
    if (!(kg > 0)) {
      $("status-msg").textContent = "Indiquer la masse posée, en kg (ex. 1 ou 2).";
      return;
    }
    if (sim) {
      var signed = forceN < 0 ? -Math.abs(scaleVal) : Math.abs(scaleVal) || 877;
      scaleVal = signed;
      statusCal = true;
      tared = true;
      $("scale").value = String(scaleVal.toFixed(3));
      forceN = TVM.kgToN(kg);
      dispN = forceN;
      pushCalLog(kg, scaleVal);
      $("status-msg").textContent = "Étalonnage simulé (" + kg + " kg, échelle " + scaleVal + "). Sur le banc : tare à vide, poser la masse, puis Étalonner.";
      showForce();
    } else {
      send(TVM.cmd.calibrate(kg, TVM.kgToN(kg)));
      $("status-msg").textContent = "Étalonnage en cours (laisser la masse posée)…";
    }
  };
  $("btn-invert").onclick = function () {
    unlockAudio();
    if (sim) {
      scaleVal = -scaleVal;
      forceN = -forceN;
      dispN = forceN;
      $("scale").value = String(scaleVal.toFixed(3));
      $("status-msg").textContent = "Sens inversé (simulation). Échelle " + scaleVal.toFixed(3) + " counts/N.";
      showForce();
    } else send(TVM.cmd.invert());
  };
  $("btn-scale").onclick = function () {
    var sc = parseFloat($("scale").value);
    if (!(Math.abs(sc) >= 0.5) || Math.abs(sc) > 2000000) {
      $("status-msg").textContent = "Échelle hors plage (0,5 à 2e6 counts/N, signe autorisé).";
      return;
    }
    scaleVal = sc;
    if (!sim) send(TVM.cmd.setScale(sc));
    $("status-msg").textContent = "Échelle " + sc + " counts/N" + (sc < 0 ? " (jauge inverse)" : "");
  };
  $("btn-limit").onclick = function () {
    var n = parseFloat($("limit").value);
    if (!(n >= 50)) return;
    if (n > TVM.HARD_CAP) n = TVM.HARD_CAP;
    limitN = n;
    $("limit").value = String(n);
    chart.setLimit(limitN);
    chart.draw();
    if (!sim) send(TVM.cmd.setLimit(n));
    showForce();
    $("status-msg").textContent = "Limite logicielle " + n + " N — au-delà : ARRÊTER le TVM au pupitre (pas de relais).";
  };
  $("btn-sps").onclick = function () {
    sps = parseInt($("sps").value, 10) || 10;
    if (!sim) send(TVM.cmd.setSps(sps));
    showForce();
  };
  $("unit").onchange = function () {
    unit = $("unit").value;
    if (!sim) send(TVM.cmd.setUnit(unit));
    showForce();
  };
  $("csv-fmt").onchange = function () {
    csvFmt = $("csv-fmt").value;
    try { localStorage.setItem("tvm-csv-fmt", csvFmt); } catch (e) {}
  };

  $("btn-reconnect").onclick = function () {
    unlockAudio();
    wantLive = true;
    stopSim();
    sim = false;
    setPill($("pill-link"), "Connexion…", "");
    $("status-msg").textContent = "Connexion WebSocket /ws …";
    connect(0);
  };
  $("btn-sim").onclick = function () {
    unlockAudio();
    wantLive = false;
    try { if (ws) ws.close(); } catch (e) {}
    ws = null;
    startSim("Mode simulation forcé. Ouvrir ce fichier suffit, l'ESP32 n'est pas requis.");
  };

  document.body.addEventListener("click", unlockAudio, { once: true });

  window.addEventListener("resize", function () { chart.resize(); });
  chart.resize();
  chart.setLimit(limitN);
  setModeUi("traction");
  showForce();
  loadCalLog();

  try {
    var saved = localStorage.getItem("tvm-epro");
    if (saved) $("name-epro").value = saved;
    var cf = localStorage.getItem("tvm-csv-fmt");
    if (cf) { csvFmt = cf; $("csv-fmt").value = cf; }
  } catch (e) {}
  $("name-epro").addEventListener("change", function () {
    try { localStorage.setItem("tvm-epro", $("name-epro").value); } catch (e2) {}
  });

  if (location.protocol !== "file:" && "serviceWorker" in navigator) {
    var host = location.hostname;
    if (host === "localhost" || host === "127.0.0.1") {
      navigator.serviceWorker.register("sw.js").catch(function () {});
    }
  }

  if (location.protocol === "file:") {
    startSim("Fichier local — simulation. Pour le banc : joindre TVM-TRACTION puis http://192.168.4.1");
  } else {
    setPill($("pill-link"), "Connexion…", "");
    connect(0);
  }
})();
