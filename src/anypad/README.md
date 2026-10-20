# Files taken from AnyPad PS5

Source: https://github.com/elmonomalvad0/any-pad, commit `44b8312c9abaf4aaff03d0c74c62a4da010c0108`
(0.9.5-beta), GPL-3.0-or-later (see `LICENSE`). Thanks to its author for the virtual DualSense path.

Taken as is: `ps5_vpad.c/.h` (virtual DualSense), `log.c/.h`, `util.c/.h`, `pad.h`, `ps5_power.c/.h`.

Changes:
- `ps5_vpad.c`: the user-choice file moves from `/data/anypad/bind_user` to `/data/xpad-ps5/bind_user`.
- `host.h`: replaced by a stub that only defines `HOST_MAX_PADS`.
