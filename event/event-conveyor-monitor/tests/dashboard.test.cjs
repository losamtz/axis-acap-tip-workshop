const {test} = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');
function setup() {
  const nodes = new Map();
  const node = () => ({dataset: {}, textContent: '', className: '', children: [], handlers: {}, value: '', checked: false,
    addEventListener(event, fn) { this.handlers[event] = fn; },
    reportValidity() { return true; },
    append(...children) { this.children.push(...children); },
    replaceChildren() { this.children = []; }});
  const get = id => { if (!nodes.has(id)) nodes.set(id, node()); return nodes.get(id); };
  const context = vm.createContext({document: {getElementById: get, createElement: node},
    AbortController, Date, URLSearchParams, setTimeout: () => 1, clearTimeout() {},
    fetch: async () => { throw new Error('disconnected'); }});
  const source = fs.readFileSync(path.join(__dirname, '../app/html/app.js'), 'utf8');
  vm.runInContext(source.replace(/poll\(\);\s*$/, ''), context);
  return {get, render(state) { context.state = state; vm.runInContext('render(state)', context); },
    setFetch(fn) { context.fetch = fn; },
    poll: () => vm.runInContext('poll()', context)};
}
const base = {enabled: true, monitoredSeconds: 0, ready: true, blocked: false, jam: false, blockedSeconds: 0,
  phaseSeconds: 0, thresholdSeconds: 10, receivedJam: null, packageCount: 0,
  published: 1, received: 0, errors: 0, history: []};

test('published jam does not fabricate a received state', () => {
  const ui = setup(); ui.render({...base, blocked: true, jam: true, blockedSeconds: 10, phaseSeconds: 30});
  assert.equal(ui.get('condition').textContent, 'Jam active');
  assert.equal(ui.get('received-jam').textContent, 'Waiting for an event');
  assert.equal(ui.get('phase-block').className, 'active');
  ui.render({...base, receivedJam: true});
  assert.equal(ui.get('received-jam').textContent, 'Jam active · true');
});

test('publication and subscription activity retain distinct directions and payloads', () => {
  const ui = setup();
  ui.render({...base, history: [
    {timestampMs: 1700000000000, direction: 'Received', topic: 'JamActive', active: true, blockedSeconds: 10, packageCount: 4},
    {timestampMs: 1700000000000, direction: 'Published', topic: 'PackagePassed', active: false, blockedSeconds: 0, packageCount: 4}]});
  const rows = ui.get('history').children;
  assert.equal(rows.length, 2);
  assert.equal(rows[0].children[1].textContent, 'Received');
  assert.equal(rows[0].children[2].children[0].textContent, 'JamActive = true');
  assert.equal(rows[1].children[1].textContent, 'Published');
  assert.equal(rows[1].children[2].children[0].textContent, 'PackagePassed');
  ui.render(base);
  assert.equal(ui.get('history').children.length, 0);
  assert.equal(ui.get('empty').hidden, false);
});

test('disconnect freezes illustration and reconnect restores current condition', async () => {
  const ui = setup(); ui.render(base); await ui.poll();
  assert.equal(ui.get('offline').hidden, false);
  assert.equal(ui.get('conveyor').dataset.state, 'offline');
  ui.render({...base, blocked: true});
  assert.equal(ui.get('offline').hidden, true);
  assert.equal(ui.get('conveyor').dataset.state, 'blocked');
});

test('declaration readiness and Event API errors are visible', () => {
  const ui = setup(); ui.render({...base, ready: false, errors: 2});
  assert.equal(ui.get('condition').textContent, 'Waiting for declarations');
  assert.equal(ui.get('conveyor').dataset.state, 'unknown');
  assert.match(ui.get('errors').textContent, /2 Event API error/);
});


test('edits survive polling, and save waits for matching applied parameters', async () => {
  const ui = setup(); ui.render(base);
  ui.get('threshold').value = '5'; ui.get('settings').handlers.input();
  ui.render({...base, phaseSeconds: 2});
  assert.equal(ui.get('threshold').value, '5');
  ui.setFetch(async (url, options) => {
    assert.equal(url, '/axis-cgi/param.cgi');
    assert.equal(options.body.get('root.Event_conveyor_monitor.JamThresholdSeconds'), '5');
    assert.equal(options.body.get('root.Event_conveyor_monitor.Enabled'), 'yes');
    return {ok: true, text: async () => 'OK'};
  });
  await ui.get('settings').handlers.submit({preventDefault() {}, target: ui.get('settings')});
  assert.match(ui.get('save-message').textContent, /Waiting/);
  ui.render({...base, thresholdSeconds: 5});
  assert.match(ui.get('save-message').textContent, /saved and applied/);
});

test('VAPIX error response does not claim success; reset uses latest running values', async () => {
  const ui = setup(); ui.render(base);
  ui.setFetch(async () => ({ok: true, text: async () => '# Error'}));
  await ui.get('settings').handlers.submit({preventDefault() {}, target: ui.get('settings')});
  assert.match(ui.get('save-message').textContent, /Could not confirm/);
  ui.render({...base, enabled: false, thresholdSeconds: 25});
  ui.get('reset').handlers.click();
  assert.equal(ui.get('enabled').checked, false);
  assert.equal(ui.get('threshold').value, 25);
  assert.match(ui.get('policy').textContent, /disabled/);
});
