// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * ASUS EC fan duty control using the private 0x25c/0x25d protocol
 * implemented by asus-fan-control-ec.
 *
 * This driver intentionally exposes duty control only. It does not expose
 * the userspace project's RPM/MMIO telemetry path.
 */
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#define DRIVER_NAME "asus_ec_fan"
#define DATA_PORT 0x25c
#define CMD_PORT  0x25d
#define PREAMBLE  0xff
#define CMD_TABLE 0xdd
#define TBL_READ  0x02
#define TBL_WRITE 0x82
#define REG_TEST_MODE 0x31
#define REG_FAN_INDEX 0x32
#define REG_PWM_DUTY  0x35
#define OBF 0x01
#define IBF 0x02
#define POLL_LIMIT 1000
#define POLL_MIN_US 100
#define POLL_MAX_US 150
#define FAN_COUNT 2

struct asus_ec_fan {
	struct device *hwmon_dev;
	struct mutex lock;
	bool manual;
	u8 duty[FAN_COUNT];
};

static struct asus_ec_fan *ecfan;
static unsigned int io_gap_ms = 20;
module_param(io_gap_ms, uint, 0644);
MODULE_PARM_DESC(io_gap_ms, "Delay after EC writes in milliseconds (default 20)");

static const struct dmi_system_id asus_ec_fan_dmi_table[] = {
	{ .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "FX608JHR"),
	} },
	{ }
};
MODULE_DEVICE_TABLE(dmi, asus_ec_fan_dmi_table);

static int ec_wait_ibf_clear(void)
{
	int i;
	for (i = 0; i < POLL_LIMIT; i++) {
		if (!(inb(CMD_PORT) & IBF))
			return 0;
		usleep_range(POLL_MIN_US, POLL_MAX_US);
	}
	return -ETIMEDOUT;
}

static int ec_drain_obf(void)
{
	int i;
	for (i = 0; i < POLL_LIMIT; i++) {
		if (!(inb(CMD_PORT) & OBF))
			return 0;
		inb(DATA_PORT);
		usleep_range(POLL_MIN_US, POLL_MAX_US);
	}
	return -ETIMEDOUT;
}

static int ec_wait_obf_set(void)
{
	int i;
	for (i = 0; i < POLL_LIMIT; i++) {
		if (inb(CMD_PORT) & OBF)
			return 0;
		usleep_range(POLL_MIN_US, POLL_MAX_US);
	}
	return -ETIMEDOUT;
}

static int ec_xact(const u8 *payload, size_t len, bool read_result, u8 *result)
{
	size_t i;
	int ret;

	if (len > 8)
		return -EINVAL;

	ret = ec_drain_obf();
	if (ret)
		return ret;
	ret = ec_wait_ibf_clear();
	if (ret)
		return ret;
	outb(PREAMBLE, CMD_PORT);

	ret = ec_wait_ibf_clear();
	if (ret)
		return ret;
	outb(CMD_TABLE, CMD_PORT);

	for (i = 0; i < len; i++) {
		ret = ec_wait_ibf_clear();
		if (ret)
			return ret;
		outb(payload[i], DATA_PORT);
	}
	ret = ec_wait_ibf_clear();
	if (ret)
		return ret;

	if (read_result) {
		ret = ec_wait_obf_set();
		if (ret)
			return ret;
		if (result)
			*result = inb(DATA_PORT);
	}
	return 0;
}

static int ec_table_write(u8 reg, u8 value)
{
	const u8 payload[] = { TBL_WRITE, reg, value };
	int ret = ec_xact(payload, ARRAY_SIZE(payload), false, NULL);

	if (!ret && io_gap_ms)
		msleep(io_gap_ms);
	return ret;
}

static int ec_table_read(u8 reg, u8 *value)
{
	const u8 payload[] = { TBL_READ, reg, 0x00 };
	return ec_xact(payload, ARRAY_SIZE(payload), true, value);
}

static int ec_select_fan(unsigned int fan)
{
	return ec_table_write(REG_FAN_INDEX, fan);
}

static int ec_set_manual(bool manual)
{
	return ec_table_write(REG_TEST_MODE, manual ? 1 : 0);
}

static int ec_set_duty(unsigned int fan, u8 duty)
{
	int ret;

	ret = ec_select_fan(fan);
	if (ret)
		return ret;
	ret = ec_set_manual(true);
	if (ret)
		return ret;
	/* 0x31 is global, and setting it may disturb the selector. */
	ret = ec_select_fan(fan);
	if (ret)
		return ret;
	ret = ec_table_write(REG_PWM_DUTY, duty);
	if (!ret) {
		ecfan->manual = true;
		ecfan->duty[fan] = duty;
	}
	return ret;
}

static int ec_release(void)
{
	int ret = ec_select_fan(0);

	if (ret)
		return ret;
	ret = ec_set_manual(false);
	if (!ret)
		ecfan->manual = false;
	return ret;
}

static umode_t asus_ec_fan_is_visible(const void *data,
		enum hwmon_sensor_types type, u32 attr, int channel)
{
	if (type != hwmon_pwm || channel >= FAN_COUNT)
		return 0;
	if (attr == hwmon_pwm_input)
		return 0644;
	if (attr == hwmon_pwm_enable && channel == 0)
		return 0644;
	return 0;
}

static int asus_ec_fan_read(struct device *dev, enum hwmon_sensor_types type,
		u32 attr, int channel, long *val)
{
	int ret = 0;
	u8 value;

	if (type != hwmon_pwm || channel >= FAN_COUNT)
		return -EOPNOTSUPP;

	mutex_lock(&ecfan->lock);
	if (attr == hwmon_pwm_input) {
		ret = ec_select_fan(channel);
		if (!ret)
			ret = ec_table_read(REG_PWM_DUTY, &value);
		if (!ret) {
			ecfan->duty[channel] = value;
			*val = value;
		}
	} else if (attr == hwmon_pwm_enable) {
		*val = ecfan->manual ? 1 : 2;
	} else {
		ret = -EOPNOTSUPP;
	}
	mutex_unlock(&ecfan->lock);
	return ret;
}

static int asus_ec_fan_write(struct device *dev, enum hwmon_sensor_types type,
		u32 attr, int channel, long val)
{
	int ret;

	if (type != hwmon_pwm || channel >= FAN_COUNT)
		return -EOPNOTSUPP;

	mutex_lock(&ecfan->lock);
	if (attr == hwmon_pwm_input) {
		if (val < 0 || val > 255)
			ret = -EINVAL;
		else
			ret = ec_set_duty(channel, val);
	} else if (attr == hwmon_pwm_enable && channel == 0) {
		if (val == 1) {
			ret = ec_set_manual(true);
			if (!ret)
				ecfan->manual = true;
		} else if (val == 2) {
			ret = ec_release();
		} else {
			ret = -EINVAL;
		}
	} else {
		ret = -EOPNOTSUPP;
	}
	mutex_unlock(&ecfan->lock);
	return ret;
}

static const struct hwmon_ops asus_ec_fan_hwmon_ops = {
	.is_visible = asus_ec_fan_is_visible,
	.read = asus_ec_fan_read,
	.write = asus_ec_fan_write,
};

static const struct hwmon_channel_info * const asus_ec_fan_info[] = {
	HWMON_CHANNEL_INFO(pwm,
		HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
		HWMON_PWM_INPUT),
	NULL,
};

static const struct hwmon_chip_info asus_ec_fan_chip_info = {
	.ops = &asus_ec_fan_hwmon_ops,
	.info = asus_ec_fan_info,
};

static int __init asus_ec_fan_init(void)
{
	int ret;
	u8 mode;

	if (!dmi_check_system(asus_ec_fan_dmi_table))
		return -ENODEV;

	if (!request_region(DATA_PORT, 2, DRIVER_NAME))
		return -EBUSY;

	ecfan = kzalloc(sizeof(*ecfan), GFP_KERNEL);
	if (!ecfan) {
		release_region(DATA_PORT, 2);
		return -ENOMEM;
	}
	mutex_init(&ecfan->lock);

	mutex_lock(&ecfan->lock);
	ret = ec_select_fan(0);
	if (!ret)
		ret = ec_table_read(REG_PWM_DUTY, &ecfan->duty[0]);
	if (!ret)
		ret = ec_select_fan(1);
	if (!ret)
		ret = ec_table_read(REG_PWM_DUTY, &ecfan->duty[1]);
	if (!ret)
		ret = ec_table_read(REG_TEST_MODE, &mode);
	if (!ret)
		ecfan->manual = !!mode;
	mutex_unlock(&ecfan->lock);
	if (ret)
		goto err_free;

	ecfan->hwmon_dev = hwmon_device_register_with_info(NULL, DRIVER_NAME,
			ecfan, &asus_ec_fan_chip_info, NULL);
	if (IS_ERR(ecfan->hwmon_dev)) {
		ret = PTR_ERR(ecfan->hwmon_dev);
		goto err_free;
	}

	pr_info(DRIVER_NAME ": registered for ASUS FX608JHR\n");
	return 0;

err_free:
	kfree(ecfan);
	ecfan = NULL;
	release_region(DATA_PORT, 2);
	return ret;
}

static void __exit asus_ec_fan_exit(void)
{
	if (!ecfan)
		return;

	hwmon_device_unregister(ecfan->hwmon_dev);
	mutex_lock(&ecfan->lock);
	if (ecfan->manual && ec_release())
		pr_warn(DRIVER_NAME ": failed to return fan control to EC\n");
	mutex_unlock(&ecfan->lock);
	kfree(ecfan);
	ecfan = NULL;
	release_region(DATA_PORT, 2);
}

module_init(asus_ec_fan_init);
module_exit(asus_ec_fan_exit);

MODULE_AUTHOR("Victor Muller / asus-fan-control-ec contributors");
MODULE_DESCRIPTION("ASUS EC fan duty control via private x86 I/O ports");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.0");
