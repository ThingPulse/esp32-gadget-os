# Learned airport markers

The nearby-aircraft radar learns airport coordinates from the existing adsbdb
route responses. No airport database is bundled and no extra HTTP requests are
made. IATA labels (such as ZRH) are preferred, with ICAO as fallback. Coordinates
and codes are validated; airports without coordinates are ignored.

Only airports inside the current radar radius are retained, deduplicated by
ICAO, with a maximum of 64 entries. One file, `/cache/plane-airports.json`, holds
the location/radius and learned airports. It is loaded when the matching view
opens, including after reboot/deep sleep. Changing the radar range or location
removes the previous file and in-memory entries; changing the configured search
location/range also invalidates it. Closing the app does not clear the cache.
The existing session-only radar range selection is unchanged: if a restart
opens a different radius, that is a different view and invalidates the cache.

Airport updates travel with the network-worker result and merge on the UI thread.
Responses superseded by a search-area change cannot repopulate the old cache.
Cached route responses can provide airport coordinates again without new requests.
Persistence writes occur only when airport records change, once per response
batch. A malformed/truncated cache is discarded and relearned. Filesystem failure
does not prevent showing learned airports in RAM.

Markers are muted grey-blue dots and short labels, rendered beneath aircraft and
trails. Overlapping airport labels are suppressed, while their dots remain.

Verification:

- `pio run -e native`
- `python3 scripts/test-planes-providers.py` (also runs airport cache tests)
- `python3 scripts/test-nearby-planes.py`
- `pio run -e esp32-color-kit-pro-display-test`

Cache tests cover restart, view invalidation, corrupt data, invalid coordinates,
deduplication, geographic filtering, and capacity limits. UI fixtures include ZRH.
