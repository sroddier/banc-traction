/* Application atelier — traction / fatigue / calib
 * Fonctionne en file:// (simulation) et derrière l'AP ESP32.
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
  var fmaxN = 0;
  var unit = "N";
  var limitN = TVM.LIMIT_DEFAULT;
  var sps = 80;
  var samples = [];
  var cyclesCsv = [];
  var lastFmin = 0;
  var lastFmax = 0;
  var cyc = 0;
  var recT0 = 0;
  var statusCal = false;
  var scaleVal = 877;
  var simBroken = false;

  function setPill(el, text, cls) {
    el.textContent = text;
    el.className = "pill" + (cls ? " " + cls : "");
  }

  function showForce() {
    var el = $("force-n");
    el.classList.remove("warn", "break");
    if (broken) el.classList.add("break");
    else if (forceN >= limitN * 0.9) el.classList.add("warn");
    if (unit === "kg") {
      el.textContent = fmt(TVM.nToKg(forceN), 3);
      $("force-unit").textContent = "kg";
    } else {
      el.textContent = fmt(forceN, 1);
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
    $("rec-info").textContent = running
      ? (samples.length + " pts · " + ((performance.now() - recT0) / 1000).toFixed(1) + " s")
      : "arrêté";
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

  function onSample(o) {
    forceN = +o.N || 0;
    if (o.fmax != null) fmaxN = Math.max(fmaxN, +o.fmax);
    if (forceN > fmaxN) fmaxN = forceN;
    if (o.cyc != null) cyc = +o.cyc;
    if (o.fmin != null) lastFmin = +o.fmin;
    if (o.sps) sps = +o.sps;
    var evt = o.evt || "";
    if (evt === "break") broken = true;
    if (evt === "limit" || evt === "stop") running = false;
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
    }
    if (chart.pts.length % 2 === 0) chart.draw();
    showForce();
  }

  function onStatus(o) {
    if (o.limit_N) limitN = +o.limit_N;
    if (o.sps) sps = +o.sps;
    if (o.scale) {
      scaleVal = +o.scale;
      $("scale").value = String(scaleVal.toFixed(2));
    }
    if (o.unit) unit = o.unit;
    if (o.cal != null) statusCal = !!o.cal;
    if (o.mode && o.mode !== mode) setModeUi(o.mode);
    $("limit").value = String(Math.round(limitN));
    $("unit").value = unit;
    if (o.msg) $("status-msg").textContent = o.msg + (statusCal ? "" : " · non étalonné");
    showForce();
  }

  function handleMsg(o) {
    if (!o || !o.type) return;
    if (o.type === "sample") onSample(o);
    else if (o.type === "status") onStatus(o);
  }

  /* ---------- WebSocket ---------- */
  function wsUrls() {
    var urls = [];
    var port = TVM.WS_PORT;
    if (location.protocol !== "file:") {
      urls.push("ws://" + location.hostname + ":" + port + "/");
    }
    urls.push("ws://192.168.4.1:" + port + "/");
    return urls;
  }

  function connect(i) {
    if (!wantLive) return;
    var urls = wsUrls();
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
      setPill($("pill-link"), "ESP32 " + sock.url.replace(/^ws:\/\//, "").replace(/\/$/, ""), "on");
      $("status-msg").textContent = "Connecté au banc. Mesure réelle HX711.";
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
        // Courbe de traction typique : élastique, palier, rupture
        if (!running || simBroken) {
          N = broken ? fmaxN * 0.05 + Math.random() : (Math.random() - 0.5) * 1.5;
        } else {
          var k = 220; // N/s
          N = k * dt + 15 * Math.sin(dt * 3) + (Math.random() - 0.5) * 6;
          if (N > 1650 && !simBroken) {
            // striction puis rupture
            N = 1650 - (dt - 1650 / k) * 80 + (Math.random() - 0.5) * 10;
            if (N < 1650 * 0.78) {
              simBroken = true;
              broken = true;
              evt = "break";
              running = false;
              N = 40 + Math.random() * 10;
            }
          }
          if (N >= limitN) {
            evt = "limit";
            running = false;
            N = limitN;
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
        fmax: mode === "fatigue" ? lastFmax : fmaxN
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
  function csvTraction() {
    var name = ($("name-epro").value || "eprouvette").replace(/[^\w\-]+/g, "_");
    var t0 = samples.length ? samples[0].t : 0;
    var lines = ["time_ms,force_N,force_kg,event"];
    for (var i = 0; i < samples.length; i++) {
      var s = samples[i];
      lines.push((s.t - t0) + "," + s.N.toFixed(2) + "," + s.kg.toFixed(4) + "," + (s.evt || ""));
    }
    download("traction-" + name + "-" + stamp() + ".csv", lines.join("\n"));
  }
  function csvFatigue() {
    var lines = ["cycle,t_ms,Fmin_N,Fmax_N"];
    for (var i = 0; i < cyclesCsv.length; i++) {
      var c = cyclesCsv[i];
      lines.push(c.cycle + "," + c.t + "," + Number(c.fmin).toFixed(2) + "," + Number(c.fmax).toFixed(2));
    }
    download("fatigue-cycles-" + stamp() + ".csv", lines.join("\n"));
  }

  /* ---------- Actions ---------- */
  function doTare() {
    if (sim) {
      forceN = 0;
      $("status-msg").textContent = "Tare simulation (zéro).";
      handleMsg({ type: "sample", t: Math.round(simT), N: 0, kg: 0, raw: 0, evt: "tare", cyc: cyc, fmin: lastFmin, fmax: fmaxN });
    } else send(TVM.cmd.tare());
  }
  function doStart() {
    running = true;
    broken = false;
    simBroken = false;
    samples = [];
    recT0 = performance.now();
    chart.clear();
    if (sim) { simT0 = performance.now(); simT = 0; }
    if (mode !== "fatigue") { fmaxN = Math.max(0, forceN); }
    if (!sim) send(TVM.cmd.start());
    $("status-msg").textContent = mode === "fatigue"
      ? "Comptage cycles — lancer le Repeat du TVM."
      : "Mesure traction en cours.";
  }
  function doStop() {
    running = false;
    if (sim) {
      handleMsg({ type: "sample", t: Math.round(simT), N: forceN, kg: TVM.nToKg(forceN), raw: 0, evt: "stop", cyc: cyc, fmin: lastFmin, fmax: fmaxN });
      $("status-msg").textContent = "STOP simulation (pas de relais).";
    } else send(TVM.cmd.stop());
  }
  function doReset() {
    running = false;
    broken = false;
    fmaxN = 0;
    cyc = 0;
    lastFmin = 0;
    lastFmax = 0;
    samples = [];
    cyclesCsv = [];
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
      N: forceN, kg: TVM.nToKg(forceN), raw: 0, evt: "break",
      cyc: cyc, fmin: lastFmin, fmax: fmaxN
    };
    if (sim) handleMsg(obj);
    else {
      samples.push({ t: obj.t, N: forceN, kg: obj.kg, evt: "break" });
      chart.push(obj.t, forceN, "break");
      chart.draw();
    }
    $("status-msg").textContent = "Rupture marquée (opérateur).";
    showForce();
  }
  function applyMode(m) {
    setModeUi(m);
    doReset();
    if (!sim) send(TVM.cmd.setMode(m));
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

  $("btn-cal").onclick = function () {
    var kg = parseFloat($("ref-kg").value);
    if (!(kg > 0)) return;
    if (sim) {
      scaleVal = 877;
      statusCal = true;
      $("scale").value = "877";
      $("status-msg").textContent = "Étalonnage simulé (masse " + kg + " kg). Sur le banc réel, poser la masse puis cliquer.";
    } else send(TVM.cmd.calibrate(kg, TVM.kgToN(kg)));
  };
  $("btn-scale").onclick = function () {
    var sc = parseFloat($("scale").value);
    if (!(sc > 1)) return;
    scaleVal = sc;
    if (!sim) send(TVM.cmd.setScale(sc));
    $("status-msg").textContent = "Échelle " + sc + " counts/N";
  };
  $("btn-limit").onclick = function () {
    var n = parseFloat($("limit").value);
    if (!(n >= 50)) return;
    if (n > TVM.HARD_CAP) n = TVM.HARD_CAP;
    limitN = n;
    $("limit").value = String(n);
    if (!sim) send(TVM.cmd.setLimit(n));
    showForce();
    $("status-msg").textContent = "Limite logicielle " + n + " N (plafond " + TVM.HARD_CAP + " N).";
  };
  $("btn-sps").onclick = function () {
    var v = $("sps").value;
    if (v === "auto") sps = 80;
    else sps = parseInt(v, 10);
    if (!sim && v !== "auto") send(TVM.cmd.setSps(sps));
    showForce();
  };
  $("unit").onchange = function () {
    unit = $("unit").value;
    if (!sim) send(TVM.cmd.setUnit(unit));
    showForce();
  };

  $("btn-reconnect").onclick = function () {
    wantLive = true;
    stopSim();
    sim = false;
    setPill($("pill-link"), "Connexion…", "");
    $("status-msg").textContent = "Connexion WebSocket (port 81)…";
    connect(0);
  };
  $("btn-sim").onclick = function () {
    wantLive = false;
    try { if (ws) ws.close(); } catch (e) {}
    ws = null;
    startSim("Mode simulation forcé. Ouvrir ce fichier suffit, l'ESP32 n'est pas requis.");
  };

  window.addEventListener("resize", function () { chart.resize(); });
  chart.resize();
  setModeUi("traction");
  showForce();

  try {
    var saved = localStorage.getItem("tvm-epro");
    if (saved) $("name-epro").value = saved;
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

  // file:// ou pas d'ESP32 → simulation immédiate ; sinon on tente le WS
  if (location.protocol === "file:") {
    startSim("Fichier local — simulation. Pour le banc : joindre TVM-TRACTION puis http://192.168.4.1");
  } else {
    setPill($("pill-link"), "Connexion…", "");
    connect(0);
  }
})();
