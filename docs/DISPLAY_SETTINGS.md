# Display settings — 2.5.4-dev1 test build

This development build starts from the working 2.5.3 release. Display uses the same order and names in every theme. Each theme keeps its own look and existing preferences. Dimmed settings say **Disabled**; tap one to learn why it does not apply. Switching themes does not reset saved values.

| Under Display | What you can change |
|---|---|
| Brightness | Screen brightness. Outdoor mode overrides it while enabled. |
| Theme | Choose the theme directly. |
| Appearance | Light/dark, automatic appearance, Outdoor mode, Null colour scheme, accent and theme colours. |
| Music screen | Background and controls; Text, Information, Progress and Immersive subpages; the separate Now Playing style for themes that use it. |
| Standby | Screensaver delay, screen-off delay and supported screensaver styles. Screen-off delay is additional time after the screensaver. |
| Navigation | Menu items, Quick settings, Shortcuts, Gestures, interaction hints, navigation side and supported menu timeout. |
| Layout & extras | General text size, rotation, time format, list motion, transitions, album-list track numbers, album view and startup disc colour. Online & cache contains online artwork, lyrics, cover caching and weather. |

Music screen → Information → Track number changes the Music title. Layout & extras → Album track numbers changes album lists. They are independent.

Null keeps its dark appearance and right-side menu. Its Colour scheme option includes Matrix. Squash uses fixed gesture controls, navigation side and QS layout. Null and Squash use their own palettes instead of the custom accent. Disco, Ring and Braun support automatic light/dark changes. Themes with their own screensaver do not use the generic screensaver-style selector.

Help still contains How to. Hint preferences and reset now live under Display → Navigation. Long setting names wrap within their rows instead of moving continuously.

## What changed for efficiency

Null pauses its frequent visual timer when Music, QS and an open menu do not need it. Visual callbacks skip a fully-off screen, unused QS state copies are removed, and unchanged settings/scan labels avoid repeated text updates. Clock refresh resumes on wake. Playback, History, animation timings and artwork quality retain their existing behavior.

Battery/status polling deliberately continues with the screen off. The source documents a hardware freeze when that keep-alive was removed. It must not be paused as a battery optimization.

These changes reduce identified work; battery-life gains have not been measured on the Disc.
