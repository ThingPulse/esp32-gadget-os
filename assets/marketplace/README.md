# Marketplace screenshots

These are real native-renderer screenshots with fixture data, shown as example
screens in Add apps. They are local assets, so browsing adds no screenshot network
requests. Unknown catalogue apps retain their description and a generic icon.

Sources in `.pio/ui-previews`:

| Asset | Rendered fixture |
| --- | --- |
| agent-usage | agent-dashboard-activity |
| system-monitor | system-monitor-overview |
| iss-tracker | iss-installable-next |
| stocks | stocks-package-watchlist |
| transit | phase3-transit-board |
| planes | phase3-planes-radar |
| home-assistant | phase3-home-dashboard |

After updating the PNGs here, run `python3 scripts/build-marketplace-previews.py`
(Pillow required). It generates `src/experimental/MarketplacePreviews.inc` with
120×180, 16-colour indexed LVGL images: 76,048 image bytes for all seven apps.
The source PNGs are retained for future resizing and screenshot updates.
Benefit text, requirements, icons and preview associations are maintained in
`src/experimental/MarketplacePresentation.h`.
