/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <mach/regs-pmu.h>
#include <mach/regs-usb-phy.h>
#include <plat/cpu.h>
#include <plat/usb-phy.h>
#include <plat/regs-usb3-exynos-drd-phy.h>

#define PHY_ENABLE	1
#define PHY_DISABLE	0
#define EXYNOS4_USB_CFG		(S3C_VA_SYS + 0x21C)
#define EXYNOS5_USB_CFG		S5P_SYSREG(0x0230)

enum usb_phy_type {
	USB_PHY		= (0x1 << 0),
	USB_PHY0	= (0x1 << 0),
	USB_PHY1	= (0x1 << 1),
	USB_PHY_HSIC0	= (0x1 << 1),
	USB_PHY_HSIC1	= (0x1 << 2),
};

struct exynos_usb_phy {
	u8 phy0_usage;
	u8 phy1_usage;
	u8 phy2_usage;
	unsigned long flags;
};


static atomic_t host_usage;
static struct exynos_usb_phy usb_phy_control;
static DEFINE_SPINLOCK(phy_lock);
static bool ext_clk;

static struct regulator *hub_reset;

static int exynos4_usb_host_phy_is_on(void)
{
	return (readl(EXYNOS4_PHYPWR) & PHY1_STD_ANALOG_POWERDOWN) ? 0 : 1;
}

static int exynos5_usb_host_phy20_is_on(void)
{
	return (readl(EXYNOS5_PHY_HOST_CTRL0) & HOST_CTRL0_PHYSWRSTALL) ? 0 : 1;
}

/* Returning 'clk' here so as to disable clk in phy_init/exit functions */
static struct clk *exynos_usb_phy_clock_enable(struct platform_device *pdev)
{
	int err;
	struct clk *phy_clk = NULL;

	if (!soc_is_exynos5250())
		return NULL;

	phy_clk = clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(phy_clk)) {
		dev_err(&pdev->dev, "Failed to get phy clock\n");
		return NULL;
	}

	err = clk_enable(phy_clk);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable phy clock\n");
		clk_put(phy_clk);
		return NULL;
	}

	return phy_clk;
}

static void exynos_usb_phy_clock_disable(struct clk *phy_clk)
{
	clk_disable(phy_clk);
	clk_put(phy_clk);
}

static void exynos_usb_mux_change(struct platform_device *pdev, int val)
{
	u32 is_host;
	if (!soc_is_exynos5250())
		return;

	is_host = readl(EXYNOS5_USB_CFG);
	writel(val, EXYNOS5_USB_CFG);
	if (is_host != val)
		dev_dbg(&pdev->dev, "Change USB MUX from %s to %s",
			is_host ? "Host" : "Device", val ? "Host" : "Device");
}

static void exynos_usb_phy_control(enum usb_phy_type phy_type , int on)
{
	spin_lock(&phy_lock);
	if (soc_is_exynos5250()) {
		if (phy_type & USB_PHY0) {
			if (on == PHY_ENABLE
				&& (usb_phy_control.phy0_usage++) == 0)
				writel(S5P_USBDRD_PHY_ENABLE,
						S5P_USBDRD_PHY_CONTROL);
			else if (on == PHY_DISABLE
				&& (--usb_phy_control.phy0_usage) == 0)
				writel(~S5P_USBDRD_PHY_ENABLE,
						S5P_USBDRD_PHY_CONTROL);

		} else if (phy_type & USB_PHY1) {
			if (on == PHY_ENABLE
				&& (usb_phy_control.phy1_usage++) == 0)
				writel(S5P_USBHOST_PHY_ENABLE,
						S5P_USBHOST_PHY_CONTROL);
			else if (on == PHY_DISABLE
				&& (--usb_phy_control.phy1_usage) == 0)
				writel(~S5P_USBHOST_PHY_ENABLE,
						S5P_USBHOST_PHY_CONTROL);
		}
	}
	spin_unlock(&phy_lock);
}

static u32 exynos_usb_phy_set_clock(struct platform_device *pdev)
{
	struct clk *ref_clk;
	u32 refclk_freq = 0;

	ref_clk = clk_get(&pdev->dev, "ext_xtal");

	if (IS_ERR(ref_clk)) {
		dev_err(&pdev->dev, "Failed to get reference clock\n");
		return PTR_ERR(ref_clk);
	}
	if (soc_is_exynos5250()) {
		switch (clk_get_rate(ref_clk)) {
		case 96 * 100000:
			refclk_freq = EXYNOS5_CLKSEL_9600K;
			break;
		case 10 * MHZ:
			refclk_freq = EXYNOS5_CLKSEL_10M;
			break;
		case 12 * MHZ:
			refclk_freq = EXYNOS5_CLKSEL_12M;
			break;
		case 192 * 100000:
			refclk_freq = EXYNOS5_CLKSEL_19200K;
			break;
		case 20 * MHZ:
			refclk_freq = EXYNOS5_CLKSEL_20M;
			break;
		case 50 * MHZ:
			refclk_freq = EXYNOS5_CLKSEL_50M;
			break;
		case 24 * MHZ:
		default:
			/* default reference clock */
			refclk_freq = EXYNOS5_CLKSEL_24M;
			break;
		}
	}
	clk_put(ref_clk);

	return refclk_freq;
}

/*
 * Sets the phy clk as EXTREFCLK (XXTI) which is internal clock form clock core.
 */
static u32 exynos_usb_phy30_set_clock_int(struct platform_device *pdev)
{
	u32 reg;
	u32 refclk;

	refclk = exynos_usb_phy_set_clock(pdev);

	reg = EXYNOS_USB3_PHYCLKRST_REFCLKSEL_EXT_REF_CLK |
	      EXYNOS_USB3_PHYCLKRST_FSEL(refclk);

	switch (refclk) {
	case EXYNOS5_CLKSEL_50M:
		/* TODO: multiplier seems wrong; others make ~2.5GHz */
		reg |= (EXYNOS_USB3_PHYCLKRST_MPLL_MULTIPLIER(0x02) |
			EXYNOS_USB3_PHYCLKRST_SSC_REF_CLK_SEL(0x00));
		break;
	case EXYNOS5_CLKSEL_20M:
		reg |= (EXYNOS_USB3_PHYCLKRST_MPLL_MULTIPLIER_20MHZ_REF |
			EXYNOS_USB3_PHYCLKRST_SSC_REF_CLK_SEL(0x00));
		break;
	case EXYNOS5_CLKSEL_19200K:
		/* TODO: multiplier seems wrong; others make ~2.5GHz */
		reg |= (EXYNOS_USB3_PHYCLKRST_MPLL_MULTIPLIER(0x02) |
			EXYNOS_USB3_PHYCLKRST_SSC_REF_CLK_SEL(0x88));
		break;
	case EXYNOS5_CLKSEL_24M:
	default:
		reg |= (EXYNOS_USB3_PHYCLKRST_MPLL_MULTIPLIER_24MHZ_REF |
			EXYNOS_USB3_PHYCLKRST_SSC_REF_CLK_SEL(0x88));
		break;
	}

	return reg;
}

/*
 * Sets the phy clk as ref_pad_clk (XusbXTI) which is clock from external PLL.
 */
static u32 exynos_usb_phy30_set_clock_ext(struct platform_device *pdev)
{
	u32 reg;

	reg = EXYNOS_USB3_PHYCLKRST_REFCLKSEL_PAD_REF_CLK |
		EXYNOS_USB3_PHYCLKRST_FSEL_PAD_100MHZ |
		EXYNOS_USB3_PHYCLKRST_MPLL_MULTIPLIER_100MHZ_REF;

	return reg;
}

static int _exynos5_usb_phy30_init(struct platform_device *pdev,
						bool use_ext_clk)
{
	u32 reg;

	exynos_usb_phy_control(USB_PHY0, PHY_ENABLE);

	/* Reset USB 3.0 PHY */
	writel(0x00000000, EXYNOS_USB3_PHYREG0);

	reg = 0x24d4e6e4;
	if (use_ext_clk)
		reg |= (0x1 << 31);
	writel(reg, EXYNOS_USB3_PHYPARAM0);

	writel(0x03fff81c, EXYNOS_USB3_PHYPARAM1);
	writel(0x00000000, EXYNOS_USB3_PHYRESUME);

	/* Setting the Frame length Adj value[6:1] to default 0x20 */
	writel(0x08000040, EXYNOS_USB3_LINKSYSTEM);
	writel(0x00000004, EXYNOS_USB3_PHYBATCHG);

	/* PHYTEST POWERDOWN Control */
	reg = readl(EXYNOS_USB3_PHYTEST);
	reg &= ~(EXYNOS_USB3_PHYTEST_POWERDOWN_SSP |
		EXYNOS_USB3_PHYTEST_POWERDOWN_HSP);
	writel(reg, EXYNOS_USB3_PHYTEST);

	/* UTMI Power Control */
	writel(EXYNOS_USB3_PHYUTMI_OTGDISABLE, EXYNOS_USB3_PHYUTMI);

	if (use_ext_clk)
		reg = exynos_usb_phy30_set_clock_ext(pdev);
	else
		reg = exynos_usb_phy30_set_clock_int(pdev);

	reg |= EXYNOS_USB3_PHYCLKRST_PORTRESET |
		/* Digital power supply in normal operating mode */
		EXYNOS_USB3_PHYCLKRST_RETENABLEN |
		/* Enable ref clock for SS function */
		EXYNOS_USB3_PHYCLKRST_REF_SSP_EN |
		/* Enable spread spectrum */
		EXYNOS_USB3_PHYCLKRST_SSC_EN |
		/* Power down HS Bias and PLL blocks in suspend mode */
		EXYNOS_USB3_PHYCLKRST_COMMONONN;

	writel(reg, EXYNOS_USB3_PHYCLKRST);

	udelay(10);

	reg &= ~(EXYNOS_USB3_PHYCLKRST_PORTRESET);
	writel(reg, EXYNOS_USB3_PHYCLKRST);

	return 0;
}

int exynos5_dwc_phyclk_switch(struct platform_device *pdev, bool use_ext_clk)
{
	return _exynos5_usb_phy30_init(pdev, use_ext_clk);
}

static int exynos5_usb_phy30_init(struct platform_device *pdev)
{
	int ret;
	struct clk *host_clk = NULL;

	host_clk = exynos_usb_phy_clock_enable(pdev);
	if (!host_clk) {
		dev_err(&pdev->dev, "Failed to enable USB3.0 host clock\n");
		return -ENODEV;
	}

	ret = _exynos5_usb_phy30_init(pdev, ext_clk);

	exynos_usb_phy_clock_disable(host_clk);

	return ret;
}

static int exynos5_usb_phy30_exit(struct platform_device *pdev)
{
	u32 reg;
	struct clk *host_clk = NULL;

	host_clk = exynos_usb_phy_clock_enable(pdev);
	if (!host_clk) {
		dev_err(&pdev->dev, "Failed to enable USB3.0 host clock\n");
		return -ENODEV;
	}

	reg = EXYNOS_USB3_PHYUTMI_OTGDISABLE |
		EXYNOS_USB3_PHYUTMI_FORCESUSPEND |
		EXYNOS_USB3_PHYUTMI_FORCESLEEP;
	writel(reg, EXYNOS_USB3_PHYUTMI);

	/* Resetting the PHYCLKRST enable bits to reduce leakage current */
	reg = readl(EXYNOS_USB3_PHYCLKRST);
	reg &= ~(EXYNOS_USB3_PHYCLKRST_REF_SSP_EN |
		EXYNOS_USB3_PHYCLKRST_SSC_EN |
		EXYNOS_USB3_PHYCLKRST_COMMONONN);
	writel(reg, EXYNOS_USB3_PHYCLKRST);

	/* Control PHYTEST to remove leakage current */
	reg = readl(EXYNOS_USB3_PHYTEST);
	reg |= (EXYNOS_USB3_PHYTEST_POWERDOWN_SSP |
		EXYNOS_USB3_PHYTEST_POWERDOWN_HSP);
	writel(reg, EXYNOS_USB3_PHYTEST);

	exynos_usb_phy_control(USB_PHY0, PHY_DISABLE);

	exynos_usb_phy_clock_disable(host_clk);

	return 0;
}

static int exynos5_usb_phy20_init(struct platform_device *pdev)
{
	u32 refclk_freq;
	u32 hostphy_ctrl0, otgphy_sys, hsic_ctrl, ehcictrl;
	struct clk *host_clk = NULL;
	int ret;

	atomic_inc(&host_usage);

	host_clk = exynos_usb_phy_clock_enable(pdev);
	if (!host_clk) {
		dev_err(&pdev->dev, "Failed to get host_clk\n");
		return -ENODEV;
	}

	if (exynos5_usb_host_phy20_is_on()) {
		dev_err(&pdev->dev, "Already power on PHY\n");
		return 0;
	}

	exynos_usb_mux_change(pdev, 1);

	exynos_usb_phy_control(USB_PHY1, PHY_ENABLE);

	/* Host and Device should be set at the same time */
	hostphy_ctrl0 = readl(EXYNOS5_PHY_HOST_CTRL0);
	hostphy_ctrl0 &= ~(HOST_CTRL0_FSEL_MASK);
	otgphy_sys = readl(EXYNOS5_PHY_OTG_SYS);
	otgphy_sys &= ~(OTG_SYS_CTRL0_FSEL_MASK);

	/* 2.0 phy reference clock configuration */
	refclk_freq = exynos_usb_phy_set_clock(pdev);
	hostphy_ctrl0 |= (refclk_freq << HOST_CTRL0_CLKSEL_SHIFT);
	otgphy_sys |= (refclk_freq << OTG_SYS_CLKSEL_SHIFT);

	/* COMMON Block configuration during suspend */
	hostphy_ctrl0 &= ~(HOST_CTRL0_COMMONON_N);
	otgphy_sys |= (OTG_SYS_COMMON_ON);

	/* otg phy reset */
	otgphy_sys &= ~(OTG_SYS_FORCE_SUSPEND | OTG_SYS_SIDDQ_UOTG
						| OTG_SYS_FORCE_SLEEP);
	otgphy_sys &= ~(OTG_SYS_REF_CLK_SEL_MASK);
	otgphy_sys |= (OTG_SYS_REF_CLK_SEL(0x2) | OTG_SYS_OTGDISABLE);
	otgphy_sys |= (OTG_SYS_PHY0_SW_RST | OTG_SYS_LINK_SW_RST_UOTG
						| OTG_SYS_PHYLINK_SW_RESET);
	writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);
	udelay(10);
	otgphy_sys &= ~(OTG_SYS_PHY0_SW_RST | OTG_SYS_LINK_SW_RST_UOTG
						| OTG_SYS_PHYLINK_SW_RESET);
	writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);
	/* host phy reset */
	hostphy_ctrl0 &= ~(HOST_CTRL0_PHYSWRST | HOST_CTRL0_PHYSWRSTALL
						| HOST_CTRL0_SIDDQ);
	hostphy_ctrl0 &= ~(HOST_CTRL0_FORCESUSPEND | HOST_CTRL0_FORCESLEEP);
	hostphy_ctrl0 |= (HOST_CTRL0_LINKSWRST | HOST_CTRL0_UTMISWRST);
	writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);
	udelay(10);
	hostphy_ctrl0 &= ~(HOST_CTRL0_LINKSWRST | HOST_CTRL0_UTMISWRST);
	writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);

	/* HSIC phy reset */
	WARN_ON(hub_reset != NULL);
	hub_reset = regulator_get(NULL, "hsichub-reset-l");
	if (!IS_ERR(hub_reset)) {
		/* toggle the reset line of the HSIC hub chip. */

		/* Ought to be disabled but force line just in case */
		ret = regulator_force_disable(hub_reset);
		WARN_ON(ret);

		/* keep reset active during 100 us */
		udelay(100);
		ret = regulator_enable(hub_reset);
		WARN_ON(ret);

		/* Hub init phase takes up to 4 ms */
		usleep_range(4000, 10000);
	}

	hsic_ctrl = (HSIC_CTRL_REFCLKDIV(0x24) | HSIC_CTRL_REFCLKSEL(0x2)
						| HSIC_CTRL_PHYSWRST);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);
	udelay(10);
	hsic_ctrl &= ~(HSIC_CTRL_PHYSWRST);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);

	udelay(80);

	ehcictrl = readl(EXYNOS5_PHY_HOST_EHCICTRL);
	ehcictrl |= (EHCICTRL_ENAINCRXALIGN | EHCICTRL_ENAINCR4
				| EHCICTRL_ENAINCR8 | EHCICTRL_ENAINCR16);
	writel(ehcictrl, EXYNOS5_PHY_HOST_EHCICTRL);

	exynos_usb_phy_clock_disable(host_clk);

	return 0;
}

static int exynos5_usb_phy20_exit(struct platform_device *pdev)
{
	u32 hostphy_ctrl0, otgphy_sys, hsic_ctrl;
	struct clk *host_clk = NULL;
	int ret;

	if (atomic_dec_return(&host_usage) > 0) {
		dev_info(&pdev->dev, "still being used\n");
		return -EBUSY;
	}

	host_clk = exynos_usb_phy_clock_enable(pdev);
	if (!host_clk) {
		dev_err(&pdev->dev, "Failed to get host_clk\n");
		return -ENODEV;
	}

	hsic_ctrl = (HSIC_CTRL_REFCLKDIV(0x24) | HSIC_CTRL_REFCLKSEL(0x2)
				| HSIC_CTRL_SIDDQ | HSIC_CTRL_FORCESLEEP
				| HSIC_CTRL_FORCESUSPEND);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);

	hostphy_ctrl0 = readl(EXYNOS5_PHY_HOST_CTRL0);
	hostphy_ctrl0 |= (HOST_CTRL0_SIDDQ);
	hostphy_ctrl0 |= (HOST_CTRL0_FORCESUSPEND | HOST_CTRL0_FORCESLEEP);
	hostphy_ctrl0 |= (HOST_CTRL0_PHYSWRST | HOST_CTRL0_PHYSWRSTALL);
	writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);

	otgphy_sys = readl(EXYNOS5_PHY_OTG_SYS);
	otgphy_sys |= (OTG_SYS_FORCE_SUSPEND | OTG_SYS_SIDDQ_UOTG
				| OTG_SYS_FORCE_SLEEP);
	writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);

	exynos_usb_phy_control(USB_PHY1, PHY_DISABLE);

	exynos_usb_phy_clock_disable(host_clk);

	/* put HSIC under reset */
	WARN_ON(hub_reset == NULL);
	if (!IS_ERR_OR_NULL(hub_reset)) {
		ret = regulator_disable(hub_reset);
		WARN_ON(ret);
		regulator_put(hub_reset);
		hub_reset = NULL;
	}

	return 0;
}


static int exynos4_usb_phy1_init(struct platform_device *pdev)
{
	struct clk *otg_clk;
	struct clk *xusbxti_clk;
	u32 phyclk;
	u32 rstcon;
	int err;

	atomic_inc(&host_usage);

	otg_clk = clk_get(&pdev->dev, "otg");
	if (IS_ERR(otg_clk)) {
		dev_err(&pdev->dev, "Failed to get otg clock\n");
		return PTR_ERR(otg_clk);
	}

	err = clk_enable(otg_clk);
	if (err) {
		clk_put(otg_clk);
		return err;
	}

	if (exynos4_usb_host_phy_is_on())
		return 0;

	writel(readl(S5P_USBHOST_PHY_CONTROL) | S5P_USBHOST_PHY_ENABLE,
			S5P_USBHOST_PHY_CONTROL);

	/* set clock frequency for PLL */
	phyclk = readl(EXYNOS4_PHYCLK) & ~CLKSEL_MASK;

	xusbxti_clk = clk_get(&pdev->dev, "xusbxti");
	if (xusbxti_clk && !IS_ERR(xusbxti_clk)) {
		switch (clk_get_rate(xusbxti_clk)) {
		case 12 * MHZ:
			phyclk |= CLKSEL_12M;
			break;
		case 24 * MHZ:
			phyclk |= CLKSEL_24M;
			break;
		default:
		case 48 * MHZ:
			/* default reference clock */
			break;
		}
		clk_put(xusbxti_clk);
	}

	writel(phyclk, EXYNOS4_PHYCLK);

	/* floating prevention logic: disable */
	writel((readl(EXYNOS4_PHY1CON) | FPENABLEN), EXYNOS4_PHY1CON);

	/* set to normal HSIC 0 and 1 of PHY1 */
	writel((readl(EXYNOS4_PHYPWR) & ~PHY1_HSIC_NORMAL_MASK),
			EXYNOS4_PHYPWR);

	/* set to normal standard USB of PHY1 */
	writel((readl(EXYNOS4_PHYPWR) & ~PHY1_STD_NORMAL_MASK), EXYNOS4_PHYPWR);

	/* reset all ports of both PHY and Link */
	rstcon = readl(EXYNOS4_RSTCON) | HOST_LINK_PORT_SWRST_MASK |
		PHY1_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(10);

	rstcon &= ~(HOST_LINK_PORT_SWRST_MASK | PHY1_SWRST_MASK);
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(80);

	clk_disable(otg_clk);
	clk_put(otg_clk);

	return 0;
}

static int exynos4_usb_phy1_exit(struct platform_device *pdev)
{
	struct clk *otg_clk;
	int err;

	if (atomic_dec_return(&host_usage) > 0)
		return 0;

	otg_clk = clk_get(&pdev->dev, "otg");
	if (IS_ERR(otg_clk)) {
		dev_err(&pdev->dev, "Failed to get otg clock\n");
		return PTR_ERR(otg_clk);
	}

	err = clk_enable(otg_clk);
	if (err) {
		clk_put(otg_clk);
		return err;
	}

	writel((readl(EXYNOS4_PHYPWR) | PHY1_STD_ANALOG_POWERDOWN),
			EXYNOS4_PHYPWR);

	writel(readl(S5P_USBHOST_PHY_CONTROL) & ~S5P_USBHOST_PHY_ENABLE,
			S5P_USBHOST_PHY_CONTROL);

	clk_disable(otg_clk);
	clk_put(otg_clk);

	return 0;
}

int s5p_usb_phy_use_ext_clk(struct platform_device *pdev, bool ext_clk_val)
{
	ext_clk = ext_clk_val;
	return 0;
}

int s5p_usb_phy_init(struct platform_device *pdev, int type)
{
	if (type == S5P_USB_PHY_HOST) {
		if (soc_is_exynos5250())
			return exynos5_usb_phy20_init(pdev);
		else
			return exynos4_usb_phy1_init(pdev);
	} else if (type == S5P_USB_PHY_DRD) {
		if (soc_is_exynos5250())
			return exynos5_usb_phy30_init(pdev);
		else
			dev_err(&pdev->dev, "USB 3.0 DRD not present\n");
	}
	return -EINVAL;
}

int s5p_usb_phy_exit(struct platform_device *pdev, int type)
{
	if (type == S5P_USB_PHY_HOST) {
		if (soc_is_exynos5250())
			return exynos5_usb_phy20_exit(pdev);
		else
			return exynos4_usb_phy1_exit(pdev);
	} else if (type == S5P_USB_PHY_DRD) {
		if (soc_is_exynos5250())
			return exynos5_usb_phy30_exit(pdev);
		else
			dev_err(&pdev->dev, "USB 3.0 DRD not present\n");
	}
	return -EINVAL;
}
