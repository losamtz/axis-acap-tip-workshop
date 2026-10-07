# Web monitoring policy

Configure a warehouse monitoring policy through a camera-hosted JSON API, verify
what was saved, and evaluate hypothetical occupancy conditions. This example
extends the teaching ideas of `web-parameter` while keeping that minimal example
unchanged. The page follows the workshop's AXIS-style design.

There is no live sensor, background monitoring loop, video analysis or alarm
transmission. **Test policy** evaluates supplied numbers using saved settings.

## Build and try

From this directory:

```bash
docker build --tag web-monitoring-policy --build-arg ARCH=aarch64 .
mkdir -p build
container_id=$(docker create web-monitoring-policy)
docker cp "$container_id":/opt/app/Web_Monitoring_Policy_1_0_0_aarch64.eap ./build/
docker rm "$container_id"
```

Sign the EAP if required by the camera, install it, and start the app. Open
Settings or `/local/web_monitoring_policy/index.html` on the camera. This SDK
12.10.0 aarch64 build requires AXIS OS 12.10.68 or later within the package's
compatibility range. For a different supported architecture, change ARCH and the
filename. Do not change the architecture based only on the laptop's CPU.

1. Name the site and save a 30-second occupancy limit and 20-second cooldown.
2. Check **Confirmed saved settings** for the backend's readback.
3. Test 29 seconds occupied with no previous alert: **Within limit**.
4. Test 30 seconds occupied with no previous alert: **Overdue**, alert eligible.
5. Test 30 seconds occupied and 19 seconds since the last alert: **Overdue**, suppressed by cooldown.
6. Change the last-alert age to 20: eligible again. No actual alert is sent.
7. Disable monitoring, save, and repeat: **Disabled**.
8. Restart the app and use Refresh to verify the settings persisted. A camera
   reboot is the stronger check that values were written to disk.

Editing the form does not change saved settings or the policy used for testing.
Refresh preserves edits. Restore current values resets the form to the last
confirmed snapshot; refresh first if another administrator may have changed it.
Multiple administrators use last-write-wins; this example has no revision lock.

## Learning flow

```mermaid
sequenceDiagram
    participant Page as Browser form
    participant API as FastCGI JSON API
    participant Param as AXParameter
    Page->>API: POST settings JSON
    API->>API: Validate every field before any writes
    API->>Param: Set each value with do_sync=TRUE
    API->>Param: Read all settings back
    API-->>Page: Confirmed values or explicit save failure
    Page->>API: POST hypothetical occupied duration and alert age
    API->>Param: Read saved settings
    API-->>Page: Decision and exact settings used; no writes
```

This example teaches application-owned HTTP routing, JSON validation, persistence,
readback and error semantics. The loading-bay monitor instead teaches parameters
changing a continuously running simulation. VDO teaches frame acquisition and
analysis; BBox teaches drawing graphics. None of those APIs is needed here.

## Configuration

| JSON field | AXParameter name | Default | Allowed values |
| --- | --- | --- | --- |
| siteName | SiteName | Warehouse bay 1 | Nonblank printable text, up to 64 UTF-8 bytes |
| enabled | Enabled | true / yes | JSON boolean; stored as yes/no |
| occupancyLimitSeconds | OccupancyLimitSeconds | 30 | Integer 1–86400 |
| alertCooldownSeconds | AlertCooldownSeconds | 20 | Integer 1–86400 |

Parameter group: `root.Web_monitoring_policy`. The API accepts only these named
settings, not arbitrary camera parameter paths. GET and tests read parameters
on each request so externally updated values are reflected. Invalid values
written outside this API produce a read error; correct them through the settings
POST or Parameter API. Site names and form errors are rendered as text.

## API

All paths are relative to `/local/web_monitoring_policy/`. The camera web server
requires admin access for both endpoints. POST additionally requires
`Content-Type: application/json` and `X-Policy-Request: 1`. No CORS headers are
emitted; the custom header prevents ordinary cross-origin HTML form submissions.

| Method | Endpoint | Behavior |
| --- | --- | --- |
| GET | settings.cgi | Read and validate saved parameters |
| POST | settings.cgi | Validate and save all four fields, then read back |
| POST | test.cgi | Evaluate hypothetical inputs using saved settings |

Save body (all fields required):

```json
{"siteName":"Warehouse bay 1","enabled":true,"occupancyLimitSeconds":30,"alertCooldownSeconds":20}
```

Successful save/read response:

```json
{"ok":true,"settings":{"siteName":"Warehouse bay 1","enabled":true,"occupancyLimitSeconds":30,"alertCooldownSeconds":20}}
```

Test body (both fields required):

```json
{"occupiedSeconds":35,"secondsSinceLastAlert":5}
```

Use `null` for secondsSinceLastAlert when no alert has occurred. Both numeric
inputs must be integer seconds between 0 and 604800. Tests do not remember a
previous test, update settings, or change the last-alert time.

A policy is Overdue at `occupiedSeconds >= occupancyLimitSeconds`. An alert is
eligible only if enabled and overdue, and either there is no previous alert or
its age is at least alertCooldownSeconds. An overdue but suppressed result stays
Overdue; cooldown affects alert eligibility, not occupancy classification.

Example using the camera's trusted HTTPS endpoint (curl prompts for password):

```bash
curl --anyauth --user root 'https://CAMERA_IP/local/web_monitoring_policy/settings.cgi'
curl --anyauth --user root \
  -H 'Content-Type: application/json' -H 'X-Policy-Request: 1' \
  --data '{"occupiedSeconds":35,"secondsSinceLastAlert":null}' \
  'https://CAMERA_IP/local/web_monitoring_policy/test.cgi'
```

## Persistence and failures

`ax_parameter_set(..., TRUE, ...)` synchronizes each write. Passing FALSE defers
disk writes and callbacks until a later synchronized call; it is not simply an
option for suppressing callbacks. See the
[AXParameter reference](https://www.developer.axis.com/acap/api/src/api/axparameter/html/ax__parameter_8h.html).

Validation completes before any settings are written. The four writes are **not
transactional**: a runtime failure can leave a partial update. On first failure
the API stops writing, attempts readback, and returns HTTP 500 with `ok:false`,
the failed parameter and the actual readable settings (or null). No rollback or
all-or-nothing guarantee is implied. Successful saves require all writes to
succeed and the readback to match the requested values. Readback is confirmation
at that instant, not a lock against later external changes.

| HTTP status | Meaning |
| --- | --- |
| 400 | Invalid length, incomplete body, malformed JSON or duplicate JSON field |
| 403 | Missing required POST header |
| 404 | Unknown endpoint |
| 405 | Unsupported method, with Allow header |
| 413 | Body exceeds 4096 bytes |
| 415 | Unsupported content type |
| 422 | Wrong type, missing/unknown field or out-of-range value; no writes |
| 500 | Parameter access failure or unconfirmed save |

FastCGI handles one request at a time. Socket read/write timeouts bound idle I/O;
the browser has a five-second request timeout. On a browser timeout the server
may already have saved some or all values: Refresh before retrying. There is no
periodic browser polling, and the page labels failures as unconfirmed rather
than silently displaying old values as current. Stop signals end the accept loop;
parameter handles and the socket are released.

## Tests and source map

```bash
node --test tests/dashboard.test.cjs
docker build -f tests/Dockerfile --tag web-monitoring-policy-tests .
```

C tests inject a fake parameter store to exercise full validation before writing,
readback, partial-write failures, failed reads, cooldown boundaries, disabled
policies and side-effect-free tests. Browser logic tests cover edits, typed JSON,
validation messages, partial-save readback, testing saved values and reconnection.
The SDK build checks linkage and manifest validity. Camera authorization, real
persistence across reboot, FastCGI transport and rendered layout still need device
verification.

- `app/main.c`: FastCGI routing, bounded JSON body handling, HTTP responses and AXParameter adapters.
- `app/policy.c`: validation, readback-aware saving and stateless decisions.
- `app/html/app.js`: configuration form, saved snapshot and hypothetical tests.
- `tests/policy_test.c`: injected-store tests independent of camera SDK services.

## Reading the code in class

Start with `Policy` and `Store` in `app/policy.h`. `Policy` holds validated
settings. `Store` supplies read/write functions: the real app uses AXParameter,
while tests use an in-memory substitute.

Follow one save request in this order:

1. `main()` initializes the parameter handle and FastCGI socket, then accepts requests.
2. `handle_request()` selects an endpoint and checks the HTTP method.
3. `read_json_body()` checks content type and length, reads the complete body,
   and parses JSON.
4. `policy_save()` validates all fields, performs synchronized writes, and reads
   back the settings. Its comments identify these three stages.
5. `send_json_response()` sends the result and releases the response JSON object.

Next read `policy_test()`: its explicit branches distinguish Disabled, Within
limit, and Overdue, then explain whether the cooldown permits an alert.
Finally, read `app/html/app.js` to see how the forms call the endpoints and show
confirmed values or errors.

The source uses normal multi-line formatting. `.clang-format` (clang-format 18+)
and `.prettierrc.json` record the style for future changes; formatting tools are
not needed to build the application.
