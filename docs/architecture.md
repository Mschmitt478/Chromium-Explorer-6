# Architecture

- `main.cpp` owns startup and profile lifetime, including same-user local socket delivery for subsequent desktop launches. A profile lock guards the data directory.
- `engine.cpp` owns each Qt WebEngine profile, constructs WebEngine views, rejects certificate errors, prompts on supported permissions, and routes downloads through explicit save dialogs.
- `window.cpp` implements Qt Widgets browser windows, tabs, browser controls, preferences, and Explorer panels. It uses a native error panel with Retry. Qt WebEngine draws all website content.
- `theme.cpp` contains the Luna-inspired widget style and original vector painting for toolbar icons. KDE draws the outer title bar and frame. Historical screenshots informed the grouping of the chrome, the stacked toolbars, and the Explorer sidebar. The implementation uses its own palette, proportions, icons, and branding. [GUIdebook Windows XP Pro screenshots](https://guidebookgallery.org/screenshots/winxppro/)
- `store.cpp` owns address resolution and SQLite storage for favorites and history. Settings use QSettings. Chromium stores its profile and cache in Qt's XDG data and cache locations. Flatpak scopes those locations to the app ID.
- `packaging/` contains desktop registration and the KDE/WebEngine Flatpak pairing. Local file access uses selection dialogs. Unsupported URL schemes are blocked by WebEngine's profile policy.

## Current limits

- The bundled package uses the Chromium snapshot and security backports provided by Qt WebEngine. Codec, WebRTC, and DRM availability depend on that build. Check the version in Help → About.
- Supported site permissions are prompted on every request. Windows from the same private session share an off-the-record profile until the process exits. Explicitly downloaded files remain on disk.
- History groups by date. Favorite folders are flat. The saved window session covers the initial window, including its open tabs and selected tab. Download progress is tracked for the current session.
- The current interface uses an original Luna-inspired theme. Alternate palettes, a bundled start page, per-site preferences, persistent download history, and AppImage packaging are future work.
- Print Preview uses Qt WebEngine's PDF viewer and is subject to its inline content size limit. Printer discovery depends on CUPS.
- Continued release maintenance requires tracking the supported KDE runtime and WebEngine BaseApp pair, rebuilding promptly for security updates, and retesting the packaged renderer. [Flathub WebEngine BaseApp](https://github.com/flathub/io.qt.qtwebengine.BaseApp)
