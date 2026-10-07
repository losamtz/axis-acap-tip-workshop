"use strict";

const byId = (id) => document.getElementById(id);
let current = null;
let dirty = false;
let saving = false;
let connected = false;
let pending = null;
let lastHeartbeat = null;
let heartbeatChangedAt = 0;
let lastInputSequence = null;

function renderInputPreview(status) {
  const canvas = byId("model-input");
  const message = byId("input-message");
  const preview = status.inputPreview;
  if (!status.previewEnabled || !status.available || !preview || preview.ageSeconds > 3) {
    canvas.hidden = true;
    lastInputSequence = null;
    message.textContent = !status.previewEnabled
      ? "Enable Model input preview in Monitoring policy and save."
      : status.previewError || "Waiting for a fresh model-input snapshot.";
    return;
  }
  try {
    if (
      !Number.isInteger(preview.width) ||
      !Number.isInteger(preview.height) ||
      preview.width < 1 ||
      preview.width > 1024 ||
      preview.height < 1 ||
      preview.height > 1024
    ) {
      throw new Error("Invalid preview dimensions");
    }
    if (preview.sequence !== lastInputSequence) {
      const rgb = atob(preview.rgb);
      if (rgb.length !== preview.width * preview.height * 3) {
        throw new Error("Invalid preview byte count");
      }
      canvas.width = preview.width;
      canvas.height = preview.height;
      const drawing = canvas.getContext("2d");
      const image = drawing.createImageData(preview.width, preview.height);
      for (let source = 0, target = 0; source < rgb.length; source += 3, target += 4) {
        image.data[target] = rgb.charCodeAt(source);
        image.data[target + 1] = rgb.charCodeAt(source + 1);
        image.data[target + 2] = rgb.charCodeAt(source + 2);
        image.data[target + 3] = 255;
      }
      drawing.putImageData(image, 0, 0);
      lastInputSequence = preview.sequence;
    }
    canvas.hidden = false;
    message.textContent = `${preview.width} × ${preview.height} RGB model input · snapshot age ${preview.ageSeconds.toFixed(1)} s`;
  } catch (error) {
    canvas.hidden = true;
    lastInputSequence = null;
    message.textContent = `Preview unavailable: ${error.message}`;
  }
}

function showMessage(text) {
  byId("save-message").textContent = text;
}

function populate(settings) {
  byId("enabled").checked = settings.enabled;
  byId("preview-enabled").checked = settings.previewEnabled;
  byId("view-area").value = String(settings.requestedView);
  ["x", "y", "width", "height"].forEach((name, index) => {
    byId(name).value = Math.round(settings.zone[index] * 100);
  });
  byId("confidence").value = settings.confidence;
  byId("activation").value = settings.activationSeconds;
  byId("clear").value = settings.clearSeconds;
}

function sameSettings(actual, expected) {
  return (
    actual.previewEnabled === expected.previewEnabled &&
    actual.requestedView === expected.requestedView &&
    actual.enabled === expected.enabled &&
    actual.confidence === expected.confidence &&
    actual.activationSeconds === expected.activationSeconds &&
    actual.clearSeconds === expected.clearSeconds &&
    actual.zone.every((value, index) => Math.abs(value - expected.zone[index]) < 0.00001)
  );
}

function render(status) {
  const now = performance.now();
  if (status.heartbeatMs !== lastHeartbeat) {
    lastHeartbeat = status.heartbeatMs;
    heartbeatChangedAt = now;
  } else if (now - heartbeatChangedAt > 3000) {
    throw new Error("Processing has stopped responding");
  }
  connected = true;
  current = status;
  const selector = byId("view-area");
  const signature = JSON.stringify([status.views, status.requestedView]);
  if (selector.dataset.views !== signature) {
    const previous = selector.value;
    selector.replaceChildren();
    const choices = [...status.views];
    if (!choices.some((entry) => entry.view === status.requestedView)) {
      choices.push({ view: status.requestedView, unavailable: true });
    }
    for (const entry of choices) {
      const option = document.createElement("option");
      option.value = String(entry.view);
      option.textContent = `View area ${entry.view}${entry.unavailable ? " (unavailable)" : ""}`;
      selector.appendChild(option);
    }
    selector.dataset.views = signature;
    selector.value = dirty ? previous : String(status.requestedView);
  }
  byId("active-view").textContent = status.activeView
    ? `View area ${status.activeView}`
    : "No active view";
  byId("view-message").textContent = status.switchingView
    ? `View area ${status.requestedView} requested. ${status.message}`
    : `Analyzing view area ${status.activeView}. Review the zone after changing the camera's crop.`;
  window.dispatchEvent(
    new CustomEvent("monitor-view", {
      detail: { view: status.activeView, switching: status.switchingView },
    }),
  );
  byId("connection").textContent = "Connected";
  byId("fields").disabled = saving;
  byId("state").textContent = status.state;
  byId("state").dataset.alarm = String(status.alarm);
  byId("status-message").textContent = status.message;
  byId("people").textContent = status.available ? status.people.length : "—";
  byId("duration").textContent = status.available ? `${status.occupiedSeconds.toFixed(1)} s` : "—";
  byId("inference").textContent = status.available ? `${status.inferenceMs.toFixed(0)} ms` : "—";
  byId("raw-detections").textContent = status.available ? status.rawDetections : "—";
  const personScore = status.bestPersonConfidence;
  byId("person-confidence").textContent = !status.available
    ? "—"
    : personScore == null
      ? "No person candidate"
      : `${personScore.toFixed(1)}% (minimum ${status.confidence}%)`;
  byId("detection-hint").textContent = !status.available
    ? "Waiting for a valid inference result."
    : status.people.length > 0
      ? "Person detections passed the confidence and box checks."
      : personScore == null
        ? "The model returned no person candidates. Cars and other classes are not drawn."
        : personScore < status.confidence
          ? "Person candidates are below the confidence threshold. Try a tighter view or temporarily lower the threshold for testing."
          : "Person candidates were returned, but their boxes were rejected. Check the input view and logs.";
  byId("fps").textContent = status.available ? status.fps.toFixed(1) : "—";
  byId("outputs").textContent = [
    status.overlayOk ? "Overlay updated" :
      status.overlayStreams === 0 ? "No video stream has an overlay; start preview with overlays enabled" :
      "Overlay unavailable; previous boxes may remain",
    status.eventOk ? "Event published" : "Event pending or unavailable",
  ].join(" · ");
  renderInputPreview(status);
  if (pending && sameSettings(status, pending)) {
    pending = null;
    dirty = false;
    showMessage(
      status.switchingView
        ? "View saved. Switching the inference stream; review the zone when ready."
        : "Settings applied. Zone timing restarted.",
    );
  } else if (pending && Date.now() > pending.deadline) {
    pending = null;
    showMessage("Running settings not confirmed. Use current values before retrying.");
  }
  if (!dirty && !saving && !pending) populate(status);
}

async function request(url, options = {}) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 5000);
  try {
    const response = await fetch(url, {
      credentials: "same-origin",
      cache: "no-store",
      ...options,
      signal: controller.signal,
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return await response.text();
  } finally {
    clearTimeout(timer);
  }
}

async function poll() {
  try {
    render(JSON.parse(await request("status.cgi")));
  } catch (error) {
    connected = false;
    renderInputPreview({ previewEnabled: current?.previewEnabled, available: false });
    window.dispatchEvent(new CustomEvent("monitor-view", { detail: { view: 0 } }));
    byId("connection").textContent = "Unavailable";
    byId("state").textContent = "Unknown";
    byId("state").dataset.alarm = "false";
    byId("status-message").textContent = error.message;
    byId("fields").disabled = true;
    for (const id of [
      "people",
      "duration",
      "inference",
      "fps",
      "raw-detections",
      "person-confidence",
    ])
      byId(id).textContent = "—";
    byId("outputs").textContent = "No current output confirmation.";
    byId("detection-hint").textContent = "Waiting for a valid inference result.";
  } finally {
    setTimeout(poll, 500);
  }
}

byId("settings").addEventListener("input", () => {
  dirty = true;
  pending = null;
  showMessage("Unsaved changes.");
});
byId("reset").addEventListener("click", () => {
  if (current) {
    dirty = false;
    pending = null;
    populate(current);
    showMessage("Form restored to running settings.");
  }
});
byId("settings").addEventListener("submit", async (event) => {
  event.preventDefault();
  if (!connected || saving || !event.target.reportValidity()) return;
  const zone = ["x", "y", "width", "height"].map((name) => Number(byId(name).value));
  if (
    !zone.every(Number.isInteger) ||
    zone[0] < 0 ||
    zone[1] < 0 ||
    zone[2] < 1 ||
    zone[3] < 1 ||
    zone[0] + zone[2] > 100 ||
    zone[1] + zone[3] > 100
  ) {
    showMessage("The zone must fit inside the image.");
    return;
  }
  const requestedView = Number(byId("view-area").value);
  if (!current.views.some((entry) => entry.view === requestedView)) {
    showMessage("Select an available view area. The view list refreshes automatically.");
    return;
  }
  const settings = {
    requestedView,
    previewEnabled: byId("preview-enabled").checked,
    enabled: byId("enabled").checked,
    zone: zone.map((value) => value / 100),
    confidence: Number(byId("confidence").value),
    activationSeconds: Number(byId("activation").value),
    clearSeconds: Number(byId("clear").value),
  };
  const body = new URLSearchParams({ action: "update" });
  const values = {
    ViewArea: settings.requestedView,
    ModelInputPreview: settings.previewEnabled ? "yes" : "no",
    Enabled: settings.enabled ? "yes" : "no",
    Zone: zone.join(","),
    Confidence: settings.confidence,
    ActivationSeconds: settings.activationSeconds,
    ClearSeconds: settings.clearSeconds,
  };
  for (const [name, value] of Object.entries(values)) {
    body.set(`root.Larod_restricted_zone.${name}`, value);
  }
  saving = true;
  byId("fields").disabled = true;
  showMessage("Saving…");
  try {
    const response = await request("/axis-cgi/param.cgi", { method: "POST", body });
    if (response.trim() !== "OK") throw new Error("Camera rejected settings");
    pending = { ...settings, deadline: Date.now() + 15000 };
    showMessage("Saved. Waiting for running values…");
  } catch (error) {
    showMessage(`Save not confirmed: ${error.message}. Some settings may have changed.`);
  } finally {
    saving = false;
    byId("fields").disabled = !connected;
  }
});
poll();
