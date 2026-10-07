const { test } = require("node:test");
const assert = require("node:assert/strict");
const vm = require("node:vm");
const fs = require("node:fs");
const path = require("node:path");
function setup(play = async () => {}) {
  const nodes = new Map();
  const get = (id) => {
    if (!nodes.has(id))
      nodes.set(id, {
        handlers: {},
        addEventListener(event, fn) {
          this.handlers[event] = fn;
        },
      });
    return nodes.get(id);
  };
  const video = get("camera-video");
  Object.assign(video, {
    play,
    pause() {},
    load() {
      this.loads = (this.loads || 0) + 1;
    },
    removeAttribute(name) {
      delete this[name];
    },
  });
  const window = {
    handlers: {},
    addEventListener(event, fn) {
      this.handlers[event] = fn;
    },
  };
  const context = vm.createContext({ document: { getElementById: get }, window });
  vm.runInContext(fs.readFileSync(path.join(__dirname, "../app/html/video.js"), "utf8"), context);
  window.handlers["monitor-view"]({ detail: { view: 1 } });
  video.loads = 0;
  return {
    get,
    video,
    window,
    start: () => get("video-start").handlers.click(),
    stop: () => get("video-stop").handlers.click(),
  };
}
test("preview starts on demand with same-origin MP4, and stop releases the stream", async () => {
  const ui = setup();
  assert.equal(ui.video.src, undefined);
  await ui.start();
  const url = new URL(ui.video.src, "https://camera.test");
  assert.equal(url.origin, "https://camera.test");
  assert.equal(url.pathname, "/axis-cgi/media.cgi");
  for (const [key, value] of Object.entries({
    container: "mp4",
    videocodec: "h264",
    camera: "1",
    audio: "0",
    fps: "10",
    overlays: "all",
  })) {
    assert.equal(url.searchParams.get(key), value);
  }
  assert.equal(ui.video.muted, true);
  ui.video.handlers.playing();
  assert.match(ui.get("video-status").textContent, /Playing/);
  ui.stop();
  assert.equal(ui.video.src, undefined);
  assert.equal(ui.video.loads, 1);
  assert.equal(ui.get("video-start").disabled, false);
  assert.equal(ui.get("video-stop").disabled, true);
  ui.video.handlers.pause();
  assert.match(ui.get("video-status").textContent, /stopped/);
});
test("media errors allow retry and navigation closes the stream", async () => {
  const ui = setup();
  await ui.start();
  ui.video.handlers.error();
  assert.match(ui.get("video-status").textContent, /unavailable/);
  assert.equal(ui.video.src, undefined);
  await ui.start();
  assert.ok(ui.video.src);
  ui.window.handlers.pagehide();
  assert.equal(ui.video.src, undefined);
});
test("late play rejection cannot stop a newer preview", async () => {
  let reject;
  const ui = setup(
    () =>
      new Promise((resolve, fail) => {
        reject = fail;
      }),
  );
  const first = ui.start();
  ui.stop();
  ui.video.play = async () => {};
  await ui.start();
  reject(new Error("aborted"));
  await first;
  assert.ok(ui.video.src);
  assert.equal(ui.get("video-start").disabled, true);
});
test("blocked playback retains native Play controls, other failures release the stream", async () => {
  const ui = setup(async () => {
    throw Object.assign(new Error(), { name: "NotAllowedError" });
  });
  await ui.start();
  assert.match(ui.get("video-status").textContent, /Press Play/);
  assert.ok(ui.video.src);
  ui.stop();
  ui.video.play = async () => {
    throw new Error("unsupported");
  };
  await ui.start();
  assert.match(ui.get("video-status").textContent, /unavailable/);
  assert.equal(ui.video.src, undefined);
});

test("preview follows the active view and closes the old stream on a view change", async () => {
  const ui = setup();
  await ui.start();
  ui.window.handlers["monitor-view"]({ detail: { view: 0, switching: true } });
  assert.equal(ui.video.src, undefined);
  ui.window.handlers["monitor-view"]({ detail: { view: 3, switching: false } });
  assert.equal(new URL(ui.video.src, "https://camera.test").searchParams.get("camera"), "3");
  ui.window.handlers["monitor-view"]({ detail: { view: 0 } });
  assert.equal(ui.video.src, undefined);
  assert.equal(ui.get("video-start").disabled, true);
  await ui.start();
  assert.equal(ui.video.src, undefined);
});

test("stopped preview stays stopped across a view switch", () => {
  const ui = setup();
  ui.window.handlers["monitor-view"]({ detail: { view: 0, switching: true } });
  ui.window.handlers["monitor-view"]({ detail: { view: 3 } });
  assert.equal(ui.video.src, undefined);
});

test("lost status cancels automatic resume during a view switch", async () => {
  const ui = setup();
  await ui.start();
  ui.window.handlers["monitor-view"]({ detail: { view: 0, switching: true } });
  ui.window.handlers["monitor-view"]({ detail: { view: 0 } });
  ui.window.handlers["monitor-view"]({ detail: { view: 3 } });
  assert.equal(ui.video.src, undefined);
});
