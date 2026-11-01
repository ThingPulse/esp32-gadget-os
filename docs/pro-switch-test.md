# Pro switch contact diagnostic

For the **original ESP32-S3 N16R2 Pro prototype** only. It monitors GPIO1,
GPIO4 and GPIO43 as inputs with internal pull-ups. Later board revisions may
use these pins for LCD control and require a different diagnostic.

Build:

```sh
pio run -e esp32-color-kit-pro-switch-test
```

Upload and open USB serial (upload temporarily replaces the application firmware):

```sh
pio run -e esp32-color-kit-pro-switch-test -t upload
pio device monitor -e esp32-color-kit-pro-switch-test
```

Release the switch, then operate each action separately: pull down, push up,
press in. Each level change produces a line such as `GPIO4 HIGH` or `GPIO4 LOW`.
Record which GPIO corresponds to pull-down; do not infer physical direction
from the GPIO number. State repeats every two seconds and transitions are
debounced for 25 ms. External switch wiring determines the idle level; the
internal pull-up does not override a stronger external pull-down.

## Prototype results

User-operated test on 2026-10-31 identified:

| Action | GPIO |
| --- | --- |
| Pull down | 1 |
| Push up | 4 |
| Press in | 43 |

The supplied log shows all three inputs LOW between operations, with individual
HIGH pulses. This indicates active-HIGH operation, assuming the switch was
released between actions. The original diagnostic incorrectly labelled LOW as
active and HIGH as released; those labels have been replaced with raw levels.
For the existing circuit, verify HIGH while held before configuring GPIO1
deep-sleep wake on HIGH. A future switch circuit may use a different polarity.
GPIO4 and GPIO43 can be repurposed only after disconnecting their switch contacts.

The program does not initialize the display, network, or settings storage.
The screen is not used for feedback. It identifies contacts; it does not test
deep-sleep wake. Restore the intended Pro application build afterwards.
