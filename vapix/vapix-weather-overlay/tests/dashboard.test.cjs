const {test} = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');
function setup() {
  const nodes = new Map();
  const get = id => {
    if (!nodes.has(id)) nodes.set(id, {dataset: {}, handlers: {}, value: '', checked: false,
      addEventListener(event, fn) { this.handlers[event] = fn; }, reportValidity() { return true; }});
    return nodes.get(id);
  };
  const context = vm.createContext({document: {getElementById: get}, AbortController, Date, URLSearchParams, TextEncoder,
    setTimeout: () => 1, clearTimeout() {}, fetch: async () => { throw new Error('offline'); }});
  const source = fs.readFileSync(path.join(__dirname, '../app/html/app.js'), 'utf8');
  vm.runInContext(source.replace(/poll\(\);\s*$/, ''), context);
  return {get, render(state) { context.state = state; vm.runInContext('render(state)', context); },
    fetch(fn) { context.fetch = fn; }, poll: () => vm.runInContext('poll()', context)};
}
const state = {enabled: true, locationName: '', latitude: 55.7047, longitude: 13.191, refreshSeconds: 600,
  valid: true, stale: false, temperature: 8.5, wind: 24, condition: 'Rain', nextFetchSeconds: 500,
  fetchedAt: 1700000010, weatherTime: 1700000000, overlaySynced: true,
  overlayText: 'WX-WORKSHOP | weather', weatherError: '', overlayError: '', activity: 'Monitoring weather'};

test('stale weather and VAPIX failure remain distinct from last applied text', () => {
  const ui = setup(); ui.render({...state, stale: true, overlaySynced: false, overlayError: 'HTTP status 500'});
  assert.equal(ui.get('freshness').textContent, 'Stale data');
  assert.equal(ui.get('overlay-state').textContent, 'Pending / error');
  assert.equal(ui.get('preview').textContent, state.overlayText);
  assert.match(ui.get('overlay-error').textContent, /500/);
});
test('a location without weather never displays the old temperature', () => {
  const ui = setup(); ui.render(state); ui.render({...state, latitude: 0, longitude: 0, valid: false});
  assert.equal(ui.get('temperature').textContent, '—');
  assert.equal(ui.get('condition').textContent, 'Weather unavailable');
});
test('form edits survive polling and save waits for actual running values', async () => {
  const ui = setup(); ui.render(state); ui.get('latitude').value = '59.3'; ui.get('settings').handlers.input();
  ui.render(state); assert.equal(ui.get('latitude').value, '59.3');
  ui.fetch(async (url, options) => {
    assert.equal(url, '/axis-cgi/param.cgi');
    assert.equal(options.body.get('root.Vapix_weather_overlay.Latitude'), '59.3');
    return {ok: true, text: async () => 'OK'};
  });
  await ui.get('settings').handlers.submit({preventDefault() {}, target: ui.get('settings')});
  assert.match(ui.get('save-message').textContent, /Waiting/);
  ui.render({...state, latitude: 59.3}); assert.match(ui.get('save-message').textContent, /Settings applied/);
});
test('HTTP 200 parameter errors do not count as successful saves', async () => {
  const ui = setup(); ui.render(state);
  ui.fetch(async () => ({ok: true, text: async () => '# Error'}));
  await ui.get('settings').handlers.submit({preventDefault() {}, target: ui.get('settings')});
  assert.match(ui.get('save-message').textContent, /Could not confirm/);
});
test('disconnect disables editing and disabled status confirms removal', async () => {
  const ui = setup(); ui.render(state); await ui.poll();
  assert.equal(ui.get('fields').disabled, true); assert.equal(ui.get('offline').hidden, false);
  ui.render({...state, enabled: false, overlayText: ''});
  assert.equal(ui.get('fields').disabled, false); assert.equal(ui.get('overlay-state').textContent, 'Removed');
  assert.equal(ui.get('next').textContent, 'Disabled');
});

test('location label survives polling and saves only after the running name matches', async () => {
  const ui = setup(); ui.render(state);
  ui.get('location-name').value = 'Malmö, Sweden'; ui.get('settings').handlers.input();
  ui.render(state); assert.equal(ui.get('location-name').value, 'Malmö, Sweden');
  ui.fetch(async (url, options) => {
    assert.equal(options.body.get('root.Vapix_weather_overlay.LocationName'), 'Malmö, Sweden');
    return {ok: true, text: async () => 'OK'};
  });
  await ui.get('settings').handlers.submit({preventDefault() {}, target: ui.get('settings')});
  ui.render(state); assert.match(ui.get('save-message').textContent, /Waiting/);
  ui.render({...state, locationName: 'Malmö, Sweden'});
  assert.match(ui.get('save-message').textContent, /Settings applied/);
  assert.equal(ui.get('location').textContent, 'Malmö, Sweden · 55.7047, 13.1910');
  ui.render(state); assert.equal(ui.get('location').textContent, '55.7047, 13.1910');
});
test('invalid names are rejected before saving, including multibyte overflow', async () => {
  const ui = setup(); ui.render(state);
  ui.fetch(async () => { assert.fail('Invalid name must not be submitted'); });
  for (const name of ['å'.repeat(33), 'North|South', 'North\nSouth']) {
    ui.get('location-name').value = name;
    await ui.get('settings').handlers.submit({preventDefault() {}, target: ui.get('settings')});
    assert.match(ui.get('save-message').textContent, /Location name must/);
  }
});
