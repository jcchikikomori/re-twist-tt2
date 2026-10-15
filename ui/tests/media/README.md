# Probe media

Tiny files for `tt2 --probe-media`, which CI runs against every build and package to prove its image and audio
decoders work. Made for this project (MIT, like the code); nothing here comes from Text Twist 2.

| File | Made with | Purpose |
| --- | --- | --- |
| `probe.jpg` | Pillow 9.0.1, 16x16 gradient, baseline JPEG (like the splash) | JPEG decoder present |
| `probe.png` | Pillow 9.0.1, 16x16 RGBA with an alpha ramp | PNG decoder present |
| `probe.ogg` | ffmpeg `sine=440`, 0.2 s mono, libvorbis | Ogg Vorbis decoder present |
| `broken.png`, `broken.ogg` | a valid signature followed by junk | a corrupt file must fail |
