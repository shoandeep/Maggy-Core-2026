# Archive — frozen reference snapshot

This folder is a **read-only snapshot** of the entire repository as it stood at
commit `3655d05` ("V58"), captured before the commercialization refactor began.

It exists purely as a reference so the original, known-working firmware and web
controller remain easy to compare against as the project evolves. **Do not build,
deploy, or develop from this folder** — all active development happens in the
repository root (`/public`, `/XiaoDevModeTest`, etc.).

Contents mirror the tracked files at snapshot time:

- `XiaoDevModeTest/XiaoDevModeTest.ino` — original ESP32 firmware
- `public/` — original Firebase-hosted PWA controller
- `firebase.json`, `.firebaserc`, `.gitignore` — original project config

Note: secret files (`secrets.h`, `public/config.js`) were never tracked in git,
so they are intentionally absent here.
