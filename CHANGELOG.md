# Changelog

All notable changes to AxSynth are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.0.0]

### Added

- Wavetable synthesizer as a VST3 instrument for Windows 64-bit.
- Live wavetables from an Axis network camera (MJPEG stream): the brightness inside a user-drawn rectangle becomes 32 wavetable frames with 64 harmonics each.
- Camera connection dialog (IP, user, password, "Verbinden") with automatic HTTPS/HTTP and Digest/Basic detection, resolution discovery and silent reconnect.
- Controls: Position with LFO (free or tempo synced), Cutoff, Resonance, Attack, Release, Output.
- Camera connection and rectangle are saved with the DAW project and restored on load; the password is stored encrypted (Windows DPAPI).
- MSI installer that installs to `C:\Program Files\Common Files\VST3`.
