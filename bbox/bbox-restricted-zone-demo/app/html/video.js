/* Camera-hosted MP4 stream; authentication is handled by the browser session. */
'use strict';
(() => {
  const video = document.getElementById('camera-video');
  const start = document.getElementById('video-start');
  const stop = document.getElementById('video-stop');
  const status = document.getElementById('video-status');
  const stream = '/axis-cgi/media.cgi?container=mp4&videocodec=h264&video=1&audio=0&camera=1&fps=10';
  let active = false, attempt = 0;
  function release(message) {
    active = false;
    ++attempt;
    video.pause();
    video.removeAttribute('src');
    video.load(); // Release the continuous HTTP stream, including when paused.
    start.disabled = false;
    stop.disabled = true;
    status.textContent = message;
  }
  start.addEventListener('click', async () => {
    if (active) return;
    active = true;
    const currentAttempt = ++attempt;
    start.disabled = true;
    stop.disabled = false;
    status.textContent = 'Connecting to camera 1…';
    video.muted = true;
    video.src = stream;
    try {
      await video.play();
    } catch (error) {
      if (!active || currentAttempt !== attempt) return;
      if (error.name === 'NotAllowedError') {
        status.textContent = 'Press Play in the video controls to start playback.';
      } else {
        release('Preview unavailable. Check your camera login, media streaming support, and browser H.264 playback, then try again.');
      }
    }
  });
  for (const [event, message] of [
    ['playing', 'Playing camera 1. The stream may have a short delay.'],
    ['waiting', 'Buffering camera video…'],
    ['stalled', 'Video data interrupted. If playback does not resume, stop and restart the preview.'],
    ['pause', 'Preview paused. Stop preview to close the stream; start again to return to live video.']
  ]) {
    video.addEventListener(event, () => { if (active) status.textContent = message; });
  }
  video.addEventListener('error', () => {
    if (active) release('Preview unavailable. Check your camera login, media streaming support, and browser H.264 playback, then try again.');
  });
  video.addEventListener('ended', () => { if (active) release('Stream ended. Start preview to reconnect.'); });
  stop.addEventListener('click', () => release('Preview stopped. Start to view camera 1 and its simulated boxes.'));
  window.addEventListener('pagehide', () => release('Preview stopped. Start to view camera 1 and its simulated boxes.'));
})();
