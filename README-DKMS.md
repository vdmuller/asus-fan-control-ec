# ASUS EC fan DKMS module

Experimental DKMS/hwmon driver for ASUS TUF Gaming F16 FX608JHR.

The driver reuses the private 0x25c/0x25d register-table transaction already
implemented by this repository. It intentionally exposes duty control only:
no RPM/tachometer or /dev/mem telemetry.

## hwmon interface

- `pwm1`, `pwm2`: duty cycle, 0-255, read/write.
- `pwm1_enable`: 1 = manual, 2 = EC automatic.

The EC's manual-mode register (0x31) is global. Therefore only pwm1_enable is
exposed and it controls both fans. Writing pwm1/pwm2 automatically enters
manual mode using the same mode-first sequence as the userspace implementation.

This module is DMI-gated to ASUSTeK COMPUTER INC. / FX608JHR.

