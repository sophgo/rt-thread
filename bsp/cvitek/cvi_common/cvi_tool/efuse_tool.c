/* SPDX-License-Identifier: Apache-2.0 */
#include <rtthread.h>
#include <finsh.h>
#include <shell.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "drv_efuse.h"

static int efuse_usage(void)
{
    rt_kprintf("Usage:\n"
               "  efuse sn\n"
               "  efuse dump\n"
               "  efuse read <hex-byte-offset>\n"
               "  efuse write <hex-byte-offset> <hex-value>\n"
               "  efuse fastboot [enable|revert [sd|usb|uart|all ...]]\n"
               "  efuse bootfreq [enable]\n"
               "Offsets: 00..fc, aligned to 4 bytes (SW_INFO=2c, CUSTOMER=04).\n"
               "fastboot/bootfreq without arguments only query; writes require typing yes.\n");
    return -1;
}

static int efuse_parse_hex(const char *arg, uint32_t *value)
{
    char *end;
    unsigned long parsed;

    if (!arg || !isxdigit((unsigned char)*arg))
        return -1;
    errno = 0;
    parsed = strtoul(arg, &end, 16);
    if (end == arg || *end || errno == ERANGE || parsed > UINT32_MAX)
        return -1;
    *value = (uint32_t)parsed;
    return 0;
}

/* Read from the shell's console, avoiding a separate stdin device. Only a
 * complete, exact "yes" line permits a burn; EOF/Ctrl-C/overflow cancel. */
static int efuse_confirm(void)
{
    char answer[4] = {0};
    unsigned length = 0;
    int overflow = 0;
    int ch;

    rt_kprintf("OTP bits cannot be cleared. Type yes and press Enter to program: ");
    for (;;) {
        ch = finsh_getchar();
        if (ch < 0 || ch == 3) {
            rt_kprintf("\nCancelled.\n");
            return 0;
        }
        if (ch == '\r' || ch == '\n') {
            /* A CRLF command terminator may leave one newline in the queue. */
            if (!length && !overflow)
                continue;
            break;
        }
        if (length < sizeof(answer) - 1)
            answer[length++] = (char)ch;
        else
            overflow = 1;
        rt_kprintf("%c", ch);
    }
    rt_kprintf("\n");
    if (!overflow && !strcmp(answer, "yes"))
        return 1;
    rt_kprintf("Cancelled.\n");
    return 0;
}

static int efuse_sn(void)
{
    uint32_t data[2] = {0};
    uint32_t size = sizeof(data);

    if (cvi_efuse_get_chip_sn(data, &size) != sizeof(data) || size != sizeof(data)) {
        rt_kprintf("Read chip SN failed.\n");
        return -1;
    }
    rt_kprintf("Chip SN: %08x %08x\n", data[0], data[1]);
    return 0;
}

static int efuse_dump(void)
{
    uint32_t value;
    unsigned word;

    for (word = 0; word < 64; word++) {
        if (cvi_efuse_read_word_from_shadow(word, &value) != EFUSE_OK) {
            rt_kprintf("Read offset 0x%02x failed.\n", word * 4);
            return -1;
        }
        rt_kprintf("%08x%c", value, (word & 3) == 3 ? '\n' : ' ');
    }
    return 0;
}

static int efuse_word(int argc, char **argv, int write)
{
    uint32_t offset, value = 0, before, after;
    int ret;

    if (argc != (write ? 3 : 2) || efuse_parse_hex(argv[1], &offset) ||
        offset > 0xfc || (offset & 3) ||
        (write && efuse_parse_hex(argv[2], &value)))
        return efuse_usage();
    if (cvi_efuse_read_word_from_shadow(offset / 4, &before) != EFUSE_OK)
        return -1;
    if (!write) {
        rt_kprintf("0x%08x = 0x%08x\n", offset, before);
        return 0;
    }

    rt_kprintf("Offset 0x%02x: 0x%08x -> 0x%08x\n", offset, before, before | value);
    if (!efuse_confirm())
        return -1;
    if (cvi_efuse_pwr_on() != EFUSE_OK)
        return -1;
    ret = cvi_efuse_program_word(offset / 4, value);
    if (ret == EFUSE_OK) {
        ret = cvi_efuse_read_word_from_shadow(offset / 4, &after);
        if (ret == EFUSE_OK && after != (before | value))
            ret = EFUSE_ERROR;
    }
    if (cvi_efuse_pwr_off() != EFUSE_OK)
        ret = EFUSE_ERROR;
    rt_kprintf("Write %s.\n", ret == EFUSE_OK ? "complete" : "failed");
    return ret == EFUSE_OK ? 0 : -1;
}

static int efuse_fastboot_status(void)
{
    static const char *sources[] = {"SD", "USB", "UART"};
    static const char *modes[] = {"normal", "fastboot", "disabled", "normal (reverted)"};
    uint32_t sw_info, customer;
    unsigned i;

    if (cvi_efuse_read_word_from_shadow(EFUSE_SW_INFO_WORD, &sw_info) != EFUSE_OK ||
        cvi_efuse_read_word_from_shadow(EFUSE_CUSTOMER_WORD, &customer) != EFUSE_OK) {
        rt_kprintf("Read fastboot status failed.\n");
        return -1;
    }
    rt_kprintf("SW_INFO (0x2c): 0x%08x\nCUSTOMER (0x04): 0x%08x\n", sw_info, customer);
    for (i = 0; i < 3; i++)
        rt_kprintf("%s: %s\n", sources[i], modes[(sw_info >> (22 + i * 2)) & 3]);
    rt_kprintf("IsFastBootEnabled: %s\n",
               CVI_EFUSE_IsFastBootEnabled() == CVI_SUCCESS ? "enabled" : "disabled");
    return 0;
}

static int efuse_fastboot_cmd(int argc, char **argv)
{
    uint32_t sources = 0;
    CVI_BOOL revert;
    int i, ret, status;

    if (argc == 1)
        return efuse_fastboot_status();
    if (!strcmp(argv[1], "enable"))
        revert = CVI_FALSE;
    else if (!strcmp(argv[1], "revert"))
        revert = CVI_TRUE;
    else
        return efuse_usage();
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "sd")) sources |= EFUSE_FASTBOOT_SD;
        else if (!strcmp(argv[i], "usb")) sources |= EFUSE_FASTBOOT_USB;
        else if (!strcmp(argv[i], "uart")) sources |= EFUSE_FASTBOOT_UART;
        else if (!strcmp(argv[i], "all")) sources |= EFUSE_FASTBOOT_ALL;
        else return efuse_usage();
    }
    if (argc == 2)
        sources = EFUSE_FASTBOOT_ALL;
    if (efuse_fastboot_status())
        return -1;
    rt_kprintf("Fastboot %s: sources=0x%x (SD=1 USB=2 UART=4).\n",
               revert ? "revert" : "enable", sources);
    if (revert)
        rt_kprintf("Reverted sources cannot be enabled as fastboot again.\n");
    if (!efuse_confirm())
        return -1;
    ret = CVI_EFUSE_ConfigFastBoot(sources, revert);
    rt_kprintf("Fastboot %s %s.\n", revert ? "revert" : "enable",
               ret == CVI_SUCCESS ? "complete" : "failed");
    status = efuse_fastboot_status();
    return ret == CVI_SUCCESS && status == 0 ? 0 : -1;
}

static int efuse_bootfreq_status(void)
{
    uint32_t bonding0;

    if (cvi_efuse_read_word_from_shadow(0xa, &bonding0) != EFUSE_OK) {
        rt_kprintf("Read bonding0 failed.\n");
        return -1;
    }
    rt_kprintf("BONDING0 (0x28): 0x%08x\nBoot freq [11:10]: %u (%s)\n", bonding0,
               (bonding0 >> 10) & 3,
               CVI_EFUSE_IsBootFreqHigher() == CVI_SUCCESS ? "750MHz" : "default");
    return 0;
}

static int efuse_bootfreq_cmd(int argc, char **argv)
{
    int ret;

    if (argc == 1)
        return efuse_bootfreq_status();
    if (argc != 2 || strcmp(argv[1], "enable"))
        return efuse_usage();
    if (efuse_bootfreq_status())
        return -1;
    if (CVI_EFUSE_IsBootFreqHigher() == CVI_SUCCESS) {
        rt_kprintf("Boot freq already set.\n");
        return 0;
    }
    rt_kprintf("Program BONDING0 bit11, takes effect from next power-on.\n");
    if (!efuse_confirm())
        return -1;
    ret = CVI_EFUSE_BootFreqHigher();
    rt_kprintf("Boot freq %s.\n", ret == CVI_SUCCESS ? "complete" : "failed");
    return ret == CVI_SUCCESS ? 0 : -1;
}

static int efuse(int argc, char **argv)
{
    if (argc < 2)
        return efuse_usage();
    if (!strcmp(argv[1], "sn") && argc == 2)
        return efuse_sn();
    if (!strcmp(argv[1], "dump") && argc == 2)
        return efuse_dump();
    if (!strcmp(argv[1], "read"))
        return efuse_word(argc - 1, argv + 1, 0);
    if (!strcmp(argv[1], "write"))
        return efuse_word(argc - 1, argv + 1, 1);
    if (!strcmp(argv[1], "fastboot"))
        return efuse_fastboot_cmd(argc - 1, argv + 1);
    if (!strcmp(argv[1], "bootfreq"))
        return efuse_bootfreq_cmd(argc - 1, argv + 1);
    return efuse_usage();
}
MSH_CMD_EXPORT(efuse, eFUSE query and provisioning tool);

static int efuser_word(int argc, char **argv)
{
    return efuse_word(argc, argv, 0);
}
MSH_CMD_EXPORT(efuser_word, Read eFUSE word at hex byte offset);

static int efusew_word(int argc, char **argv)
{
    return efuse_word(argc, argv, 1);
}
MSH_CMD_EXPORT(efusew_word, Program eFUSE word with confirmation);
