// Dino 8 Touch service worker: caches the app shell so the PWA starts with no
// network at all (standalone mode keeps working; the desktop connection
// resumes when the network returns). The Dino 8 API (/touch/api/) is never
// cached - model data and command results always come live from the desktop.
'use strict';
var VERSION = 'dino8-touch-v1';
var SHELL = ['./', 'index.html', 'app.css', 'app.js', 'manifest.webmanifest', 'icon.svg', 'icon-192.png', 'icon-512.png'];

self.addEventListener('install', function (e) {
  e.waitUntil(caches.open(VERSION).then(function (c) { return c.addAll(SHELL); }).then(function () { return self.skipWaiting(); }));
});

self.addEventListener('activate', function (e) {
  e.waitUntil(caches.keys().then(function (keys) {
    return Promise.all(keys.filter(function (k) { return k !== VERSION && k.indexOf('dino8-touch-') === 0; }).map(function (k) { return caches.delete(k); }));
  }).then(function () { return self.clients.claim(); }));
});

self.addEventListener('fetch', function (e) {
  var req = e.request, url = new URL(req.url);
  if (req.method !== 'GET' || url.origin !== self.location.origin) return;   // API calls to other hosts: straight to network
  if (url.pathname.indexOf('/touch/api/') !== -1) return;                     // never cache model data
  e.respondWith(
    caches.match(req, { ignoreSearch: true }).then(function (hit) {
      var net = fetch(req).then(function (res) {
        if (res && res.ok) { var copy = res.clone(); caches.open(VERSION).then(function (c) { c.put(req, copy); }); }
        return res;
      }).catch(function () { return null; });
      if (hit) { e.waitUntil(net); return hit; }                                // cache first, refreshed in the background
      return net.then(function (res) {
        if (res) return res;
        if (req.mode === 'navigate') return caches.match('index.html');
        return new Response('offline', { status: 503, statusText: 'offline' });
      });
    })
  );
});
