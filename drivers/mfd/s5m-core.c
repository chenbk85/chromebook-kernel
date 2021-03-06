/*
 * s5m87xx.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/s5m87xx/s5m-core.h>
#include <linux/mfd/s5m87xx/s5m-pmic.h>
#include <linux/mfd/s5m87xx/s5m-rtc.h>
#include <linux/regmap.h>

#ifdef CONFIG_OF
static struct of_device_id __devinitdata s5m87xx_pmic_dt_match[] = {
	{.compatible = "samsung,s5m8767-pmic"},
	{},
};
#endif

static struct mfd_cell s5m8751_devs[] = {
	{
		.name = "s5m8751-pmic",
	}, {
		.name = "s5m-charger",
	}, {
		.name = "s5m8751-codec",
	},
};

static struct mfd_cell s5m8763_devs[] = {
	{
		.name = "s5m8763-pmic",
	}, {
		.name = "s5m-rtc",
	}, {
		.name = "s5m-charger",
	},
};

static struct mfd_cell s5m8767_devs[] = {
	{
		.name = "s5m8767-pmic",
	}, {
		.name = "s5m-rtc",
	},
};

int s5m_reg_read(struct s5m87xx_dev *s5m87xx, u8 reg, void *dest)
{
	return regmap_read(s5m87xx->regmap, reg, dest);
}
EXPORT_SYMBOL_GPL(s5m_reg_read);

int s5m_bulk_read(struct s5m87xx_dev *s5m87xx, u8 reg, int count, u8 *buf)
{
	return regmap_bulk_read(s5m87xx->regmap, reg, buf, count);
}
EXPORT_SYMBOL_GPL(s5m_bulk_read);

int s5m_reg_write(struct s5m87xx_dev *s5m87xx, u8 reg, u8 value)
{
	return regmap_write(s5m87xx->regmap, reg, value);
}
EXPORT_SYMBOL_GPL(s5m_reg_write);

int s5m_bulk_write(struct s5m87xx_dev *s5m87xx, u8 reg, int count, u8 *buf)
{
	return regmap_raw_write(s5m87xx->regmap, reg, buf, count);
}
EXPORT_SYMBOL_GPL(s5m_bulk_write);

int s5m_reg_update(struct s5m87xx_dev *s5m87xx, u8 reg, u8 val, u8 mask)
{
	return regmap_update_bits(s5m87xx->regmap, reg, mask, val);
}
EXPORT_SYMBOL_GPL(s5m_reg_update);

static struct regmap_config s5m_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static inline int s5m87xx_i2c_get_driver_data(struct i2c_client *i2c,
					       const struct i2c_device_id *id)
{
#ifdef CONFIG_OF
	if (i2c->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(s5m87xx_pmic_dt_match, i2c->dev.of_node);
		return (int)match->data;
	}
#endif
	return (int)id->driver_data;
}

#ifdef CONFIG_OF
static struct s5m_platform_data *s5m87xx_i2c_parse_dt_pdata(struct device *dev)
{
	struct s5m_platform_data *pd;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		dev_err(dev, "could not allocate memory for pdata\n");
		return ERR_PTR(-ENOMEM);
	}

	if (of_property_read_u32(dev->of_node, "s5m-core,device_type",
				 &pd->device_type)) {
		dev_warn(dev, "no OF device_type property");
	}
	dev_dbg(dev, "OF device_type property = %u", pd->device_type);

	if (of_get_property(dev->of_node, "s5m-core,enable-low-jitter", NULL)) {
		if (!(pd->device_type == S5M8767X))
			dev_warn(dev, "no low-jitter for this PMIC type\n");
		else
			pd->low_jitter = true;
	}
	dev_dbg(dev, "OF low-jitter property: %u\n", pd->low_jitter);

	return pd;
}
#else
static struct s5m_platform_data *s5m87xx_i2c_parse_dt_pdata(struct device *dev)
{
	return 0;
}
#endif

static int s5m87xx_set_low_jitter(struct s5m87xx_dev *s5m87xx)
{
	if (!s5m87xx->pdata->low_jitter)
		return 0;

	return s5m_reg_update(s5m87xx, S5M8767_REG_CTRL1,
			      S5M8767_LOW_JITTER_MASK,
			      S5M8767_LOW_JITTER_MASK);
}

static int __devinit s5m87xx_i2c_probe(struct i2c_client *i2c,
				       const struct i2c_device_id *id)
{
	struct s5m_platform_data *pdata = i2c->dev.platform_data;
	struct s5m87xx_dev *s5m87xx;
	int ret;

	s5m87xx = devm_kzalloc(&i2c->dev, sizeof(struct s5m87xx_dev),
				GFP_KERNEL);
	if (s5m87xx == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, s5m87xx);
	s5m87xx->dev = &i2c->dev;
	s5m87xx->i2c = i2c;
	s5m87xx->irq = i2c->irq;
	s5m87xx->type = s5m87xx_i2c_get_driver_data(i2c, id);

	if (s5m87xx->dev->of_node) {
		pdata = s5m87xx_i2c_parse_dt_pdata(s5m87xx->dev);
		if (IS_ERR(pdata)) {
			ret = PTR_ERR(pdata);
			goto err;
		}
	}
	s5m87xx->pdata = pdata;

	if (!pdata) {
		ret = -ENODEV;
		dev_warn(s5m87xx->dev, "No platform data found\n");
		goto err;
	}

	s5m87xx->device_type = pdata->device_type;
	/* TODO(tbroch): address whether we want this addtional interrupt node
	   and add it to DT parsing if yes.
	*/
	s5m87xx->ono = pdata->ono;
	/* TODO(tbroch): remove hack below and parse irq_base via DT */
	s5m87xx->irq_base = pdata->irq_base = MAX77686_IRQ_BASE;
#ifdef OF_CONFIG
	s5m87xx->wakeup = i2c->flags & I2C_CLIENT_WAKE;
#else
	s5m87xx->wakeup = pdata->wakeup;
#endif

	s5m87xx->regmap = regmap_init_i2c(i2c, &s5m_regmap_config);
	if (IS_ERR(s5m87xx->regmap)) {
		ret = PTR_ERR(s5m87xx->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		goto err;
	}

	s5m87xx->rtc = i2c_new_dummy(i2c->adapter, RTC_I2C_ADDR);
	i2c_set_clientdata(s5m87xx->rtc, s5m87xx);

	if (pdata && pdata->cfg_pmic_irq)
		pdata->cfg_pmic_irq();

	s5m_irq_init(s5m87xx);

	pm_runtime_set_active(s5m87xx->dev);

	switch (s5m87xx->device_type) {
	case S5M8751X:
		ret = mfd_add_devices(s5m87xx->dev, -1, s5m8751_devs,
				      ARRAY_SIZE(s5m8751_devs), NULL, 0);
		break;
	case S5M8763X:
		ret = mfd_add_devices(s5m87xx->dev, -1, s5m8763_devs,
				      ARRAY_SIZE(s5m8763_devs), NULL, 0);
		break;
	case S5M8767X:
		ret = mfd_add_devices(s5m87xx->dev, -1, s5m8767_devs,
				      ARRAY_SIZE(s5m8767_devs), NULL, 0);
		break;
	default:
		/* If this happens the probe function is problem */
		BUG();
	}

	if (ret < 0)
		goto err;

	if (s5m87xx_set_low_jitter(s5m87xx) < 0) {
		dev_err(s5m87xx->dev, "failed to configure low-jitter\n");
		ret = -EIO;
		goto err;
	}

	return ret;

err:
	mfd_remove_devices(s5m87xx->dev);
	s5m_irq_exit(s5m87xx);
	i2c_unregister_device(s5m87xx->rtc);
	regmap_exit(s5m87xx->regmap);
	return ret;
}

static int s5m87xx_i2c_remove(struct i2c_client *i2c)
{
	struct s5m87xx_dev *s5m87xx = i2c_get_clientdata(i2c);

	mfd_remove_devices(s5m87xx->dev);
	s5m_irq_exit(s5m87xx);
	i2c_unregister_device(s5m87xx->rtc);
	regmap_exit(s5m87xx->regmap);
	return 0;
}

static const struct i2c_device_id s5m87xx_i2c_id[] = {
	{ "s5m87xx", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s5m87xx_i2c_id);

static struct i2c_driver s5m87xx_i2c_driver = {
	.driver = {
		   .name = "s5m87xx",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(s5m87xx_pmic_dt_match),
	},
	.probe = s5m87xx_i2c_probe,
	.remove = s5m87xx_i2c_remove,
	.id_table = s5m87xx_i2c_id,
};

static int __init s5m87xx_i2c_init(void)
{
	return i2c_add_driver(&s5m87xx_i2c_driver);
}

subsys_initcall(s5m87xx_i2c_init);

static void __exit s5m87xx_i2c_exit(void)
{
	i2c_del_driver(&s5m87xx_i2c_driver);
}
module_exit(s5m87xx_i2c_exit);

MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_DESCRIPTION("Core support for the S5M MFD");
MODULE_LICENSE("GPL");
