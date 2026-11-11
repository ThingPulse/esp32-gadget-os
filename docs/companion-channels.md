# App-owned companion payloads (SDK 1.7)

Gadget OS owns pairing, authentication, HTTP framing, bounded storage and routing.
An installable app owns its JSON schema, field validation, observation ages and
display behavior. Adding a field, changing an app schema or adding a channel no
longer needs a firmware change within the transport limits below.

## Publishing

An authenticated `GET /v1/status` advertises `appChannels: 1` and
`maxPayload: 2048`. Send the app's JSON directly:

```text
POST /v1/apps/agent-usage/activity
X-Companion-Key: <existing pairing token>
Content-Type: application/json
Content-Length: <UTF-8 byte length>

{"schema":1,"age":0,"count":0,"total":0,"attention":0,"enabled":1,"future_metric":123}
```

App IDs and channel names both match `[a-z][a-z0-9-]{0,31}`. No URL decoding,
query parameters or additional path segments are accepted. Authentication is
checked before publication. Browser Origin headers and ambiguous/chunked HTTP
requests remain rejected by the existing transport. This does not change the
existing trusted-LAN HTTP security model.

The body must be exactly one JSON object, at most **2048 UTF-8 bytes**, with
nesting depth at most eight. The firmware checks those transport constraints but
does not require a `schema` field or recognize any application fields. It retains
the exact bytes, including unknown fields, whitespace and schema versions.
Successful publication returns 200, invalid transport data 400, and publication
more frequently than every 500 ms on the same channel 429. Invalid publications
do not replace the previous valid payload.

## Reading inside an app

```c
char json[2049];
uint32_t residence_ms = 0;
int32_t n = host->companion_channel_read(
    "activity", json, sizeof json, &residence_ms);
if (n > 0) {
    /* Validate your schema and every consumed field before using it.
       Add residence_ms / 1000 to observation ages in YOUR schema.
       Decrease YOUR relative expiry countdowns by the same amount. */
}
```

The host supplies the running app's ID. An app cannot pass another app's ID to
this SDK function. This is routing isolation, not a new memory sandbox for native
ELF code. The function returns the payload's byte length excluding its terminating
NUL, `-1` for unavailable/invalid arguments/insufficient capacity, or `-2` when
unpaired. It does not truncate. Output and age are meaningful only on success.
Check the host's ABI size, major/minor version and function pointer before use.

`residence_ms` is monotonic time since receipt on the device, not the age of the
underlying measurement. Payload contents are never rewritten. Apps must combine
their own source-observation ages with residence time and elapsed display time;
reading the same snapshot repeatedly must not make it fresh again. Payloads
received a week ago, or across a monotonic-clock reset, are unavailable.

The cache holds eight app/channel pairs, with a maximum 16 KiB of payload bytes
plus bookkeeping. A ninth pair evicts the oldest publication. Data is RAM-only;
reboot, re-pairing and forgetting the paired computer clear it. Apps must handle
missing, stale, evicted and invalid data without displaying fabricated zeros.

## Existing applications and migration

The legacy endpoints are aliases into the same store:

| Legacy POST | App | Channel |
| --- | --- | --- |
| `/v1/agent-usage` | `agent-usage` | `usage` |
| `/v1/activity` | `agent-usage` | `activity` |
| `/v1/system` | `system-monitor` | `system` |

SDK 1.3–1.6 readers retain their original ABI positions and use **frozen
compatibility adapters** to provide the old sanitized, age-adjusted views.
Do not extend those adapters for new features. All new development uses
`companion_channel_read`, appended to SDK 1.7. The existing System Monitor refresh
signal remains available; it does not interpret or constrain payload fields.

Gadget Companion 0.7.0 negotiates generic channels and falls back to legacy URLs
on old firmware. Old companions can still publish via the aliases on new firmware.
Existing installed apps keep working with either publisher on the new firmware.

This migration requires one firmware update to SDK 1.7, Agent Dashboard 0.7.0 and
System Monitor 0.2.0. Updated apps require SDK 1.7; the package installer checks
this before installation. After that, companion/app payload evolution within the
transport limits requires only those components to be updated. Increasing payload
limits or changing authentication/transport capabilities still requires firmware.

## Verification

`scripts/test-companion.py` checks exact future-field round trips, routing,
rate/size/depth limits, invalid-input preservation, eviction, monotonic ageing,
revocation and legacy adapters. Agent Dashboard and System Monitor tests validate
raw app payloads and residence-time handling. Companion tests cover feature
negotiation, unchanged payload forwarding, authentication and legacy fallback.
