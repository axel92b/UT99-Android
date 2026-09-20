<img width="2172" height="724" alt="image" src="https://github.com/Andiweli/UT99-Android/blob/master/images/unreal-header.jpg" />

# Unreal Tournament (UT99) Android

<p align="left">
  <a href="README.md">README</a>
  &nbsp;|&nbsp;
  <a href="ROADMAP.md">ROADMAP</a>
  &nbsp;|&nbsp;
  <a href="https://github.com/Andiweli/UT99-Android/releases">DOWNLOAD</a>
</p>

![OS](https://img.shields.io/badge/up%20to-Android%2016-green)
![Architecture](https://img.shields.io/badge/architecture-32/64bit-orange.svg)
![AI](https://img.shields.io/badge/AI-assisted%20coding-6e7781)
![Controller](https://img.shields.io/badge/Controls-Gamepad/Touch/Keyb-blueviolet)
![Multiplayer](https://img.shields.io/badge/Multiplayer-local%20WiFi-blueviolet)
[![Support via PayPal](https://img.shields.io/badge/Support%20via-PayPal-0070BA?logo=paypal\&logoColor=white)](https://paypal.me/andiweli)


> [!NOTE]
> Unofficial fan port.  
> No game data included.  
> Several bugs with triggers and events not starting 100%.  
> Requires legally obtained Unreal Tournament v1.400 game files.  
> This project is not official and is not endorsed by Epic Games.

> [!IMPORTANT]
> Video games on smartphones are great, but not user-friendly if they require more than two thumbs to control. For this reason, this port is designed for controller input only and does only offer basic touchscreen controls.
>
> This project is for preservation, experimentation and personal use only.  
> Unreal Tournament, Unreal Engine and related trademarks are owned by Epic Games.  
> This project is not affiliated with or endorsed by Epic Games.

---

## ▣ Screenshots

<img width="1920" height="1080" alt="ut99-0" src="https://github.com/user-attachments/assets/e7b74300-e685-47fb-82f8-4a81405855e2" />
<img width="1920" height="1080" alt="ut99-1" src="https://github.com/user-attachments/assets/81ffadd5-bf30-4444-ab3b-698c4e56430f" />
<img width="1920" height="1080" alt="ut99-2" src="https://github.com/user-attachments/assets/516263cf-caba-44e3-95f4-4b0852e79780" />

---

## ◈ Features

- Android support Android 4.x (OUYA) up to Android 16
- Improved Game Data Import – Unreal Tournament data can be imported via folder or ZIP selection and automatically installs to the app's data folder
- Android Storage Access Fixed – SAF support added for modern Android versions where direct SD/file access is restricted
- Legacy storage behavior friendly for old sideload devices (place game data on your microSD/UT99 folder)
- Local WiFi multiplayer and botmatches are available
- Added touch controls featuring *RetroTouch* for use without a controller
- Added keyboard support (tested with Chromebook)

> [!NOTE]
> Expect occasional issues, especially on very old Android devices or unusual controller mappings.

---

## ▣ Requirements

- Android device with OpenGL ES 2.0 support.
- Android 4.1 / API 16 or newer.
- A compatible device.
- Android-compatible game controller recommended.
- Original Unreal Tournament / UT99 [PC game data v400](https://archive.org/download/ut-99_202512/UT99.iso).

Required game data folders:

```text
UnrealTournament/
├── System/
├── Maps/
├── Textures/
├── Sounds/
└── Music/
```

The installer accepts the folders either directly at the selected root or inside one top-level Unreal Tournament folder.

---

## ◎ Installation

1. Install the APK on your Android device.
2. Copy your Unreal Tournament game data to your device, either:
   - as an extracted folder, or
   - as a ZIP file.
3. Start **Unreal Tournament**.
4. If no game data is found, the installer screen appears.
5. Choose one of the following:
   - **Select UT99 folder**
   - **Select UT99 ZIP**
6. Wait until the import is finished.
7. The game starts automatically once the required data is found.

The app installs the game data into its private Android data folder.

---

## ◇ Default controller layout

The default controller mapping is designed for Android gamepads, OUYA and handheld devices such as Retroid-style controllers.

| Control | Action |
|---|---|
| Left Stick | Move forward / backward / strafe |
| Right Stick | Look / turn |
| Left Trigger | Alternate Fire |
| Right Trigger | Fire |
| Left Shoulder | Previous Weapon |
| Right Shoulder | Next Weapon |
| A / right face button | Jump |
| B / bottom face button / OUYA O | Crouch |
| Y / left face button / OUYA U | Walk |
| X / top face button / OUYA Y | Wave |
| D-Pad Left / Right | Previous / next weapon; navigation in menus |
| D-Pad Up / Down | Previous / next inventory item; navigation in menus |
| Start | Pause / menu |
| Back | Back / cancel depending on context |

> [!NOTE]
> Button names can differ between Android controllers.  
> If movement or looking feels wrong, open the in-game controls menu and reassign the affected controls.

Controller movement and D-pad actions use separate bindings. Gameplay does not
also synthesize keyboard arrows from these controls. Android key and SDL hat
reports of the same D-pad press are combined, while menus still receive
navigation keys and physical keys for control reassignment. Existing
`AndroidUser.ini` bindings are preserved; custom assignments are not reset.
The phone's accelerometer is not treated as a movement joystick.

Controller input is sampled before the world update. Right-stick aiming uses
the `JoyU` / `JoyV` turn/look bindings instead of the mouse-smoothing path, and
its speed is scaled by frame time rather than FPS. Existing mouse sensitivity,
mouse-axis speed and invert settings remain the reference for aiming; controller
`ScaleRUV` also scales it. Custom right-stick direction actions remain bindable
and replace continuous aiming in their assigned direction.

Physical sticks honor `DeadZoneXYZ` (movement) and `DeadZoneRUV` (aiming) under
`[NSDLDrv.NSDLClient]` in `AndroidUT99.ini`; both default to `0.10`. Triggers
activate at **20%** travel. Touch movement keeps its existing 35% threshold,
and touch-look / physical-mouse smoothing settings are unchanged.

### Audio latency

Android audio is mixed only when the output queue has room, with at most one
software block queued ahead. Stopping audio or resuming a paused output clears
stale queued sound.

On Android 8.1+ devices using AAudio, playback requests the native output rate
and low-latency mode, with up to 512 frames requested per software block. It
requests two hardware bursts, increasing that request to at most four if
underruns occur. AAudio
partial writes are completed instead of silently dropping the remainder.
Older Android/OpenSL ES/AudioTrack paths retain their existing sample-rate and
256-1024-frame block settings.

Existing audio settings are preserved. `Latency` controls the requested block
size within those limits, not total speaker/headphone delay; the opened device
and buffer duration are logged at startup. Mixer lateness, hardware underruns
and output errors are also logged. Device parameters are kept fixed while the
mixer is running, so restart the app after changing output settings.
Bluetooth audio can still add latency outside the game's control.

---

## ▣ Game data notes

Game data is not bundled with this repository.

You need to provide your own legal copy of Unreal Tournament / UT99.  
The Android installer checks for the required folders:

```text
System
Maps
Textures
Sounds
Music
```

If these folders are missing, the game will not start and the installer screen will ask you to select a valid folder or ZIP file.

---

## ◈ Building from source

This project is intended to be built with Android Studio.

General setup:

1. Clone the repository.
2. Open the project in Android Studio.
3. Make sure the Android SDK 2022, NDK 23.1.7779620 and CMake 3.22.1 are installed.
4. Fetch or provide required third-party dependencies such as SDL2 if they are not already present (now included for all those asking AI).
5. Build the `app` module.

For GitHub Actions release APKs signed with a reusable debug key, see the
[workflow and signing setup](BUILD-FLAVORS.md#github-actions-release-apk).

Current Android build characteristics:

```text
Application ID: com.ast.ut99
Minimum SDK:   16
Target SDK:    28
Compile SDK:   33
ABI:           armeabi-v7a
Renderer:      OpenGL ES 2.0
```

---

## ▣ Credits

This Android port is based on Unreal Engine 1 / Unreal Tournament source code work and SDL/OpenGL ES based mobile porting efforts.

Special thanks to the Unreal Engine 1 preservation and porting community.

---

## ❤️ Support

If you enjoy this project and would like to support my work, you can make a small contribution via PayPal.

Your support helps me spend more time maintaining existing projects, fixing bugs, improving compatibility, and working on new features.

[![Support via PayPal](https://img.shields.io/badge/Support%20via-PayPal-0070BA?logo=paypal\&logoColor=white)](https://paypal.me/andiweli)

Thank you for your support!

---

## ◎ Legal

Unreal Tournament, Unreal Engine and related names, assets and trademarks are property of Epic Games.
Portions of the materials used are trademarks and/or copyrighted works of Epic Games, Inc.

This repository does **not** include commercial game data.  
You must own a legal copy of Unreal Tournament / UT99 to use this port.
This material is not official and is not endorsed by Epic.

All rights reserved by Epic.

Do not use this project for commercial purposes.
