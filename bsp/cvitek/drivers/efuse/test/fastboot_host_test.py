#!/usr/bin/env python3
"""Verify FastBoot OTP transitions using real driver code and simulated MMIO.

Run on the host with Python 3 and a C compiler; no board is accessed.
"""
import os
from pathlib import Path
import subprocess
import tempfile

STUBS = {'rtos_types.h': '',
 'finsh.h': '#define MSH_CMD_EXPORT_ALIAS(...)\n#define MSH_CMD_EXPORT(...)\n',
 'rtthread.h': '#define rt_kprintf printf\n',
 'shell.h': 'int finsh_getchar(void);\n',
 'mmio.h': '#include <stdint.h>\n'
           'uint32_t mmio_read_32(uintptr_t addr);\n'
           'void mmio_write_32(uintptr_t addr, uint32_t data);\n',
 'common.h': '#include <stdio.h>\n'
             '#include <stdlib.h>\n'
             '#include <stddef.h>\n'
             '#define CVI_SUCCESS 0\n'
             '#define CVI_FAILURE (-1)\n'}

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "drv_efuse.h"
#include "cvi_efuse.h"
static uint32_t otp[64], shadow[64], address, chip;
static unsigned burns, power_on, power_off;
static int drop_customer, drop_sw;
uint32_t mmio_read_32(uintptr_t addr)
{
    if (addr == 0x0300008c) return chip;
    if (addr == EFUSE_BASE + EFUSE_STATUS) return 0;
    assert(addr >= EFUSE_SHADOW_REG && addr < EFUSE_SHADOW_REG + 256);
    return shadow[(addr - EFUSE_SHADOW_REG) / 4];
}
void mmio_write_32(uintptr_t addr, uint32_t data)
{
    if (addr == EFUSE_BASE + EFUSE_ADR) { address = data; return; }
    assert(addr == EFUSE_BASE + EFUSE_MODE);
    if (data == 0x10) power_on++;
    else if (data == 0x18) power_off++;
    else if (data == 0x30) memcpy(shadow, otp, sizeof(otp));
    else if (data == 0x14) {
        unsigned word = (address >> 1) & 63, bit = address >> 7;
        burns++;
        if ((drop_customer && word == EFUSE_CUSTOMER_WORD) ||
            (drop_sw && word == EFUSE_SW_INFO_WORD)) return;
        otp[word] |= 1U << bit;
    } else assert(0);
}
static void reset(uint32_t sw, uint32_t customer)
{
    memset(otp, 0, sizeof(otp)); otp[11] = sw; otp[1] = customer;
    memcpy(shadow, otp, sizeof(otp));
    burns = power_on = power_off = 0; drop_customer = drop_sw = 0;
}
static unsigned popcount(uint32_t x) { return __builtin_popcount(x); }
static const char *input;
static unsigned input_reads;
int finsh_getchar(void)
{
    /* No OTP programming is allowed while confirmation is incomplete. */
    assert(!burns && !power_on);
    input_reads++;
    return *input ? (unsigned char)*input++ : -1;
}
#include "efuse_tool.c"
static int run_cmd(int argc, char **argv, const char *answer)
{
    input = answer;
    input_reads = 0;
    return efuse(argc, argv);
}
static void command_tests(void)
{
    chip = 0x1811c;
    char *query[] = {"efuse", "fastboot"};
    char *enable[] = {"efuse", "fastboot", "enable"};
    char *uart[] = {"efuse", "fastboot", "enable", "uart"};
    char *revert[] = {"efuse", "fastboot", "revert", "uart"};
    char *bad_source[] = {"efuse", "fastboot", "enable", "oops"};
    char *sn[] = {"efuse", "sn"};
    char *dump[] = {"efuse", "dump"};
    char *read[] = {"efuse", "read", "fc"};
    char *write[] = {"efuse", "write", "fc", "80000001"};
    const char *cancel[] = {"no\n", "yesx\n", "YES\n", "yes", "\003", ""};
    reset(0, 0);
    assert(run_cmd(2, query, "yes\n") == 0 && !burns && !power_on && !input_reads);
    assert(run_cmd(2, sn, "") == 0 && !burns && !power_on && !input_reads);
    assert(run_cmd(2, dump, "") == 0 && !burns && !power_on && !input_reads);
    assert(run_cmd(3, read, "") == 0 && !burns && !power_on && !input_reads);
    assert(run_cmd(4, bad_source, "yes\n") != 0 && !burns && !input_reads);
    for (unsigned i = 0; i < sizeof(cancel) / sizeof(cancel[0]); i++) {
        reset(0, 0);
        assert(run_cmd(3, enable, cancel[i]) != 0 && !burns && !power_on);
        assert(run_cmd(4, write, cancel[i]) != 0 && !burns && !power_on);
    }
    reset(0, 0);
    assert(run_cmd(3, enable, "yes\n") == 0 && input_reads == 4);
    assert(shadow[11] == 0x05400000 && shadow[1] == 0x1e1e64);
    reset(0, 0);
    assert(run_cmd(4, uart, "\nyes\r\n") == 0);
    assert(shadow[11] == 0x04000000 && shadow[1] == 0x1e1e64);
    reset(0x05400000, 0x1e1e64);
    assert(run_cmd(4, revert, "yes\n") == 0);
    assert(shadow[11] == 0x0d400000 && shadow[1] == 0x1e1e64);
    reset(0, 0);
    assert(run_cmd(4, write, "yes\n") == 0 && shadow[63] == 0x80000001);
    assert(power_on == 1 && power_off == 1);
    reset(0, 0);
    char *bad_offsets[] = {"", "1", "100", "-4", "+4", " 4", "xyz", "100000000"};
    for (unsigned i = 0; i < sizeof(bad_offsets) / sizeof(bad_offsets[0]); i++) {
        write[2] = bad_offsets[i];
        assert(run_cmd(4, write, "yes\n") != 0 && !burns && !power_on && !input_reads);
    }
    write[2] = "2c";
    write[3] = "100000000";
    assert(run_cmd(4, write, "yes\n") != 0 && !burns && !input_reads);
    write[3] = "4000000";
    drop_sw = 1;
    assert(run_cmd(4, write, "yes\n") != 0 && power_on == 1 && power_off == 1);
    reset(0, 0);
    char *alias_read[] = {"efuser_word", "2c"};
    char *alias_write[] = {"efusew_word", "04", "1"};
    assert(efuser_word(2, alias_read) == 0 && !burns && !power_on);
    input = "no\n";
    assert(efusew_word(3, alias_write) != 0 && !burns && !power_on);
    input = "yes\n";
    assert(efusew_word(3, alias_write) == 0 && shadow[1] == 1);
}
int main(void)
{
    command_tests();
    unsigned tests = 0;
    const uint32_t chips[] = {0x1811c, 0x1800b, 0x1815, 0x18114};
    const uint32_t magic[] = {0x1e1e64, 0x1e1564, 0x1e1e64, 1};
    for (unsigned c = 0; c < 4; c++) {
        chip = chips[c];
        for (unsigned fields = 0; fields < 64; fields++)
        for (unsigned mask = 0; mask < 8; mask++)
        for (unsigned undo = 0; undo < 2; undo++) {
            uint32_t initial = (fields << 22) | 0xa0001234;
            uint32_t expected = initial;
            int fail = 0;
            for (unsigned i = 0; i < 3; i++) {
                if (!(mask & (1U << i))) continue;
                unsigned mode = (fields >> (2*i)) & 3;
                if (undo && (mode == 1 || mode == 2)) expected |= 3U << (22 + 2*i);
                if (!undo && mode == 0) expected |= 1U << (22 + 2*i);
                if (!undo && mode == 3) fail = 1;
            }
            reset(initial, 0);
            int ret = CVI_EFUSE_ConfigFastBoot(mask, undo);
            assert(ret == (fail ? CVI_FAILURE : CVI_SUCCESS));
            if (fail) expected = initial;
            assert(shadow[11] == expected);
            int changed = expected != initial;
            assert(shadow[1] == ((!undo && changed) ? magic[c] : 0));
            assert(power_on == (unsigned)changed && power_off == (unsigned)changed);
            assert(burns == 2 * (popcount(expected ^ initial) +
                ((!undo && changed) ? popcount(magic[c]) : 0)));
            if (changed) {
                unsigned previous = burns;
                assert(CVI_EFUSE_ConfigFastBoot(mask, undo) == CVI_SUCCESS);
                assert(burns == previous);
            }
            tests++;
        }
    }
    chip = 0x1811c;
    reset(0, 0x80000000);
    assert(CVI_EFUSE_ConfigFastBoot(1, 0) == CVI_FAILURE && burns == 0);
    reset(1U << 22, 0x80000000);
    assert(CVI_EFUSE_ConfigFastBoot(1, 1) == CVI_SUCCESS && shadow[1] == 0x80000000);
    reset(0, 0); drop_customer = 1;
    assert(CVI_EFUSE_ConfigFastBoot(1, 0) == CVI_FAILURE && shadow[11] == 0 && power_off == 1);
    reset(0, 0); drop_sw = 1;
    assert(CVI_EFUSE_ConfigFastBoot(1, 0) == CVI_FAILURE && power_off == 1);
    reset(0, 0);
    assert(CVI_EFUSE_ConfigFastBoot(8, 0) == CVI_FAILURE && burns == 0);
    reset(0, 0x1e1e64);
    assert(CVI_EFUSE_EnableFastBoot() == CVI_SUCCESS && shadow[11] == 0x05400000);
    uint32_t selected = EFUSE_FASTBOOT_ALL;
    int undo = 0;
#ifdef BSP_USING_EFUSE_FASTBOOT
    selected = 0;
#ifdef BSP_EFUSE_FASTBOOT_SD
    selected |= EFUSE_FASTBOOT_SD;
#endif
#ifdef BSP_EFUSE_FASTBOOT_USB
    selected |= EFUSE_FASTBOOT_USB;
#endif
#ifdef BSP_EFUSE_FASTBOOT_UART
    selected |= EFUSE_FASTBOOT_UART;
#endif
#ifdef BSP_EFUSE_FASTBOOT_REVERT
    undo = 1;
#endif
#endif
    uint32_t before = undo ? 0x05400000 : 0;
    uint32_t after = before;
    for (unsigned i = 0; i < 3; i++)
        if (selected & (1U << i)) after |= (undo ? 3U : 1U) << (22 + 2*i);
    reset(before, undo ? 0x80000000 : 0);
    efuse_fastboot();
    assert(shadow[11] == after);
    assert(shadow[1] == (undo ? 0x80000000 : selected ? 0x1e1e64 : 0));
    printf("PASS: %u exhaustive cases plus command confirmation, conflict, fault, API and idempotence checks\n", tests);
    return 0;
}
'''

def main():
    root = Path(__file__).resolve().parents[5]
    cases = {
        "off": [],
        "default": ["UART", "SD", "USB"],
        "uart": ["UART"],
        "sd": ["SD"],
        "usb": ["USB"],
        "none": [],
        "revert": ["UART", "SD", "USB", "REVERT"],
        "uart-revert": ["UART", "REVERT"],
    }
    with tempfile.TemporaryDirectory(prefix="rtt-efuse-test-") as directory:
        work = Path(directory)
        for name, content in STUBS.items():
            (work / name).write_text(content, encoding="utf-8")
        (work / "test.c").write_text(HARNESS, encoding="utf-8")
        command = [os.environ.get("CC", "cc"), "-std=c99",
                   "-ffunction-sections", "-fdata-sections", "-I" + str(work)]
        command += ["-I" + str(root / path) for path in [
            "components/cvi_comps/cvi_mpi/include",
            "bsp/cvitek/drivers/efuse", "bsp/cvitek/libraries/efuse",
            "bsp/cvitek/cvi_common/cvi_tool"]]
        command += [str(work / "test.c"),
                    str(root / "bsp/cvitek/drivers/efuse/drv_efuse.c"),
                    str(root / "bsp/cvitek/libraries/efuse/cvi_efuse.c"),
                    "-Wl,--gc-sections", "-o", str(work / "test")]
        for name, flags in cases.items():
            options = [] if name == "off" else ["-DBSP_USING_EFUSE_FASTBOOT"]
            options += ["-DBSP_EFUSE_FASTBOOT_" + flag for flag in flags]
            subprocess.run(command + options, check=True)
            result = subprocess.run([str(work / "test")], stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True)
            if result.returncode:
                print(result.stdout)
                result.check_returncode()
            print(name + ": " + result.stdout.splitlines()[-1])


if __name__ == "__main__":
    main()
