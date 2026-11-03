#include <lwip/ip_addr.h>
#include <lwip/sys.h>
#include <lwip/inet.h>
#include <netdev.h>
#include <netif/ethernetif.h>
#include <rtdbg.h>
#include <rtdevice.h>
#include <rthw.h>
#include <rtthread.h>

#define TEST_PING_TARGET   "8.8.8.8"
#define TEST_PING_TIMES    4

int eth_test(void)
{
    struct netdev *netdev = RT_NULL;
    struct rt_device *device;
    int ping_result;
    rt_uint8_t mac_addr[6];
    char mac_str[18] = {0};

    rt_kprintf("\n--- Starting Ethernet Driver Test ---\n");

    device = rt_device_find("eth0");
    if (!device)
    {
        rt_kprintf("[FAIL] Device 'e0' not found!\n");
        return -RT_ERROR;
    }
    
    if (rt_device_control(device, NIOCTL_GADDR, mac_addr) == RT_EOK)
    {
        rt_snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                   mac_addr[0], mac_addr[1], mac_addr[2],
                   mac_addr[3], mac_addr[4], mac_addr[5]);
        rt_kprintf("[PASS] [1/3]MAC Address: %s\n", mac_str);
    }
    else
    {
        rt_kprintf("[FAIL] Get MAC address failed!\n");
        return -RT_ERROR;
    }

    netdev = netdev_get_by_name("eth0");
    if (!netdev)
    {
        rt_kprintf("[FAIL] Get netdev 'e0' failed!\n");
        return -RT_ERROR;
    }

    rt_kprintf("Waiting for network ready...");
    while (!(netdev_is_up(netdev) && 
             netdev_is_link_up(netdev) && 
             netdev->ip_addr.addr))
    {
        rt_thread_mdelay(500);
        rt_kprintf(".");
    }
    rt_kprintf("\n[PASS] [2/3]Network ready! IP: %s\n", inet_ntoa(netdev->ip_addr));

    rt_kprintf("Pinging %s ...\n", TEST_PING_TARGET);
    ping_result = netdev_ping(netdev, TEST_PING_TARGET, TEST_PING_TIMES, 2000);
    
    if (ping_result == 0)
    {
        rt_kprintf("[PASS] [3/3]Ping test successful!\n");
    }
    else
    {
        rt_kprintf("[FAIL] Ping test failed! Error: %d\n", ping_result);
    }

    rt_kprintf("\n--- END Ethernet Driver Test ---\n");
    return RT_EOK;
}
MSH_CMD_EXPORT(eth_test, ethernet test);
