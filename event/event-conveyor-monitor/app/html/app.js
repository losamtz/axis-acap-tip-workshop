'use strict';
const byId = id => document.getElementById(id);
let lastHistory = '';
let current = null;
let dirty = false;
let saving = false;
let pending = null;
function message(text) { byId('save-message').textContent = text; }
function populate(state) {
  byId('enabled').checked = state.enabled;
  byId('threshold').value = state.thresholdSeconds;
}
function render(state) {
  current = state;
  byId('fields').disabled = saving;
  if (pending && state.enabled === pending.enabled && state.thresholdSeconds === pending.thresholdSeconds) {
    pending = null; dirty = false;
    message('Settings saved and applied by the application.');
  } else if (pending && Date.now() > pending.deadline) {
    pending = null;
    message('Save accepted, but running values do not match yet. Check current values before retrying.');
  }
  if (!dirty && !pending && !saving) populate(state);
  byId('connection').textContent = '● Connected';
  byId('connection').dataset.state = 'online';
  byId('offline').hidden = true;
  byId('ready').textContent = state.ready ? 'Events declared' : 'Declaring events…';
  byId('conveyor').dataset.state = !state.ready ? 'unknown' : state.jam ? 'jam' : state.blocked ? 'blocked' : 'flowing';
  byId('condition').textContent = !state.ready ? 'Waiting for declarations' : state.jam ? 'Jam active' : state.blocked ? 'Checkpoint blocked' : 'Packages flowing';
  byId('blocked').textContent = state.blockedSeconds;
  byId('progress').value = Math.min(state.monitoredSeconds, state.thresholdSeconds);
  byId('progress').max = state.thresholdSeconds;
  byId('progress-label').textContent = `Monitored: ${state.monitoredSeconds}s · threshold: ${state.thresholdSeconds}s`;
  byId('phase').textContent = state.ready ? `Cycle ${state.phaseSeconds} / 60s` : '—';
  byId('policy').textContent = !state.enabled ? 'Jam monitoring is disabled. Package-passage events continue.' :
    state.jam ? 'JamActive=true. It clears on recovery, disabling monitoring, or raising the threshold above monitored time.' :
    state.blocked ? `Jam activates after ${Math.max(state.thresholdSeconds - state.monitoredSeconds, 0)} more monitored seconds, if the blockage continues.` :
    'JamActive=false. Each simulated passage publishes a separate PackagePassed event.';
  byId('received-jam').textContent = state.receivedJam === null ? 'Waiting for an event' : state.receivedJam ? 'Jam active · true' : 'No jam · false';
  byId('received-state').dataset.state = state.receivedJam === true ? 'jam' : 'clear';
  byId('packages').textContent = state.packageCount;
  byId('published').textContent = state.published;
  byId('received').textContent = state.received;
  byId('errors').textContent = state.errors ? `${state.errors} Event API error(s). Check the application log.` : 'No Event API errors reported.';
  const phase = state.phaseSeconds < 20 ? 'flow' : state.phaseSeconds < 40 ? 'block' : 'clear';
  for (const name of ['flow', 'block', 'clear']) byId(`phase-${name}`).className = state.ready && name === phase ? 'active' : '';
  const key = JSON.stringify(state.history);
  if (key !== lastHistory) {
    lastHistory = key;
    byId('history').replaceChildren();
    for (const item of state.history) {
      const row = document.createElement('li');
      const time = document.createElement('time');
      const date = new Date(item.timestampMs);
      time.dateTime = date.toISOString(); time.textContent = date.toLocaleTimeString(); time.title = date.toLocaleString();
      const direction = document.createElement('span'); direction.className = 'direction';
      direction.dataset.direction = item.direction; direction.textContent = item.direction;
      const details = document.createElement('div');
      const title = document.createElement('strong');
      title.textContent = item.topic === 'JamActive' ? `JamActive = ${item.active}` : 'PackagePassed';
      const detail = document.createElement('span');
      detail.textContent = `ConveyorId=1 · PackageCount=${item.packageCount}` +
        (item.topic === 'JamActive' ? ` · BlockedSeconds=${item.blockedSeconds}` : '');
      details.append(title, detail); row.append(time, direction, details); byId('history').append(row);
    }
  }
  byId('empty').hidden = state.history.length > 0;
}
async function poll() {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 5000);
  try {
    const response = await fetch('status.cgi', {credentials: 'same-origin', cache: 'no-store', signal: controller.signal});
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const state = await response.json();
    if (typeof state.ready !== 'boolean' || typeof state.jam !== 'boolean' ||
        typeof state.enabled !== 'boolean' || !Number.isInteger(state.thresholdSeconds) ||
        !Array.isArray(state.history)) throw new Error('Invalid status');
    render(state);
  } catch (error) {
    byId('connection').textContent = '● Disconnected';
    byId('connection').dataset.state = 'offline';
    byId('offline').hidden = false;
    byId('conveyor').dataset.state = 'offline';
    byId('fields').disabled = true;
  } finally {
    clearTimeout(timeout);
    setTimeout(poll, 1000);
  }
}
byId('settings').addEventListener('input', () => {
  dirty = true; pending = null;
  message('Unsaved changes. Live status uses the running settings.');
});
byId('reset').addEventListener('click', () => {
  if (!current) return;
  dirty = false; pending = null; populate(current);
  message('Form restored to current running values.');
});
byId('settings').addEventListener('submit', async event => {
  event.preventDefault();
  if (saving || !current || !event.target.reportValidity()) return;
  const settings = {enabled: byId('enabled').checked, thresholdSeconds: Number(byId('threshold').value)};
  const body = new URLSearchParams({action: 'update'});
  body.set('root.Event_conveyor_monitor.Enabled', settings.enabled ? 'yes' : 'no');
  body.set('root.Event_conveyor_monitor.JamThresholdSeconds', settings.thresholdSeconds);
  saving = true; byId('fields').disabled = true; message('Saving settings…');
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 5000);
  try {
    const response = await fetch('/axis-cgi/param.cgi', {method: 'POST', body,
      credentials: 'same-origin', cache: 'no-store', signal: controller.signal});
    const text = await response.text();
    if (!response.ok || text.trim() !== 'OK') throw new Error(text.trim().slice(0, 180) || `HTTP ${response.status}`);
    pending = {...settings, deadline: Date.now() + 10000};
    message('Saved. Waiting for confirmation from the application…');
  } catch (error) {
    pending = null;
    message(`Could not confirm save: ${error.message}. Some values may have changed; check live status.`);
  } finally {
    clearTimeout(timeout); saving = false;
    byId('fields').disabled = byId('connection').dataset.state !== 'online';
  }
});
poll();
