/* Camera-hosted MP4 stream; authentication is handled by the browser session. */
"use strict";
(() => {
  const video = document.getElementById("camera-video");
  const start = document.getElementById("video-start");
  const stop = document.getElementById("video-stop");
  const status = document.getElementById("video-status");
  let view = 0;
  let resumeAfterSwitch = false;
  let active = false,
    attempt = 0;
  function release(message) {
    active = false;
    ++attempt;
    video.pause();
    video.removeAttribute("src");
    video.load(); // Release the continuous HTTP stream, including when paused.
    start.disabled = view === 0;
    stop.disabled = true;
    status.textContent = message;
  }
  async function startPreview() {
    if (active || view === 0) return;
    active = true;
    const currentAttempt = ++attempt;
    start.disabled = true;
    stop.disabled = false;
    status.textContent = `Connecting to view area ${view}…`;
    video.muted = true;
    video.src = `/axis-cgi/media.cgi?container=mp4&videocodec=h264&video=1&audio=0&camera=${view}&fps=10&overlays=all`;
    try {
      await video.play();
    } catch (error) {
      if (!active || currentAttempt !== attempt) return;
      if (error.name === "NotAllowedError") {
        status.textContent = "Press Play in the video controls to start playback.";
      } else {
        release(
          "Preview unavailable. Check your camera login, media streaming support, and browser H.264 playback, then try again.",
        );
      }
    }
  }
  start.addEventListener("click", startPreview);
  for (const [event, message] of [
    ["playing", "Playing selected view. The stream may have a short delay."],
    ["waiting", "Buffering camera video…"],
    [
      "stalled",
      "Video data interrupted. If playback does not resume, stop and restart the preview.",
    ],
    [
      "pause",
      "Preview paused. Stop preview to close the stream; start again to return to live video.",
    ],
  ]) {
    video.addEventListener(event, () => {
      if (active) status.textContent = message;
    });
  }
  video.addEventListener("error", () => {
    if (active)
      release(
        "Preview unavailable. Check your camera login, media streaming support, and browser H.264 playback, then try again.",
      );
  });
  video.addEventListener("ended", () => {
    if (active) release("Stream ended. Start preview to reconnect.");
  });
  stop.addEventListener("click", () => {
    resumeAfterSwitch = false;
    release("Preview stopped. Start to view the active view area and its detections.");
  });
  window.addEventListener("monitor-view", (event) => {
    const nextView = Number(event.detail.view);
    if (!Number.isInteger(nextView) || nextView < 0 || nextView > 256) return;
    if (nextView === 0 && !event.detail.switching) resumeAfterSwitch = false;
    if (nextView !== view) {
      const resume = active || resumeAfterSwitch;
      resumeAfterSwitch = resume && Boolean(event.detail.switching);
      view = nextView;
      release(view ? `View area ${view} selected. Start preview.` : "Waiting for an active view.");
      if (view && resume) {
        resumeAfterSwitch = false;
        startPreview();
      }
    }
  });
  window.addEventListener("pagehide", () =>
    release("Preview stopped. Start to view the active view area and its detections."),
  );
})();
