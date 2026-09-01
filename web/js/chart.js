/* Courbe atelier : force(t) sans bibliothèque externe. */
(function (g) {
  "use strict";
  function Chart(canvas) {
    this.c = canvas;
    this.ctx = canvas.getContext("2d");
    this.pts = [];
    this.maxPts = 4000;
    this.pad = { l: 56, r: 16, t: 18, b: 32 };
    this.w = 0;
    this.h = 0;
    this.limitN = 4000;
  }
  Chart.prototype.clear = function () { this.pts = []; this.draw(); };
  Chart.prototype.setLimit = function (n) { this.limitN = n; };
  Chart.prototype.push = function (t, N, evt) {
    this.pts.push({ t: t, N: N, evt: evt || "" });
    if (this.pts.length > this.maxPts) this.pts.splice(0, this.pts.length - this.maxPts);
  };
  Chart.prototype.resize = function () {
    var r = this.c.getBoundingClientRect();
    var dpr = window.devicePixelRatio || 1;
    var w = Math.max(320, r.width);
    var h = Math.max(180, r.height);
    this.c.width = w * dpr;
    this.c.height = h * dpr;
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    this.w = w;
    this.h = h;
    this.draw();
  };
  Chart.prototype.exportPng = function (name) {
    var a = document.createElement("a");
    a.href = this.c.toDataURL("image/png");
    a.download = name || "courbe-traction.png";
    a.click();
  };
  Chart.prototype.draw = function () {
    var ctx = this.ctx, w = this.w || this.c.clientWidth, h = this.h || this.c.clientHeight;
    if (!w || !h) return;
    var p = this.pad;
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = "#f3e8e0";
    ctx.fillRect(0, 0, w, h);

    var pts = this.pts;
    var tMin = pts.length ? pts[0].t : 0;
    var tMax = pts.length ? pts[pts.length - 1].t : 1000;
    if (tMax <= tMin) tMax = tMin + 1000;
    var dataMin = 0, dataMax = 0, i;
    for (i = 0; i < pts.length; i++) {
      if (pts[i].N > dataMax) dataMax = pts[i].N;
      if (pts[i].N < dataMin) dataMin = pts[i].N;
    }
    function niceCeil(v) {
      var a = Math.abs(v);
      if (a < 10) return (v < 0 ? -1 : 1) * Math.max(10, Math.ceil(a / 2) * 2);
      var step = a <= 25 ? 5 : a <= 50 ? 10 : a <= 100 ? 10 : a <= 250 ? 25
               : a <= 500 ? 50 : a <= 1000 ? 100 : a <= 2500 ? 250 : 500;
      var n = Math.ceil(a / step) * step;
      return v < 0 ? -n : n;
    }
    var nMax = niceCeil(Math.max(dataMax * 1.18, 20));
    var nMin = dataMin >= -2 ? 0 : niceCeil(dataMin * 1.18);
    if (nMax <= nMin) nMax = nMin + 20;
    if (this.limitN && dataMax >= this.limitN * 0.55 && this.limitN > nMax) {
      nMax = niceCeil(this.limitN);
    }

    function x(t) { return p.l + (t - tMin) / (tMax - tMin) * (w - p.l - p.r); }
    function y(n) { return p.t + (1 - (n - nMin) / (nMax - nMin)) * (h - p.t - p.b); }

    ctx.strokeStyle = "#e0d0c4";
    ctx.lineWidth = 1;
    ctx.fillStyle = "#7a655c";
    ctx.font = "11px ui-sans-serif, system-ui, sans-serif";
    ctx.textAlign = "right";
    ctx.textBaseline = "middle";
    var span = nMax - nMin;
    var gstep = span <= 40 ? 5 : span <= 100 ? 10 : span <= 250 ? 25
              : span <= 500 ? 50 : span <= 1000 ? 100 : span <= 2500 ? 250 : 500;
    var g0 = Math.ceil(nMin / gstep) * gstep;
    for (var gv = g0; gv <= nMax + 0.01; gv += gstep) {
      var yy = y(gv);
      ctx.beginPath();
      ctx.moveTo(p.l, yy);
      ctx.lineTo(w - p.r, yy);
      ctx.stroke();
      ctx.fillText(gv.toFixed(0) + " N", p.l - 6, yy);
    }

    if (this.limitN && this.limitN > nMin && this.limitN <= nMax) {
      var yl = y(this.limitN);
      ctx.strokeStyle = "#c41616";
      ctx.lineWidth = 1.5;
      ctx.setLineDash([6, 5]);
      ctx.beginPath();
      ctx.moveTo(p.l, yl);
      ctx.lineTo(w - p.r, yl);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = "#c41616";
      ctx.textAlign = "left";
      ctx.fillText("limite " + Math.round(this.limitN) + " N", p.l + 6, yl - 8);
    } else if (this.limitN) {
      ctx.fillStyle = "#c45a4a";
      ctx.textAlign = "right";
      ctx.font = "10px ui-sans-serif, system-ui, sans-serif";
      ctx.fillText("limite " + Math.round(this.limitN) + " N hors échelle", w - p.r, p.t + 4);
    }

    ctx.textAlign = "center";
    ctx.textBaseline = "top";
    ctx.fillStyle = "#7a655c";
    ctx.fillText(((tMax - tMin) / 1000).toFixed(1) + " s", w - p.r - 24, h - p.b + 10);
    ctx.textAlign = "left";
    ctx.fillText("0 s", p.l, h - p.b + 10);

    if (pts.length >= 2) {
      ctx.beginPath();
      ctx.moveTo(x(pts[0].t), y(pts[0].N));
      for (i = 1; i < pts.length; i++) ctx.lineTo(x(pts[i].t), y(pts[i].N));
      ctx.strokeStyle = "#983830";
      ctx.lineWidth = 2;
      ctx.stroke();
      var last = pts[pts.length - 1];
      ctx.beginPath();
      ctx.moveTo(x(pts[0].t), y(pts[0].N));
      for (i = 1; i < pts.length; i++) ctx.lineTo(x(pts[i].t), y(pts[i].N));
      ctx.lineTo(x(last.t), y(nMin));
      ctx.lineTo(x(pts[0].t), y(nMin));
      ctx.closePath();
      var lg = ctx.createLinearGradient(0, y(Math.max(nMax, 1)), 0, y(nMin));
      lg.addColorStop(0, "rgba(152,56,48,0.22)");
      lg.addColorStop(1, "rgba(152,56,48,0)");
      ctx.fillStyle = lg;
      ctx.fill();
    }

    for (i = 0; i < pts.length; i++) {
      if (!pts[i].evt) continue;
      var col = pts[i].evt === "break" ? "#c41616"
              : pts[i].evt === "limit" ? "#c45a12"
              : pts[i].evt === "hxfail" ? "#c41616"
              : pts[i].evt === "cycle" ? "#2f6b4f"
              : pts[i].evt === "peak" ? "#3d5c8a"
              : "#7a655c";
      ctx.strokeStyle = col;
      ctx.setLineDash([4, 4]);
      ctx.beginPath();
      ctx.moveTo(x(pts[i].t), p.t);
      ctx.lineTo(x(pts[i].t), h - p.b);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = col;
      ctx.textAlign = "center";
      ctx.textBaseline = "top";
      ctx.font = "10px ui-sans-serif, system-ui, sans-serif";
      ctx.fillText(pts[i].evt, x(pts[i].t), p.t + 2);
    }
  };
  g.TvmChart = Chart;
})(typeof window !== "undefined" ? window : this);
