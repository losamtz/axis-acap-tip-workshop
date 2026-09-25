'use strict';
const byId = id => document.getElementById(id);
let current = null, dirty = false, saving = false, pending = null;
const message = text => { byId('save-message').textContent = text; };
const stamp = seconds => seconds ? new Date(seconds * 1000).toLocaleString() : '—';
function populate(state) {
  byId('enabled').checked = state.enabled;
  byId('latitude').value = state.latitude;
  byId('longitude').value = state.longitude;
  byId('refresh').value = state.refreshSeconds;
}
function matches(state, settings) {
  return ['enabled', 'latitude', 'longitude', 'refreshSeconds'].every(key => state[key] === settings[key]);
}
function render(state) {
  current = state;
  byId('fields').disabled = saving;
  byId('connection').textContent = '● Connected'; byId('connection').dataset.state = 'online';
  byId('offline').hidden = true;
  byId('location').textContent = `${state.latitude.toFixed(4)}, ${state.longitude.toFixed(4)}`;
  byId('temperature').textContent = state.valid ? state.temperature.toFixed(1) : '—';
  byId('condition').textContent = state.valid ? state.condition : 'Weather unavailable';
  byId('wind').textContent = state.valid ? `${Math.round(state.wind)} km/h` : '—';
  byId('next').textContent = state.enabled ? `${state.nextFetchSeconds}s` : 'Disabled';
  byId('weather-time').textContent = `Weather timestamp: ${state.valid ? stamp(state.weatherTime) : '—'}`;
  byId('updated').textContent = `Last successful fetch: ${stamp(state.fetchedAt)}`;
  byId('freshness').textContent = !state.enabled ? 'Disabled' : !state.valid ? 'No data' : state.stale ? 'Stale data' : 'Current data';
  byId('freshness').className = `badge ${state.valid && !state.stale && state.enabled ? 'fresh' : 'stale'}`;
  byId('weather-error').textContent = state.weatherError || (!state.enabled ? 'Weather requests paused.' : state.valid ? 'Last weather request succeeded.' : 'Waiting for weather.');
  byId('weather-error').className = `api-state ${state.weatherError ? 'error' : ''}`;
  byId('overlay-state').textContent = !state.overlaySynced ? 'Pending / error' : state.enabled ? 'Applied' : 'Removed';
  byId('preview').textContent = state.overlayText || (state.overlaySynced && !state.enabled ? 'Weather overlay removed.' : 'No overlay confirmed yet.');
  byId('overlay-error').textContent = state.overlayError || (state.overlaySynced ? 'Last VAPIX operation succeeded.' : 'Waiting for VAPIX confirmation.');
  byId('overlay-error').className = `api-state ${state.overlayError ? 'error' : ''}`;
  byId('activity').textContent = state.activity;
  if (pending && matches(state, pending)) {
    pending = null; dirty = false; message('Settings applied. Weather and overlay requests may still be in progress.');
  } else if (pending && Date.now() > pending.deadline) {
    pending = null; message('Save accepted; running values are not confirmed yet. Check current values before retrying.');
  }
  if (!dirty && !pending && !saving) populate(state);
}
async function request(url, options = {}) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 5000);
  try {
    const response = await fetch(url, {credentials: 'same-origin', cache: 'no-store', ...options, signal: controller.signal});
    const text = await response.text();
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return text;
  } finally { clearTimeout(timer); }
}
async function poll() {
  try {
    const state = JSON.parse(await request('status.cgi'));
    if (typeof state.enabled !== 'boolean' || !Number.isFinite(state.latitude) || !Number.isFinite(state.longitude)) throw new Error('Invalid status');
    render(state);
  } catch (error) {
    byId('connection').textContent = '● Disconnected'; byId('connection').dataset.state = 'offline';
    byId('offline').hidden = false; byId('fields').disabled = true;
  } finally { setTimeout(poll, 1000); }
}
byId('settings').addEventListener('input', () => { dirty = true; pending = null; message('Unsaved changes.'); });
byId('reset').addEventListener('click', () => {
  if (!current) return;
  dirty = false; pending = null; populate(current); message('Form restored to current running values.');
});
byId('settings').addEventListener('submit', async event => {
  event.preventDefault();
  if (!current || saving || !event.target.reportValidity()) return;
  const settings = {enabled: byId('enabled').checked, latitude: Number(byId('latitude').value),
    longitude: Number(byId('longitude').value), refreshSeconds: Number(byId('refresh').value)};
  const body = new URLSearchParams({action: 'update'});
  const scope = 'root.Vapix_weather_overlay.';
  body.set(scope + 'Enabled', settings.enabled ? 'yes' : 'no');
  body.set(scope + 'Latitude', settings.latitude); body.set(scope + 'Longitude', settings.longitude);
  body.set(scope + 'RefreshSeconds', settings.refreshSeconds);
  saving = true; byId('fields').disabled = true; message('Saving…');
  try {
    const text = await request('/axis-cgi/param.cgi', {method: 'POST', body});
    if (text.trim() !== 'OK') throw new Error(text.trim().slice(0, 180) || 'Empty response');
    pending = {...settings, deadline: Date.now() + 45000}; message('Saved. Waiting for the application…');
  } catch (error) {
    pending = null; message(`Could not confirm save: ${error.message}. Some values may have changed; check running settings.`);
  } finally { saving = false; byId('fields').disabled = byId('connection').dataset.state !== 'online'; }
});
poll();
