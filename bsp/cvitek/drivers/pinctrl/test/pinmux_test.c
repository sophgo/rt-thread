#include <rtthread.h>
#include <drv_gpio.h>
#include <board.h>
#include <drivers/dev_pin.h>
#include <mmio.h>
#include "pinctrl.h"
#include <drv_pinmux.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <func.h>
#include <finsh.h>

#define NELEMS(x)   (sizeof(x) / sizeof((x)[0]))
#define PINMUX_BASE 0x03001000
#define INVALID_PIN 9999

// 1. For each pin, first obtain its base, the number of funcs, and the corresponding func value
// 2. Then call `pinmux -w` to set
// 3. Read via `devmem` to check if it works correctly
int pinmux_test(int argc, char **argv)
{
	struct pinlist *test_pinlist = NULL;
	struct funlist *test_pinlist_func = NULL;
	int pin_list_num = 0;
	int pin_func_num = 0;

#if defined(SOC_TYPE_CV180XB_QFN)
	test_pinlist = pin_list;
	test_pinlist_func = pin_func;
	pin_list_num = NELEMS(pin_list);
	pin_func_num = NELEMS(pin_func);
#else
#if defined(SOC_TYPE_CV181XC_QFN) || defined(SOC_TYPE_CV181XH_BGA)
	test_pinlist = mars_pin;
	test_pinlist_func = mars_pin_func;
	pin_list_num = NELEMS(mars_pin);
	pin_func_num = NELEMS(mars_pin_func);
#endif
#endif

	rt_kprintf("--- pinmux test start ---\n");
	uint32_t total_num = 0;
	uint32_t failed_num = 0;
	uint8_t failed_record[100];

	rt_memset(failed_record, 0, sizeof(failed_record));
	// For each pin
	for (int i = 0; i < pin_list_num; i++) {
		rt_kprintf("\n============= %s =============\n", test_pinlist[i].name);
		uint32_t base = PINMUX_BASE + test_pinlist[i].offset;

		rt_kprintf("Base: 0x%x\n", base);
		// For each func of the pin
		for (int j = 0; j < pin_func_num; j++) {
			if (strncmp(test_pinlist_func[j].name, test_pinlist[i].name, strlen(test_pinlist[i].name)) == 0) {
				total_num++;
				rt_kprintf("\n[%d] func: %s ", j + 1, test_pinlist_func[j].func);
				uint32_t f_val = convert_func_to_value(test_pinlist[i].name, test_pinlist_func[j].func);

				if (f_val == INVALID_PIN) {
					rt_kprintf("\nERROR: invalid pin or func\n");
					failed_record[failed_num++] = i + j;
					continue;
				} else {
					rt_kprintf("val: %d\n", f_val);
				}
				// Get original reg value
				uint32_t ori_reg_val = mmio_read_32(base);
				char *cmd1[128];

				// Call pinmux -w to set
				snprintf(cmd1, sizeof(cmd1), "cvi_pinmux -w %s/%s", test_pinlist[i].name, test_pinlist_func[j].func);
				rt_kprintf("cmd: %s\n", cmd1);
				msh_exec(cmd1, strlen(cmd1));

				// Read reg value to check if it works correctly
				uint32_t reg_val = mmio_read_32(base);
				// Then set it back
				mmio_write_32(base, ori_reg_val);
				rt_kprintf("read reg_val: 0x%x\n", reg_val);
				if (reg_val == f_val) {
					rt_kprintf("set pin %s func %s [success]\n", test_pinlist[i].name, test_pinlist_func[j].func);
				} else {
					failed_record[failed_num++] = i + j;
					rt_kprintf("set pin %s func %s [failed]\n", test_pinlist[i].name, test_pinlist_func[j].func);
				}
			}
		}
	}
	rt_kprintf("--------------------\n");
	rt_kprintf("[%d/%d] passed num: %d, total num: %d\n", total_num - failed_num, total_num, total_num - failed_num, total_num);
	if (failed_num == 0) {
		rt_kprintf("pinmux test passed\n");
	} else {
		rt_kprintf("pinmux test failed\n");
		for (int i = 0; i < failed_num; i++) {
			rt_kprintf("failed pin: %.*s, failed func: 0x%s\n", strlen(test_pinlist_func[failed_record[i]].name) - 1, test_pinlist_func[failed_record[i]].name, test_pinlist_func[failed_record[i]].func);
		}
	}
	rt_kprintf("===== pinmux test end =====\n\n");

	return 0;
}

MSH_CMD_EXPORT(pinmux_test, pinmux test);
