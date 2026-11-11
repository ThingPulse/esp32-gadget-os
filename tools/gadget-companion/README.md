# Gadget Companion

## App data channels (0.7.0)

With Gadget OS SDK 1.7, companion payloads travel unchanged through app-scoped
channels. Agent Dashboard 0.7.0 and System Monitor 0.2.0 validate their own fields
and handle freshness; new metrics no longer require firmware field allowlists.
The companion detects the new transport and falls back to legacy URLs on older
firmware. Existing apps and companions remain supported on the new firmware.
See the [channel contract and migration guide](../../docs/companion-channels.md).

Update firmware once before installing the SDK 1.7 apps. Payload changes after
that need only companion/app updates, provided they fit the transport limits.

A macOS menu-bar app that connects your Gadget to your computer. The desktop
app bundles its own runtime: users do not install Node.js or keep a terminal open.
The command-line version remains available for development and Linux/Windows usage feeds.

## Install on a Mac

The [download and setup page](https://thingpulse.github.io/esp32-gadget-os/companion/)
links to Apple silicon and Intel disk images when a signed release is published.
The source change alone does not publish a release or deploy that page.

1. Open the DMG and drag **Gadget Companion** to **Applications**.
2. Open it and allow local network access when macOS asks.
3. Use the same Wi-Fi as your Gadget. On the Gadget, open **Add apps → Pair computer**,
   follow the setup screen, and tap **Pair computer**.
4. Click **Find my Gadget** on the Mac, select it, compare the code, and confirm on the Gadget.
5. Install **System Monitor** or **Agent Usage** from **Add apps** and open it from
   **Installed apps**. Both apps share the same pairing.

Close the window to leave the companion in the menu bar. Its menu can reopen the
window, reconnect, or quit. **Start at login** is optional and off by default;
it is available in packaged apps. The app reconnects after sleep and IP changes.
If discovery fails, expand **Can’t find it automatically?** and enter the Gadget’s IP.
**Forget Gadget** removes the local pairing; revoke it on the Gadget too via
**Pair computer → Forget computer** if the computer should no longer have access.

## Agent Usage setup

**System Monitor does not require Codex.** Agent Usage is optional and off by default.
Enable it in the companion to check whether the Codex CLI is installed and signed in.
The existing collector uses the CLI’s login; installing or signing in to the
Codex desktop app alone is not sufficient for this integration.

If setup is required, install and sign in to the [Codex CLI](https://developers.openai.com/codex/cli/),
then click **Check setup**. **Choose Codex…** selects an executable in a custom
location. Common Homebrew, local, Volta and nvm installations are detected without
launching a login shell. The companion does not read credential files or perform
an automatic sign-in. Missing Codex or a collection error does not stop System Monitor.

## Build the desktop app

Development requires macOS and Node.js 24. End users do not need Node.js.

```sh
cd tools/gadget-companion
npm ci
npm test
npm run desktop
npm run make                     # host architecture
npm run make -- --arch=x64        # Intel Mac download
npm run test:desktop             # isolated real Electron UI test
```

`npm run make` produces `out/downloads/Gadget-Companion-arm64.dmg` or
`Gadget-Companion-x64.dmg`, with a drag-to-Applications layout, plus a ZIP.
The `.app` is in `out/Gadget Companion-darwin-<arch>/`.
The smoke test uses an isolated profile, simulated discovery/pairing, and no
personal usage data. To test a packaged executable, set `GADGET_COMPANION_BINARY`
to its `Contents/MacOS/Gadget Companion` executable. Screenshots go to `out/screenshots/`.

Local builds are **unsigned development builds**. They are suitable for local
validation, but they are not the public download. Never instruct users to disable
Gatekeeper. Public builds require Developer ID signing and Apple notarization.
The Forge configuration follows [Electron’s macOS signing guidance](https://www.electronforge.io/guides/code-signing/code-signing-macos).

### Release configuration

The `Gadget Companion desktop` workflow creates macOS arm64/x64 artifacts on PRs,
pushes and manual dispatches. Tags matching `companion-v*` additionally require
signing and publish the two disk images to the stable `gadget-companion` release.
Unsigned builds cannot pass the release configuration check.

Configure these GitHub Actions secrets before creating a release tag:

- `APPLE_CERTIFICATE_P12`: base64-encoded Developer ID Application certificate and key.
- `APPLE_CERTIFICATE_PASSWORD`: password for that export.
- `APPLE_SIGNING_IDENTITY`: the installed Developer ID Application identity.
- `APPLE_NOTARY_KEY_P8`: App Store Connect API private key contents.
- `APPLE_API_KEY_ID` and `APPLE_API_ISSUER`: notarization API identity.

For a local signed build, set `GADGET_COMPANION_RELEASE=1`,
`APPLE_SIGNING_IDENTITY`, `APPLE_API_KEY` (path to the `.p8` file),
`APPLE_API_KEY_ID`, and `APPLE_API_ISSUER`. The identity must already be installed.
The workflow uses a temporary keychain and removes signing material afterward.
Both the app and the DMG are notarized; the DMG ticket is stapled.

The companion download page lives in `site/index.html`. The existing native-app
catalog build copies it to `/companion/`, matching the firmware QR code. Deploy
that catalog site as part of the release so the QR code has a destination.

## Command-line development

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

1. On the Gadget, open **Add apps → Pair computer**, then tap **Pair computer** on the setup screen.
2. Run the command above. Choose a device if several are discovered.
3. Compare the six-digit code on the computer and Gadget. Tap **Confirm pairing**
   on the Gadget, then **Back**.
4. Open **Agent Usage** from **Installed apps**.

Leave the terminal open. Usage updates every minute; the app checks the device's
local snapshot every five seconds. Press **Ctrl+C** to stop. The next run
reconnects using the saved pairing, including after a device reboot or IP change.
For login-at-startup integration, use the macOS desktop app.

**Firmware prerequisite:** this needs the new native-app host **API 1.3** and
Agent Usage **0.3.0**. Existing 1.2 firmware/demo apps do not gain this connection
without updating. The source catalogue now includes Agent Usage, but it will
only appear publicly after the normal signed-app catalogue release. These
changes do not publish releases or install firmware automatically.

For a developer installation before release:

```sh
.venv-build/bin/pio run -e esp32-color-kit-pro
python3 experiments/native-app/build.py --app agent_usage --version 0.5.0 \
  --chip esp32s3 --toolchain "$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"
```

Flash that firmware using your normal workflow. Install the resulting
`.pio/native-app-packages/esp32s3/agent-usage.gapp` using the existing browser or
`scripts/install-native-app.py`. This unsigned developer build needs Developer
mode; release builds are signed normally. For Grande use the
`esp32-color-kit` environment and `--chip esp32`. The firmware still
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

Install **Agent Usage** from **Add apps**, then open it from **Installed apps**. The snapshot mode does not pair or contact a
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
  Windows/Linux system collectors and desktop packaging, and multiple simultaneous hosts
  remain later steps. The macOS app includes a tray UI, standalone packaging and optional automatic startup. The independent
  companion endpoint and append-only SDK form their shared connection foundation.

## Package and test

```sh
npm test --prefix tools/gadget-companion
python3 scripts/test-companion.py
mkdir -p .pio/gadget-companion
npm pack ./tools/gadget-companion --pack-destination .pio/gadget-companion
```

The command-line npm package definition is ready for a future `npx @thingpulse/gadget-companion`
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

## Agent Dashboard activity (0.6.0)

Agent Usage is now displayed as **Agent Dashboard**, retaining the `agent-usage`
package ID and existing quota/history views. Update Gadget OS (host API **1.6**)
and install app version **0.6.0**. In Gadget Companion, enable **Codex activity**
and/or **Claude Code activity**. This adds observational lifecycle hooks, preserves
existing settings/hooks and saves a backup beside the configuration file. Turning
a provider off removes only companion hooks. After moving/reinstalling the
companion, toggle activity off/on to update the executable path.

For Codex, open `/hooks` (or desktop Settings → Hooks) to review and trust the new
hooks, then start a turn. For Claude Code, review `/hooks` or restart the agent.
Managed environments may disallow user hooks. Only local sessions that execute
these hooks are visible; remote/cloud sessions are not collected automatically.
The desktop setup uses Electron's bundled Node runtime; no terminal or separate
Node installation is needed. Automatic hook setup supports macOS and Linux;
the downloadable desktop companion currently targets macOS.

Activity is independent of the optional Codex quota collector. Hooks observe
`UserPromptSubmit`, `PreToolUse`, `PermissionRequest`, `PostToolUse`, `Stop` and
`SessionEnd`. They cannot approve, reject, continue or stop an agent. Only the
project folder name, provider, hashed session ID, status, elapsed time and a
short event description are retained. Prompts, command arguments, tool output,
responses, full paths and credentials are discarded. Approvals happen in the
agent client on the computer.

The Gadget shows six prioritized sessions (three per page), with attention first,
then working, finished and disconnected; projects sort together within each
status. The badge counts all sessions needing attention. Sessions without events
for 15 minutes become disconnected, not finished: hooks provide no heartbeat.
Legacy live-session records expire after 24 hours; durable task history is retained
as described below. If the companion stops sending, active tasks show disconnected
after 15 seconds; finished tasks remain finished. The Activity view opens by default when active
sessions are present on the first update, and never interrupts manual navigation.

CLI equivalents (Node 22+):

```sh
node tools/gadget-companion/src/cli.mjs --enable-activity codex
node tools/gadget-companion/src/cli.mjs --enable-activity claude
node tools/gadget-companion/src/cli.mjs --activity-snapshot /tmp/gadget-activity.json
GADGET_ACTIVITY_SNAPSHOT=/tmp/gadget-activity.json .pio/build/native/program
```

Use `--disable-activity codex` or `--disable-activity claude` to remove hooks.
Native preview without `GADGET_ACTIVITY_SNAPSHOT` uses three sample sessions.
An explicit snapshot path never falls back to sample data. Session data is pushed
to the authenticated `/v1/activity` endpoint every two seconds, separately from
`/v1/agent-usage`, with its own 2 KiB allowlisted snapshot.

Hook references: [Codex](https://learn.chatgpt.com/docs/hooks) and
[Claude Code](https://code.claude.com/docs/en/hooks).

## Durable task history and reported tokens

The Mac companion's **Task history** lists individual agent turns, newest first,
with project, provider, start time, duration and outcome. Expand a task for usage
details; Previous/Next browse the full retained history. The Gadget prioritizes
active tasks, then recent finished tasks, up to six, and shows **reported tokens**
in task details. Viewing those new fields requires the updated firmware and Agent
Dashboard app; older versions ignore the optional fields.

History survives companion and computer restarts. It lives in
`~/.gadget-companion/activity/history/`, with owner-only files, retaining the most
recent **500 turns per session** without a time expiry. Disabling activity stops
collection but preserves history. Collection starts with observed hooks; it does
not backfill old conversations. A task means a submitted prompt/turn, not a
semantic project milestone. Hooks that lack turn IDs use observed prompt boundaries.
Missing start events are labelled, and silence or interruption never means success.

For existing installations, switch **Codex activity** off and on after installing
the updated companion, then review and trust the changed hooks in `/hooks`.
The new Codex hooks are `PreModelRequest`, `PostModelRequest` and `Interrupt`;
they require a Codex version supporting those events. No automatic trust changes
are made. Claude Code retains task history but displays **Tokens: Not reported**.

Codex usage comes directly from the documented
[model-request hooks](https://learn.chatgpt.com/docs/hooks#premodelrequest-and-postmodelrequest),
correlated by session, turn and agent. Each paired pre/post request contributes
its reported input and output tokens; duplicate post events are ignored.
Cached input is included in input, and reasoning output is included in output;
neither is added again. Agent-scoped requests are kept separate from the parent's
turn. The display deliberately says **reported tokens**, because missing hooks,
unsupported versions, failed requests without usage or collection enabled mid-turn
can leave gaps. These counts cover observed requests, not a guaranteed complete
billable task total. Known zero is distinguished from missing usage.

No task allowance percentage or cost estimate is calculated. Account-level usage
remains separate. The collector does not read transcripts, prompts, credentials
or model responses. Only sanitized task metadata and numeric usage are retained.
