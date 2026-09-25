// Dependency-free interaction checks. Run: node --test tests/dashboard.test.cjs
const {test} = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');

function setup() {
  const nodes = new Map();
  function node() {
    return {dataset: {}, value: '', checked: false, disabled: false, hidden: false,
      textContent: '', children: [], handlers: {},
      addEventListener(event, fn) { this.handlers[event] = fn; },
      replaceChildren() { this.children = []; },
      append(...children) { this.children.push(...children); },
      reportValidity() { return true; }};
  }
  const get = id => { if (!nodes.has(id)) nodes.set(id, node()); return nodes.get(id); };
  const requests = [];
  let answer = 'OK';
  let fail = false;
  const context = vm.createContext({
    document: {getElementById: get, createElement: node},
    URLSearchParams, AbortController, Date,
    setTimeout: () => 1, clearTimeout() {},
    fetch: async (url, options) => {
      requests.push({url, options});
      if (fail) throw new Error('offline');
      return {ok: true, text: async () => answer};
    }
  });
  // Drive polling explicitly to make the test deterministic.
  const source = fs.readFileSync(path.join(__dirname, '../app/html/app.js'), 'utf8');
  vm.runInContext(source.replace(/poll\(\);\s*$/, ''), context);
  return {get, requests, run: code => vm.runInContext(code, context),
    render(state) { context.state = state; vm.runInContext('render(state)', context); },
    answer(value) { answer = value; }, fail() { fail = true; }};
}
const state = {enabled: true, occupied: true, tracking: true, maxOccupancySeconds: 30,
  alertCooldownSeconds: 20, elapsedSeconds: 12, nextAlertSeconds: 18,
  phaseRemainingSeconds: 78, totalAlerts: 0, alerts: []};

test('live updates preserve edits and current-values button restores applied settings', () => {
  const ui = setup(); ui.render(state);
  assert.equal(ui.get('elapsed').textContent, 12);
  ui.get('threshold').value = '15';
  ui.get('settings').handlers.input();
  ui.render({...state, elapsedSeconds: 13});
  assert.equal(ui.get('threshold').value, '15');
  assert.equal(ui.get('elapsed').textContent, 13);
  ui.get('reset').handlers.click();
  assert.equal(ui.get('threshold').value, 30);
});

test('save uses Parameter API and waits for C status confirmation', async () => {
  const ui = setup(); ui.render(state);
  ui.get('threshold').value = '10'; ui.get('settings').handlers.input();
  await ui.get('settings').handlers.submit({preventDefault() {}, target: ui.get('settings')});
  const req = ui.requests[0];
  assert.equal(req.url, '/axis-cgi/param.cgi');
  assert.equal(req.options.method, 'POST');
  assert.equal(req.options.body.get('root.Parameter_loading_bay.MaxOccupancySeconds'), '10');
  assert.match(ui.get('save-message').textContent, /Waiting/);
  ui.render({...state, maxOccupancySeconds: 10});
  assert.match(ui.get('save-message').textContent, /saved and applied/);
});

test('HTTP 200 with VAPIX error is not treated as a successful save', async () => {
  const ui = setup(); ui.render(state); ui.answer('# Error: invalid parameter');
  await ui.get('settings').handlers.submit({preventDefault() {}, target: ui.get('settings')});
  assert.match(ui.get('save-message').textContent, /Could not confirm/);
});

test('disconnect disables settings and reconnect resumes status', async () => {
  const ui = setup(); ui.render(state); ui.fail(); await ui.run('poll()');
  assert.equal(ui.get('offline').hidden, false);
  assert.equal(ui.get('fields').disabled, true);
  ui.render({...state, enabled: false});
  assert.equal(ui.get('offline').hidden, true);
  assert.equal(ui.get('fields').disabled, false);
  assert.equal(ui.get('monitoring').textContent, 'Monitoring disabled');
});

test('actual alert entries render and history clears after restart', () => {
  const ui = setup();
  ui.render({...state, totalAlerts: 1, alerts: [{timestampMs: 1700000000000,
    elapsedSeconds: 30, limitSeconds: 30, cooldownSeconds: 20}]});
  assert.equal(ui.get('alerts').children.length, 1);
  assert.equal(ui.get('empty').hidden, true);
  ui.render(state);
  assert.equal(ui.get('alerts').children.length, 0);
  assert.equal(ui.get('empty').hidden, false);
});
