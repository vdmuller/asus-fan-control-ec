# asus-fan-control-ec

asus-fan-control-ec provides fan-control support for ASUS devices, mainly those with AMD processors and systems affected by the `ACPI Error: AE_NOT_FOUND` issue, where the standard ACPI-based interface is unavailable. 

[![Build](https://github.com/Keyitdev/asus-fan-control-ec/actions/workflows/build.yml/badge.svg)](https://github.com/Keyitdev/asus-fan-control-ec/actions/workflows/build.yml)
[![Ko-fi](https://img.shields.io/badge/support_me_on_ko--fi-F16061?logo=kofi&logoColor=f5f5f5)](https://ko-fi.com/keyitdev)

**[Quick start](#quick-start) · [Build](#build) · [Usage](#usage) · [Tested devices](#tested-devices) · [How it works](HOW-IT-WORKS.md) · [Support](#support)**

---

## Why this exists

On some ASUS laptops, the fans cannot be set the standard way. There is no hwmon interface for them, no `EmbeddedControl` region for the kernel's EC driver to attach to, and no ACPI method that reaches the fan registers at all.

Existing tools such as [asus-fan-control](https://github.com/dominiksalvet/asus-fan-control) work around this as ACPI clients: through the `acpi_call` module they invoke a firmware method the vendor already provides, which rewrites the temperature thresholds of the controller's built-in curve. That depends on an ACPI EC object that these laptops do not expose, and even where it works it only shifts when the controller changes gear - it cannot set a duty.

This project takes the other route and implements the protocol itself, talking to the **embedded controller directly**. It drives a private pair of I/O ports - 0x25C for data and 0x25D for command and status, independent of the ACPI EC at 0x62/0x66 - carrying the controller's own register-table command, and it reads temperatures and tachometers out of a read-only window onto the controller's RAM mapped through /dev/mem. No kernel module, no firmware method, nothing between the program and the hardware.

## Quick start

Build first, then check whether your machine is compatible - **before writing anything to it**.

```sh
make
sudo ./asus-fan-control-ec fan-info
```
> [!IMPORTANT]
> `fan-info` is the compatibility check, and it is the only command which is almost safe to run blind:
it writes no duty and no control mode, so it cannot change how the machine is cooled.
What it does is read each fan's speed twice over two independent paths - once through
the I/O ports, once out of the telemetry aperture - and compare them. Agreement proves
the ports, the command encoding and the register numbers are all correct on your device.

Look at the last line. If it reads

```
Result: VALIDATED against the MMIO aperture.
```

you are compatible and can carry on. Anything else - a `MISMATCH` verdict, an error,
a non-zero exit status - means the program is not talking to your embedded controller
correctly. **Stop there. Do not run `set` or `setp`**. Let me know - open issue.

Only once validated:

```sh
sudo ./asus-fan-control-ec setp 90
sudo ./asus-fan-control-ec setp -1
```

`setp 90` pins both fans at 90%. `setp -1` hands the fans back to the embedded
controller, and is the only correct way to undo the previous line.

## Requirements

Root. The program needs `ioperm` (or `/dev/port`) for the control channel and `/dev/mem` for telemetry.

If your kernel was built with `CONFIG_STRICT_DEVMEM=y` - most distribution kernels are - `/dev/mem` access to the aperture may be refused. Boot with `iomem=relaxed` if telemetry fails while the ports still work.

## Build

No dependencies beyond libc and a C11 compiler.

```sh
make
```

Produces `./asus-fan-control-ec`. Object files land in `build/`.

```sh
make clean          # remove build/ and the binary
sudo make install   # installs globally - see below
sudo make uninstall # removes everything install put on the system
```

**`make install` installs globally.** It copies files out of the source tree and
into system directories: the binary onto your `PATH`, plus the example config,
the systemd unit and the bash and zsh completion scripts. That is why it needs
root. If you only want to try the program out, do not run it - build and use
`./asus-fan-control-ec` in place. `sudo make uninstall` reverses the install.

An existing `/etc/asus-fan-curve.conf` is never overwritten by `install`.

## Usage

```
asus-fan-control-ec [global options] <command> [options]
```

### Setting fan speed
#### `set DUTY`

Sets the PWM duty on every fan, or on one fan with `--fan`.

```sh
sudo asus-fan-control-ec set 140      # duty 0-255
sudo asus-fan-control-ec set -1       # hand control back to the EC
```

```
Fan count: 2, speeds before: 3575/3673 rpm.
Attempt 1/3, fans 0 1, duty 140.
fan 0:
  Set:      OK
  PWM duty: 138 (54%) (0x35=138)
fan 1:
  Set:      OK
  PWM duty: 138 (54%) (0x35=138)
Fan count: 2, speeds after: 3796/3789 rpm.
```

Reading back `138` after writing `140` is normal (duty quantisation).

`speeds after` is sampled immediately, so it shows the fans mid-spin-up, not at their final speed.

| option | meaning |
|---|---|
| `--fan N` | act on one fan only (see the warning below) |
| `--retries N` | write attempts before giving up, default 3 |
| `--no-verify` | write and exit without reading back |
| `--order mode-first\|duty-first` | order of the manual-mode and duty writes, default `mode-first` |

#### `setp PERCENT`

Same as `set`, in percent. `-1` releases, exactly like `set -1`.

```sh
sudo asus-fan-control-ec setp 55
```

#### `curve`

Runs a fan curve from a config file until stopped.

```sh
sudo asus-fan-control-ec curve
sudo asus-fan-control-ec curve --once --dry-run   # show what it would do
```

```
Curve loaded from /etc/asus-fan-curve.conf.
  below 77 C -> 0%
  77-86 C -> 15%
  86-89 C -> 20%
  89-92 C -> 40%
  92-95 C -> 60%
  95 C and above -> 80%
sensor=max interval=3.0s hysteresis=3C panic=96C
00:21:28 temp=50C target=  0% manual    applied fan0= 2173 fan1= 2186
00:21:31 temp=50C target=  0% manual    hold    fan0=    0 fan1=    0
00:21:34 temp=50C target=  0% manual    hold    fan0=    0 fan1=    0
00:21:37 temp=88C target= 20% manual    applied fan0=  890 fan1=  740
00:21:41 temp=92C target= 60% manual    applied fan0= 3024 fan1= 2366
^CHanded fan control back to the EC.
```

The mode column reflects the **actual** state, derived from the last successful operation, not from what was requested.

| status | meaning |
|---|---|
| `applied` | took on the first try |
| `retried` | took after one or more retries |
| `hold` | no change needed |
| `FAILED` | did not take; the target is not recorded, so the next tick tries again |

| option | default | meaning |
|---|---|---|
| `--config PATH` | `/etc/asus-fan-curve.conf` | curve file |
| `--interval S` | 3.0 | seconds between ticks |
| `--hysteresis C` | 3 | degrees of resistance to stepping down |
| `--panic-temp C` | 96 | force 100% at or above this |
| `--sensor cpu\|board\|max` | `max` | which temperature drives the curve |
| `--retries N` | 3 | write attempts per change |
| `--once` | | one tick, then exit without releasing |
| `--dry-run` | | print the parsed curve and exit |
| `--silent` | | suppress the periodic line; retries and failures still print |

#### Config file format

One band per line, `percent,temperature`. Blank lines and `#` comments are ignored. Order does not matter; bands are sorted by temperature.

```
# percent,temperature
-1,0
20,60
40,75
80,90
```

Reads as: below 60 °C let the EC do its thing, from 60 °C hold 20%, from 75 °C hold 40%, from 90 °C hold 80%.

A percent of **`-1` hands that band back to the embedded controller**, which is useful for staying out of the way while idle and only taking over when things get warm.

Hysteresis works on band indexes, not on percentages: the curve steps up immediately, and steps down only once `temperature + hysteresis` also falls into the lower band.

#### Running as a service

```sh
sudo systemctl enable --now asus-fan-control-ec
```

The unit runs `curve --silent`. The daemon handles `SIGTERM`, `SIGINT` and `SIGHUP` and releases the fans back to the EC on exit; `ExecStopPost` repeats the release as a safety net.


### Reading measurements
#### `fan-speed`

```
Fan control: Manual (0x31=1)
fan 0: 3963 rpm
fan 1: 4091 rpm
```

#### `fan-info`

Full state plus a validation of the control channel against the telemetry aperture. **Run this first on a new machine.**

```
Fan count: 2 (0x30=2)
Fan control: Manual (0x31=1)

fan0:
  PWM duty:       138 (54%) (0x35=138)
  Fan speed reg:  3949 rpm (0x34/0x33 = 0F 6D)
  Fan speed mmio: 3949 rpm (0xFEDD8B7C)
  Verdict:        MATCH

fan1:
  PWM duty:       138 (54%) (0x35=138)
  Fan speed reg:  4083 rpm (0x34/0x33 = 0F F3)
  Fan speed mmio: 4083 rpm (0xFEDD8B7E)
  Verdict:        MATCH

Result: VALIDATED against the MMIO aperture.
```

`MATCH` means the fan speed read over the I/O ports agrees with the same speed read out of mapped memory, within 64 rpm. Two independent paths reaching the same number proves the handshake, the command encoding, the register identification and the fan selector are all correct. Exit status is 0 only when everything matches.

The only register this command writes is the fan selector `0x32`, which it has to write in order to read anything per fan. **It never touches the duty `0x35` or the control mode `0x31`**, so it cannot change how the machine is cooled. On an untested model that distinction matters: if `0x32` turns out to mean something else there, this command has still written to it.

It does *not* prove the speed itself is accurate. Both paths originate from the same counter inside the controller.

#### `temps-info`

```
CPU:         44 C (0xFEDD8358 = 2C, ERAM+0x58 CTMP)
Board:       39 C (0xFEDD8301 = 27, ERAM+0x01 CLOT)
Max:         44 C (higher of the two)
```

#### `version`

```
Laptop model: ASUSTeK COMPUTER INC. ASUS TUF Gaming A15 FA507NV_FA507NV (DMI)
Embedded controller version: 3.18 (0xFEDD83E4 = 03 12)
Healthy table version: 17 (0xBB 50)
```
### Global options

`--cmd-port N`, `--data-port N`, `--gap SECONDS`, `--verbose`.

`--verbose` traces every byte written to the ports. `--gap` is the delay after each write, 20 ms by default; the hardware handshake alone is not enough for the controller to keep up.

## Tested devices

| Name | Model | CPU | Embedded Controller version | Support Status |
|-|-|-|-|-|
|ASUS TUF Gaming A15|FA507NV_FA507NV|AMD|3.18|Fully supported|
|ASUS TUF Gaming A15|FA506IU_FA506IU|AMD|3.19|Fully supported|


Other models in the family may work, but the ports, addresses and register numbers were confirmed on these machines only.

If your device is not supported, you may want to check out other amazing projects, such as [asus-fan-control](https://github.com/dominiksalvet/asus-fan-control).

## Reporting problems

**Start with the [Tested devices](#tested-devices) table.** Which half of it you land in
decides what a useful report looks like.

### Your device is in the table

Then the ports, addresses and register numbers are known to be correct for it, and what
you are seeing is a bug rather than an unsupported machine. Reproduce it before writing
the report:

1. Run `sudo asus-fan-control-ec version` and `sudo asus-fan-control-ec fan-info`.
2. Re-run the command that misbehaved with `--verbose`, which traces every byte written
   to the ports.
3. Write down what you expected and what happened instead.

[Open an issue](../../issues/new) with those three things, plus your distribution and
the output of `uname -r`. If you changed `--cmd-port`, `--data-port` or `--gap`, say so -
otherwise the trace is being read against the wrong assumptions.

### Your device is not in the table

Report it anyway, and keep it short. Run `fan-info` first, because its last line answers
the only question that matters:

- **`Result: VALIDATED against the MMIO aperture.`** - the protocol works on your model.
  Open an issue with the full output of `version` and `fan-info` and the device can be
  added to the table.
- **Anything else** - a `MISMATCH` verdict, an error, a non-zero exit status. Stop there,
  do not run `set` or `setp`, and open an issue with the same two outputs and whatever
  error you saw. Two sentences describing the machine are enough.

Paste output as text rather than a screenshot; register values are the whole point of
the report and they need to be searchable.

## Safety

Trademarks and ownership. ASUS, ASUSTeK, TUF and TUF Gaming are trademarks of ASUSTeK Computer Inc. They appear here descriptively, to identify the hardware this program was written for, and nothing more. This project is independent: not affiliated with, authorised by, endorsed by, or sponsored by ASUSTeK Computer Inc., and the author claims no rights in the company's trademarks, firmware, drivers or hardware designs. No vendor code is included.

> [!WARNING]
> **No warranty.** This program writes to undocumented registers of an embedded controller. Used carelessly it can stop the fans, overheat the machine, or leave the cooling system in a state the firmware does not expect - and the register family it uses reaches further than fans, into battery state and non-volatile manufacturing data. It is distributed with absolutely no warranty. You run it as root on your own hardware, at your own risk; the author accepts no liability for damage to devices, loss of data, voided warranties, or anything else that follows from its use. Read the [Safety](#safety) section before the first write, and run fan-info before trusting any of it on a model that is not in the tested table.

> [!WARNING]
> **Duty `0` stops the fans and does not give control back.** The controller accepts zero as a valid setting and holds it indefinitely, regardless of temperature. The only correct way to return control is writing `0x31 = 0` **without touching the duty** - that is what `set -1` does.
> 
>  **`--fan` does not limit the damage.** Because `0x31` is global and manual mode freezes both fans, a write aimed at one fan changes the state of both. Run `fan-info` afterwards and check the other fan's duty.
> 
> **Always release before exiting.** Any process holding a curve needs signal handling that releases the fans. `curve` does this; if you script `set` yourself, you own that responsibility.

> [!CAUTION]
> **Do not guess command codes.** The same `0xDD` command family includes battery state operations and writes to non-volatile memory holding the device's manufacturing data. Experimenting with undocumented registers **risks permanent damage**.

**Recovering a stuck Embedded controller:**
1. Remove all external devices.
2. Turn off the device.
3. Connect the power adapter.
4. Press and hold the power button for 40 seconds.
5. More info: [here](https://www.asus.com/support/faq/1050239/)

## Support

Everything this program does had to be worked out by hand: the port pair, the handshake,
the command encoding and every register number were found by probing an undocumented
controller and checking each guess against a second, independent read path. That is slow
work. If the project saved you from a laptop that runs its fans however it likes, a contribution is welcome
and keeps the work going.

[![Ko-fi](https://img.shields.io/badge/support_me_on_ko--fi-F16061?style=for-the-badge&logo=kofi&logoColor=f5f5f5)](https://ko-fi.com/keyitdev)

[https://ko-fi.com/keyitdev](https://ko-fi.com/keyitdev)

## How it works

If you want to read more: 
[How it works](HOW-IT-WORKS.md)

## License

Distributed under the **[GPLv3+](https://www.gnu.org/licenses/gpl-3.0.html) License**.    
Copyright (C) 2026 Keyitdev.
