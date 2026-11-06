# Gadget Companion (command-line preview)

Pair your computer once and show its Codex allowance in the installable
**Agent Usage** app. No tunnel, browser link, API-key copying or custom app URL.

## Run it

Requirements: **Node.js 22 or newer**, a **signed-in Codex CLI**, and both the
computer and Gadget on the same trusted local network. This uses the CLI login,
which can differ from the Codex desktop app login. The CLI must support
`account/rateLimits/read`.

From this repository, run:

```sh
node tools/gadget-companion/src/cli.mjs
```

There are **no npm dependencies to install**. With a downloaded companion-only
archive, run `node src/cli.mjs` from its extracted folder.

1. On the Gadget, open **Add apps → Pair computer**.
2. Run the command above. Choose a device if several are discovered.
3. Compare the six-digit code on the computer and Gadget. Tap **Confirm pairing**
   on the Gadget, then **Back**.
4. Open **Agent Usage** from **Installed apps**.

Leave the terminal open. Usage updates every minute; the app checks the device's
local snapshot every five seconds. Press **Ctrl+C** to stop. The next run
reconnects using the saved pairing, including after a device reboot or IP change.
There is no login-at-startup integration in this first phase.

**Firmware prerequisite:** this needs the new native-app host **API 1.3** and
Agent Usage **0.3.0**. Existing 1.2 firmware/demo apps do not gain this connection
without updating. The source catalogue now includes Agent Usage, but it will
only appear publicly after the normal signed-app catalogue release. These
changes do not publish releases or install firmware automatically.

For a developer installation before release:

```sh
.venv-build/bin/pio run -e esp32-color-kit-pro-native-app
python3 experiments/native-app/build.py --app agent_usage --version 0.5.0 \
  --chip esp32s3 --toolchain "$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"
```

Flash that firmware using your normal workflow. Install the resulting
`.pio/native-app-packages/esp32s3/agent-usage.gapp` using the existing browser or
`scripts/install-native-app.py`. This unsigned developer build needs Developer
mode; release builds are signed normally. For Grande use the
`esp32-color-kit-native-app` environment and `--chip esp32`. The firmware still
supports older API 1.0–1.2 apps. Packages contain no user-specific URLs or tokens.

## Options and recovery

```sh
# Re-pair, or select a different Gadget:
node tools/gadget-companion/src/cli.mjs --pair

# If discovery broadcasts are blocked, use the address shown by your router/device:
node tools/gadget-companion/src/cli.mjs --device 192.168.1.42

# Check collection and delivery once:
node tools/gadget-companion/src/cli.mjs --once

# A Codex executable outside PATH:
node tools/gadget-companion/src/cli.mjs --codex /path/to/codex
```

Allow Node access to your local network if the OS asks. Guest Wi-Fi/client
isolation can prevent devices communicating even on the same SSID. UDP port
8083 is used for discovery and TCP port 8082 for delivery. An IPv4 private LAN
address is required; IPv6-only networks are not supported yet.

Pairing is stored in `~/.gadget-companion/pairing.json` with owner-only permissions
on Unix. On Windows it inherits your user-profile ACLs. `--state <file>` overrides
this path. Do not share that file. To revoke it on the device, open **Pair
computer → Forget computer**. Only one computer is paired at a time in this
phase; pairing a new one replaces the previous authorization and snapshot.

On Windows, the official npm Codex wrapper is resolved to its JS entry and run
with Node, without invoking a command shell. For another installation layout,
use `--codex C:\path\to\codex.exe`.

## Simulator

To continuously write live data for the existing SDL preview:

```sh
node tools/gadget-companion/src/cli.mjs --snapshot .pio/agent-usage/live.json
```

In a second terminal:

```sh
GADGET_USAGE_SNAPSHOT="$PWD/.pio/agent-usage/live.json" .pio/build/native/program
```

Open **Agent Usage Preview**. The snapshot mode does not pair or contact a
Gadget. `--once` also works with `--snapshot`. The normal preview continues to
use clearly labelled sample values when this environment variable is absent.

## Privacy and scope

- Reads rate-limit and token-usage summaries using Codex's local app-server. Does not start
  agent turns, open credential files, collect prompts, or upload telemetry.
- Sends provider enabled flags, used percentages, window lengths, reset
  countdowns, seven daily token totals, lifetime/peak totals, streaks, longest-turn duration and observation ages to the paired Gadget. A computer name is
  included during pairing. No data goes to devices.thingpulse.com.
- **This preview uses plain HTTP on a trusted LAN.** The pairing token and usage
  summaries are not encrypted in transit; matching codes are a user confirmation,
  not protection against an active network attacker. Do not forward these ports
  or use it on untrusted shared networks. TLS/authenticated key exchange is future
  work before wider deployment.
- Incoming requests are bounded, reject browser Origin headers and ambiguous
  HTTP framing, and require the paired token to read status or submit summaries.
  Pairing requires an explicitly opened, two-minute device screen and a physical
  confirmation. There is no unauthenticated usage-reading endpoint.
- Device snapshots are RAM-only. After a reboot the app waits for a fresh push.
  Offline snapshots retain their original age; expired quota windows show
  unavailable rather than claiming a full allowance. Collector failures retain
  the last successful summary while this process is running.
- First service: **Codex allowance and token statistics**. Cost estimates, Claude integration,
  Windows/Linux system collectors, multiple simultaneous hosts, tray UI,
  standalone executables and automatic startup remain later steps. The independent
  companion endpoint and append-only SDK form their shared connection foundation.

## Package and test

```sh
npm test --prefix tools/gadget-companion
python3 scripts/test-companion.py
mkdir -p .pio/gadget-companion
npm pack ./tools/gadget-companion --pack-destination .pio/gadget-companion
```

The npm package definition is ready for a future `npx @thingpulse/gadget-companion`
installation, but **nothing has been published to npm**. Until publication, use
the direct Node command or a local package archive. It runs without a transpiler,
package download or third-party runtime dependencies.

Automated tests cover Codex RPC, expiry/staleness, discovery filtering, state
permissions, malformed HTTP requests and the firmware snapshot parser. The CI
matrix exercises the CLI on macOS, Windows and Linux; local checks alone do not
establish that each OS has been physically tested with a Gadget.

## Codex details (Agent Usage 0.5.0)

The overview always shows remaining allowance. Tap **Codex >** to select
**Today**, **7 days** (including today), or **Month** (current calendar month
through today). All dates use UTC. Month bars group up to five days, with their
date ranges shown underneath. Period totals marked **Known** include only reported
days and show coverage, such as `4/6 days`. Missing days never imply zero usage.
Today may say **Not reported yet** if Codex has not supplied today’s bucket.

The detail screen also shows lifetime tokens,
peak daily tokens, current/best streak and longest turn. Back returns to the
allowance view; Back again closes the app. Large counts use rounded K/M/B/T
suffixes. Missing metrics show `--`; a missing day is not treated as zero.

Use companion 0.3.0 and the matching updated firmware for the detail fields.
The SDK stays at 1.3. Older Codex CLIs without `account/usage/read` still provide
allowances; detail metrics remain unavailable. Detail observation age is tracked
independently so a successful quota refresh cannot make old token data look fresh.

## System Monitor (companion 0.4.0)

The same paired companion now supplies the installable System Monitor 0.1.0 app
on SDK 1.4 firmware. macOS CPU, RAM, process and network readings update every
two seconds independently of Codex polling; battery and disk queries are cached
for up to 30 seconds. Use `--no-codex` for system monitoring alone.

See [setup, metric definitions and privacy](../../docs/system-monitor.md).
No process arguments are collected; basename, PID and resource counters are sent.
Only the five leaders in each CPU/memory ranking are forwarded. History stays
in memory. No additional pairing or companion process is required.
