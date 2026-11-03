#include "vi_snsr_i2c.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdint.h>



#include "vi_common.h"

enum {
    SNSR_I2C_DBG_DISABLE = 0,
    SNSR_I2C_DBG_VERIFY,
    SNSR_I2C_DBG_PRINT,
};

typedef uint8_t __u8;
static struct cvi_i2c_dev *g_i2c_dev;

static int snsr_i2c_write(struct cvi_i2c_dev *dev, struct isp_i2c_data *i2c)
{
	struct rt_i2c_bus_device	*i2c_bus;
	char						i2c_name[64];
	u8							i2c_dev = i2c->i2c_dev;
	uint8_t						buf[i2c->addr_bytes + i2c->data_bytes];
	int							ret = 0;
	struct rt_i2c_msg			msg[1];

	sprintf(i2c_name, "i2c%d", i2c_dev);
	i2c_bus = (struct rt_i2c_bus_device *)rt_device_find(i2c_name);
	if (!i2c_bus) {
		printf("I2C device not found\n");
		return -ENODEV;
	}

	if (i2c->addr_bytes == 2) {
		buf[0] = i2c->reg_addr >> 8;
		buf[1] = i2c->reg_addr & 0xff;
		if (i2c->data_bytes == 2) {
			buf[2] = i2c->data >> 8;
			buf[3] = i2c->data & 0xff;
		} else if (i2c->data_bytes == 1) {
			buf[2] = i2c->data;
		}
	} else if (i2c->addr_bytes == 1) {
		buf[0] = i2c->reg_addr;
		if (i2c->data_bytes == 2) {
			buf[1] = i2c->data >> 8;
			buf[2] = i2c->data & 0xff;
		} else if (i2c->data_bytes == 1) {
			buf[1] = i2c->data;
		}
	}

	msg[0].addr  = i2c->dev_addr;
	msg[0].flags = RT_I2C_WR;
	msg[0].buf   = buf;
	msg[0].len   = i2c->addr_bytes + i2c->data_bytes;

	ret = rt_i2c_transfer(i2c_bus, msg, 1);

	return (ret == 1) ? 0 : -EIO;
}

#if 0
static int snsr_i2c_burst_queue(struct cvi_i2c_dev *dev, struct isp_i2c_data *i2c)
{
	struct i2c_client *client;
	struct i2c_adapter *adap;
	u8 i2c_dev = i2c->i2c_dev;
	struct cvi_i2c_ctx *ctx;
	struct i2c_msg *msg;
	int idx = 0;
	u8 *tx;

	if (!dev)
		return -ENODEV;

	/* check i2c device number. */
	if (i2c_dev >= I2C_MAX_NUM)
		return -EINVAL;

	/* Get i2c client */
	ctx = &dev->ctx[i2c_dev];
	if (ctx->msg_idx >= I2C_MAX_MSG_NUM)
		return -EINVAL;

	client = ctx->client;
	adap = client->adapter;
	msg = &ctx->msg[ctx->msg_idx];
	tx = &ctx->buf[4*ctx->msg_idx];

	/* Config reg addr */
	if (i2c->addr_bytes == 1) {
		tx[idx++] = i2c->reg_addr & 0xff;
	} else {
		tx[idx++] = i2c->reg_addr >> 8;
		tx[idx++] = i2c->reg_addr & 0xff;
	}

	/* Config data */
	if (i2c->data_bytes == 1) {
		tx[idx++] = i2c->data & 0xff;
	} else {
		tx[idx++] = i2c->data >> 8;
		tx[idx++] = i2c->data & 0xff;
	}

	/* Config msg */
	msg->addr = i2c->dev_addr;
	msg->buf = tx;
	msg->len = idx;
	msg->flags = I2C_M_WRSTOP;

	ctx->addr_bytes = i2c->addr_bytes;
	ctx->data_bytes = i2c->data_bytes;
	ctx->msg_idx++;

	return 0;
}

static void snsr_i2c_verify(struct i2c_client *client, struct cvi_i2c_ctx *ctx, uint32_t size)
{
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	int i, step, ret;
	u8 tx[4];

	msg.addr = ctx->msg[0].addr;
	msg.buf = tx;

	/*
	 * In burst mode we have no idea when the transfer completes,
	 * therefore we wait for 1ms before verifying the results in debug mode.
	 */
	usleep_range(1000, 2000);
	step = ctx->addr_bytes + ctx->data_bytes;
	for (i = 0; i < size; i++) {
		if (step == 2) {
			/* 1 byte address, 1 byte data*/
			tx[0] = ctx->msg[i].buf[0];
			msg.buf = tx;
			msg.len = 1;
			msg.flags = 0;
			ret = i2c_transfer(adap, &msg, 1);
			if (ret != 1) {
				dev_dbg(&client->dev, "%s, i2c xfer ng\n", __func__);
				break;
			}
			tx[0] = 0;
			msg.flags = I2C_M_RD;
			ret = i2c_transfer(adap, &msg, 1);
			if (ret != 1) {
				dev_dbg(&client->dev, "%s, i2c xfer ng\n", __func__);
				break;
			}
			if (ctx->msg[i].buf[1] != tx[0]) {
				dev_dbg(&client->dev, "%s, addr 0x%02x, w: 0x%02x, r: 0x%02x\n",
						__func__, ctx->msg[i].buf[0], ctx->msg[i].buf[1], tx[0]);
			}
		} else if (step == 3) {
			/* 2 byte address, 1 byte data*/
			tx[0] = ctx->msg[i].buf[0];
			tx[1] = ctx->msg[i].buf[1];
			msg.buf = tx;
			msg.len = 2;
			msg.flags = 0;
			ret = i2c_transfer(adap, &msg, 1);
			if (ret != 1) {
				dev_dbg(&client->dev, "%s, i2c xfer ng\n", __func__);
				break;
			}
			tx[0] = 0;
			msg.len = 1;
			msg.flags = I2C_M_RD;
			ret = i2c_transfer(adap, &msg, 1);
			if (ret != 1) {
				dev_dbg(&client->dev, "%s, i2c xfer ng\n", __func__);
				break;
			}
			if (ctx->msg[i].buf[2] != tx[0]) {
				dev_dbg(&client->dev, "%s, addr 0x%02x,0x%02x w: 0x%02x, r: 0x%02x\n",
						__func__, ctx->msg[i].buf[0], ctx->msg[i].buf[1],
						ctx->msg[i].buf[2], tx[0]);
			}
		} else {
			/* 2 byte address, 2 byte data*/
			tx[0] = ctx->msg[i].buf[0];
			tx[1] = ctx->msg[i].buf[1];
			msg.buf = tx;
			msg.len = 2;
			msg.flags = 0;
			ret = i2c_transfer(adap, &msg, 1);
			if (ret != 1) {
				dev_dbg(&client->dev, "%s, i2c xfer ng\n", __func__);
				break;
			}
			tx[0] = 0;
			tx[1] = 0;
			msg.len = 2;
			msg.flags = I2C_M_RD;
			ret = i2c_transfer(adap, &msg, 1);
			if (ret != 1) {
				dev_dbg(&client->dev, "%s, i2c xfer ng\n", __func__);
				break;
			}
			if ((ctx->msg[i].buf[2] != tx[0]) || (ctx->msg[i].buf[3] != tx[1])) {
				dev_dbg(&client->dev, "%s, addr 0x%02x 0x%02x, w: 0x%02x 0x%02x, r: 0x%02x 0x%02x\n",
						__func__, ctx->msg[i].buf[0], ctx->msg[i].buf[1],
						ctx->msg[i].buf[2], ctx->msg[i].buf[3],
						tx[0], tx[1]);
			}
		}
	}
}

static void snsr_i2c_print(struct i2c_client *client, struct cvi_i2c_ctx *ctx, uint32_t size)
{
	int i, step;

	step = ctx->addr_bytes + ctx->data_bytes;

	for (i = 0; i < size; i++) {
		if (step == 2)
			dev_dbg(&client->dev, "a: 0x%02x, d: 0x%02x",
					ctx->msg[i].buf[0], ctx->msg[i].buf[1]);
		else if (step == 3)
			dev_dbg(&client->dev, "a: 0x%02x, 0x%02x, d: 0x%02x",
					ctx->msg[i].buf[0], ctx->msg[i].buf[1], ctx->msg[i].buf[2]);
		else
			dev_dbg(&client->dev, "a: 0x%02x, 0x%02x, d: 0x%02x, 0x%02x",
					ctx->msg[i].buf[0], ctx->msg[i].buf[1],
					ctx->msg[i].buf[2], ctx->msg[i].buf[3]);
	}
}

static int snsr_i2c_burst_fire(struct cvi_i2c_dev *dev, uint32_t i2c_dev)
{
	struct i2c_client *client;
	struct i2c_adapter *adap;
	struct cvi_i2c_ctx *ctx;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	struct timespec64 ts;
#else
	struct timeval tv;
#endif
	uint64_t t1, t2;
	int ret, retry = 5;

	if (!dev)
		return -ENODEV;

	/* check i2c device number. */
	if (i2c_dev >= I2C_MAX_NUM)
		return -EINVAL;

	/* Get i2c client */
	ctx = &dev->ctx[i2c_dev];
	if (!ctx->msg_idx)
		return 0;
	client = ctx->client;
	adap = client->adapter;

	while (retry--) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		ktime_get_real_ts64(&ts);
		t1 = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#else
		do_gettimeofday(&tv);
		t1 = tv.tv_sec * 1000000 + tv.tv_usec;
#endif
		ret = i2c_transfer(adap, ctx->msg, ctx->msg_idx);
		if (ret == ctx->msg_idx) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
			ktime_get_real_ts64(&ts);
			t2 = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#else
			do_gettimeofday(&tv);
			t2 = tv.tv_sec * 1000000 + tv.tv_usec;
#endif

			dev_dbg(&client->dev, "burst [%d] success %lld us\n", ret, (t2 - t1));
			if (snsr_i2c_dbg == SNSR_I2C_DBG_VERIFY)
				snsr_i2c_verify(client, ctx, ret);
			else if (snsr_i2c_dbg == SNSR_I2C_DBG_PRINT)
				snsr_i2c_print(client, ctx, ret);
			break;
		} else if (ret == -EAGAIN) {
			dev_dbg(&client->dev, "retry\n");
		} else {
			dev_err(&client->dev, "fail to send burst\n");
		}
	}

	ctx->msg_idx = 0;

	return ret == 1 ? 0 : -EIO;
}
#endif

static long snsr_i2c_ioctl(void *hdlr, unsigned int cmd, void *arg)
{
	struct cvi_i2c_dev *dev = (struct cvi_i2c_dev *)hdlr;

	switch (cmd) {
	case CVI_SNS_I2C_WRITE:
		return snsr_i2c_write(dev, (struct isp_i2c_data *)arg);
#if 0
	case CVI_SNS_I2C_BURST_QUEUE:
		return snsr_i2c_burst_queue(dev, (struct isp_i2c_data *)arg);
	case CVI_SNS_I2C_BURST_FIRE:
		i2c_dev = *argp;
		return snsr_i2c_burst_fire(dev, i2c_dev);
#endif
	default:
		return -EIO;
	}

	return 0;
}

// static int i2c_init(struct cvi_i2c_ctx *ctx, u8 i2c_id)
// {
// 	s32 ret = CSI_OK;

// 	ctx->master_iic = (csi_iic_t *)malloc(sizeof(csi_iic_t));
// 	if (!ctx->master_iic)
// 		return -ENOMEM;
// 	ret = csi_iic_init(ctx->master_iic, i2c_id);
// 	if (ret != CSI_OK) {
// 		printf("csi_iic_initialize error\n");
// 		return -ENOCSI;
// 	}
// 	/* config iic master mode */
// 	ret = csi_iic_mode(ctx->master_iic, IIC_MODE_MASTER);
// 	if (ret != CSI_OK) {
// 		printf("csi_iic_set_mode error\n");
// 		return -1;
// 	}

// 	/* config iic 7bit address mode */
// 	ret = csi_iic_addr_mode(ctx->master_iic, IIC_ADDRESS_7BIT);
// 	if (ret != CSI_OK) {
// 		printf("csi_iic_set_addr_mode error\n");
// 		return -1;
// 	}

// 	/* config iic standard speed*/
// 	ret = csi_iic_speed(ctx->master_iic, IIC_BUS_SPEED_STANDARD);
// 	if (ret != CSI_OK) {
// 		printf("csi_iic_set_speed error\n");
// 		return -1;
// 	}

// 	return ret;
// }

// static int i2c_uninit(struct cvi_i2c_ctx *ctx)
// {
// 	csi_iic_uninit(ctx->master_iic);
// 	free(ctx->master_iic);

// 	return 0;
// }

static int snsr_i2c_register_cb(struct cvi_i2c_dev *dev)
{
	/* register cmm callbacks */
	return vip_sys_register_cmm_cb(0, dev, snsr_i2c_ioctl);
}

static int _init_resource(struct cvi_i2c_dev *pdev)
{
    int                 rc  = 0;
    struct cvi_i2c_dev *dev = pdev;
    struct cvi_i2c_ctx *ctx = NULL;
    int                 i   = 0;

    if (!dev) return -ENODEV;

    // for (i = 0; i < I2C_MAX_NUM; i++) {
    // 	ctx = &dev->ctx[i];
    // 	rc = i2c_init(ctx, i);
    // 	if (rc < 0)
    // 		continue;
    // 	ctx->buf = (uint8_t *)aos_zalloc(I2C_BUF_SIZE);
    // 	if (!ctx->buf) {
    // 		free(ctx->master_iic);
    // 		return -ENOMEM;
    // 	}
    // }

    return 0;
}

int cvi_snsr_i2c_probe(void)
{
	s32 rc = 0;

	g_i2c_dev = (struct cvi_i2c_dev *)rt_malloc(sizeof(struct cvi_i2c_dev));
	if (!g_i2c_dev)
		return -ENOMEM;

	rt_spin_lock_init(&g_i2c_dev->lock);
	rt_mutex_init(&g_i2c_dev->mutex, "i2c_mutex", RT_IPC_FLAG_PRIO);

    // _init_resource(g_i2c_dev);

	rc = snsr_i2c_register_cb(g_i2c_dev);
	if (rc < 0) {
		printf("Failed to register cmm for snsr, %d\n", rc);
		return rc;
	}

	return 0;
}

int cvi_snsr_i2c_remove(void)
{
	struct cvi_i2c_dev *dev = NULL;
	//struct cvi_i2c_ctx *ctx = NULL;
	//int                 i   = 0;

	dev = g_i2c_dev;
	rt_mutex_detach(&g_i2c_dev->mutex);

	if (!dev) {
		printf("invalid param\r\n");
		return -EINVAL;
	}

	//	for (i = 0; i < I2C_MAX_NUM; i++) {
	//	ctx = &dev->ctx[i];
	//	if (ctx->master_iic)
	//		i2c_uninit(ctx);
	//	free(ctx->buf);
	//	}

	return 0;
}
