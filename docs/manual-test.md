# Manual acceptance test

Run before every release with the MSI from a release workflow dry run of the commit you will tag, on a clean Windows 10/11 machine. After publishing, install the published MSI once and repeat the Install section. A release needs every box checked in both DAWs. Record the result in the sign-off table at the bottom.

Requirements: an Axis camera reachable on the network, with its IP, user and password.

## Install

- [ ] Run `AxSynth-<version>-win64.msi`: license page, UAC prompt, install, finish. No other options.
- [ ] `C:\Program Files\Common Files\VST3\AxSynth.vst3\Contents\x86_64-win\AxSynth.vst3` exists.
- [ ] Installing a newer version replaces the old one (only one AxSynth entry in Apps); installing an older one over a newer one is refused.

## FL Studio

- [ ] Plugin scan finds AxSynth; load it on a channel.
- [ ] Before connecting: playing notes gives a clean sine, no crackles.
- [ ] Gear icon, enter IP, user, password, click **Verbinden**: the camera image appears.
- [ ] Wrong password: a short one-line German error message, no crash, no hang.
- [ ] Drag a rectangle over the image; the sound follows the image content while notes play.
- [ ] Position, LFO, Cutoff, Resonance, Attack, Release, Output respond and can be automated.
- [ ] Click a knob, then use the computer keyboard (typing keyboard, transport keys): FL Studio gets the keys, not the plugin.
- [ ] Save the project, close FL Studio completely, restart it, open the project.
- [ ] The camera reconnects by itself and the rectangle is in the same place.
- [ ] Unplug the camera network cable while playing: audio keeps running, the plugin reconnects when the cable is back.

## Ableton Live

- [ ] Plugin scan finds AxSynth; load it on a MIDI track.
- [ ] Before connecting: playing notes gives a clean sine, no crackles.
- [ ] Gear icon, enter IP, user, password, click **Verbinden**: the camera image appears.
- [ ] Drag a rectangle over the image; the sound follows the image content while notes play.
- [ ] Save the Live Set, quit Live completely, restart it, open the set.
- [ ] The camera reconnects by itself and the rectangle is in the same place.
- [ ] Two AxSynth instances in one set work independently.

## Uninstall

- [ ] Uninstall via Settings, Apps: the `AxSynth.vst3` folder is gone, other plugins in `Common Files\VST3` are untouched.

## Sign-off

| Version | Date | Tester | FL Studio | Ableton Live | Notes |
|---------|------|--------|-----------|--------------|-------|
|         |      |        |           |              |       |
