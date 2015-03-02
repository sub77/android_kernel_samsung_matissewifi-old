/*
 * TC358764 MIPI-DSI to LVDS bridge panel driver.
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd
 *
 * Andrzej Hajda <a.hajda at samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#define FLD_MASK(start, end)    (((1 << ((start) - (end) + 1)) - 1) << (end))
#define FLD_VAL(val, start, end) (((val) << (end)) & FLD_MASK(start, end))

/* PPI layer registers */
#define PPI_STARTPPI		0x0104 /* START control bit */
#define PPI_LPTXTIMECNT		0x0114 /* LPTX timing signal */
#define PPI_LANEENABLE		0x0134 /* Enables each lane */
#define PPI_TX_RX_TA		0x013C /* BTA timing parameters */
#define PPI_D0S_CLRSIPOCOUNT	0x0164 /* Assertion timer for Lane 0 */
#define PPI_D1S_CLRSIPOCOUNT	0x0168 /* Assertion timer for Lane 1 */
#define PPI_D2S_CLRSIPOCOUNT	0x016C /* Assertion timer for Lane 2 */
#define PPI_D3S_CLRSIPOCOUNT	0x0170 /* Assertion timer for Lane 3 */

/* DSI layer registers */
#define DSI_STARTDSI		0x0204 /* START control bit of DSI-TX */
#define DSI_LANEENABLE		0x0210 /* Enables each lane */

/* Video path registers */
#define VP_CTRL			0x0450 /* Video Path Control */
#define VP_CTRL_MSF(v)		FLD_VAL(v, 0, 0) /* Magic square in RGB666 */
#define VP_CTRL_VTGEN(v)	FLD_VAL(v, 4, 4) /* Use chip clock for timing */
#define VP_CTRL_EVTMODE(v)	FLD_VAL(v, 5, 5) /* Event mode */
#define VP_CTRL_RGB888(v)	FLD_VAL(v, 8, 8) /* RGB888 mode */
#define VP_CTRL_VSDELAY(v)	FLD_VAL(v, 31, 20) /* VSYNC delay */
#define VP_HTIM1		0x0454 /* Horizontal Timing Control 1 */
#define VP_HTIM1_HBP(v)		FLD_VAL(v, 24, 16)
#define VP_HTIM1_HSYNC(v)	FLD_VAL(v, 8, 0)
#define VP_HTIM2		0x0458 /* Horizontal Timing Control 2 */
#define VP_HTIM2_HFP(v)		FLD_VAL(v, 24, 16)
#define VP_HTIM2_HACT(v)	FLD_VAL(v, 10, 0)
#define VP_VTIM1		0x045C /* Vertical Timing Control 1 */
#define VP_VTIM1_VBP(v)		FLD_VAL(v, 23, 16)
#define VP_VTIM1_VSYNC(v)	FLD_VAL(v, 7, 0)
#define VP_VTIM2		0x0460 /* Vertical Timing Control 2 */
#define VP_VTIM2_VFP(v)		FLD_VAL(v, 23, 16)
#define VP_VTIM2_VACT(v)	FLD_VAL(v, 10, 0)
#define VP_VFUEN		0x0464 /* Video Frame Timing Update Enable */

/* LVDS registers */
#define LV_MX0003		0x0480 /* Mux input bit 0 to 3 */
#define LV_MX0407		0x0484 /* Mux input bit 4 to 7 */
#define LV_MX0811		0x0488 /* Mux input bit 8 to 11 */
#define LV_MX1215		0x048C /* Mux input bit 12 to 15 */
#define LV_MX1619		0x0490 /* Mux input bit 16 to 19 */
#define LV_MX2023		0x0494 /* Mux input bit 20 to 23 */
#define LV_MX2427		0x0498 /* Mux input bit 24 to 27 */
#define LV_MX(b0, b1, b2, b3)	(FLD_VAL(b0, 4, 0) | FLD_VAL(b1, 12, 8) | \
				FLD_VAL(b2, 20, 16) | FLD_VAL(b3, 28, 24))

/* Input bit numbers used in mux registers */
enum {
	LVI_R0,
	LVI_R1,
	LVI_R2,
	LVI_R3,
	LVI_R4,
	LVI_R5,
	LVI_R6,
	LVI_R7,
	LVI_G0,
	LVI_G1,
	LVI_G2,
	LVI_G3,
	LVI_G4,
	LVI_G5,
	LVI_G6,
	LVI_G7,
	LVI_B0,
	LVI_B1,
	LVI_B2,
	LVI_B3,
	LVI_B4,
	LVI_B5,
	LVI_B6,
	LVI_B7,
	LVI_HS,
	LVI_VS,
	LVI_DE,
	LVI_L0
};

#define LV_CFG			0x049C /* LVDS Configuration */
#define LV_PHY0			0x04A0 /* LVDS PHY 0 */
#define LV_PHY0_RST(v)		FLD_VAL(v, 22, 22) /* PHY reset */
#define LV_PHY0_IS(v)		FLD_VAL(v, 15, 14)
#define LV_PHY0_ND(v)		FLD_VAL(v, 4, 0)

/* System registers */
#define SYS_RST			0x0504 /* System Reset */
#define SYS_ID			0x0580 /* System ID */

static const char * const tc358764_supplies[] = {
	"vddc", "vddio", "vddmipi", "vddlvds133", "vddlvds112"
};

struct tc358764 {
	struct device *dev;
	struct drm_panel bridge;

	struct regulator_bulk_data supplies[ARRAY_SIZE(tc358764_supplies)];
	int reset_gpio;
	struct drm_panel *panel;
};

int tc358764_read(struct tc358764 *ctx, u16 addr, u32 *val)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	const struct mipi_dsi_host_ops *ops = dsi->host->ops;
	struct mipi_dsi_msg msg = {
		.type = MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM,
		.channel = dsi->channel,
		.flags = MIPI_DSI_MSG_USE_LPM,
		.tx_buf = &addr,
		.tx_len = 2,
		.rx_buf = val,
		.rx_len = 4
	};
	ssize_t ret;

	if (!ops || !ops->transfer)
		return -ENOSYS;

	addr = cpu_to_le16(addr);

	ret = ops->transfer(dsi->host, &msg);
	if (ret >= 0)
		*val = le32_to_cpu(*val);

	return ret;
}

int tc358764_write(struct tc358764 *ctx, u16 addr, u32 val)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	const struct mipi_dsi_host_ops *ops = dsi->host->ops;
	u8 data[6];
	struct mipi_dsi_msg msg = {
		.type = MIPI_DSI_GENERIC_LONG_WRITE,
		.channel = dsi->channel,
		.flags = MIPI_DSI_MSG_USE_LPM | MIPI_DSI_MSG_REQ_ACK,
		.tx_buf = data,
		.tx_len = 6
	};

	if (!ops || !ops->transfer)
		return -ENOSYS;

	data[0] = addr;
	data[1] = addr >> 8;
	data[2] = val;
	data[3] = val >> 8;
	data[4] = val >> 16;
	data[5] = val >> 24;

	return ops->transfer(dsi->host, &msg);
}

#define bridge_to_tc358764(p) container_of(p, struct tc358764, bridge)

static int tc358764_init(struct tc358764 *ctx)
{
	u32 v = 0;

	tc358764_read(ctx, SYS_ID, &v);
	dev_info(ctx->dev, "ID: %#x\n", v);

	/* configure PPI counters */
	tc358764_write(ctx, PPI_TX_RX_TA, 0x20003);
	tc358764_write(ctx, PPI_LPTXTIMECNT, 2);
	tc358764_write(ctx, PPI_D0S_CLRSIPOCOUNT, 5);
	tc358764_write(ctx, PPI_D1S_CLRSIPOCOUNT, 5);
	tc358764_write(ctx, PPI_D2S_CLRSIPOCOUNT, 5);
	tc358764_write(ctx, PPI_D3S_CLRSIPOCOUNT, 5);

	/* enable four data lanes and clock lane */
	tc358764_write(ctx, PPI_LANEENABLE, 0x1f);
	tc358764_write(ctx, DSI_LANEENABLE, 0x1f);

	/* start */
	tc358764_write(ctx, PPI_STARTPPI, 1);
	tc358764_write(ctx, DSI_STARTDSI, 1);

	/* configure video path */
	tc358764_write(ctx, VP_CTRL, VP_CTRL_VSDELAY(15) | VP_CTRL_RGB888(1) |
		       VP_CTRL_EVTMODE(1) | BIT(17) | BIT(19));

	/* reset PHY */
	tc358764_write(ctx, LV_PHY0, LV_PHY0_RST(1)
		       | BIT(18) | LV_PHY0_IS(2) | LV_PHY0_ND(6));
	tc358764_write(ctx, LV_PHY0, BIT(18) | LV_PHY0_IS(2) | LV_PHY0_ND(6));

	/* reset bridge */
	tc358764_write(ctx, SYS_RST, BIT(2));

	/* set bit order */
	tc358764_write(ctx, LV_MX0003, LV_MX(LVI_R0, LVI_R1, LVI_R2, LVI_R3));
	tc358764_write(ctx, LV_MX0407, LV_MX(LVI_R4, LVI_R7, LVI_R5, LVI_G0));
	tc358764_write(ctx, LV_MX0811, LV_MX(LVI_G1, LVI_G2, LVI_G6, LVI_G7));
	tc358764_write(ctx, LV_MX1215, LV_MX(LVI_G3, LVI_G4, LVI_G5, LVI_B0));
	tc358764_write(ctx, LV_MX1619, LV_MX(LVI_B6, LVI_B7, LVI_B1, LVI_B2));
	tc358764_write(ctx, LV_MX2023, LV_MX(LVI_B3, LVI_B4, LVI_B5, LVI_L0));
	tc358764_write(ctx, LV_MX2427, LV_MX(LVI_HS, LVI_VS, LVI_DE, LVI_R6));
	tc358764_write(ctx, LV_CFG, 0xd);

	return 0;
}

static void tc358764_reset(struct tc358764 *ctx)
{
	msleep(20);
	gpio_set_value(ctx->reset_gpio, 0);
	msleep(20);
	gpio_set_value(ctx->reset_gpio, 1);
	msleep(40);
}

static void tc358764_poweron(struct tc358764 *ctx)
{
	int ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies),
					ctx->supplies);
	if (ret < 0)
		dev_err(ctx->dev, "error enabling regulators (%d)\n", ret);

	tc358764_reset(ctx);

	drm_panel_enable(ctx->panel);
	msleep(40);
}

static void tc358764_poweroff(struct tc358764 *ctx)
{
	int ret;

	tc358764_reset(ctx);

	drm_panel_disable(ctx->panel);
	msleep(40);

	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		dev_err(ctx->dev, "error enabling regulators (%d)\n", ret);
}

int tc358764_disable(struct drm_panel *bridge)
{
	struct tc358764 *ctx = bridge_to_tc358764(bridge);

	tc358764_poweroff(ctx);

	return 0;
}

int tc358764_enable(struct drm_panel *bridge)
{
	struct tc358764 *ctx = bridge_to_tc358764(bridge);

	tc358764_poweron(ctx);

	return tc358764_init(ctx);
}

int tc358764_get_modes(struct drm_panel *bridge)
{
	struct tc358764 *ctx = bridge_to_tc358764(bridge);

	if (!ctx->panel->drm)
		drm_panel_attach(ctx->panel, ctx->bridge.connector);

	return ctx->panel->funcs->get_modes(ctx->panel);
}

static const struct drm_panel_funcs tc358764_drm_funcs = {
	.disable = tc358764_disable,
	.enable = tc358764_enable,
	.get_modes = tc358764_get_modes,
};

/* of_* functions will be removed after acceptance of of_graph patches */
static struct device_node *
of_get_child_by_name_reg(struct device_node *parent, const char *name, u32 reg)
{
	struct device_node *np;

	for_each_child_of_node(parent, np) {
		u32 r;

		if (!np->name || of_node_cmp(np->name, name))
			continue;

		if (of_property_read_u32(np, "reg", &r) < 0)
			r = 0;

		if (reg == r)
			break;
	}

	return np;
}

static struct device_node *of_graph_get_port_by_reg(struct device_node *parent,
						    u32 reg)
{
	struct device_node *ports, *port;

	ports = of_get_child_by_name(parent, "ports");
	if (ports) {
		port = of_get_child_by_name_reg(ports, "port", reg);
		of_node_put(ports);
	} else {
		port = of_get_child_by_name_reg(parent, "port", reg);
	}
	return port;
}

static struct device_node *
of_graph_get_endpoint_by_reg(struct device_node *port, u32 reg)
{
	return of_get_child_by_name_reg(port, "endpoint", reg);
}

static struct device_node *
of_graph_get_remote_port_parent(const struct device_node *node)
{
	struct device_node *np;
	unsigned int depth;

	/* Get remote endpoint node. */
	np = of_parse_phandle(node, "remote-endpoint", 0);

	/* Walk 3 levels up only if there is 'ports' node. */
	for (depth = 3; depth && np; depth--) {
		np = of_get_next_parent(np);
		if (depth == 2 && of_node_cmp(np->name, "ports"))
			break;
	}
	return np;
}

static struct device_node *tc358764_of_find_panel_node(struct device *dev)
{
	struct device_node *np, *ep;

	np = of_graph_get_port_by_reg(dev->of_node, 1);
	if (!np)
		return NULL;

	ep = of_graph_get_endpoint_by_reg(np, 0);
	of_node_put(np);
	if (!ep)
		return NULL;

	np = of_graph_get_remote_port_parent(ep);
	of_node_put(ep);

	return np;
}

static int tc358764_parse_dt(struct tc358764 *ctx)
{
	struct device *dev = ctx->dev;
	struct device_node *np = dev->of_node;
	struct device_node *lvds;

	ctx->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);
	if (ctx->reset_gpio < 0) {
		dev_err(dev, "no reset GPIO pin provided\n");
		//return ctx->reset_gpio;
	}

	lvds = tc358764_of_find_panel_node(ctx->dev);
	if (!lvds) {
		dev_err(dev, "cannot find panel node\n");
		return -EINVAL;
	}
	ctx->panel = of_drm_find_panel(lvds);
	if (!ctx->panel) {
		dev_info(dev, "panel not registered\n");
		return -EPROBE_DEFER;
	}

	return 0;
}

static int tc358764_configure_regulators(struct tc358764 *ctx)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); ++i)
		ctx->supplies[i].supply = tc358764_supplies[i];

	ret = devm_regulator_bulk_get(ctx->dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		dev_info(ctx->dev, "failed to get regulators: %d\n", ret);

	return ret;
}

static int tc358764_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct tc358764 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(struct tc358764), GFP_KERNEL);
	if (!ctx) {
		dev_err(dev, "failed to allocate tc358764 structure.\n");
		return -ENOMEM;
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST
		| MIPI_DSI_MODE_VIDEO_AUTO_VERT;

	ret = tc358764_parse_dt(ctx);
	if (ret < 0)
		return ret;

	ret = tc358764_configure_regulators(ctx);
	if (ret < 0)
		return ret;

	ret = devm_gpio_request_one(dev, ctx->reset_gpio, GPIOF_DIR_OUT,
				    "TC358764_RESET");
	if (ret < 0) {
		dev_info(dev, "failed to request reset gpio\n");
		return ret;
	}

	drm_panel_init(&ctx->bridge);
	ctx->bridge.dev = dev;
	ctx->bridge.funcs = &tc358764_drm_funcs;

	ret = drm_panel_add(&ctx->bridge);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->bridge);

	return ret;
}

static int tc358764_remove(struct mipi_dsi_device *dsi)
{
	struct tc358764 *ctx = mipi_dsi_get_drvdata(dsi);

	tc358764_poweroff(ctx);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->bridge);

	return 0;
}

static struct of_device_id tc358764_of_match[] = {
	{ .compatible = "toshiba,tc358764" },
	{ }
};
MODULE_DEVICE_TABLE(of, tc358764_of_match);

static struct mipi_dsi_driver tc358764_driver = {
	.probe = tc358764_probe,
	.remove = tc358764_remove,
	.driver = {
		.name = "panel_tc358764",
		.owner = THIS_MODULE,
		.of_match_table = tc358764_of_match,
	},
};
module_mipi_dsi_driver(tc358764_driver);

MODULE_AUTHOR("Andrzej Hajda <a.hajda at samsung.com>");
MODULE_DESCRIPTION("MIPI-DSI based Driver for TC358764 DSI/LVDS Bridge");
MODULE_LICENSE("GPL v2");
