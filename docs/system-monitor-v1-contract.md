# System Monitor v1 — approved implementation contract

Status: approved by the user in chat on 2026-11-06. Implementation and validation notes: [System Monitor](system-monitor.md).

Visual reference: [three-screen mockup](previews/system-monitor-v1.png).
Generated using the built-in image-generation tool; [exact prompt](previews/system-monitor-v1-prompt.txt).
All numbers, names and charts in the image are example data.

## Scope and layout

One installable System Monitor app, initially collecting from macOS using the existing Node Gadget Companion and existing device pairing. No second companion, copied API key or separate browser pairing. Windows/Linux collectors are outside this first acceptance milestone.

Implement at 320 × 480 logical pixels on Grande and Pro, using the current Gadget OS fonts, palette and touch feedback. The image establishes visual hierarchy and content; the following geometry establishes the actual screen fit. Small raster variations from image generation are not new UI requirements.

- Header: y0–44, 44px back target, centered System Monitor title, 44px refresh target. Back closes to the launcher from any tab.
- Host/status row: y44–78, computer name and Live/Offline indicator. Long host names truncate.
- Body: y84–400. No scrolling for the three specified views.
- Freshness text: y410. Bottom navigation: y432–480, equal Overview / Processes / Storage targets. Selection highlights mint and taps show pressed feedback immediately.
- Body text at least 14px; header approximately 24px; primary metrics approximately 32px, adjusted only enough to fit actual values. Preserve full metric units.

## Overview

- Two cards, each 144px wide: CPU usage percent with logical-core count; memory usage percent with used/total physical RAM.
- Both cards contain a line chart showing up to two minutes of real samples. CPU and memory charts use fixed 0–100% vertical scales. Do not fill missing history with invented readings or connect across offline gaps. History starts accumulating when the companion starts.
- Network card: download and upload rates, automatically scaled B/s, KB/s or MB/s. Measure the active default-route interface; avoid counting both tunnel and underlying interface traffic. If a suitable interface cannot be identified, show unavailable.
- Bottom card: laptop battery percentage and charging/on-battery/plugged-in status, plus uptime. On a computer without a battery, replace battery content with an AC-power/no-battery indication and keep uptime. Unknown charging states remain unavailable.
- CPU is utilization of the whole machine, bounded 0–100%. Memory uses a documented macOS physical-memory calculation; reclaimable cache must not be presented as application memory. Swap and memory pressure are not additional v1 widgets.

## Processes

- CPU / Memory segmented control with immediate pressed/selected feedback.
- Exactly the top five readable processes, descending by the selected metric, stable tie-breaking by PID. Show fewer if fewer are available.
- CPU view: process name and CPU percentage normalized to total machine capacity (not macOS's per-core 100% convention). Caption: CPU: share of whole system.
- Memory view: the same layout, sorted by resident memory, values automatically scaled MB/GB; column title Memory and caption Resident memory per process.
- Names truncate to one line. Processes remain separate rather than aggregating every process belonging to an application. No command arguments, environment variables, process inspection dialog or kill actions.

## Storage

- Startup volume only in v1: volume name, available space, used/total capacity, a usage bar and percentage.
- Use the writable startup/data volume's filesystem capacity without counting APFS volumes sharing a container twice. Display the filesystem's reported available capacity, not a promise to match Finder's purgeable-space estimate.
- Secondary explanatory panel as pictured. No disk I/O, temperature, cleanup, SMART or multi-volume controls in v1.

## Collection, freshness and missing data

- CPU, memory, network and process samples target two-second intervals. Slower battery/capacity queries may be cached for up to 30 seconds. Collect outside the device UI thread; skip overlapping samples rather than queueing them.
- Refresh requests a new sample without waiting synchronously in the device UI. Automatic sampling continues while the companion runs, including with the app closed.
- Show Updated Ns ago using the sample's observation time. Mark the connection Offline after 10 seconds without a fresh sample; show Last seen …, mute last-known readings and stop extending charts. Reconnect automatically using the existing pairing.
- Before the first sample, show Waiting for computer and unavailable values. With no pairing, explain Add apps → Pair computer.
- Unsupported/denied individual metrics show -- / Unavailable; never substitute zero. The rest of the screen remains usable. No administrator prompt required for v1.
- Samples and short chart history remain in memory; no persistent process history or third-party telemetry. Only allowlisted metrics and process display names/PIDs go to the paired device. Existing companion LAN transport limitations still apply.

## Acceptance

Before calling v1 complete: render all views at 320 × 480; verify bounds and long/large values; verify real process sorting in both modes, zero versus unavailable, initial loading and offline/reconnect behavior. Compare live figures against the Mac's counters using the same definitions. Test device navigation and press feedback without blocking network or storage work. Ask the user to verify readability and responsiveness on the Pro. Do not flash the Grande or publish a release without the applicable user authorization.

Changes to this scope or replacement of a pictured feature require discussion before implementation is treated as matching the contract.
