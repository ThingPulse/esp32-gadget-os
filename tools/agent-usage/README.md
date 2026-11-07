# Agent Usage — installable prototype

**Recommended local setup:** use [Gadget Companion](../gadget-companion/README.md).
API 1.3 / app 0.5.0 pair directly over Wi-Fi with no tunnel or private URL.
The Python bridge and HTTPS instructions below are retained as a legacy advanced
workflow and for Claude status-line experiments.

The overview shows **Remaining** allowance only. Codex details show token usage
with **Today / 7 days / Month** periods, using Gadget Companion 0.3.0. Each provider keeps its own two
quota windows and reset countdowns. Missing values remain unavailable; old
samples are marked stale after five minutes. Passing a reset time never implies
100% availability. Refresh runs on launch, manually, and every minute.

Agent Usage now requires **API 1.3**, with a compact navigation header,
provider cards, large percentages and progress bars, plus usage-period tabs in
the Codex detail screen.
The SDK renderer is shared between SDL and device firmware. Old Used/Remaining preferences are ignored. The live endpoint can be read from the
`endpoint` setting (falling back to the private build URL), but a browser UI for
editing that setting and public catalogue
publication remain follow-up work. Nothing is sent to ThingPulse's reporting
service.

The screenshots in `docs/previews/agent-usage-remaining.png` and
`agent-usage-used.png` are rendered from the real app, not design mockups.

## A. Test in the native SDL simulator

From the repository root:

```sh
.venv-build/bin/pio run -e native
.pio/build/native/program
```

Open **Agent Usage Preview** on the simulator launcher (scroll if needed).
It uses clearly labelled sample values. Open **Codex >** and try **Today / 7 days / Month**, the top-right refresh icon,
and the top-left back arrow. The preview compiles the **same C source** as the installable app
against a desktop LVGL host adapter. It does not load or emulate an Xtensa
`.gapp`; the desktop CPU cannot execute the ESP32 binary.

To test real Codex allowance in the simulator, first sign into your Codex CLI
normally, then:

```sh
python3 tools/agent-usage/companion.py collect-codex
python3 tools/agent-usage/companion.py snapshot --providers codex \
  --output .pio/agent-usage/snapshot.json
GADGET_USAGE_SNAPSHOT="$PWD/.pio/agent-usage/snapshot.json" .pio/build/native/program
```

Refresh rereads that file. Repeat collection/export to get a newer snapshot.
A snapshot's age advances while the app is open; export again before restarting
it because the portable payload contains relative times. There is no automatic
collection in this simulator file mode. Omit `--providers codex` when exporting
to include Claude as well. A missing, malformed or oversized file exercises
failure handling. Edit `*_age` to 600 to test stale data, `*_reset` to 2 for
expiry, `*_used` to -1 for missing data, or `*_enabled` to 0 to hide a provider.

## B. Test on Grande and Pro without a server

Build the offline demo packages:

```sh
python3 experiments/native-app/build.py --app agent_usage --usage-demo --version 0.5.0 \
  --chip esp32 --toolchain "$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"
python3 experiments/native-app/build.py --app agent_usage --usage-demo --version 0.5.0 \
  --chip esp32s3 --toolchain "$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"
```

| Device | Package |
| --- | --- |
| Color Kit Grande | `.pio/native-app-packages/esp32/agent-usage.gapp` |
| Color Kit Pro | `.pio/native-app-packages/esp32s3/agent-usage.gapp` |

1. Use firmware built with `esp32-color-kit` or
   `esp32-color-kit-pro`. **Update this firmware once to include API 1.3** before installing the new
   app. Older firmware rejects the package as incompatible. Older installed
   Counter/ISS packages still run on the new firmware. Subsequent compatible
   app updates do not need a firmware reflash.
2. Open **Add apps → Browser installation**, then scan the QR code.
3. For an unsigned local build, enable **Developer mode** on the device. It must
   stay enabled to run unsigned apps. Alternatively, build with `--signing-key`
   pointing to the private key matching the public key trusted by the firmware.
4. Upload the package for that device, return to the launcher and swipe to
   **Installed apps → Agent Usage Demo**.
5. Confirm both provider readings fit, open Codex details, switch periods, refresh and back,
   reopen, and check another app still works. Demo mode requires no Wi-Fi once
   installed and always says **Sample**.

No backups, restores or filesystem-image uploads are required. Both packages
use ID `agent-usage`; installing the live version replaces the demo cleanly.

## Install directly from a Mac (no iPhone/browser)

Connect USB and keep the Mac/device on the same trusted Wi-Fi. Close serial
monitors and leave the device at its launcher. List serial ports, then upload:

```sh
.venv-build/bin/python -m serial.tools.list_ports
.venv-build/bin/python scripts/install-native-app.py \
  --port /dev/cu.YOUR_PORT \
  --package .pio/native-app-packages/esp32s3/agent-usage.gapp
```

Use `esp32` instead of `esp32s3` for Grande. The command opens the existing
installer over USB, captures the private pairing URL without printing it,
uploads the exact package over Wi-Fi and verifies its installed version. USB
is used for pairing, not the file transfer. Some serial adapters reset the
device when opened; the script allows time for boot. No firmware changes,
filesystem upload, backup or restore are performed.

Unsigned packages still require Developer mode, enabled on the device's
installer screen. A failure leaves that screen open so you can enable it and
retry. Chip/API mismatches fail before uploading. After success the command
returns the device to its launcher.

## Live companion setup

Requires Python 3.10+ and a Codex CLI supporting `account/rateLimits/read`.
The companion queries only account rate limits; it never starts a thread or
turn, reads provider credential files, or exports prompts/transcripts.

```sh
python3 tools/agent-usage/companion.py serve --poll-codex
# Codex only:
python3 tools/agent-usage/companion.py serve --poll-codex --providers codex
```

This listens only on `127.0.0.1:8787`. Codex is sampled every 60 seconds. A
failed query preserves the last successful observation and its original age.
It uses the CLI's existing login, which may differ from a desktop app's login.
The bridge writes sanitized summaries and a random read-access token into
`.pio/agent-usage/` with private file permissions. This directory is gitignored.

### Claude Code

Configure a status-line command manually in Claude Code. Use absolute paths for
both the script and state directory, since Claude can run in any project:

```json
{
  "statusLine": {
    "type": "command",
    "command": "python3 /absolute/repo/tools/agent-usage/companion.py --state /absolute/repo/.pio/agent-usage claude-statusline"
  }
}
```

Quote paths containing spaces within the command string. If you already use a
status-line script, integrate this command into it by feeding **the same stdin
JSON** to both scripts; do not silently replace your existing status line. The
bridge prints a simple `Gadget usage bridge` line and stores only quota fields.
It ignores context-window percentages, token counts, prompts, paths and account
credentials. Events without quota fields preserve the previous sample.

Start a Claude Code session and make a normal request. Quota data depends on
your subscription and client version and may be absent before the first
response. When Claude stops supplying events, the device's data becomes stale.

### HTTPS endpoint for a device

The current SDK accepts HTTPS only, with the firmware's CA bundle and no
redirects. Put the loopback bridge behind an HTTPS reverse proxy with a
publicly trusted certificate, on a hostname reachable from your device.
Self-signed certificates and plain `http://192.168...` URLs will not work.
Keep proxy access logging disabled for the private `/usage/<token>` path.

For example, an already provisioned reverse proxy should forward requests for
`https://usage.example.com` to `http://127.0.0.1:8787`, preserving the path.
Generate a private URL file after the bridge has created its access token:

```sh
python3 - <<'PY'
import json, os
from pathlib import Path
state = Path('.pio/agent-usage')
token = json.loads((state/'access-token.json').read_text())['token']
with os.fdopen(os.open(state/'endpoint.txt', os.O_WRONLY|os.O_CREAT|os.O_TRUNC, 0o600), 'w') as f:
    f.write('https://usage.example.com/usage/' + token)  # replace hostname
PY
python3 experiments/native-app/build.py --app agent_usage --chip esp32s3 \
  --usage-url-file .pio/agent-usage/endpoint.txt \
  --toolchain "$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin"
```

Use `--chip esp32` for Grande. Install via the same browser workflow. The URL is
embedded in this private package and grants read access to your quota summary:
do not publish that package or commit the URL. No provider login token is
embedded. To revoke the read URL, stop the server, delete `access-token.json`,
restart and rebuild with the new URL.

The app remains outside `apps.json` and the public catalogue until runtime
configuration/pairing exists. The bridge is a local personal integration, not a
hosted account-authentication service.

## Automated tests

After building both packages and installing the PlatformIO dependencies:

```sh
python3 scripts/test-agent-usage.py
python3 scripts/test-native-ui.py  # after the native simulator build
```

Checks companion normalization, credential-field exclusion, expired/missing
windows, mock Codex RPC handshake, both Xtensa ELF structures, and the app source
under ASan/UBSan: remaining allowance, usage periods, partial history, disabled provider, expired readings, malformed
responses, HTTP 429 and 100 close cycles. Shared-host UI tests also exercise real LVGL button events, bounds at 320×480,
widget/text quotas, invalid handles, close gating, 100 cleanup cycles and
settings isolation/persistence, and produce PNG screenshots. These do not prove physical touch,
Xtensa execution or device TLS connectivity. CI builds both chip fixtures and
runs the tests. Real Claude and device-network tests need the user's setup.

Sources: [Codex app-server](https://learn.chatgpt.com/docs/app-server#6-rate-limits-chatgpt)
and [Claude Code status line](https://code.claude.com/docs/en/statusline#rate-limit-usage).
