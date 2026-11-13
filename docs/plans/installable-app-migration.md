# Installable app migration

Status: phases 1 and 2 implemented (2026-11-13); phases 3–4 are pending.
No apps are removed from firmware in phase 1.

## Target

Stock firmware provides Weather, Calculator, Add apps and Settings. Transit,
Stocks, Planes and Home Assistant become independently installable packages.
The existing installable ISS, Agent Dashboard and System Monitor remain supported.
Shared device settings (network, language, display, time and device location)
remain host-owned. App settings and validation belong to the app.

## Phase 1 — Settings entry contract and unified launcher

- Extend the append-only SDK with a settings launch mode. An optional boolean
  `settings` manifest capability exposes the entry under Settings → Apps.
- Launch only the selected app. Its normal entry builds its settings UI when
  requested. In-app gear buttons call the same renderer locally. Closing an
  external settings session returns to Settings → Apps; local Back returns to
  the normal app view without unloading it.
- Preserve shutdown, callback cancellation and unloading guarantees. Old
  packages remain compatible and do not acquire settings entries implicitly.
- Combine built-in and installed tiles into one paginated launcher, filling
  available slots. Persist ordering by stable identity; updates retain position,
  new apps append, and removed apps disappear. Manual rearrangement is deferred.
- Verify manifest validation, launch/return state, pagination/order, native builds
  and existing app/settings regressions. Simulator adapters must explicitly
  support settings; never pretend to execute an installed Xtensa image.

## Phase 2 — Stocks pilot

- Package the Stocks UI, quote provider and refresh lifecycle together.
- Extend bounded SDK UI/input/network APIs as needed rather than exposing LVGL
  internals. Use the phase 1 settings entry for its watchlist editor.
- Import existing watchlists once, without overwriting package-owned settings.
- Verify quote/history/error behavior, multiline input, install/update/uninstall,
  settings navigation and resource cleanup on Grande, Pro and native.

## Phase 3 — Transit, Planes and Home Assistant

- Move each app's UI, provider logic, settings and service lifecycle into packages.
- Import transit stops, Planes preferences and Home Assistant configuration once.
- Keep device location shared, with an optional app-specific override.
- Add necessary bounded HTTP/authentication capabilities for Home Assistant;
  preserve credentials without exposing tokens in logs or catalogue metadata.
- Validate feature parity, offline/error behavior and repeated open/close cycles.

## Phase 4 — Minimal stock firmware and release

- Remove migrated screens and registered background services from firmware.
- Keep Weather, Calculator, Add apps and Settings as the built-in launcher apps.
- Publish compatible signed packages and document the required firmware version.
- Preserve old configuration until successful migration; document upgrade and
  uninstall behavior. Do not silently delete customer configuration.
- Measure firmware size, idle memory and repeated app lifecycle behavior on both
  hardware targets. Verify clean installs and upgrades from existing firmware.

## Deferred

Manual launcher rearrangement, a declarative settings-form system and automatic
installation of migrated apps are separate follow-ups. Hardware flashing,
package publication and release rollout are not part of phase 1.

## Phase 1 implementation and verification

- API 1.8 adds `launch_mode()` and the opt-in `settings` manifest flag. Package,
  catalogue and browser-install compatibility reporting agree on version 1.8.
- Settings → Apps lists supported installed packages, with a clear empty state.
  The native settings demo exercises external and in-app entry, persistent
  settings, and Back navigation. No released app is opted in implicitly.
- Launcher pages hold nine mixed tiles. Built-ins have explicit stable IDs;
  package positions use manifest IDs. Inventory is validated once per launcher
  rebuild, not once per tile, and removing the final package clears refresh state.
- Validation passed: native launcher/settings integration (including repeat
  cleanup and page clamping), settings forms/keyboard, Stocks, simulator store
  install/open/remove/restart, catalogue compatibility, package/ELF corruption
  checks, old ABI/lifecycle cycles, and both chip builds of the settings demo.
- Native, Grande and Pro firmware compile checks passed.
  Rendered native launcher and settings screens were visually reviewed.
- Device flashing, on-device lifecycle testing and package publication have not
  been performed. Phase 2 implementation is recorded below.

## Phase 2 implementation and verification

- Stocks 0.1.0 is an independent API 1.9 package, included in the source catalogue
  and desktop simulator. Its UI, Yahoo provider/parser and refresh scheduler are
  compiled into the app, not resident firmware services. Regular builds exclude
  the bundled app/service/provider; fallback sources remain for reference tests.
- Both entry paths share the watchlist editor, including multiline Enter, exact
  symbol validation and order. Existing Yahoo watchlists import once without
  overwriting app settings or deleting legacy configuration. Updates preserve
  the app list; uninstall removes it without resurrecting the legacy list.
- SDK additions are append-only: bounded text input/keyboard, scroll/gestures,
  variable-length charts, numeric services and 64 KiB extended JSON/HTTP. Native
  executes the package source with asynchronous real or explicit fixture data.
- Automated coverage exercises quotes/history, invalid data, cached failures,
  Retry-After, stale callbacks after edits/closure, input, both settings paths,
  migration, reinstall/uninstall and repeated cleanup. Package/ELF validation
  covers both chips; existing ABI, settings, launcher and store checks remain.
- Validation artifacts: `.pio/stocks-phase2-packages/{esp32,esp32s3}/stocks.gapp`
  (unsigned developer builds) and `.pio/ui-previews/stocks-package-*.png`.
- Native, Grande and Pro builds and the automated checks listed above passed.
  Rendered watchlist, detail/chart and editor screens were visually reviewed.
- Grande/Pro compile validation does not substitute for on-device runtime tests.
  Device flashing, on-device lifecycle checks and signed catalogue publication
  are still pending; no release or remote deployment was performed.
- Next: phase 3 (Transit, Planes and Home Assistant).
