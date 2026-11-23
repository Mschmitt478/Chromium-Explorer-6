# Validation

Run `RUN_GUI_TESTS=ON ./scripts/test.sh` from a development environment with a desktop and a working Chromium sandbox. The default `./scripts/test.sh` runs the storage and address tests without a graphical session.

- The storage test closes and reopens the database and checks favorites, folder changes, visits, and deletion.
- The engine test uses a local HTTP fixture to check JavaScript rendering, same-origin storage, Back and Forward state, and private profile isolation. It also opens the download save dialog and compares the saved bytes with the fixture.

## Bazzite smoke test

The user-scoped Flatpak launched on Plasma Wayland and XWayland and rendered Example Domain over HTTPS. A second invocation opened the IANA Example Domains page in the running browser. The stored visits were still present after closing the browser and reopening it. On Wayland, the running renderer reported nested PID namespaces, `NoNewPrivs: 1`, and `Seccomp: 2`.

A localhost HTTPS fixture with a self-signed certificate was rejected. The error panel displayed the untrusted-certificate reason and offered Retry.

The tested NVIDIA configuration fell back from GBM to Vulkan. Chromium also logged that the host system bus socket was unavailable inside the Flatpak. Both messages occurred while browsing worked.

## Screenshots

- [Classic single-page window on KDE Wayland](screenshots/wayland.png)
- [Certificate rejection on XWayland](screenshots/certificate-error.png)

The historical reference informed the arrangement of the menus, toolbar, address row, and Explorer Bar. The browser uses original artwork and its own window title. [GUIdebook Windows XP Pro screenshots](https://guidebookgallery.org/screenshots/winxppro/)

To compare the top chrome of another build, capture the active window with the same KDE decoration, scale, size, and address as the Wayland reference. Pass the capture to `scripts/compare-chrome.sh` with ImageMagick installed. It compares the menu, toolbar, and address row with a 5% color tolerance, excluding the page content and KDE title bar.

Before a release, validate the packaged file picker and downloads, printer discovery and physical printing, camera and microphone access, and the layout at 1024×768 and under fractional scaling.
