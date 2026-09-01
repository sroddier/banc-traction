#!/usr/bin/env node
/* Tests de la machine à états fatigue (même algo que le firmware). */
var assert = require("assert");
var path = require("path");
var TVM = require(path.join(__dirname, "..", "js", "protocol.js"));

var failed = 0;
function check(name, fn) {
  try {
    fn();
    console.log("ok  " + name);
  } catch (e) {
    failed += 1;
    console.log("FAIL " + name + " — " + e.message);
  }
}

check("un cycle sinusoïde 80–900 N", function () {
  var f = new TVM.Fatigue(25, 40);
  var evtCycle = 0;
  for (var i = 0; i < 200; i++) {
    var t = i / 40;
    var N = 490 + 410 * Math.sin(2 * Math.PI * t);
    if (f.step(N) === "cycle") evtCycle += 1;
  }
  assert.ok(f.cyc >= 4, "cycles=" + f.cyc);
  assert.ok(evtCycle >= 4);
});

check("bruit 5 N ne compte pas", function () {
  var f = new TVM.Fatigue(25, 40);
  for (var i = 0; i < 100; i++) f.step(10 + (i % 2 ? 5 : 0));
  assert.strictEqual(f.cyc, 0);
});

check("amplitude 30 N < min 40 N ne compte pas", function () {
  var f = new TVM.Fatigue(25, 40);
  var seq = [];
  for (var i = 0; i < 30; i++) seq.push(50);
  for (i = 0; i < 30; i++) seq.push(80);
  for (i = 0; i < 30; i++) seq.push(50);
  for (i = 0; i < 30; i++) seq.push(80);
  seq.forEach(function (n) { f.step(n); });
  assert.strictEqual(f.cyc, 0);
});

check("triangle 100–400 compte au moins 1 cycle", function () {
  var f = new TVM.Fatigue(25, 40);
  function ramp(a, b, n) {
    var out = [];
    for (var i = 0; i < n; i++) out.push(a + (b - a) * i / (n - 1));
    return out;
  }
  [].concat(ramp(100, 400, 20), ramp(400, 100, 20), ramp(100, 400, 20))
    .forEach(function (n) { f.step(n); });
  assert.ok(f.cyc >= 1, "cycles=" + f.cyc);
  assert.ok(f.fmax > 350, "fmax=" + f.fmax);
});

check("reset remet à zéro", function () {
  var f = new TVM.Fatigue();
  [0, 80, 900, 80, 900].forEach(function (n) { f.step(n); });
  f.reset();
  assert.strictEqual(f.cyc, 0);
  assert.strictEqual(f.st, 0);
});

check("wsUrls file:// ne met que l'AP", function () {
  var u = TVM.wsUrls({ protocol: "file:", hostname: "", port: "" });
  assert.deepStrictEqual(u, ["ws://192.168.4.1/ws"]);
});

check("wsUrls AP sans port", function () {
  var u = TVM.wsUrls({ protocol: "http:", hostname: "192.168.4.1", port: "" });
  assert.ok(u[0] === "ws://192.168.4.1/ws");
});

check("csv FR", function () {
  assert.strictEqual(TVM.csvEscapeFr(12.5, 1), "12,5");
});

check("kgToN", function () {
  var n = TVM.kgToN(1);
  assert.ok(Math.abs(n - 9.80665) < 1e-6);
});

if (failed) {
  console.log("\n" + failed + " échec(s)");
  process.exit(1);
}
console.log("\nTous les tests OK");
