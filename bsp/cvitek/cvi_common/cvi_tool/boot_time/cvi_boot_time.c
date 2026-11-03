/* CV180x/CV181x FSBL/boot loader record ABI: twelve 16-bit millisecond values. */
#include <stdint.h>
#include <rtthread.h>
#include "cvi_boot_time.h"
#include "tick.h"

#if defined(RT_USING_FINSH) && defined(FINSH_USING_MSH)
#include <finsh.h>
#endif

#define TIME_RECORDS_ADDR 0x0e000010UL
#define TIME_RECORDS_COUNT 12

static const char *const stage_names[TIME_RECORDS_COUNT] = {
    "fsbl_start", "ddr_init_start", "ddr_init_end", "release_blcp_2nd",
    "load_loader_2nd_end", "fsbl_decomp_start", "fsbl_decomp_end", "fsbl_exit",
    "boot_main", "load_rtt_start", "load_rtt_end", "boot_end"
};
static uint16_t boot_records[TIME_RECORDS_COUNT];
static uint32_t app_main_ms;
static int recorded;

void cvi_boot_time_record(void)
{
    const volatile uint16_t *records = (const volatile uint16_t *)TIME_RECORDS_ADDR;
    uint32_t time_us;
    unsigned int i;

    if (recorded)
        return;

    time_us = rt_tick_get_us();
    app_main_ms = time_us / 1000 + (time_us % 1000 != 0);
    for (i = 0; i < TIME_RECORDS_COUNT; i++)
        boot_records[i] = records[i];
    recorded = 1;
}

void cvi_boot_time_dump(void)
{
    unsigned int i;

    if (!recorded)
        return;

    for (i = 0; i < TIME_RECORDS_COUNT; i++)
        rt_kprintf("%s %u\n", stage_names[i], (unsigned int)boot_records[i]);
    rt_kprintf("app_main %u\n", (unsigned int)app_main_ms);
}

#if defined(RT_USING_FINSH) && defined(FINSH_USING_MSH)
static int boottime(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        rt_kprintf("Usage: boottime\n");
        return -RT_EINVAL;
    }
    if (!recorded) {
        rt_kprintf("Boot time not recorded; call cvi_boot_time_record() at main entry.\n");
        return -RT_ERROR;
    }
    cvi_boot_time_dump();
    return RT_EOK;
}
MSH_CMD_EXPORT(boottime, "Print saved boot timestamps in milliseconds");
#endif
