const { test } = require("node:test");
const assert = require("node:assert/strict");
const vm = require("node:vm");
const fs = require("node:fs");
const path = require("node:path");
const settings = {
  siteName: "Warehouse B",
  enabled: true,
  occupancyLimitSeconds: 30,
  alertCooldownSeconds: 20,
};
function setup() {
  const nodes = new Map();
  const get = (id) => {
    if (!nodes.has(id))
      nodes.set(id, {
        value: "",
        checked: false,
        dataset: {},
        handlers: {},
        addEventListener(e, f) {
          this.handlers[e] = f;
        },
        setAttribute(k, v) {
          this[k] = v;
        },
        reportValidity() {
          return true;
        },
      });
    return nodes.get(id);
  };
  const c = vm.createContext({
    document: { getElementById: get },
    AbortController,
    setTimeout: () => 1,
    clearTimeout() {},
    fetch: async () => {
      throw Error("offline");
    },
  });
  vm.runInContext(
    fs
      .readFileSync(path.join(__dirname, "../app/html/app.js"), "utf8")
      .replace(/refresh\(\);\s*$/, ""),
    c,
  );
  return {
    get,
    fetch(fn) {
      c.fetch = fn;
    },
    refresh() {
      return vm.runInContext("refresh()", c);
    },
  };
}
const response = (body, ok = true) => ({ ok, status: ok ? 200 : 500, json: async () => body });
async function ready() {
  const ui = setup();
  ui.fetch(async () => response({ ok: true, settings }));
  await ui.refresh();
  return ui;
}
const submit = (ui, id) => ui.get(id).handlers.submit({ preventDefault() {}, target: ui.get(id) });
test("refresh preserves edits and restore uses confirmed settings", async () => {
  const ui = await ready();
  ui.get("site").value = "Draft";
  ui.get("settings").handlers.input();
  await ui.refresh();
  assert.equal(ui.get("site").value, "Draft");
  ui.get("restore").handlers.click();
  assert.equal(ui.get("site").value, "Warehouse B");
});
test("save posts typed JSON and displays readback", async () => {
  const ui = await ready();
  ui.get("site").value = "New site";
  ui.fetch(async (url, o) => {
    assert.equal(url, "settings.cgi");
    assert.equal(o.headers["X-Policy-Request"], "1");
    const body = JSON.parse(o.body);
    assert.equal(body.occupancyLimitSeconds, 30);
    return response({ ok: true, settings: { ...settings, siteName: body.siteName } });
  });
  await submit(ui, "settings");
  assert.equal(ui.get("saved-site").textContent, "New site");
  assert.match(ui.get("save-message").textContent, /confirmed/);
});
test("validation and partial failures never claim success", async () => {
  const ui = await ready();
  ui.fetch(async () =>
    response({ ok: false, error: "Invalid settings", errors: { siteName: "Required" } }, false),
  );
  await submit(ui, "settings");
  assert.equal(ui.get("site-error").textContent, "Required");
  ui.fetch(async () =>
    response(
      { ok: false, error: "Partial save", settings: { ...settings, siteName: "Partially saved" } },
      false,
    ),
  );
  await submit(ui, "settings");
  assert.equal(ui.get("save-message").textContent, "Partial save");
  assert.equal(ui.get("saved-site").textContent, "Partially saved");
});
test("test uses saved policy, not unsaved settings", async () => {
  const ui = await ready();
  ui.get("site").value = "Unsaved";
  ui.get("settings").handlers.input();
  ui.get("occupied").value = "40";
  ui.get("no-previous").checked = true;
  ui.fetch(async (url, o) => {
    assert.equal(url, "test.cgi");
    assert.deepEqual(JSON.parse(o.body), { occupiedSeconds: 40, secondsSinceLastAlert: null });
    return response({ ok: true, settings, state: "Overdue", explanation: "No alert was sent." });
  });
  await submit(ui, "test");
  assert.equal(ui.get("site").value, "Unsaved");
  assert.match(ui.get("test-result").textContent, /Warehouse B: Overdue/);
});
test("network failure marks saved values unconfirmed and allows refresh recovery", async () => {
  const ui = await ready();
  ui.fetch(async () => {
    throw Error("offline");
  });
  await ui.refresh();
  assert.equal(ui.get("fields").disabled, true);
  assert.equal(ui.get("refresh").disabled, false);
  assert.match(ui.get("saved-status").textContent, /not confirmed/);
  ui.fetch(async () => response({ ok: true, settings }));
  await ui.refresh();
  assert.equal(ui.get("fields").disabled, false);
});
