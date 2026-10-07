"use strict";
const byId = (id) => document.getElementById(id);
const fields = {
  siteName: "site",
  enabled: "enabled",
  occupancyLimitSeconds: "limit",
  alertCooldownSeconds: "cooldown",
};
let current = null;
let dirty = false;
let busy = false;
let connected = false;
function controls() {
  byId("fields").disabled = !connected || busy;
  byId("refresh").disabled = busy;
  byId("test-fields").disabled = !connected || busy;
  byId("last").disabled = byId("no-previous").checked;
}

function populate(settings) {
  byId("site").value = settings.siteName;
  byId("enabled").checked = settings.enabled;
  byId("limit").value = settings.occupancyLimitSeconds;
  byId("cooldown").value = settings.alertCooldownSeconds;
}

function confirmed(settings) {
  current = settings;
  byId("saved-site").textContent = settings.siteName;
  byId("saved-enabled").textContent = settings.enabled ? "Enabled" : "Disabled";
  byId("saved-limit").textContent = `${settings.occupancyLimitSeconds}s`;
  byId("saved-cooldown").textContent = `${settings.alertCooldownSeconds}s`;
  byId("saved-status").textContent = "Read back from camera just now.";
}

function clearErrors() {
  for (const id of Object.values(fields)) {
    byId(id + "-error").textContent = "";
    byId(id).setAttribute("aria-invalid", "false");
  }
}

function showErrors(errors = {}) {
  for (const [key, text] of Object.entries(errors)) {
    const id = fields[key];
    if (id) {
      byId(id + "-error").textContent = text;
      byId(id).setAttribute("aria-invalid", "true");
    }
  }
}

function online() {
  connected = true;
  byId("connection").textContent = "Connected";
  byId("connection").dataset.state = "online";
}

function unavailable() {
  connected = false;
  byId("connection").textContent = "Unavailable";
  byId("connection").dataset.state = "offline";
  byId("saved-status").textContent =
    "Last known values; current camera settings are not confirmed.";
}

async function api(path, body) {
  const controller = new AbortController(),
    timer = setTimeout(() => controller.abort(), 5000);
  try {
    // GET loads settings. POST carries a typed JSON object and the app header.
    const options = {
      credentials: "same-origin",
      cache: "no-store",
      signal: controller.signal,
    };
    if (body !== undefined) {
      options.method = "POST";
      options.headers = {
        "Content-Type": "application/json",
        "X-Policy-Request": "1",
      };
      options.body = JSON.stringify(body);
    }
    const response = await fetch(path, options);
    const result = await response.json();
    if (!response.ok || result.ok !== true) {
      const error = new Error(result.error || `HTTP ${response.status}`);
      error.response = result;
      throw error;
    }
    return result;
  } finally {
    clearTimeout(timer);
  }
}

async function refresh() {
  if (busy) {
    return;
  }
  busy = true;
  controls();
  try {
    const result = await api("settings.cgi");
    confirmed(result.settings);
    online();
    if (!dirty) {
      populate(current);
    }
    byId("save-message").textContent = dirty
      ? "Saved values refreshed. Your edits are preserved."
      : "Settings loaded.";
  } catch (error) {
    unavailable();
    byId("save-message").textContent = `Could not load settings: ${error.message}`;
  } finally {
    busy = false;
    controls();
  }
}

byId("settings").addEventListener("input", () => {
  dirty = true;
  byId("save-message").textContent = "Unsaved changes. Tests use the saved policy.";
});
byId("refresh").addEventListener("click", refresh);
byId("restore").addEventListener("click", () => {
  if (current) {
    populate(current);
    dirty = false;
    clearErrors();
    byId("save-message").textContent =
      "Form restored to last confirmed values. Refresh to check external changes.";
  }
});
byId("settings").addEventListener("submit", async (event) => {
  event.preventDefault();
  if (busy || !connected || !event.target.reportValidity()) return;
  const body = {
    siteName: byId("site").value,
    enabled: byId("enabled").checked,
    occupancyLimitSeconds: Number(byId("limit").value),
    alertCooldownSeconds: Number(byId("cooldown").value),
  };
  busy = true;
  clearErrors();
  controls();
  byId("save-message").textContent = "Saving and reading back…";
  try {
    const result = await api("settings.cgi", body);
    confirmed(result.settings);
    populate(current);
    dirty = false;
    online();
    byId("save-message").textContent = "Saved and confirmed on the camera.";
    byId("test-result").textContent = "Settings changed. Run the policy test again.";
  } catch (error) {
    showErrors(error.response?.errors);
    if (error.response?.settings) {
      confirmed(error.response.settings);
      online();
    } else if (!error.response?.errors) unavailable();
    byId("save-message").textContent = error.message;
  } finally {
    busy = false;
    controls();
  }
});
byId("no-previous").addEventListener("change", controls);
byId("test").addEventListener("input", () => {
  byId("test-result").textContent = "Test inputs changed. Run the test again.";
});
byId("test").addEventListener("submit", async (event) => {
  event.preventDefault();
  if (busy || !connected || !event.target.reportValidity()) return;
  const body = {
    occupiedSeconds: Number(byId("occupied").value),
    secondsSinceLastAlert: byId("no-previous").checked ? null : Number(byId("last").value),
  };
  busy = true;
  controls();
  byId("test-result").textContent = "Testing saved policy…";
  try {
    const result = await api("test.cgi", body);
    online();
    confirmed(result.settings);
    if (!dirty) {
      populate(current);
    }
    byId("test-result").textContent =
      `${result.settings.siteName}: ${result.state}. ${result.explanation} Used limit ${result.settings.occupancyLimitSeconds}s and cooldown ${result.settings.alertCooldownSeconds}settings.`;
  } catch (error) {
    if (!error.response?.errors) unavailable();
    byId("test-result").textContent = `Test failed: ${error.message}`;
  } finally {
    busy = false;
    controls();
  }
});
refresh();
