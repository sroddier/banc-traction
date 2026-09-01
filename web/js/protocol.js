/* Protocole JSON partagé avec firmware/include/protocol.h  (v1.1)
 * Un objet par message WebSocket. Pas de champ déplacement.
 */
(function (g) {
  "use strict";
  var TVM = g.TVM || {};
  TVM.VERSION = "1.1.1";
  TVM.G = 9.80665;
  TVM.WS_PATH = "/ws";
  TVM.LIMIT_DEFAULT = 4000;
  TVM.HARD_CAP = 4899;
  TVM.SSID = "TVM-TRACTION";
  TVM.PASS = "traction1";
  TVM.CELL_KG = 500;
  TVM.CELL_MVV = 2.0030;

  TVM.parse = function (s) {
    try { return JSON.parse(s); } catch (e) { return null; }
  };

  TVM.nToKg = function (n) { return n / TVM.G; };
  TVM.kgToN = function (kg) { return kg * TVM.G; };

  TVM.wsUrls = function (loc) {
    loc = loc || (typeof location !== "undefined" ? location : { protocol: "file:", hostname: "", port: "" });
    var urls = [];
    var path = TVM.WS_PATH;
    var host = loc.hostname || "";
    var port = loc.port || "";
    var isEspHttp = loc.protocol && loc.protocol !== "file:" && (!port || port === "80");
    if (isEspHttp && host) urls.push("ws://" + host + path);
    if (host !== "192.168.4.1") urls.push("ws://192.168.4.1" + path);
    return urls;
  };

  TVM.cmd = {
    tare:      function () { return JSON.stringify({ cmd: "tare" }); },
    start:     function () { return JSON.stringify({ cmd: "start" }); },
    stop:      function () { return JSON.stringify({ cmd: "stop" }); },
    reset:     function () { return JSON.stringify({ cmd: "reset" }); },
    getStatus: function () { return JSON.stringify({ cmd: "getStatus" }); },
    markBreak: function () { return JSON.stringify({ cmd: "break" }); },
    setMode:   function (m) { return JSON.stringify({ cmd: "setMode", mode: m }); },
    setSps:    function (s) { return JSON.stringify({ cmd: "setSps", sps: s }); },
    setUnit:   function (u) { return JSON.stringify({ cmd: "setUnit", unit: u }); },
    setLimit:  function (n) { return JSON.stringify({ cmd: "setLimit", limit_N: n }); },
    setScale:  function (s) { return JSON.stringify({ cmd: "setScale", scale: s }); },
    calibrate: function (refKg, refN) {
      var o = { cmd: "calibrate" };
      if (refKg != null) o.ref_kg = refKg;
      if (refN != null) o.ref_N = refN;
      return JSON.stringify(o);
    }
  };

  /* Détection de cycle identique au firmware (pics de force, pas le vérin). */
  TVM.Fatigue = function (hystN, minAmpN) {
    this.hyst = hystN == null ? 25 : hystN;
    this.minAmp = minAmpN == null ? 40 : minAmpN;
    this.st = 0;
    this.ext = 0;
    this.cyc = 0;
    this.fmin = 0;
    this.fmax = 0;
  };
  TVM.Fatigue.prototype.reset = function () {
    this.st = 0; this.ext = 0; this.cyc = 0; this.fmin = 0; this.fmax = 0;
  };
  TVM.Fatigue.prototype.step = function (f) {
    var evt = "";
    switch (this.st) {
      case 0:
        this.ext = f;
        if (f > this.hyst) { this.st = 1; this.fmin = f; }
        break;
      case 1:
        if (f > this.ext) this.ext = f;
        if (f < this.ext - this.hyst) {
          this.fmax = this.ext;
          evt = "peak";
          this.st = 2;
          this.ext = f;
        }
        break;
      case 2:
        if (f < this.ext) this.ext = f;
        if (f > this.ext + this.hyst) {
          this.fmin = this.ext;
          this.st = 3;
          this.ext = f;
        }
        break;
      case 3:
        if (this.fmax - this.fmin >= this.minAmp) {
          this.cyc += 1;
          evt = "cycle";
        }
        this.st = 1;
        this.ext = f;
        break;
    }
    return evt;
  };

  TVM.csvEscape = function (n, dec) {
    var s = Number(n).toFixed(dec);
    return s;
  };
  TVM.csvEscapeFr = function (n, dec) {
    return Number(n).toFixed(dec).replace(".", ",");
  };

  g.TVM = TVM;
  if (typeof module !== "undefined" && module.exports) module.exports = TVM;
})(typeof globalThis !== "undefined" ? globalThis : this);
