# Maggy — Commercialization Roadmap

Sequenced plan to move from single-device prototype to commercial product.
Checked items are done on branch `claude/repo-analysis-commercial-WKD0q`.

## Phase 0 — Security (stop the bleeding)

> ⚠️ Several of these require action outside this repo (Firebase console) or
> rewrite shared git history. They are intentionally **not** auto-applied —
> confirm before proceeding.

- [ ] **Rotate the leaked Firebase credential** in the Firebase console
      (the token exposed in `public/config.js` git history). _Owner action._
- [ ] **Scrub git history** of `public/config.js` (e.g. `git filter-repo`).
      _Destructive / rewrites history — needs explicit go-ahead._
- [ ] Author and deploy real RTDB **security rules** (target in
      `database.rules.json`). Deploy only **after** the auth migration, or it
      will lock out the current unauthenticated device.
- [ ] Put **OTA behind authentication** (ElegantOTA credentials).
- [ ] Remove `client->setInsecure()`; pin/verify TLS certificates.
- [ ] Gate the remote dev-portal trigger so it can't be abused.
- [x] Provide `secrets.example.h` / `config.example.js` templates so secrets
      stay out of the repo.

## Phase 1 — Multi-tenancy (unblocks selling >1 unit)

- [ ] Introduce a per-device `deviceId` and per-device DB namespace
      (`devices/{deviceId}/...`).
- [ ] Add Firebase Auth (users) + device auth (custom token / claims).
- [ ] Build a pairing / onboarding flow (claim a device to an account).
- [ ] Migrate firmware + web app off the shared `shoans_secret_vault_7788` path.

## Phase 2 — Firmware hardening

- [x] PlatformIO project with pinned library versions (`platformio.ini`).
- [x] Fix the right-eye eyelid smoothing bug.
- [ ] Make weather/HTTP non-blocking; reuse a single TLS client.
- [ ] Replace 200 ms polling with Firebase streaming (or a single JSON read).
- [ ] Migrate to ArduinoJson v7.
- [ ] Add a hardware watchdog timer and robust reconnect logic.
- [ ] Rename the `XiaoDevModeTest` folder to the product name.

## Phase 3 — Tooling, docs & web quality

- [x] CI: firmware build + config validation (GitHub Actions).
- [x] README, analysis, roadmap, LICENSE.
- [ ] Real PWA offline caching in `sw.js` + a web build/minify step.
- [x] Defensive guard in the Creator image upload path.
- [ ] OTA firmware **versioning** + rollback strategy.

## Phase 4 — Fleet operations & launch

- [ ] Crash/telemetry dashboards and alerting.
- [ ] Staged OTA rollouts.
- [ ] Unify branding across firmware, app, and Firebase project.
- [ ] Hardware compliance (FCC/CE) and third-party API attribution.
