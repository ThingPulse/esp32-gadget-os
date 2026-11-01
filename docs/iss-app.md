# Where is the ISS?

The ISS home-screen tile opens a 320 × 480 LVGL app with **Next pass** and
**Passes** tabs. It uses Pollux Labs' public, keyless API:
https://iss-api.polluxlabs.io/

The app follows the active Weather location. If Weather has none, it uses the
configured Planes coordinates. Tap the location to open Weather location settings.
The device timezone and 12/24-hour preference control all displayed clock times.

## Behaviour

- Next pass defaults to the next optically visible pass. Its compass marks only
  rise/set directions, with north up; it does not show a measured live position.
- During a pass, the time-progress bar runs from rise to set. The visibility
  countdown uses the separate visible-start/end timestamps. It handles delayed
  visibility, partial visibility and invisible passes selected from All.
- Passes defaults to Visible; All includes non-visible passes. Tap a row to see
  its details on the first tab. Expired rows disappear without continuous list
  rebuilds that would interrupt scrolling.
- Visibility is a prediction, not a weather or obstruction check.
- Missing location, missing clock, offline/cache, empty forecasts, API failures
  and stale orbital data have explicit states. English and German are supported.

## Network and cache

Requests run in a worker, with TLS certificate verification, finite timeouts and
bounded responses. Each refresh fetches up to 20 general and 20 visible passes
within seven days; visible predictions beyond the first general results are
merged and deduplicated. Only existing location coordinates are transmitted.

The app refreshes every 30 minutes while open. Manual requests respect a one-minute
minimum gap; failures back off and numeric Retry-After headers are respected.
No network requests start while the app is paused or firmware installation is
active. Late responses for a previous location are discarded.

One `/cache/iss.json` file holds the last successful responses and location key.
Fresh matching forecasts are reused after restart. Failed refreshes retain the
last good forecast; old data is labelled. Countdown updates require no network.
The display uses a small line-drawn satellite and compass, with no bitmap assets
or global orbit database in firmware.

## Verification

```sh
.venv-build/bin/pio run -e native
python3 scripts/test-iss.py
# Optionally verify a saved API response too:
python3 scripts/test-iss.py /tmp/iss-live.json
.venv-build/bin/pio run -e esp32-color-kit-pro-display-test
.venv-build/bin/pio run -e esp32-color-kit
```

Set `ISS_PREVIEW_DIR` to an existing directory when running the tests to capture
native UI fixtures as PPM files. Regression tests cover parsing, visible windows,
pass selection, cache reload, cooldown/backoff, location changes during requests,
list stability, screen cleanup, and English/German layouts.
