import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { test } from 'node:test';
import vm from 'node:vm';

const root = new URL('../..', import.meta.url);

async function emJsBody(path, name, next) {
  const source = await readFile(new URL(path, root), 'utf8');
  const start = source.indexOf(`EM_JS(void, ${name}`);
  assert.notEqual(start, -1, `${name} is present`);
  const body = source.indexOf('), {', start);
  const end = source.indexOf(`\n})\n\n${next}`, body);
  assert.notEqual(end, -1, `${name} body is complete`);
  return source.slice(body + 4, end);
}

class SourcePromise {
  constructor(state, value, tracker) {
    this.state = state;
    this.value = value;
    this.tracker = tracker;
    if (state === 'rejected') tracker.rejections.push(this);
  }

  static resolve(value, tracker) {
    return value instanceof SourcePromise ? value : new SourcePromise('fulfilled', value, tracker);
  }

  static reject(value, tracker) {
    return new SourcePromise('rejected', value, tracker);
  }

  then(onFulfilled, onRejected) {
    const callback = this.state === 'fulfilled' ? onFulfilled : onRejected;
    if (typeof callback !== 'function') {
      this.handled = true;
      return new SourcePromise(this.state, this.value, this.tracker);
    }
    if (this.state === 'rejected') this.handled = true;
    try {
      return SourcePromise.resolve(callback(this.value), this.tracker);
    } catch (error) {
      return SourcePromise.reject(error, this.tracker);
    }
  }

  catch(onRejected) { return this.then(undefined, onRejected); }
}

function httpHarness(fetchResult, callbacks = {}) {
  const tracker = {rejections: []};
  const heap = new Uint8Array(1024);
  const completions = [];
  const context = {
    AbortController,
    Headers,
    HEAPU8: heap,
    Module: {},
    Uint8Array,
    clearTimeout() {},
    console: {error() {}},
    fetch() { return fetchResult(tracker); },
    parseInt,
    setTimeout() { return 1; },
    UTF8ToString(ptr) { return ptr === 1 ? '/fixture' : 'GET'; },
    wasmExports: {malloc: callbacks.malloc || (() => 64)},
    _nt_http_web_on_complete(...args) {
      completions.push(args);
      if (callbacks.complete) callbacks.complete(...args);
    },
    _nt_http_web_on_progress(...args) {
      if (callbacks.progress) callbacks.progress(...args);
    },
  };
  return {context, tracker, completions};
}

function response({body = null, arrayBuffer, headers = {}}) {
  return {
    status: 200,
    body,
    headers: {
      forEach(callback) { for (const [key, value] of Object.entries(headers)) callback(value, key); },
      get() { return null; },
    },
    arrayBuffer,
  };
}

function unhandled(tracker) {
  return tracker.rejections.filter((entry) => !entry.handled).map((entry) => entry.value);
}

test('http web bridge lets a completion callback fault escape without a second completion', async () => {
  const body = await emJsBody('engine/http/web/nt_http_web.c', 'nt_http_web_fetch', 'EM_JS(void, nt_http_web_cancel');
  const callbackFault = new Error('completion fault');
  const harness = httpHarness(
    (tracker) => SourcePromise.resolve(response({arrayBuffer: () => SourcePromise.resolve(new Uint8Array([1]), tracker)}), tracker),
    {complete() { throw callbackFault; }},
  );
  const fetch = vm.runInNewContext(`(function(slot_index, generation, url_ptr, method_ptr, body_ptr, body_size, headers_ptr, headers_size, timeout_ms) {${body}\n})`, harness.context);

  fetch(1, 1, 1, 2, 0, 0, 0, 0, 0);

  assert.equal(harness.completions.length, 1);
  assert.deepEqual(unhandled(harness.tracker), [callbackFault]);
});

test('http web bridge lets a progress callback fault escape without completion', async () => {
  const body = await emJsBody('engine/http/web/nt_http_web.c', 'nt_http_web_fetch', 'EM_JS(void, nt_http_web_cancel');
  const progressFault = new Error('progress fault');
  const harness = httpHarness(
    (tracker) => {
      const reader = {read: () => SourcePromise.resolve({done: false, value: new Uint8Array([1])}, tracker)};
      return SourcePromise.resolve(response({body: {getReader: () => reader}}), tracker);
    },
    {progress() { throw progressFault; }},
  );
  const fetch = vm.runInNewContext(`(function(slot_index, generation, url_ptr, method_ptr, body_ptr, body_size, headers_ptr, headers_size, timeout_ms) {${body}\n})`, harness.context);

  fetch(1, 1, 1, 2, 0, 0, 0, 0, 0);

  assert.equal(harness.completions.length, 0);
  assert.deepEqual(unhandled(harness.tracker), [progressFault]);
});

test('http web bridge still reports a transport rejection as a failed request', async () => {
  const body = await emJsBody('engine/http/web/nt_http_web.c', 'nt_http_web_fetch', 'EM_JS(void, nt_http_web_cancel');
  const transportFault = new Error('network down');
  const harness = httpHarness((tracker) => SourcePromise.reject(transportFault, tracker));
  const fetch = vm.runInNewContext(`(function(slot_index, generation, url_ptr, method_ptr, body_ptr, body_size, headers_ptr, headers_size, timeout_ms) {${body}\n})`, harness.context);

  fetch(1, 7, 1, 2, 0, 0, 0, 0, 0);

  assert.equal(harness.completions.length, 1);
  assert.deepEqual(harness.completions[0].slice(0, 2), [1, 7]);
  assert.equal(harness.completions[0][6], 0);
  assert.deepEqual(unhandled(harness.tracker), []);
});

test('http web bridge reports an array-buffer rejection as a failed request', async () => {
  const body = await emJsBody('engine/http/web/nt_http_web.c', 'nt_http_web_fetch', 'EM_JS(void, nt_http_web_cancel');
  const streamFault = new Error('body read failed');
  const harness = httpHarness(
    (tracker) => SourcePromise.resolve(response({arrayBuffer: () => SourcePromise.reject(streamFault, tracker)}), tracker),
  );
  const fetch = vm.runInNewContext(`(function(slot_index, generation, url_ptr, method_ptr, body_ptr, body_size, headers_ptr, headers_size, timeout_ms) {${body}\n})`, harness.context);

  fetch(1, 7, 1, 2, 0, 0, 0, 0, 0);

  assert.equal(harness.completions.length, 1);
  assert.equal(harness.completions[0][6], 0);
  assert.deepEqual(unhandled(harness.tracker), []);
});

test('http web bridge reports a reader rejection as a failed request', async () => {
  const body = await emJsBody('engine/http/web/nt_http_web.c', 'nt_http_web_fetch', 'EM_JS(void, nt_http_web_cancel');
  const streamFault = new Error('stream read failed');
  const harness = httpHarness(
    (tracker) => {
      const reader = {read: () => SourcePromise.reject(streamFault, tracker)};
      return SourcePromise.resolve(response({body: {getReader: () => reader}}), tracker);
    },
  );
  const fetch = vm.runInNewContext(`(function(slot_index, generation, url_ptr, method_ptr, body_ptr, body_size, headers_ptr, headers_size, timeout_ms) {${body}\n})`, harness.context);

  fetch(1, 7, 1, 2, 0, 0, 0, 0, 0);

  assert.equal(harness.completions.length, 1);
  assert.equal(harness.completions[0][6], 0);
  assert.deepEqual(unhandled(harness.tracker), []);
});

function crashHarness(previousAbort) {
  const listeners = new Map();
  const events = [];
  const storage = new Map();
  const heap = new Uint8Array(1024);
  const words = new Uint32Array(heap.buffer);
  const strings = new Map([
    [300, 'nt.test.crash'], [301, 'game'], [302, 'engine'], [303, 'wasm-debug'], [304, 'vendor'], [305, 'renderer'], [306, 'api'], [307, 'caps'], [308, 'phase=test'],
  ]);
  heap.set(new TextEncoder().encode('ERROR [test] token\n'), 200);
  words[100 >> 2] = 19;
  words[104 >> 2] = 0;
  const window = {
    localStorage: {setItem(key, value) { storage.set(key, value); }},
    addEventListener(type, listener) { listeners.set(type, listener); },
    dispatchEvent(event) {
      events.push(event);
      const listener = listeners.get(event.type);
      if (listener) listener(event);
    },
  };
  return {
    context: {
      CustomEvent: class { constructor(type, init) { this.type = type; this.detail = init.detail; } },
      Date: {now: () => 42},
      Error,
      HEAPU8: heap,
      HEAPU32: words,
      Module: {onAbort: previousAbort},
      TextDecoder,
      UTF8ToString(ptr) { return strings.get(ptr) || ''; },
      WebAssembly,
      window,
    },
    events,
    storage,
  };
}

test('crash web bridge records a caught abort before calling the prior shell handler', async () => {
  const body = await emJsBody('engine/crash/web/nt_crash_web.c', 'nt_crash_web_install', 'EM_JS(uint32_t, nt_crash_web_load');
  const receiver = {called: 0};
  let observed;
  const harness = crashHarness(function(reason) {
    this.called++;
    observed = {
      arguments: [...arguments],
      event: harness.events[0],
      receiver: this,
      record: harness.storage.get('nt.test.crash'),
      reason,
    };
  });
  const install = vm.runInNewContext(`(function(ring, ring_size, pos, full, name, game, engine, preset, vendor, vendor_cap, renderer, renderer_cap, api, api_cap, caps, caps_cap, context, context_cap) {${body}\n})`, harness.context);

  install(200, 64, 100, 104, 300, 301, 302, 303, 304, 32, 305, 32, 306, 32, 307, 32, 308, 32);
  harness.context.Module.onAbort.call(receiver, 17, 'extra');

  assert.equal(receiver.called, 1);
  assert.equal(observed.receiver, receiver);
  assert.equal(observed.reason, 17);
  assert.deepEqual(observed.arguments, [17, 'extra']);
  assert.match(observed.record, /reason: 17/);
  assert.equal(observed.event.type, 'nt-crash');
  const record = harness.storage.get('nt.test.crash');
  assert.match(record, /reason: 17/);
  assert.match(record, /stack:/);
});

test('crash web bridge contains a throwing prior abort handler after recording', async () => {
  const body = await emJsBody('engine/crash/web/nt_crash_web.c', 'nt_crash_web_install', 'EM_JS(uint32_t, nt_crash_web_load');
  const harness = crashHarness(() => { throw new Error('shell handler fault'); });
  const install = vm.runInNewContext(`(function(ring, ring_size, pos, full, name, game, engine, preset, vendor, vendor_cap, renderer, renderer_cap, api, api_cap, caps, caps_cap, context, context_cap) {${body}\n})`, harness.context);

  install(200, 64, 100, 104, 300, 301, 302, 303, 304, 32, 305, 32, 306, 32, 307, 32, 308, 32);
  assert.doesNotThrow(() => harness.context.Module.onAbort(null));
  assert.match(harness.storage.get('nt.test.crash'), /reason: abort/);
});
