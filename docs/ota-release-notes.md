# OTA release notes

The available-update dialog opens a separate **What's new** QR view. Back returns
to the same offer without dismissing or ignoring it. Installation hides the notes
button and keeps the progress/cancel flow. The installed confirmation includes
the QR directly, before the existing Close action.

`release_notes::url()` opens the illustrated English notes directly:
https://github.com/ThingPulse/esp32-gadget-os/blob/master/docs/releases/since-v0.1.3.en.md
Both OTA dialogs use this destination, regardless of the installed or offered
version. The page links to the German translation. QR generation is local, with
a black-on-white symbol and white quiet zone; it adds no device network requests.

## Publishing

- Maintain `docs/releases/since-v0.1.3.en.md` and `.de.md` together. They currently
  describe cumulative user-facing changes since v0.1.3 for Color Kit Grande.
- Update the short `release_notes` summary in the staging script and its German
  catalogue entry when the release's highlights change.
- Keep screenshots in the repository and include the notes and images in the
  release's tagged commit. Relative links work when reading the pages on GitHub.
- `scripts/app-market-release.mjs` writes `.app-market/release-notes.md` while
  staging firmware. It links both language pages and embeds the English notes,
  rewriting image URLs to the same version tag. No moving `master` image links
  are used in the release body.
- The release workflow supplies that file as `body_path`. Review both language
  pages and update the cumulative content before tagging a later release.
- There is no separate website deployment. The QR page follows `master`, so
  note updates become visible when pushed there. The GitHub release body still
  links to its tagged snapshot. Local edits do not publish either.

Existing firmware cannot acquire the new pre-installation button until updated.
The new firmware can show its QR in the confirmation after that first update.
Native OTA fixtures use `1.0.0-demo` but encode the same notes-page URL as devices.

## Verification

Build native and `esp32-color-kit`. The native tutorial supports `ota_available`,
`tap_exact` on **What's new**, **Back**, and **Install**, then `ota_progress`,
`ota_restart`, and `ota_installed`. Capture these screens in English and German
and decode their QR codes to verify the destination. No real firmware is installed
by the native tutorial. Release staging can be verified against a temporary
fixture repository without publishing assets.
