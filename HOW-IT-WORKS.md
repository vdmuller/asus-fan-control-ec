## How it works

Some information stated in this document may be incorrect, this document is the result of my reverse engineering.
 
### Control channel

Fan control is **not** a memory region you can poke. It is a command protocol on a private pair of I/O ports, independent of the ACPI EC (`0x62`/`0x66`) and of the OEM host interface (`0x6C`/`0x68`).

| port | role |
|---|---|
| `0x25C` | data |
| `0x25D` | command and status |

Status bits on `0x25D`: `0x01` OBF (a byte is waiting to be read), `0x02` IBF (input buffer busy, do not write).

A transaction is: drain any stale output, wait for IBF to clear, write the preamble `0xFF` to the command port, write the command, then write each payload byte to the data port, waiting for IBF between bytes. For a read, wait for OBF and take the result from the data port.

Two commands matter:

| command | payload | meaning |
|---|---|---|
| `0xBB` | `50` | table version, one byte returned |
| `0xDD` | `<selector> <register> <value>` | register table access |

In the selector byte, **bit 7 selects the direction**: `0x02` reads from table 2, `0x82` writes to it.

So reading register `0x30` is `DD 02 30 00`, and writing 140 to register `0x35` is `DD 82 35 8C`.

### Registers

All in table 2, all reached with command `0xDD`.

| reg | name | access | range | notes |
|---|---|---|---|---|
| `0x30` | fan count | R | `2` | constant; used as a liveness check before any write |
| `0x31` | fan control mode | R/W | `0` / `1` | `0` = EC's own curve, `1` = manual. **Global** |
| `0x32` | fan select | W | `0`–`1` | chooses which fan `0x33`/`0x34`/`0x35` refer to |
| `0x33` | tachometer, low byte | R | | |
| `0x34` | tachometer, high byte | R | | `rpm = (0x34 << 8) \| 0x33` |
| `0x35` | PWM duty | R/W | `0`–`255` | per fan |

Two properties are easy to get wrong:

**`0x31` is global.** It is one bit for the whole controller, not one per fan. Clearing it while fan 1 is selected releases fan 0 as well. Consequently `--fan` does not confine the effect of a write.

**Entering manual mode freezes both fans** wherever the EC's curve last put them. `set X --fan 0` therefore means "freeze both fans, and give fan 0 the value X" - the fan you did not name stops responding to temperature and sits at whatever duty it happened to have.

**While `0x31` is `0`, register `0x35` belongs to the controller.** It writes its own computed duty there and overwrites anything the host puts in. Reading it in that state is useful telemetry: it shows what the EC's curve is currently doing.

### Setting a fan

```
DD 82 32 <index>       select the fan
DD 82 31 01            manual mode
DD 82 32 <index>       re-assert the selection
DD 82 35 <duty>        duty 0-255
```

Order matters. A duty written while `0x31` is still `0` gets overwritten by the EC's curve before manual mode engages - the vendor software writes in the opposite order and has this bug.

The selector is written twice on purpose. Coming out of automatic mode, a duty write has been observed landing on the wrong fan, as if the mode write had cleared the selection. Re-asserting it costs one transaction and removes the failure.

Releasing is the mode register alone, and nothing else:

```
DD 82 31 00            leave the duty untouched
```

### Reading fan speed

Two independent paths:

- **Registers** `0x34` and `0x33` over the ports, three transactions per fan.
- **The aperture**, a read-only mirror of the controller's RAM in host physical memory. Fan speeds sit at `0xFEDD8B7C` and `0xFEDD8B7E` as big-endian 16-bit values, ready to use.

The aperture is the better source for telemetry: two bytes at roughly 1.2 µs each, no transaction, no contention with writes. `fan-info` reads both and compares them - that comparison is the validation test.

### Temperature

Temperature is **never written and never read over the control ports.** It comes entirely from the aperture, one byte per sensor:

| field | address | meaning | confidence |
|---|---|---|---|
| `CTMP` | `0xFEDD8358` | CPU temperature, whole °C | confirmed |
| `CLOT` | `0xFEDD8301` | second sensor, whole °C | name only |

`CTMP` is confirmed by the machine's own ACPI tables. The thermal zone's `_TMP` method reads that exact byte and converts it with `TTMP = CTMP * 10 + 2732`, which is degrees Celsius turned into tenths of a kelvin. That pins down the location, the unit and the fact that the rest of the system trusts the same number.

`CLOT` is declared in the ACPI field but never read by anything, so its meaning rests on the field name alone. Treat it as unverified.

Practical notes:

- **One unsigned byte.** Whole degrees, no fractions, no negatives. Do not expect it to agree with `k10temp` to the decimal - that driver reads the CPU's own registers over a completely different path.
- **The refresh rate belongs to the EC.** Reading a hundred times a second gets you nothing that reading once a second does not.
- **The controller does not measure the CPU.** It receives that number from the SoC and stores it. You are reading the EC's picture of the world.

You cannot set a temperature. What the curve does is map a temperature to a percent, convert that to a duty with `duty = round(percent * 255 / 100)`, and write the duty. `--sensor` chooses which of the two readings drives the mapping: `cpu`, `board`, or `max` (the default, and the safer choice with two sensors on one curve).

---

## The vendor path on Windows

Recorded for comparison, and as the origin of the findings above.

```
HealthyTable_SetFanPwmDuty(duty)              AsusWinIO64.dll, RVA 0x28AE0
  └─ RwEcCmd(len, cmd, buf, dir, &status)     RVA 0x200D0
       └─ DeviceIoControl(\\.\AsusSAIO,
                          0x80102070,
                          req, 16, req, 16)
            └─ IOCTL dispatch                 AsusSAIO.sys, RVA 0x12A0
                 └─ handler                   RVA 0x1AA0
                      └─ transaction core     RVA 0x1828
                           └─ port write      RVA 0x1D70
                                └─ out dx, al RVA 0x1DCB
```

The 16-byte request structure, same buffer in and out:

| offset | type | field |
|---|---|---|
| `0x00` | u8 | command code, `0xBB` or `0xDD` |
| `0x01` | u8 | payload length, 1–8 |
| `0x02` | u8[8] | payload |
| `0x0A` | u8 | direction: 1 expects a returned byte, 0 is a write |
| `0x0B` | u8 | byte returned by the EC |
| `0x0C` | u32 | status, `0` = OK, `0x102` = timeout |

The library needs **SYSTEM**, not merely administrator, and the ASUS System Analysis service has to be running.

- **`0x12A0`** is the IOCTL dispatcher. It compares the control code against a list; `cmp eax, 0x80102070` sits at `0x1367` and branches to `0x13F7`.
- **`0x13F7`** rejects the request unless both buffers are at least `0x10` bytes, copies 16 bytes in, calls `0x1AA0`, copies 16 bytes back and sets `Information = 0x10`. This is where the *16 bytes, same buffer both ways* claim comes from.
- **`0x1AA0`** unpacks the structure exactly as tabulated: command from `[buf]`, length from `[buf+1]`, payload pointer `buf+2`, direction from `[buf+0xA]`, status pointer `buf+0xC`; after calling `0x1828` it stores the returned byte at `[buf+0xB]`. The call is bracketed by `KeWaitForSingleObject` and `KeReleaseMutex` - the serialising mutex is real, and any independent implementation needs its own.
- **`0x1828`** is the transaction core and carries the port numbers as literals: `mov edx, 0x25c`, and `mov edx, 0x25d` paired with `mov cl, 0xff` - the preamble byte written to the command port.
- **`0x1734`** is the status-bit wait. It polls at most `0x3E8` = **1000** times and returns `0x102` on expiry. Each iteration calls a helper at `0x1000` that wraps `KeDelayExecutionThread` with a relative interval of `arg × -10` in 100 ns units; the caller passes `0x64` = 100, so **100 µs per poll**. The `POLL_LIMIT` and `POLL_DELAY` constants in this program are the same numbers, arrived at independently.
- **`0x1D70`** is a generic port write that switches on a width byte: 1 → `out dx, al` at `0x1DCB`, 2 → `out dx, ax`, 4 → `out dx, eax`.

Two imports are worth noting. `MmMapIoSpace` / `MmUnmapIoSpace` are the driver's own route to the telemetry aperture - the same physical page this program maps through `/dev/mem`. `IoGetCurrentProcess`, `ZwOpenProcessTokenEx`, `ZwQueryInformationToken` and `RtlEqualSid` are the caller check that makes administrator insufficient: the driver compares the calling token's SID rather than checking for elevation.
