# Maggy

> A desktop companion robot: an expressive animated face on an ESP32, controlled
> from a phone over the web.

Maggy is a small hardware + software product. A Seeed **Xiao ESP32** drives a
128×64 OLED that renders an animated, emotive face, reacts to motion and touch,
and syncs in real time with a **Progressive Web App** remote control.

---

## Repository layout

| Path | What it is |
|------|------------|
| `XiaoDevModeTest/` | ESP32 firmware (Arduino/PlatformIO sketch) |
| `public/` | Web controller PWA (deployed to Firebase Hosting) |
| `database.rules.json` | **Target** Firebase Realtime Database security rules (see roadmap) |
| `platformio.ini` | Reproducible firmware build definition |
| `firebase.json`, `.firebaserc` | Firebase Hosting / project config |
| `docs/` | Architecture analysis and the commercialization roadmap |
| `archive/` | Frozen snapshot of the pre-refactor codebase (reference only) |

> Note: the firmware folder is still named `XiaoDevModeTest` for historical
> reasons. Renaming it to a product name is tracked in the roadmap.

## Hardware

- Seeed **Xiao ESP32** (C3/S3 — confirm your variant in `platformio.ini`)
- **SH1106** 128×64 I²C OLED
- **MPU6050** accelerometer / gyroscope
- Capacitive touch input (GPIO 5)
- Mode push-button (GPIO 21)

## Features

- 11 emotion/modes (sensor-driven *Auto* mode + manual selection)
- Motion reactions: shake → dizzy, tilt → happy, face-down → sleepy, touch → love
- NTP clock + live weather (open-meteo)
- Pomodoro timer (tilt-to-set on device, or controlled from the app)
- Five animated mini-games
- "Creator": draw pixel art in the app and push it to the OLED
- Over-the-air firmware updates (ElegantOTA)
- Real-time remote control + device telemetry via Firebase Realtime Database

---

## Building the firmware

### PlatformIO (recommended — reproducible)

```bash
# 1. Provide your secrets (never commit this file — it is gitignored)
cp XiaoDevModeTest/secrets.example.h XiaoDevModeTest/secrets.h
#    then edit secrets.h with your Firebase host + auth token

# 2. Build / upload
pio run                  # compile
pio run --target upload  # flash over USB
pio device monitor       # serial monitor @ 115200
```

Confirm the `board` in `platformio.ini` matches your exact Xiao variant.

### Arduino IDE

Open `XiaoDevModeTest/XiaoDevModeTest.ino`, install the libraries listed in
`platformio.ini` (`lib_deps`), create `XiaoDevModeTest/secrets.h`, select your
Xiao ESP32 board, and upload.

## Web controller

The PWA in `public/` is static and deployed via Firebase Hosting:

```bash
firebase deploy --only hosting
```

It expects a `public/config.js` (gitignored) that defines and initializes the
Firebase web config. See `public/config.example.js`.

---

## Status

This codebase is mid-transition from a personal single-device prototype toward a
commercial, multi-device product. See **[`docs/ANALYSIS.md`](docs/ANALYSIS.md)**
for the current architecture assessment and **[`docs/ROADMAP.md`](docs/ROADMAP.md)**
for the phased plan and open security items.

## License

Proprietary — see [`LICENSE`](LICENSE).
