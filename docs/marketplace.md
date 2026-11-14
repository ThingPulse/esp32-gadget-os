# Add apps marketplace

Add apps opens the catalogue directly. Cards show an app icon, its purpose,
installation/update state and computer requirement. Details show an example
screenshot, requirements and setup guidance, plus Install, Update or Open.
The full catalogue description and version remain below the primary action.
Known app presentations are bundled with firmware; unknown apps keep the
catalogue description and a generic icon without guessing their requirements.

Manage in the header contains Update all, installed versions and removal with
confirmation. It remains accessible without a successful catalogue download.
Updates use the existing signed-package flow and preserve app settings. Browser
uploads and Pair computer are available in Settings. Computer-dependent apps
also retain Set up computer in their details and pairing on first launch.

Back from details or Manage returns to browsing, preserving the browse position.
Screenshots contain sample data; they are labeled Example screen. See
[preview maintenance](../assets/marketplace/README.md) for regeneration.

Validation: native store lifecycle, install/open/remove, bulk updates and failure
recovery, English/German previews, Settings pairing and translation regressions.
Device builds check Grande flash capacity as well as Pro. Physical-device touch,
Wi-Fi pairing and signed release publication remain separate release checks.
