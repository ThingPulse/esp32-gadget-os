# Catalog/signing/reporting implementation validation

Date: 2026-11-04. No device backups were created or restored.

## Completed

- All five PlatformIO profiles built: simulator, normal Grande/Pro and experimental
  Grande/Pro. A symbol audit confirms normal firmware contains no native loader.
- Actual C++ package parser tests passed under ASan/UBSan: all truncations,
  every single-byte corruption, metadata/chip/API/ELF checks.
- Actual C++ signature verification passed with disposable keys: valid P-256
  signature, unknown key, missing key, signature corruption, recomputed-checksum
  payload tampering, wrong chip and correctly signed invalid ELF.
- Catalog generation, immutable rebuild reuse and untrusted publication rejection
  passed. Generated local site contains four variants (two apps × two chips).
- Browser page state-machine tests passed: filtering, install flow, installed
  state, unsigned policy, URL/size bounds, credential isolation and offline fallback.
- Existing 40,000 ELF mutations, desktop ABI cycles and asynchronous HTTP
  close/unload/late-result tests passed. Check-in and i18n regressions passed.
- Usage ledger tests passed for timer wrap, idle time, version separation,
  capacity/retention, saturation and queued-report consent invalidation.
- Separate backend: 17 tests passed; Astro/TypeScript checks returned zero errors,
  warnings or hints; production build and complete HTTP smoke test passed,
  including the new endpoint through real middleware and dashboard rendering.
- Dashboard visually checked with synthetic local data; screenshot is
  `results/catalog-usage-preview.jpg`. It is not production usage data.

## Hardware evidence and limitations

The Pro passed signed upload/replacement/removal, pairing/Host/Origin protection,
interrupted transfer recovery, eight-app capacity and running-app guards. It
rejected unsigned uploads with Developer mode off, invalid signatures, unknown
keys and payload tampering even after the checksum was recomputed. Chrome successfully installed
signed ISS Position 0.2.0. Signed Native Counter 0.1.0 was also installed.

An initial Pro stress run completed 100 opens, 100 acknowledged resets and 100
acknowledged closes without a reported crash. **It did not pass the strict cycle
analyzer:** cycle 99 had six callbacks where seven were required during
concurrent tampered-package upload tests. The capture and
summary preserve that result. The test's fixed-time close could occur before the
required timer count; the final harness now waits for the same count with a
five-second failure timeout. This changes test timing, not the acceptance threshold.

The final builds (including that harness change and cancellation of queued reports
on opt-out) were flashed and verified on both devices. Both serial monitors
connected. **The user then asked not to rerun tests on both devices.** The queued
Pro repeat/Grande API run was rejected before execution by automatic approval
review. Stop commands were sent and the monitors closed. No rerun was performed.
Therefore final-build repeated-cycle validation and Grande signed-installer
validation remain unverified. Earlier Pro results above are not represented as
verification of the final build. Firmware remains installed as requested.

## Not published/deployed

GitHub Pages/catalog publication, the signing secret/protected environment and
backend deployment have not been configured or triggered. The firmware therefore
shows the catalog-unavailable fallback until the one-time setup in `CATALOG.md`
is completed. Signed local uploads work independently. The generated site and
signed sample packages are in `.pio/app-site` and `.pio/signed-apps`.

The signing private key is outside the repositories in the user's private
configuration directory. Only its public key is committed. No real usage reports
were sent during these tests; the local firmware builds had no ingestion key.
