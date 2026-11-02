// SolEnshaded - Offline PWA Service Worker with COOP/COEP Cross-Origin Isolation
const CACHE_NAME = 'solenshaded-pwa-v1';

const PRECACHE_ASSETS = [
    './',
    './index.html',
    './solenshaded.html',
    './solenshaded.js',
    './solenshaded.wasm',
    './solenshaded.data',
    './manifest.json'
];

function ensureCoopCoepHeaders(response) {
    if (!response || response.type === 'opaque') {
        return response;
    }
    const headers = new Headers(response.headers);
    let modified = false;

    if (!headers.get('Cross-Origin-Opener-Policy')) {
        headers.set('Cross-Origin-Opener-Policy', 'same-origin');
        modified = true;
    }
    if (!headers.get('Cross-Origin-Embedder-Policy')) {
        headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
        modified = true;
    }

    if (modified) {
        return new Response(response.body, {
            status: response.status,
            statusText: response.statusText,
            headers: headers
        });
    }
    return response;
}

self.addEventListener('install', (event) => {
    event.waitUntil(
        caches.open(CACHE_NAME).then((cache) => {
            return cache.addAll(PRECACHE_ASSETS).catch((err) => {
                console.warn('[SolEnshaded SW] Non-fatal precache error:', err);
            });
        }).then(() => self.skipWaiting())
    );
});

self.addEventListener('activate', (event) => {
    event.waitUntil(
        caches.keys().then((keys) => {
            return Promise.all(
                keys.map((key) => {
                    if (key !== CACHE_NAME) {
                        return caches.delete(key);
                    }
                })
            );
        }).then(() => self.clients.claim())
    );
});

self.addEventListener('fetch', (event) => {
    if (event.request.method !== 'GET') return;

    event.respondWith(
        caches.match(event.request).then((cachedResponse) => {
            if (cachedResponse) {
                return ensureCoopCoepHeaders(cachedResponse);
            }
            return fetch(event.request).then((networkResponse) => {
                if (!networkResponse || networkResponse.status !== 200) {
                    return networkResponse;
                }
                const responseToCache = networkResponse.clone();
                caches.open(CACHE_NAME).then((cache) => {
                    cache.put(event.request, responseToCache);
                });
                return ensureCoopCoepHeaders(networkResponse);
            }).catch(() => {
                if (event.request.headers.get('accept')?.includes('text/html')) {
                    return caches.match('./index.html');
                }
            });
        })
    );
});

