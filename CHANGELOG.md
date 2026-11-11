# Changelog

All notable changes to AxSynth are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.3.0]

### Added

- Presets: Init, Pad, Synth, Pluck, Bass, Keys, Arp, Sweep (selector next to the gear icon and in the DAW preset menu).
- Lines: play 1 to 4 wavetable lines at once; Spread sets how far apart they are. Lines are spread across the stereo field.
- Contrast: stretches the brightness range inside the rectangle, so small changes in the scene (a hand, a shadow) change the sound clearly.
- Filter Env: the envelope opens the filter by up to 5 octaves (plucks, basses).

### Changed

- The rectangle height maps to the harmonics like an EQ (low at the top, high at the bottom, log spaced), so every part of the rectangle is audible. Before, almost all of the sound came from the top few percent.

## [1.2.0]

### Added

- Tone: sets how fast the harmonics fall off (0.5 bright to 2.5 dark). The default 1.2 gives a fuller, rounder sound than before (fixed at 0.7 in earlier versions).
- Unison: 1 to 7 detuned oscillators per voice, spread across the stereo field (default 3), with a Detune control (0 to 50 cents, default 12).
- Sub: sine oscillator one octave below the note, mixed before the filter.

### Changed

- Output is now stereo (unison spread). A mono track gets the mono sum.

## [1.1.0]

### Changed

- Full ADSR envelope: new Decay and Sustain controls (Sustain defaults to 100 %, so existing projects sound the same).
- The active wavetable frame moves through the 3D stack with Position instead of staying in front.
- Connect timeout: an unreachable camera is reported after 2 s.
- Licensed under the MIT License.
- The installer lets you choose the install folder (default `C:\Program Files\Common Files\VST3`); upgrades reuse the chosen folder.

### Fixed

- Wavetable: lower mipmaps could clip and alias on some images; all mips now share one scale.
- Host-synced LFO: bar divisions follow the host's time signature (3/4, 6/8, ...).
- Corrupt project values (NaN) no longer silence the plugin.
- MIDI: All Sound Off silences at once, Reset All Controllers resets pitch bend and mod wheel; mod wheel changes are smoothed.
- Camera: a wrong password is reported correctly when a port is given.
- Camera: credentials are only sent after the camera asks for them, so the password is never sent as plain Basic auth to a server that did not request it.
- Camera: oversized or malformed JPEG frames are rejected instead of using a lot of memory.
- Camera: auto-reconnect stops after a wrong password; a project opened on another PC or Windows user (password not decryptable) opens the setup panel and keeps the stored password.
- Saving a project twice gives identical data, so hosts no longer mark it as modified.
- UI: the rectangle is hidden while the setup panel is open; clicks on the plugin no longer take keyboard focus from the DAW; the setup panel no longer closes before showing an error after reconnecting; a double right-click on LFO Rate no longer resets it.
- Builds use MSVC 2022 again and treat compiler warnings as errors.

## [1.0.0]

### Added

- Wavetable synthesizer as a VST3 instrument for Windows 64-bit.
- Live wavetables from an Axis network camera (MJPEG stream): the brightness inside a user-drawn rectangle becomes 32 wavetable frames with 64 harmonics each.
- Camera connection dialog (IP, user, password, "Verbinden") with automatic HTTPS/HTTP and Digest/Basic detection, resolution discovery and silent reconnect.
- Controls: Position with LFO (free or tempo synced), Cutoff, Resonance, Attack, Release, Output.
- Camera connection and rectangle are saved with the DAW project and restored on load; the password is stored encrypted (Windows DPAPI).
- MSI installer that installs to `C:\Program Files\Common Files\VST3`.
