/* Service worker : installable seulement en http://localhost (contexte sécurisé).
 * L'AP ESP32 est en HTTP clair : la page fonctionne sans SW, c'est normal.
 */
var CACHE = "tvm-traction-v1.2.2";
var ASSETS = [
  "./",
  "./index.html",
  "./css/app.css",
  "./js/protocol.js",
  "./js/chart.js",
  "./js/app.js",
  "./manifest.json",
  "./icon.svg",
  "./img/logo-artaud.png"
];
self.addEventListener("install", function (e) {
  e.waitUntil(caches.open(CACHE).then(function (c) { return c.addAll(ASSETS); }));
});
self.addEventListener("activate", function (e) {
  e.waitUntil(caches.keys().then(function (keys) {
    return Promise.all(keys.filter(function (k) { return k !== CACHE; }).map(function (k) { return caches.delete(k); }));
  }));
});
self.addEventListener("fetch", function (e) {
  if (e.request.method !== "GET") return;
  e.respondWith(
    caches.match(e.request).then(function (hit) {
      return hit || fetch(e.request);
    })
  );
});
