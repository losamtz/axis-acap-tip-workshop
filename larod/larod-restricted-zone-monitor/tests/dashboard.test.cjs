const { test } = require("node:test");
const assert = require("node:assert/strict");
const vm = require("node:vm");
const fs = require("node:fs");
const path = require("node:path");

function setup() {
  const nodes = new Map();
  let now = 0;
  const get = (id) => {
    if (!nodes.has(id)) {
      nodes.set(id, {
        value: "",
        replaceChildren() {},
        appendChild() {},
        dataset: {},
        handlers: {},
        addEventListener(event, handler) {
          this.handlers[event] = handler;
        },
        reportValidity() {
          return true;
        },
        getContext() {
          const node = this;
          return {
            clearRect() {},
            strokeRect() {},
            beginPath() {},
            arc() {},
            fill() {},
            createImageData(width, height) {
              return { data: new Uint8ClampedArray(width * height * 4) };
            },
            putImageData(image) {
              node.pixels = Array.from(image.data);
            },
          };
        },
      });
    }
    return nodes.get(id);
  };
  const context = vm.createContext({
    document: { getElementById: get, createElement: () => ({}) },
    window: { dispatchEvent() {} },
    CustomEvent: class {
      constructor(type, options) {
        this.type = type;
        this.detail = options.detail;
      }
    },
    Date,
    URLSearchParams,
    AbortController,
    atob,
    performance: { now: () => now },
    setTimeout: () => 1,
    clearTimeout() {},
    fetch: async () => {
      throw Error("offline");
    },
  });
  const script = fs.readFileSync(path.join(__dirname, "../app/html/app.js"), "utf8");
  vm.runInContext(script.replace(/poll\(\);\s*$/, ""), context);
  return {
    get,
    render(status) {
      context.status = status;
      vm.runInContext("render(status)", context);
    },
    fetch(handler) {
      context.fetch = handler;
    },
    poll() {
      return vm.runInContext("poll()", context);
    },
    time(value) {
      now = value;
    },
    submit() {
      return get("settings").handlers.submit({ preventDefault() {}, target: get("settings") });
    },
  };
}

const state = {
  previewEnabled: false,
  inputPreview: null,
  previewError: "",
  rawDetections: 20,
  personCandidates: 0,
  bestPersonConfidence: null,
  requestedView: 1,
  activeView: 1,
  activeChannel: 7,
  switchingView: false,
  views: [
    { view: 1, channel: 7 },
    { view: 2, channel: 9 },
  ],
  enabled: true,
  available: true,
  alarm: false,
  state: "Clear",
  message: "Live inference",
  confidence: 60,
  activationSeconds: 3,
  clearSeconds: 2,
  zone: [0.3, 0.2, 0.4, 0.6],
  heartbeatMs: 1000,
  occupiedSeconds: 0,
  inferenceMs: 50,
  fps: 5,
  overlayOk: true,
  eventOk: true,
  people: [],
};

test("edits survive polling and save requires matching running settings", async () => {
  const ui = setup();
  ui.render(state);
  ui.get("x").value = 20;
  ui.get("settings").handlers.input();
  ui.render(state);
  assert.equal(ui.get("x").value, 20);
  ui.fetch(async (url, options) => {
    assert.equal(url, "/axis-cgi/param.cgi");
    assert.equal(options.body.get("root.Larod_restricted_zone.Zone"), "20,20,40,60");
    assert.equal(options.body.get("root.Larod_restricted_zone.Confidence"), "60");
    return { ok: true, text: async () => "OK" };
  });
  await ui.submit();
  assert.match(ui.get("save-message").textContent, /Waiting/);
  ui.render({ ...state, zone: [0.2, 0.2, 0.4, 0.6] });
  assert.match(ui.get("save-message").textContent, /applied/);
});

test("invalid rectangles are blocked before network writes", async () => {
  const ui = setup();
  ui.render(state);
  ui.get("width").value = 100;
  ui.fetch(async () => assert.fail("must not submit"));
  await ui.submit();
  assert.match(ui.get("save-message").textContent, /fit inside/);
});

test("VAPIX text errors are not reported as successful saves", async () => {
  const ui = setup();
  ui.render(state);
  ui.fetch(async () => ({ ok: true, text: async () => "# Error" }));
  await ui.submit();
  assert.match(ui.get("save-message").textContent, /not confirmed/);
});

test("disconnect removes alarm claims and disables editing", async () => {
  const ui = setup();
  ui.render({ ...state, alarm: true, state: "Alarm" });
  await ui.poll();
  assert.equal(ui.get("state").textContent, "Unknown");
  assert.equal(ui.get("fields").disabled, true);
  assert.equal(ui.get("people").textContent, "—");
});

test("a responsive HTTP worker cannot disguise a stalled inference loop", async () => {
  const ui = setup();
  ui.render(state);
  ui.time(3100);
  ui.fetch(async () => ({ ok: true, text: async () => JSON.stringify(state) }));
  await ui.poll();
  assert.equal(ui.get("state").textContent, "Unknown");
  assert.match(ui.get("status-message").textContent, /stopped responding/);
  ui.render({ ...state, heartbeatMs: 5000 });
  assert.equal(ui.get("state").textContent, "Clear");
});

test("view selection saves a view number and reports switching instead of claiming it is active", async () => {
  const ui = setup();
  ui.render(state);
  ui.get("view-area").value = "2";
  ui.get("settings").handlers.input();
  ui.fetch(async (url, options) => {
    assert.equal(options.body.get("root.Larod_restricted_zone.ViewArea"), "2");
    return { ok: true, text: async () => "OK" };
  });
  await ui.submit();
  ui.render({ ...state, requestedView: 2, activeView: 0, switchingView: true, available: false });
  assert.match(ui.get("save-message").textContent, /Switching/);
  assert.equal(ui.get("active-view").textContent, "No active view");
  ui.render({ ...state, requestedView: 2, activeView: 2, activeChannel: 9 });
  assert.equal(ui.get("active-view").textContent, "View area 2");
});

test("unavailable views cannot be submitted", async () => {
  const ui = setup();
  ui.render(state);
  ui.get("view-area").value = "99";
  ui.fetch(async () => assert.fail("must not write"));
  await ui.submit();
  assert.match(ui.get("save-message").textContent, /available view/);
});

test("diagnostics distinguish weak people from no returned person candidate", () => {
  const ui = setup();
  ui.render(state);
  assert.match(ui.get("person-confidence").textContent, /No person/);
  assert.match(ui.get("detection-hint").textContent, /no person candidates/);
  ui.render({ ...state, personCandidates: 1, bestPersonConfidence: 42 });
  assert.match(ui.get("person-confidence").textContent, /42.0%/);
  assert.match(ui.get("detection-hint").textContent, /below/);
  ui.render({ ...state, available: false });
  assert.equal(ui.get("person-confidence").textContent, "—");
});

test("model-input preview preserves RGB channel order and disappears on stale status", () => {
  const ui = setup();
  const snapshot = {
    width: 2,
    height: 1,
    sequence: 1,
    ageSeconds: 0.2,
    rgb: Buffer.from([255, 0, 0, 0, 255, 0]).toString("base64"),
  };
  ui.render({ ...state, previewEnabled: true, inputPreview: snapshot });
  assert.deepEqual(ui.get("model-input").pixels, [255, 0, 0, 255, 0, 255, 0, 255]);
  assert.equal(ui.get("model-input").hidden, false);
  ui.render({ ...state, previewEnabled: true, inputPreview: { ...snapshot, ageSeconds: 4 } });
  assert.equal(ui.get("model-input").hidden, true);
});

test("bad preview bytes do not disrupt monitoring status", () => {
  const ui = setup();
  ui.render({
    ...state,
    previewEnabled: true,
    inputPreview: { width: 2, height: 1, sequence: 1, ageSeconds: 0.1, rgb: "AA==" },
  });
  assert.equal(ui.get("state").textContent, "Clear");
  assert.equal(ui.get("model-input").hidden, true);
  assert.match(ui.get("input-message").textContent, /byte count/);
});

test("no matching overlay stream is reported rather than claiming a drawing update", () => {
  const ui = setup();
  ui.render({ ...state, overlayOk: false, overlayStreams: 0 });
  assert.match(ui.get("outputs").textContent, /No video stream has an overlay/);
  ui.render({ ...state, overlayOk: true, overlayStreams: 1 });
  assert.match(ui.get("outputs").textContent, /Overlay updated/);
});
