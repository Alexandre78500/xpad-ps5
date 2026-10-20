# xpad-ps5

**Plug a wired Xbox controller into a jailbroken PS5 and play.** No adapter, no Bluetooth, no batteries.

xpad-ps5 is a payload that reads an Xbox One / Series controller over USB and presents it to the console as a
**virtual DualSense**. The system menu, PS4/PS5 games and emulators see an ordinary Sony controller.

```
Xbox controller ──USB──▶ xpad-ps5 (GIP driver) ──▶ virtual DualSense ──▶ menu, games, emulators
```

## Why this one

Other payloads cover controllers on a jailbroken PS5 (see [Related projects](#related-projects)). xpad-ps5 does
one thing, wired Xbox pads, and goes all the way:

- **The Xbox button is the PS button.** Press it to open the PS menu, like on a DualSense.
- **Hot-plug.** Unplug the controller mid-game, plug it back: it comes back by itself. Nothing to relaunch.
- **Starts at boot.** One command uploads it to Payload Manager and adds it to the autoload list, without FTP.
- **Tells you what is wrong.** Until a controller shows up, the payload copies the console's kernel USB log into
  its own log. A charge-only cable (the controller rumbles but is never seen) shows up as
  `giving up port reset - device vanished`, or as nothing at all.
- **Stick deadzone tuned on real hardware.** Radial deadzone of 4000/32767, from the rest drift measured on a
  real pad (-2950 on the left vertical axis). Full travel is kept outside the deadzone.
- **Plays well with the console.** Leaves cleanly before rest mode, removes its virtual pad when it stops, and
  retries the virtual pad if the console is slow to create it.
- **Confirmed on a real Microsoft pad:** Xbox One S controller (`045e:02ea`), PS5 Slim, firmware 13.40, menu and
  games.

## Button layout

Mapped by position, like a Sony pad:

| Xbox | DualSense |
|---|---|
| A / B / X / Y | Cross / Circle / Square / Triangle |
| LB / RB | L1 / R1 |
| LT / RT | L2 / R2 (analog) |
| Stick clicks | L3 / R3 |
| Menu | Options |
| View | Create |
| Xbox | PS |
| **View + Menu together** | Touchpad click |

## Requirements

- A jailbroken PS5 with a payload loader. Tested chain: WebKit Autoloader 0.6.1 → Relapse → Payload Manager 0.6.0 →
  kstuff-lite 1.11 → ShadowMountPlus 1.7-beta5fix1, firmware 13.40.
- An Xbox One or Xbox Series controller (or any pad that speaks GIP: USB interface class `ff/47/d0`).
- **A USB data cable.** Many micro-USB cables only charge. If your computer does not see the controller with that
  cable, the PS5 will not either.

## Install

### With Payload Manager (recommended)

```bash
PS5_HOST=<console ip> tools/send.sh xpad-ps5.elf --autoload
```

This uploads the ELF through Payload Manager's own upload API (`POST /manage:upload`, the same call as its web
page), appends `local/xpad-ps5` to the autoload list, and starts it. Leave out `--autoload` for a one-off run.
You can also upload `xpad-ps5.elf` from Payload Manager's page by hand.

### With any ELF loader

Send `xpad-ps5.elf` to your loader as usual (for example port 9021).

When the controller is ready it rumbles once. If another controller is already signed in to your account, the
console asks **"Who is using this controller?"**: pick a user or Guest, like for a second DualSense.

## On the console

- Log: `/data/xpad-ps5/xpad-ps5.log`, and read-only at `http://<console ip>:8096/`. The port also acts as a lock,
  so only one instance runs at a time. The page is open to your local network (like Payload Manager's), so it
  only shows the payload's own messages and leaves out any line that mentions a user: the raw kernel log and the
  console's user ids stay in the file on the console. It accepts no commands.
- The controller is looked for every second.
- To stop it: rest mode, `SIGTERM` (Payload Manager's process list), or create `/data/xpad-ps5/stop`.
- To choose the user: by default the virtual pad goes to the user who started the console, otherwise the user in
  the foreground. Put `other` (another signed-in user) or a hexadecimal user id in `/data/xpad-ps5/bind_user` to
  change that.

## Limits

- **One controller** at a time.
- **No rumble from games.** The only rumble is the short one that says the controller is ready.
- Xbox 360 controllers do not speak GIP and are not supported.
- Emulators (Porpoise, PS5SX2, RetroArch, ProsperoEden) read pads through libScePad like games do, so the virtual
  DualSense should work there. Not yet checked one by one.
- Tested on one console and firmware (PS5 Slim, 13.40). Reports from other setups are welcome: open an issue
  with your log.

## Build

```bash
make test                                  # protocol unit tests, any Unix with cc
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk make ps5    # dist/xpad-ps5.elf
```

Needs [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (built here with 0.44 and LLVM 18; on macOS set
`LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config`).

### How it works

- `src/gip.c`: the GIP protocol (power-on, input reports, Xbox button and its ack, deadzone), from the Linux
  [xpad](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c) driver. Unit-tested in
  `tests/test_gip.c`.
- `src/main.c`: finds the pad among `/dev/ugen*` through FreeBSD's `USB_FS_*` ioctls, keeps interrupt reads
  pending, and feeds the virtual pad every 16 ms.
- `src/anypad/`: the virtual DualSense (`scePadVirtualDeviceAddDevice` + MBus binding), taken from
  [AnyPad PS5](https://github.com/elmonomalvad0/any-pad). See `src/anypad/README.md`.

## Related projects

As described by their own READMEs on 2026-10-20:

| | Link | Pads | Tested |
|---|---|---|---|
| FGG-XSense | [FGGstore/FGG-XSense](https://github.com/FGGstore/FGG-XSense) | wired GIP (Xbox) | 11.60, GameSir GIP pad; no PS button; stops on unplug |
| OmniPad PS5 | [diegobarbosaa/OmniPad-PS5](https://github.com/diegobarbosaa/OmniPad-PS5) | many USB pads, up to 3 virtual | 13.60, Machenike; Xbox marked experimental |
| AnyPad PS5 | [elmonomalvad0/any-pad](https://github.com/elmonomalvad0/any-pad) | Bluetooth pads | 10.01 |
| PadBridge PS5 | [X-F1REBALL-X/PadBridge-PS5](https://github.com/X-F1REBALL-X/PadBridge-PS5) | Bluetooth pads, up to 4 | see its README |

If you need several controllers at once, or non-Xbox pads, OmniPad or the Bluetooth projects may suit you better.

## License

GPL-3.0-or-later (see `LICENSE`). The virtual DualSense code comes from AnyPad PS5, also GPL-3.0-or-later.

Not affiliated with Sony or Microsoft. Use at your own risk on a console you own.
