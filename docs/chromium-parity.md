# Desktop Chromium capability audit

Audited Internet Explorer XP 0.1.3 at `79dc908` on November 27, 2026. The installed Flatpak uses Qt WebEngine 6.11.3. Its user agent identifies Chromium 140. Qt separately backports fixes from newer Chromium releases, so the rendering baseline and security patch level are different measurements. [Qt WebEngine Chromium version matrix](https://wiki.qt.io/QtWebEngine/ChromiumVersions)

The target is a maintained desktop Chromium release on Linux with the existing XP-inspired appearance. Measure parity against the same operating system, hardware, packaging permissions, and baseline Chromium build. Track Google Chrome services and licensed components as additional dependencies. Google account sync and some extension identity APIs have restricted access even in Chromium derivatives. [Chromium API keys and derivative restrictions](https://www.chromium.org/developers/how-tos/api-keys/)

## What works in the installed build

- Existing automated coverage exercises browsing and history, JavaScript, private-profile isolation, the download flow, compact URL editing, site-provided HTTP error pages, and an unpacked Manifest V3 extension. The installed Flatpak has also been tested on Plasma Wayland and XWayland, including renderer sandboxing and certificate rejection. [Validation notes](validation.md)
- A localhost private-window check executed WebAssembly, opened IndexedDB and Cache Storage, registered and activated a service worker, and created a WebGL 2 context.
- The build advertises H.264, AAC, VP9, and AV1 support through `canPlayType`. Media Source Extensions are exposed. Actual decode, hardware acceleration, streaming playback, and video conferencing still need end-to-end tests.
- The probe found WebRTC, camera, display capture, WebAuthn, file picker, notifications, PushManager, WebHID, and Web Serial interfaces. These results only establish interface presence. A device picker, permission prompt, portal grant, or service integration may still be required.
- WebGPU was exposed but returned no usable adapter on the tested NVIDIA desktop. WebUSB was absent from the tested page, video Picture-in-Picture was disabled, and the Widevine key-system request returned `NotSupportedError` for the probed VP9 configuration.

Reproduce the probe with `python3 scripts/probe-web-capabilities.py`. It opens a private Flatpak window against a loopback fixture and prints JSON. It requires a desktop session and does not grant permissions or exercise real devices. Stock desktop Chromium was not run in this audit.

## Capability gaps

| Area | Current browser | Gap to close or verify |
|---|---|---|
| Extensions | Unpacked Manifest V3 folders, manual activation, popup views and removal | Web Store installation, signed package validation and updates, user-facing host access and permission controls, extension shortcuts and toolbar actions, and compatibility tests for service workers, declarative network rules and native messaging. Qt exposes a local-file installer. [Qt extension manager](https://doc.qt.io/qt-6.11/qwebengineextensionmanager.html) |
| Passwords and autofill | Website sign-ins use page forms and cookies | Password save and fill, password generation, KDE Secret Service or wallet integration, forms and address autofill, and safe import and export. Treat Google Password Manager and sync as separate services. |
| Sign-in and identity | WebAuthn interface present | Passkey and security-key prompts, cancellation and device selection, HTTP and proxy authentication, client-certificate selection, and external sign-in and protocol-handler flows. Test these with controlled accounts and certificates. [Qt client certificate support](https://doc.qt.io/qt-6.11/qtwebengine-features.html#client-certificates) |
| Site permissions and data | Camera, microphone, location, and notifications have app prompts. Other requests reaching that handler are denied. | Origin-aware allow and block settings, revocation UI, site storage and cookie controls, clipboard permissions, secure file-system access, and USB, HID and serial chooser flows where the platform supports them. |
| Calls and screen sharing | WebRTC, camera, and display-capture interfaces present | Test real camera and microphone capture, then wire and verify screen and window selection through the KDE PipeWire portal. Include denial, cancellation and unplugged-device cases. |
| Playback and graphics | WebGL 2 works. Codec support is advertised. No WebGPU adapter was available. | Real codec playback and hardware decode, usable WebGPU on supported hardware, page-requested fullscreen, Picture-in-Picture, media keys and controls, and DRM playback where the CDM and distribution terms permit it. [Qt media and graphics features](https://doc.qt.io/qt-6.11/qtwebengine-features.html) |
| Notifications and installed web apps | Service workers and notification-related APIs are exposed | Desktop notification delivery, background push and notifications, PWA install and launch, desktop entries, app windows and launch handlers. Qt WebEngine push is opt-in. [Qt WebEngine push support](https://doc.qt.io/qt-6.11/qtwebengine-features.html#push-notifications) |
| Tabs and recovery | Optional tabs and startup restoration for the initial window | Closed-tab undo, pinned tabs and groups, tab search, multi-window crash recovery, profile switching, and durable session state. Keep the XP presentation while exposing these commands. |
| Downloads, files, PDFs and printing | Save dialogs, session download list, basic printing and preview | Persistent download history, pause and resume, open and reveal controls, download reputation checks, portal file upload and editing tests, and PDF viewing and printing coverage in the Flatpak. |
| Browser tools and settings | DevTools, history, favorites, address search, zoom, and private windows | Full bookmark and history import and export, search provider settings, spelling dictionaries, clear-data controls by site and time, accessibility coverage, and diagnostic surfaces for GPU, network, crashes and policy. |
| Security and release engineering | Chromium sandbox and certificate rejection have been tested. App packages use the Qt WebEngine BaseApp. | Verify sandboxing throughout the packaged feature set, establish a release and security-update cadence, and assess download and phishing reputation, enterprise policy, secure DNS UI and proxy controls. Google's Safe Browsing API is omitted by Qt WebEngine. [Qt WebEngine relationship to Chromium](https://wiki.qt.io/QtWebEngine) |

The repository audit distinguishes an absent application workflow from an engine capability. A missing chooser may be implementable through a Qt signal. A missing API, packaged codec, hardware backend, or vendor service needs separate verification.

## Architecture decision for a skin over Chromium

**Prototype the desktop Chromium browser layer before growing the Qt shell further.** Keep the Chromium profile, browser services, permission UI, downloads, extensions, and settings working in the prototype. Apply the XP-inspired styling to browser controls incrementally. Every replaced surface should continue to call the upstream controller. Keep the prototype on a branch and use a separate profile before attempting migration from the current Flatpak. Regular upstream security merges and repeatable Linux packaging become part of owning a fork. [Chromium Linux build guide](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/linux/build_instructions.md)

Consider CEF in Chrome runtime and Chrome style as a comparison for the prototype. It exposes more Chromium browser behavior than a content-only embedder. Check that the desired customization can retain that behavior. CEF distinguishes Chrome and Alloy browser styles, and features relying on Chrome's UI can differ between them. [CEF architecture](https://github.com/chromiumembedded/cef/blob/master/docs/architecture.md) · [CEF tutorial](https://github.com/chromiumembedded/cef/blob/master/docs/tutorial.md)

The prototype should prove the following before replacing the current engine.

1. Build from a maintained upstream release and launch sandboxed on Bazzite Wayland and XWayland.
2. Verify real Web Store installation, permission review, update and disable behavior with a compatible extension. Prove this on the packaged build before promising the store flow.
3. Compare passkeys, password storage, camera and screen sharing, PDF and print, file pickers, playback, and WebGPU against a stock Linux Chromium build on the same machine.
4. Change the visible toolbar styling and verify the upstream action, permission, and settings paths still work.
5. Exercise an upstream security update and rollback. Decide separately how to migrate bookmarks and history and how to handle profile data that may be incompatible. Test against representative web workloads and a selected [Web Platform Tests](https://github.com/web-platform-tests/wpt) suite.
