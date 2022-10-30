/*
 * drivers/net/phy/motorcomm.c
 *
 * Driver for Motorcomm PHYs
 *
 * Author: Leilei Zhao <leilei.zhao@motorcomm.com>
 * Author: KunYi Chen <kunyi.chen@gmail.com
 *
 * Copyright (c) 2019 Motorcomm, Inc.
 * COpyright (c) 2022 UWINGS Technologies Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Support : Motorcomm Phys:
 *		Giga phys: yt8511, yt8521
 *		100/10 Phys : yt8512, yt8512b, yt8510
 *		Automotive 100Mb Phys : yt8010
 *		Automotive 100/10 hyper range Phys: yt8510
 */

#include <phy.h>
#include <common.h>
#include <miiphy.h>


#define PHY_ID_YT8511				0x0000010a
#define PHY_ID_YT8521				0x0000011a
#define MOTORCOMM_PHY_ID_MASK			0x00000fff

/* Extended Register's Address Offset Register */
#define YTPHY_PAGE_SELECT			0x1e
/* Extended Register's Data Register */
#define YTPHY_PAGE_DATA				0x1f

#define YT8511_EXT_DELAY_DRIVE	0x0d

#define YT8511_EXT_SLEEP_CTRL			0x27
#define YT8511_ESC1R_SLEEP_SW			BIT(15)
#define YT8511_PLLON_SLP			BIT(14)

#define YT8511_EXT_CLK_GATE			0x0c
/* 2b00 25m from pll
 * 2b01 25m from xtl *default*
 * 2b10 62.m from pll
 * 2b11 125m from pll
 */

#define YT8511_CLK_125M				(BIT(2) | BIT(1))

/* TX Gig-E Delay is bits 7:4, default 0x5
 * TX Fast-E Delay is bits 15:12, default 0xf
 * Delay = 150ps * N - 250ps
 * On = 2000ps, off = 50ps
 */
#define YT8511_DELAY_GE_TX_EN			(0xf << 4)
#define YT8511_DELAY_GE_TX_DIS			(0x2 << 4)
#define YT8511_DELAY_FE_TX_EN			(0xf << 12)
#define YT8511_DELAY_FE_TX_DIS			(0x2 << 12)

/* RX Delay enabled = 1.8ns 1000T, 8ns 10/100T */
#define YT8511_DELAY_RX				BIT(0)

/* Phy gmii Clock Gating Register */
#define	YT8521_CLOCK_GATING_REG			0xc
#define YT8521_CGR_RX_CLK_EN			BIT(12)

#define YT8521_EXTREG_SLEEP_CONTROL1_REG	0x27
#define YT8521_ESC1R_SLEEP_SW			BIT(15)
#define YT8521_ESC1R_PLLON_SLP			BIT(14)

#define YT8521_RGMII_CONFIG1_REG		0xA003
/* TX Gig-E Delay is bits 3:0, default 0x1
 * TX Fast-E Delay is bits 7:4, default 0xf
 * RX Delay is bits 13:10, default 0x0
 * Delay = 150ps * N
 * On = 2250ps, off = 0ps
 */
#define YT8521_RC1R_RX_DELAY_MASK		(0xF << 10)
#define YT8521_RC1R_RX_DELAY_EN			(0xF << 10)
#define YT8521_RC1R_RX_DELAY_DIS		(0x0 << 10)
#define YT8521_RC1R_FE_TX_DELAY_MASK		(0xF << 4)
#define YT8521_RC1R_FE_TX_DELAY_EN		(0xF << 4)
#define YT8521_RC1R_FE_TX_DELAY_DIS		(0x0 << 4)
#define YT8521_RC1R_GE_TX_DELAY_MASK		(0xF << 0)
#define YT8521_RC1R_GE_TX_DELAY_EN		(0xF << 0)
#define YT8521_RC1R_GE_TX_DELAY_DIS		(0x0 << 0)

/* 0xA000, 0xA001, 0xA003 ,and 0xA006 ~ 0xA00A  are common ext registers
 * of yt8521 phy. There is no need to switch reg space when operating these
 * registers.
 */

#define YT8521_REG_SPACE_SELECT_REG		0xA000
#define YT8521_RSSR_SPACE_MASK			BIT(1)
#define YT8521_RSSR_FIBER_SPACE			(0x1 << 1)
#define YT8521_RSSR_UTP_SPACE			(0x0 << 1)
#define YT8521_RSSR_TO_BE_ARBITRATED		(0xFF)

#if (YTPHY_ENABLE_WOL)
#undef SYS_WAKEUP_BASED_ON_ETH_PKT
#define SYS_WAKEUP_BASED_ON_ETH_PKT 	1
#endif

static int ytphy_read_ext(struct phy_device *phydev, u32 regnum);
static int ytphy_write_ext(struct phy_device *phydev, u32 regnum, u16 val);
static int yt8511_config(struct phy_device *phydev);
static int yt8521_config(struct phy_device *phydev);

static int ytphy_read_ext(struct phy_device *phydev, u32 regnum)
{
	int ret;
	int val;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	val = phy_read(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_DATA);

	return val;
}

static int ytphy_write_ext(struct phy_device *phydev, u32 regnum, u16 val)
{
	int ret;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_DATA, val);

	return ret;
}

static int yt8511_config(struct phy_device *phydev)
{
    int ret;
    int val;

    /* disable auto sleep */
    val = ytphy_read_ext(phydev, YT8511_EXT_SLEEP_CTRL);
    val &= (~YT8511_ESC1R_SLEEP_SW);

    ret = ytphy_write_ext(phydev, YT8511_EXT_SLEEP_CTRL, val);

    /* enable RXC clock when no wire plug */
    val = ytphy_read_ext(phydev, YT8511_EXT_CLK_GATE);
    /*
       YT8511_EXT_CLK_GATE b[7:4]
       Tx Delay time = 150ps * N - 250ps
       INTERFACE_RGMII_ID
    */
    val |= YT8511_DELAY_GE_TX_EN | YT8511_DELAY_RX;
    ret = ytphy_write_ext(phydev, YT8511_EXT_CLK_GATE, val);

    genphy_config_aneg(phydev);
    genphy_restart_aneg(phydev);

    return ret;
}

static int yt8521_config(struct phy_device *phydev)
{
    int ret;
    int val;

    /* default in UTP page */
    /* set rgmii delay mode */
    val = ytphy_read_ext(phydev, YT8521_RGMII_CONFIG1_REG);
    val &= ~(YT8521_RC1R_FE_TX_DELAY_MASK | YT8521_RC1R_GE_TX_DELAY_MASK);
    val &= ~(YT8521_RC1R_RX_DELAY_MASK);
    val = YT8521_RC1R_FE_TX_DELAY_EN | YT8521_RC1R_GE_TX_DELAY_EN;
    val |= YT8521_RC1R_RX_DELAY_EN;
    ytphy_write_ext(phydev, YT8521_RGMII_CONFIG1_REG, val);

    /* disable auto sleep */
    val = ytphy_read_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1_REG);
    val &= (~YT8521_ESC1R_SLEEP_SW);
    ret = ytphy_write_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1_REG, val);

    /* enable RXC clock when no wire plug */
    val = ytphy_read_ext(phydev, YT8521_CLOCK_GATING_REG);
    val &= (~YT8521_CGR_RX_CLK_EN);
    ret = ytphy_write_ext(phydev, YT8521_CLOCK_GATING_REG, val);

    genphy_config_aneg(phydev);
    genphy_restart_aneg(phydev);

    return ret;
}

static struct phy_driver YT8511_driver =  {
	.name = "Motorcomm YT8511",
	.uid = PHY_ID_YT8511,
	.mask = MOTORCOMM_PHY_ID_MASK,
	.features = PHY_GBIT_FEATURES,
	.config = yt8511_config,
	.startup = genphy_startup,
	.shutdown = genphy_shutdown,
};

static struct phy_driver YT8521_driver =  {
	.name = "Motorcomm YT8521",
	.uid = PHY_ID_YT8521,
	.mask = MOTORCOMM_PHY_ID_MASK,
	.features = PHY_GBIT_FEATURES,
	.config = yt8521_config,
	.startup = genphy_startup,
	.shutdown = genphy_shutdown,
};

int phy_motorcomm_init(void)
{
	phy_register(&YT8521_driver);
	phy_register(&YT8511_driver);

	return 0;
}
