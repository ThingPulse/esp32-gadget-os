# Signed apps, static catalog and optional reporting

This remains behind `GADGET_OS_NATIVE_APP_LOADER` in the two experimental
PlatformIO profiles. Normal firmware does not gain a native-app loader.

## Delivery and installation

The main launcher's **Add apps** tile opens an on-device store. It shows compatible
apps with descriptions, an explicit Install/Update action, download progress and
Open after installation. The first launcher page contains bundled apps and Add
apps; downloaded apps appear on the second page, titled Installed apps. The eight
installed-app slots fit without vertical scrolling. Dice Score and Weather Icons
are no longer registered in the launcher. Back cancels an in-flight download without installing it.
No app is installed automatically.

To uninstall on the device, open **Add apps → Installed apps**, select an app,
then confirm **Remove**. This list also includes retired and locally uploaded apps
and works without a catalogue connection. Installed catalogue entries also offer
Remove in their detail view. Confirmation explains that saved app settings and
cache are deleted; Cancel or Back leaves the app installed. Browser removal uses
the same cleanup operation. Running apps and active OS updates block removal.

The store checks for new apps after Wi-Fi connects while the launcher is idle,
then daily (retrying failures after 15 minutes). A **New** badge is based on app IDs,
not version changes. Visiting a successfully loaded catalog saves the seen IDs in
NVS, so the badge survives firmware restarts through a fresh catalog check without
reappearing for the same apps. Users can also explicitly Refresh. Catalog contents
are cached in RAM only; offline visits can browse that cache during the same boot.
The screen uses the Weather-style 44-pixel top bar, a back arrow on the left and
a refresh icon on the right. Tappable 80-pixel rows show the app name and status;
descriptions and Install/Open actions are in the detail view. The screen creates
at most four app rows at a time, with More apps/Previous apps
navigation, to bound LVGL memory use as the catalog grows. Descriptions come from
`apps.json` when the next catalog is published; older catalogs show a generic
description and remain compatible.

Desktop-rendered UI previews using sample catalogue data:
[compact list](../../docs/previews/native-app-store.png) and
[app details](../../docs/previews/native-app-details.png). These exercise the store's
layout code with transport stubbed, not downloads or touch on physical hardware.

Downloads run in a worker with TLS certificate validation, no redirects, bounded
response sizes and socket timeouts. The UI remains available for cancellation.
Only URLs under the configured HTTPS catalog directory are accepted. Before
commit, the package's signature, chip/API and identity/version/name are checked;
Developer mode never allows unsigned on-device-store downloads. Failed updates
retain the existing package. Storage/capacity/OTA conflicts produce an explanation.
Removal and custom file uploads remain in the browser manager.

`Add apps → Browser installation` pairs a browser with the device. The browser loads
`https://thingpulse.github.io/esp32-gadget-os/apps/catalog.json`, filters by chip
and host API, and downloads a compatible package directly from the same static
GitHub Pages site. It uploads that package through the existing paired device
installer. File uploads remain available when the catalog is offline.

The device verifies package integrity, its ECDSA-P256 signature, chip/API
compatibility and ELF structure before atomic replacement. It verifies again
when loading the app. Closing an app releases its resources before unloading;
installed app icons appear on the Installed apps page. Running-app replacement/removal
and OTA conflicts remain blocked.

The catalog contains at most 128 chip variants / 64 KiB. Packages remain bounded
at 70,908 bytes; at most eight packages are installed. Downloads have a 30-second
limit and send no pairing key, device identity or reporting credential to GitHub.
URLs must stay within the configured HTTPS catalog origin and directory. Native
apps have full firmware privileges: signing authenticates their publisher, not
a sandbox or a guarantee of safety. Installation is explicit; no silent updates
or automatic launch are introduced.

GitHub Releases also holds the identical versioned files in one dedicated
`native-apps` release, marked **not latest**. Using one release avoids crowding
the release list queried by older OS prerelease clients. GitHub Pages mirrors
those files to support browser downloads without a backend proxy or release-asset
redirect/CORS issues. Neither delivery path involves devices.thingpulse.com.

## Signing and Developer mode

The firmware trusts `signing-public.pem`. The private key is **not in this
repository**, any firmware image, or any published artifact. For this initial
setup it was generated locally at
`~/.config/thingpulse/gadget-app-signing.pem` with owner-only file permissions.
Keep it in secure key storage before depending on it for releases.

`build.py --signing-key <private.pem>` emits authenticated GAPP2; omitting that
argument emits legacy unsigned GAPP1 for development. The signing command
checks that its private key matches the committed public key. GitHub Actions
uses the `APP_SIGNING_PRIVATE_KEY` secret in the protected `app-signing`
environment. Do not put a private key in repository variables or build logs.

Unsigned installs and execution are rejected by default. **Developer mode can
only be changed on the device's Browser apps screen**, not through the browser API. It
persists until disabled. Existing unsigned packages remain visible/removable
in the browser, but their launcher icons and execution are disabled when the
mode is off. Invalid or unknown signatures are rejected even in Developer mode.
Custom unsigned app IDs and usage are excluded from reporting.

GAPP2 has a 124-byte header:

| Bytes | Meaning |
|---|---|
| 0–7 | `GOSAPP2\0` |
| 8–19 | Three little-endian uint32 lengths: padded JSON, RGB565 icon, ELF |
| 20–51 | SHA-256 of bytes 0–19 + bytes 52–59 + payload at byte 124 |
| 52–59 | First eight SHA-256 bytes of canonical public-key PEM, including its final newline |
| 60–123 | ECDSA-P256 signature of that digest, fixed-width `r || s` |
| 124 onward | JSON padded to four bytes, 40×40 RGB565 icon, ELF |

The signature covers all manifest, icon and executable bytes and all length/key
fields. The key identifier is a lookup hint; the full public key verifies the
signature. Key rotation/revocation currently requires a host firmware update.
There is no multi-publisher trust store or anti-rollback version policy yet.
A user can explicitly install an older correctly signed version from a file.

## Independent app releases

Edit each app's own version in `apps.json`; it need not match the OS version.
The `Publish signed native apps` workflow builds both chip variants, validates
signatures/parser behavior, creates icons, merges the catalog and publishes.
Existing app versions are immutable. An identical rebuild reuses the original
signed file (ECDSA signatures otherwise have different random nonces); changing
an existing version's content aborts publication. The `app-catalog` branch stores
the static site and old immutable package versions. App IDs not rebuilt remain
in the catalog.

One-time repository setup, **not performed by merely building this branch**:

1. Review/merge this implementation into `master`.
2. Configure the `app-signing` environment with protected refs/reviewers and its
   `APP_SIGNING_PRIVATE_KEY` secret, matching the committed public key.
3. Enable GitHub Pages with **GitHub Actions** as the source. This workflow owns
   the repository's Pages site; do not replace another existing site accidentally.
4. Dispatch the workflow for validation only, then push an `apps-v*` tag pointing
   to a commit on `master` to publish. Tags from outside `master` are rejected.
5. Review and deploy the reporting backend changes once if usage reporting is
   desired. No further web-app deployment is needed for app or OS releases.

A missing catalog returns an explanatory message; local signed uploads still
work. The release workflow is separate from the existing firmware release job.
The release metadata publishes Agent Usage and System Monitor. Native Counter
and ISS Position are retired: `retired_ids` removes their existing catalogue
entries and prevents old package artifacts from adding them again. Historical
package URLs remain intact. Their sources remain as loader regression fixtures.
Already installed copies can be uninstalled through the app manager.

On-device store validation (2026-11-05): simulator and both experimental firmware
profiles compile. Sanitized catalog tests cover chip/API filtering, malformed
entries, size bounds, duplicate IDs and URL confinement. Existing signed-package,
catalog publication, browser installer, parser/mutation and asynchronous app
lifecycle tests pass, as do language and device-check-in regressions. No devices
were flashed or tested for this change. On-device touch/layout, cancellation,
installation and runtime memory checks remain pending. The simulator build checks
that normal builds still exclude the feature; it does not exercise the store UI.

Local commands (using the installed PlatformIO toolchain):

```sh
python scripts/build-native-apps.py \
  --toolchain ~/.platformio/packages/toolchain-xtensa-esp-elf/bin \
  --signing-key ~/.config/thingpulse/gadget-app-signing.pem \
  --output .pio/signed-apps
python scripts/build-native-catalog.py --packages .pio/signed-apps --site .pio/app-site
python scripts/test-native-signing.py
node scripts/test-native-store-page.mjs
```

## Reporting only

Users can enable **Settings → System → Share app usage**. It defaults off and
saves immediately. The firmware records an explicit allow-list of built-in app
routes and authenticated native apps: app ID/version, launches, foreground
seconds, successful installs and failures after a package has been authenticated.
No app contents, settings, custom unsigned IDs or per-launch requests are sent.
The installed inventory contains authenticated native apps only.

Counters are bounded to 32 app/version entries, saved at most every five minutes
plus check-in/deep sleep, and sent with the existing installation identity.
Abrupt power loss can lose the last unsaved interval. A new consent session gets
a new epoch; turning sharing off deletes local counters. Already received server
reports are not deleted by that switch.

The existing `/api/v1/check-in` body remains unchanged. After a successful daily
OS check-in, opted-in firmware independently posts a bounded snapshot to
`/api/v1/app-usage`. A missing or unavailable usage endpoint never changes the OS
check-in result or blocks catalog/download/installation. Cumulative snapshots
are retried on later check-ins; the backend replaces snapshots rather than
adding them, and preserves the highest counters within an epoch to avoid both
double-counting and regressions after a restart.

The separate `thingpulse-device-management` change adds a migration, endpoint
and **App usage** dashboard. Dashboard activity is explicitly labeled as
cumulative counters from devices reporting within the chosen window, not activity
totals for that window. Versioned payload schema 1 avoids web deployments for new
app IDs, app versions or OS versions.
