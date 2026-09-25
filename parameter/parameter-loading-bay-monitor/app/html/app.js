'use strict';
const byId = (id) => document.getElementById(id);
let current = null;
let dirty = false;
let saving = false;
let pending = null;
let lastAlerts = '';

function message(text) { byId('save-message').textContent = text; }
function populate(state) {
  byId('enabled').checked = state.enabled;
  byId('threshold').value = state.maxOccupancySeconds;
  byId('cooldown').value = state.alertCooldownSeconds;
}
function matches(state, settings) {
  return state.enabled === settings.enabled &&
    state.maxOccupancySeconds === settings.maxOccupancySeconds &&
    state.alertCooldownSeconds === settings.alertCooldownSeconds;
}
function render(state) {
  const first = current === null;
  current = state;
  byId('fields').disabled = saving;
  byId('connection').textContent = '● Connected';
  byId('connection').dataset.state = 'online';
  byId('offline').hidden = true;
  byId('monitoring').textContent = state.enabled ? 'Monitoring enabled' : 'Monitoring disabled';
  byId('bay-visual').dataset.state = state.occupied ? 'occupied' : 'free';
  byId('occupancy').textContent = state.occupied ? 'Bay occupied' : 'Bay free';
  byId('phase').textContent = `${state.occupied ? 'Free' : 'Occupied'} in ${state.phaseRemainingSeconds}s`;
  byId('elapsed').textContent = state.elapsedSeconds;
  byId('progress').max = state.maxOccupancySeconds;
  byId('progress').value = Math.min(state.elapsedSeconds, state.maxOccupancySeconds);
  byId('limit').textContent = `Limit: ${state.maxOccupancySeconds}s`;
  byId('next-alert').textContent = state.nextAlertSeconds === null ? 'Next alert: inactive' :
    `Eligible in ${state.nextAlertSeconds}s`;
  byId('timing-note').textContent = !state.enabled ? 'Monitoring is off. The simulator continues; enabling starts a fresh timer.' :
    !state.occupied ? 'The bay is free. Occupancy timing and cooldown have reset.' :
    'Alert eligibility combines the occupancy limit and cooldown, while the bay stays occupied.';
  byId('alert-count').textContent = `${state.totalAlerts} this session`;
  const alertsKey = JSON.stringify(state.alerts);
  if (alertsKey !== lastAlerts) {
    lastAlerts = alertsKey;
    byId('alerts').replaceChildren();
    for (const alert of state.alerts) {
      const row = document.createElement('li');
      const time = document.createElement('time');
      const date = new Date(alert.timestampMs);
      time.dateTime = date.toISOString();
      time.textContent = date.toLocaleTimeString();
      time.title = date.toLocaleString();
      const details = document.createElement('div');
      const title = document.createElement('strong');
      title.textContent = `Simulated alert · occupied for ${alert.elapsedSeconds}s`;
      const meta = document.createElement('span');
      meta.textContent = `Limit ${alert.limitSeconds}s · cooldown ${alert.cooldownSeconds}s`;
      details.append(title, meta);
      row.append(time, details);
      byId('alerts').append(row);
    }
  }
  byId('empty').hidden = state.alerts.length > 0;
  byId('empty').textContent = 'No alerts yet. Alerts appear here when monitored occupancy reaches the limit.';
  if (pending && matches(state, pending)) {
    pending = null;
    dirty = false;
    message('Settings saved and applied by the application.');
  } else if (pending && Date.now() > pending.deadline) {
    pending = null;
    message('Save was accepted, but the running values do not match yet. Check the current values before retrying.');
  }
  if (first || (!dirty && !pending && !saving)) populate(state);
  if (first) message('Ready. Adjust settings and save to see the effect.');
}
async function fetchWithTimeout(url, options = {}) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 5000);
  try {
    const response = await fetch(url, {credentials: 'same-origin', cache: 'no-store', ...options, signal: controller.signal});
    const text = await response.text();
    if (!response.ok) throw new Error(`Camera returned HTTP ${response.status}`);
    return text;
  } finally { clearTimeout(timer); }
}
async function poll() {
  try {
    const state = JSON.parse(await fetchWithTimeout('status.cgi'));
    if (typeof state.enabled !== 'boolean' || typeof state.occupied !== 'boolean' ||
        !Number.isInteger(state.maxOccupancySeconds) || state.maxOccupancySeconds < 1 ||
        !Number.isInteger(state.elapsedSeconds) || !Array.isArray(state.alerts)) {
      throw new Error('Invalid status response');
    }
    render(state);
  } catch (error) {
    byId('connection').textContent = '● Disconnected';
    byId('connection').dataset.state = 'offline';
    byId('offline').hidden = false;
    byId('fields').disabled = true;
  } finally { setTimeout(poll, 1000); }
}
byId('settings').addEventListener('input', () => {
  dirty = true;
  pending = null;
  message('Unsaved changes. Live status still shows the current running settings.');
});
byId('reset').addEventListener('click', () => {
  if (!current) return;
  dirty = false;
  pending = null;
  populate(current);
  message('Form restored to the current running settings.');
});
byId('settings').addEventListener('submit', async (event) => {
  event.preventDefault();
  if (saving || !current || !event.target.reportValidity()) return;
  const settings = {
    enabled: byId('enabled').checked,
    maxOccupancySeconds: Number(byId('threshold').value),
    alertCooldownSeconds: Number(byId('cooldown').value)
  };
  const body = new URLSearchParams({action: 'update'});
  const scope = 'root.Parameter_loading_bay.';
  body.set(scope + 'Enabled', settings.enabled ? 'yes' : 'no');
  body.set(scope + 'MaxOccupancySeconds', settings.maxOccupancySeconds);
  body.set(scope + 'AlertCooldownSeconds', settings.alertCooldownSeconds);
  saving = true;
  byId('fields').disabled = true;
  message('Saving settings…');
  try {
    const text = await fetchWithTimeout('/axis-cgi/param.cgi', {method: 'POST', body});
    // VAPIX can report parameter errors in a successful HTTP response.
    if (text.trim() !== 'OK') throw new Error(text.trim().slice(0, 180) || 'Empty camera response');
    pending = {...settings, deadline: Date.now() + 10000};
    message('Saved. Waiting for the application to confirm the values…');
  } catch (error) {
    pending = null;
    message(`Could not confirm the save: ${error.message}. Some values may have changed; check the live status.`);
  } finally {
    saving = false;
    byId('fields').disabled = byId('connection').dataset.state !== 'online';
  }
});
poll();
