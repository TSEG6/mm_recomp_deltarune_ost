# Changelog

## v1.0.1
### Added
- Per track volume configuration; this was added, but not yet fine-tuned
### Changed
- Default Streamed Audio note velocity is now 100 (was: 127). This makes the soundtrack a bit lower in volume
- Moved OST replacement specific behavior from Audio API to downstream mod, to prevent API from having too opinionated default behaviors

## v1.0.0 - Initial Release

### Features
- 105 remastered OGG audio tracks replacing the original Majora's Mask soundtrack
- Dual-channel audio system with remaster and original CD OST layers
- Smooth crossfade between remaster and OST channels using sine/cosine curves (180 tick / 1 second duration)
- Toggle active channel (Remaster / CD OST) with the L button
- On-screen notification via ProxyMM Notifications when switching channels

### Tracks
- All major BGM tracks: overworld regions, dungeons, towns, boss battles, cutscenes, and credits
- Fanfare tracks: item get, song learned, boss clear, game over, and more
- Song playback tracks: Sonata of Awakening, Goron Lullaby, New Wave Bossa Nova, Elegy of Emptiness, Oath to Order, Song of Soaring, Ballad of the Wind Fish
- Special sequence IO support for Bremen March, Ballad of the Wind Fish, Frog Song, and End Credits
- Pointer variant coverage for Clock Town Day 2, Fairy Fountain, Milk Bar duplicate, and Majora's Lair

### Dependencies
- Requires MageMods AudioAPI v0.5.0+
- Requires ProxyMM Notifications v0.0.1+
- Built for Zelda 64: Recompiled v1.2.1+
