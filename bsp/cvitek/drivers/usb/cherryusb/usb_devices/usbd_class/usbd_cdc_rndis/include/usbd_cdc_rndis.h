#ifndef __USBD_CDC_RNDIS_H__
#define __USBD_CDC_RNDIS_H__

#include <stdint.h>
#include <lwip/netif.h>
#include <lwip/netifapi.h>
#include <netif/ethernetif.h>
#include "usbd_core.h"

struct cdc_rndis_device_info {
	struct usbd_endpoint cdc_rndis_in_ep;
	struct usbd_endpoint cdc_rndis_out_ep;
	struct usbd_endpoint cdc_rndis_int_ep;
	struct usbd_interface cdc_rndis_intf0;
	struct usbd_interface cdc_rndis_intf1;
    uint8_t ep;
    uint8_t interface_nums;
};

typedef struct {
    struct rt_device parent;       // RT-Thread 设备基类
    struct eth_device eth_dev;     // 以太网设备
    rt_uint8_t mac_addr[6];        // MAC地址
    rt_bool_t dhcp_started;        // DHCP状态标志
} rt_rndis_device;

static rt_rndis_device rndis_dev;

void cdc_rndis_desc_register(void);
void cdc_rndis_init(void);
void cdc_rndis_deinit(void);
err_t low_level_output(struct netif *netif, struct pbuf *p);

void rndis_device_lwip_init(void);
int rndis_device_init(void);
void rndis_device_deinit(void);

#endif