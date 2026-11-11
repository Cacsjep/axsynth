# Changelog

All notable changes to AxSynth are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.0.1]

### Fixed

- Wavetable: lower mipmaps could clip and alias on some images; all mips now share one scale.
- Host-synced LFO: bar divisions follow the host's time signature (3/4, 6/8, ...).
- Corrupt project values (NaN) no longer silence the plugin.
- MIDI: All Sound Off silences at once, Reset All Controllers resets pitch bend and mod wheel; mod wheel changes are smoothed.
- Camera: "nicht erreichbar" now appears within 5 s; a wrong password is reported correctly when a port is given.
- Camera: credentials are only sent after the camera asks for them, so the password is never sent as plain Basic auth to a server that did not request it.
- Camera: oversized or malformed JPEG frames are rejected instead of using a lot of memory.
- Camera: auto-reconnect stops after a wrong password; a project opened on another PC or Windows user (password not decryptable) opens the setup panel and keeps the stored password.
- Saving a project twice gives identical data, so hosts no longer mark it as modified.
- UI: clicks on the plugin no longer take keyboard focus from the DAW; the setup panel no longer closes before showing an error after reconnecting; a double right-click on LFO Rate no longer resets it.
- Builds use MSVC 2022 again and treat compiler warnings as errors.

## [1.0.0]

### Added

- Wavetable synthesizer as a VST3 instrument for Windows 64-bit.
- Live wavetables from an Axis network camera (MJPEG stream): the brightness inside a user-drawn rectangle becomes 32 wavetable frames with 64 harmonics each.
- Camera connection dialog (IP, user, password, "Verbinden") with automatic HTTPS/HTTP and Digest/Basic detection, resolution discovery and silent reconnect.
- Controls: Position with LFO (free or tempo synced), Cutoff, Resonance, Attack, Release, Output.
- Camera connection and rectangle are saved with the DAW project and restored on load; the password is stored encrypted (Windows DPAPI).
- MSI installer that installs to `C:\Program Files\Common Files\VST3`.
