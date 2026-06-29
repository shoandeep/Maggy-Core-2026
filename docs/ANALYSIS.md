# Maggy — Architecture & Commercial-Readiness Analysis

_Last updated: 2026-06-03. Baseline: commit `3655d05` ("V58"), preserved in `/archive`._

## 1. What the system is

Maggy is a desktop companion robot built on a Seeed **Xiao ESP32**, paired with a
**Firebase-backed PWA** remote.

| Component | Path | Role |
|---|---|---|
| Firmware | `XiaoDevModeTest/XiaoDevModeTest.ino` | Animated OLED face, sensors, WiFi/OTA, Firebase sync |
| Web controller | `public/index.html` | PWA remote: emotions, pomodoro, pixel-art Creator, dev portal |
| Hosting/config | `firebase.json`, `.firebaserc`, `manifest.json`, `sw.js` | Firebase Hosting deploy |

**Communication model:** the firmware *polls* a single Firebase Realtime Database
path `/shoans_secret_vault_7788/*` every 200 ms; the web app writes to the same
path. Shared keys: `emotion`, `dev_mode_active`, `dev_cmd`, `pomo_cmd`,
`updateID`, `img_parts/0..3`, `telemetry`.

## 2. Critical blockers for a commercial product

1. **No multi-tenancy — every device shares one global state.** All firmware and
   web clients read/write the same hardcoded path. Two units would mirror each
   other. There is no device ID, user account, or per-device namespace. This is
   the single biggest architectural blocker.

2. **Secrets are in git history.** `public/config.js` was committed (`c2a45b8`)
   then deleted (`71f727e`) but remains in history, including a 40-char token
   that has the shape of a Firebase legacy database secret. Must be rotated and
   scrubbed regardless.

3. **No authentication / authorization anywhere.**
   - Firmware uses a deprecated `legacy_token` database secret.
   - Web app writes to the RTDB with no sign-in; security rests on the secret
     path name (security by obscurity) plus unseen RTDB rules.
   - **OTA is unauthenticated** — anyone on the LAN can flash firmware.
   - The dev/WiFi config portal can be **triggered remotely** via
     `dev_mode_active`, forcing a robot into an open setup AP.

4. **TLS validation disabled.** `client->setInsecure()` for weather and no cert
   pinning for Firebase → MITM-able.

## 3. High-impact engineering issues

5. **Blocking architecture freezes the device.** `fetchWeather()` does a blocking
   HTTPS GET on the main loop; `performAction()`, calibration, and boot use
   `delay()` loops. A `new WiFiClientSecure` is heap-allocated per fetch
   (fragmentation risk).

6. **Inefficient / costly Firebase polling.** ~5 sequential RTDB reads/sec/device.
   High latency and a real billing problem at fleet scale. Move to streaming or a
   single JSON read.

7. **Deprecated / brittle dependencies.** `DynamicJsonDocument` (ArduinoJson v6);
   Firebase web `compat` SDK; firmware libraries unpinned.

8. **Confirmed code bug (fixed).** Right-eye eyelid smoothing read the *left*
   eye's value during the wake-up sequence. Corrected in this branch.

9. **Brittle image transfer.** `sendCustomDraw()` assumed exactly 4 chunks with no
   guard. A defensive length check was added.

10. **No build system / CI / tests originally.** Raw `.ino` in a folder named
    `XiaoDevModeTest`. PlatformIO + GitHub Actions CI added in this branch;
    automated tests still pending.

## 4. Product / polish gaps

- **Fragmented branding:** "Maggy", "RobotBuddy_Setup", "shoanbot",
  `esp32shoanbot`, `shoans_secret_vault_7788`. Unify under one name.
- **PWA is misleading:** `sw.js` is network-only — no real offline caching.
- **No fleet observability:** no crash reporting, OTA versioning, staged rollout,
  or rollback.
- **Compliance:** FCC/CE, weather-API attribution, OTA safety interlocks are
  unaddressed for sold hardware.

## 5. What's strong

The product concept and UX are genuinely good: the eye/emotion animation engine
(smoothing, blinking, yawning, particles, per-emotion physics) is expressive; the
motion-driven Auto behavior is clever; and the Creator canvas→OLED pipeline is a
delightful feature. The gap is productization, not vision.

See [`ROADMAP.md`](ROADMAP.md) for the sequenced plan.
