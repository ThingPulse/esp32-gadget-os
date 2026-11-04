# Independent ISS app / API 1.1

Date: 2026-11-04. Follow-up to the [counter experiment](REPORT.md).

The app is separately compiled freestanding C, loaded from
`/apps/native-iss/app.elf`, with its own endpoint, JSON field selection, display
formatting, Refresh/Close callbacks and state. The host adds a Labs launcher
entry with a satellite icon, bounded asynchronous HTTPS and numeric JSON helpers.
The built-in ISS pass app is unchanged. There is no package installer/catalogue
yet; launcher registration and the two allowed package IDs remain in firmware.

## Build and desktop validation

All five profiles build: normal and experimental Grande/Pro, plus the native
simulator. Symbol auditing confirms normal firmware contains no native loader,
HTTP wrapper or native JSON helper. Normal binary sizes are unchanged.

| Artifact | Grande | Pro |
|---|---:|---:|
| Normal firmware | 2,307,440 B | 2,289,280 B |
| Experimental API 1.1 firmware | 2,341,104 B | 2,309,712 B |
| Increase over API 1.0 experimental host | 4,880 B | 4,864 B |
| Independent ISS ELF | 3,532 B | 3,544 B |
| ISS ELF + manifest | 3,661 B | 3,675 B |

See [build audit](results/http-build-audit.json). Existing API 1.0 counter
packages remain accepted. The new package declares API 1.1. Neither app is
embedded as a firmware byte array.

ASan/UBSan tests pass for both ISS ELF preflights; exact-size/oversized/cancelled
response buffers; numeric JSON types, bounds and overflow; API compatibility;
success/offline/invalid-response/rate-limit UI; closure within a callback; and
100 close-pending/unload/reopen cycles. These perform actual desktop `dlclose`
before injecting late completions, including completions belonging to a previous
session. The existing 40,000 ELF mutations, counter lifecycle tests and synthetic
hardware-cycle analyser tests also pass. Desktop transport is simulated.

## Ownership and limits

The worker has an 8 KiB stack and a host-owned result containing a copied URL
(255-byte maximum), generation, status, and at most 2048 response bytes. No app
function or user pointer enters the worker. UI delivery takes and clears its
callback record before calling it. Close clears that record and closes the gate
before any code is unloaded. Release/acquire publication happens after transport
objects are destroyed; an old completion cannot reach a newly loaded session.

Cancellation is cooperative: the UI and app unload immediately, while the
worker may complete later. A new app may need to retry once that worker finishes.
Only one request is accepted at a time, at least five seconds apart. HTTPS uses
the built-in CA bundle and disallows redirects. Five-second connect/read and
eight-second TLS timeouts plus a 20-second body-write budget are not a hard
overall deadline for arbitrary DNS/header behaviour. This is still trusted native
code, without memory isolation. The small HTTP app does not establish sufficient
headroom for larger applications or multiple concurrent native apps.

## Hardware validation

Each device completed three automated real-network sequences:
six successful HTTPS updates, three closures during HTTPS, and three late
completions discarded **after the ELF was unloaded and a new session opened**.
Refresh and Close were delivered as actual LVGL button events. Each reopened
session subsequently refreshed successfully and returned to the launcher.
The counter and ISS packages were added while preserving the original filesystem
files (20 on Grande, 11 on Pro). Initial full-flash backups were verified before
installation. The user subsequently requested no backup/restore steps for future
tests and asked to keep the experimental build installed.

| Observation | Grande | Pro |
|---|---:|---:|
| Median successful HTTPS response | 1,903 ms | 2,030.5 ms |
| Median app load | 121.94 ms | 29.73 ms |
| Median unload | 1.99 ms | 1.84 ms |
| Median unload + launcher restoration | 160.27 ms | 94.49 ms |
| Lowest **sampled** internal free heap | 52,312 B | 122,268 B |
| Settled launcher internal free heap, successive sequences | 67,060 / 67,240 / 67,148 B | 142,404 / 142,216 / 142,356 B |
| Settled largest internal/executable block | 49,140 B each time | 98,292 B each time |
| Settled free PSRAM | 4,185,972 B each time | 2,069,512 B each time |

These are six app loads per device with active Wi-Fi/services and verbose serial
logging, not a long-term stress test or an exact TLS peak-memory measurement.
Grande logs use a 115200-baud UART and Pro uses native USB; durations are not a
controlled CPU/display comparison. Network latency also varies independently.
Temporary network allocations outlive a cancelled app; immediate launcher
snapshots during cancellation are therefore intentionally lower than settled
ones. See sanitized [Grande](results/grande-http-summary.json) and
[Pro](results/pro-http-summary.json) summaries.

The user confirmed that the Grande shows all values/buttons, Refresh and Close
work normally, the satellite launcher icon opens the app, no update overlay
covers it, and Calculator still produces 3 for 1 + 2 afterwards.

The Pro's **physical touch check did not pass reliably**: the user reported
buttons sometimes responding and sometimes not, as previously seen with the
counter experiment. Synthetic LVGL button events and actual HTTP/lifecycle
checks passed, but that does not validate the physical touch path. A temporary
touch diagnostic build was removed after the user chose to defer investigation.
No hardware cause was established and no touch-driver workaround is included.
The final Pro firmware was flashed without diagnostics and passed another live
HTTPS update and button-handler close back to the launcher. Both devices retain
the experimental build; no post-test restoration was performed, as requested.
