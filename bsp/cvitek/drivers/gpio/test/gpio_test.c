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

#define PIN_IRQ_MODE_RISING             0x00 /*!< rising edge trigger */
#define PIN_IRQ_MODE_FALLING            0x01 /*!< falling edge trigger */
#define PIN_IRQ_MODE_RISING_FALLING     0x02 /*!< rising and falling edge trigger */
#define PIN_IRQ_MODE_HIGH_LEVEL         0x03 /*!< high level trigger */
#define PIN_IRQ_MODE_LOW_LEVEL          0x04 /*!< low level trigger */

static volatile int interrupt_count = 0;
typedef struct {
rt_base_t pin_in;
uint8_t irq_mode;
} gpio_intr_args_t;

static void gpio_interrupt_handler(void *args)
{
	gpio_intr_args_t *p = (gpio_intr_args_t *)args;
	rt_base_t pin = (rt_base_t)(p->pin_in);
	uint8_t irq_mode = p->irq_mode;

	interrupt_count++;
	if (irq_mode != PIN_IRQ_MODE_HIGH_LEVEL && irq_mode != PIN_IRQ_MODE_LOW_LEVEL) {
		rt_kprintf(" !!!INT!!! ");
	} else if (interrupt_count > 19) {
		rt_pin_irq_enable(pin, PIN_IRQ_DISABLE);
		rt_kprintf("\n!!!Got 20 interrupts, close it now or it will keep firing!!!\n");
	}
}
static void gpio_output_test(rt_base_t pin, u8 level, u8 mode)
{
	//Only support normal output mode.
	rt_pin_mode(pin, PIN_MODE_OUTPUT);
	if (mode == 1) {
		if (level != 2) {
			rt_pin_write(pin, level);
			rt_kprintf(" output, level: %d\n", level);
			rt_kprintf("Please use a voltmeter to measure the actual output voltage.\n");
		} else {
			rt_kprintf("[1]Auto switching Test Starting:\n", level);
			rt_kprintf("Please use a voltmeter to measure the actual output voltage.\n");
			rt_thread_mdelay(2000);

			for (int i = 0; i < 5; i++) {
				rt_pin_write(pin, PIN_HIGH);
				rt_kprintf("Iteration %d: PIN_HIGH\n", i + 1);
				rt_thread_mdelay(800);

				rt_pin_write(pin, PIN_LOW);
				rt_kprintf("Iteration %d: PIN_LOW\n", i + 1);
				rt_thread_mdelay(800);
			}
			rt_kprintf("[1]Auto switching Test End\n");
		}
	} else {
		rt_kprintf("[1] Error: Only support normal mode(1) now.\n");
	}
}

static void gpio_intput_test(rt_base_t pin_out, rt_base_t pin_in)
{
	int read_value;

    // input mode
	rt_kprintf("\n[2] Input Mode Test Starting:\n");
	rt_kprintf("Please connect PINOUT to PININ with a jumper wire\n");
	rt_thread_mdelay(2000);
	rt_pin_mode(pin_out, PIN_MODE_OUTPUT);
	rt_pin_mode(pin_in, PIN_MODE_INPUT);

	for (int i = 0; i < 5; i++) {
		rt_pin_write(pin_out, (i % 2) ? PIN_HIGH : PIN_LOW);

		read_value = rt_pin_read(pin_in);

		rt_kprintf("OUT: %s -> IN: %s [%s]\n",
				(i % 2) ? "HIGH" : "LOW",
				read_value ? "HIGH" : "LOW",
				((i % 2) == read_value) ? "PASS" : "FAIL");

		rt_thread_mdelay(1000);
	}
	rt_kprintf("[2] Input Mode Test End\n");
}

static void gpio_intr_edge_test_loop(rt_base_t pin_out, rt_base_t pin_in, u8 irq_mode, u8 enable)
{
	rt_pin_irq_enable(pin_in, enable ? PIN_IRQ_ENABLE : PIN_IRQ_DISABLE);
	for (int i = 0; i < 5; i++) {
		rt_kprintf("---[Loop %d start:]---\n", i);
		rt_pin_write(pin_out, PIN_LOW);
		rt_thread_mdelay(100);

		rt_kprintf("\nCurrent pin level: %d\n", rt_pin_read(pin_in));

		rt_kprintf("From pin level %d to %d ", rt_pin_read(pin_in), PIN_HIGH);

		rt_pin_write(pin_out, PIN_HIGH);
		rt_thread_mdelay(100);
		rt_kprintf("\nCurrent pin level: %d\n", rt_pin_read(pin_in));

		rt_kprintf("From pin level %d to %d", rt_pin_read(pin_in), PIN_LOW);
		rt_pin_write(pin_out, PIN_LOW);
		rt_thread_mdelay(100);
		rt_kprintf("\nCurrent pin level: %d\n\n", rt_pin_read(pin_in));

		rt_thread_mdelay(500);
	}
}
static void gpio_intr_level_test_loop(rt_base_t pin_out, rt_base_t pin_in, u8 irq_mode, u8 enable)
{

	rt_pin_irq_enable(pin_in, enable ? PIN_IRQ_ENABLE : PIN_IRQ_DISABLE);

	rt_pin_write(pin_out, irq_mode == PIN_IRQ_MODE_HIGH_LEVEL ? PIN_LOW : PIN_HIGH);
	rt_thread_mdelay(100);

	rt_kprintf("\nCurrent pin level: %d, waiting 200ms for interrupt\n", rt_pin_read(pin_in));
	rt_thread_mdelay(200);

	rt_kprintf("Waiting 200ms finished. Total interrupt times: %d\n", interrupt_count);
	interrupt_count = 0;
	rt_kprintf("\nFrom pin level %d to %d, waiting 200ms for interrupt\n",
			rt_pin_read(pin_in), irq_mode == PIN_IRQ_MODE_HIGH_LEVEL ? PIN_HIGH : PIN_LOW);

	rt_pin_write(pin_out, irq_mode == PIN_IRQ_MODE_HIGH_LEVEL ? PIN_HIGH : PIN_LOW);
	rt_thread_mdelay(200);
	rt_kprintf("Waiting 200ms finished. Total interrupt times: %d (max 20)\n", interrupt_count);

}

static void gpio_interrupt_normal_test(rt_base_t pin_out, rt_base_t pin_in, u8 irq_mode)
{
	// interrupt mode
	// irq_mode: 0:rising edge, 1:falling edge, 2:both edges, 3:high level, 4:low level
	gpio_intr_args_t args;
	const char *mode_str[5] = {"rising edge", "falling edge", "both edges", "high level", "low level"};

	args.pin_in = pin_in;
	args.irq_mode = irq_mode;
	rt_kprintf("\n[3.%d] Interrupt Mode Test Starting: mode: %d: [%s]\n", irq_mode, irq_mode, mode_str[irq_mode]);
	rt_kprintf("Please connect PINOUT to PININ with a jumper wire\n");
	rt_thread_mdelay(2000);

	rt_pin_mode(pin_in, PIN_MODE_INPUT);
	rt_pin_mode(pin_out, PIN_MODE_OUTPUT);

	rt_pin_attach_irq(pin_in, irq_mode,
						gpio_interrupt_handler, &args);

	interrupt_count = 0;

	//Edge interrupt
	if (irq_mode < 3) {
		gpio_intr_edge_test_loop(pin_out, pin_in, irq_mode, 1);

		//For edge-triggered interrupts (rising or falling),
		//	it’s necessary to check whether each edge (rising and falling) can trigger.
		if (irq_mode == PIN_IRQ_MODE_RISING_FALLING) {
			rt_kprintf("Total interrupts: %d (Expected: 10) [%s]\n",
				interrupt_count, 
				(interrupt_count == 10) ? "PASS" : "FAIL");
		} else {
			rt_kprintf("Total interrupts: %d (Expected: 5) [%s]\n",
				interrupt_count,
				(interrupt_count == 5) ? "PASS" : "FAIL");
		}
	} else { //Level interrupt
		gpio_intr_level_test_loop(pin_out, pin_in, irq_mode, 1);
		rt_kprintf("Total interrupts: %d \n", interrupt_count);
	}
	rt_pin_irq_enable(pin_in, PIN_IRQ_DISABLE);
	rt_pin_detach_irq(pin_in);
	rt_kprintf("[3.%d] Interrupt Mode Test End\n", irq_mode);
}

static void gpio_interrupt_enable_test(rt_base_t pin_out, rt_base_t pin_in, u8 irq_mode)
{
	// irq_mode: 0:rising edge, 1:falling edge, 2:both edges, 3:high level, 4:low level
	const char *mode_str[5] = {"rising edge", "falling edge", "both edges", "high level", "low level"};
	gpio_intr_args_t args;

	args.pin_in = pin_in;
	args.irq_mode = irq_mode;
	rt_kprintf("\n[4] Interrupt Enable Mode Test Starting: mode: %d: [%s]\n", irq_mode, mode_str[irq_mode]);
	rt_kprintf("Please connect PINOUT to PININ with a jumper wire\n");
    rt_thread_mdelay(2000);

	rt_pin_mode(pin_out, PIN_MODE_OUTPUT);
	rt_pin_mode(pin_in, PIN_MODE_INPUT);
	rt_pin_attach_irq(pin_in, irq_mode,
						gpio_interrupt_handler, &args);

	//Disable:
	rt_kprintf("\n=== [Disable test start:] ===\n");
	interrupt_count = 0;
	gpio_intr_edge_test_loop(pin_out, pin_in, irq_mode, 0);

	rt_kprintf("Disable irq: total interrupts: %d (Expected: 0) [%s]\n",
				interrupt_count, 
				(interrupt_count == 0) ? "PASS" : "FAIL");

	//ENABLE:
	rt_kprintf("\n=== [Enable test start:] ===\n");

	interrupt_count = 0;
	gpio_intr_edge_test_loop(pin_out, pin_in, irq_mode, 1);

	rt_kprintf("Enable interrupts: %d (Expected: 5) [%s]\n",
				interrupt_count,
				(interrupt_count == 5) ? "PASS" : "FAIL");

	rt_pin_irq_enable(pin_in, PIN_IRQ_DISABLE);
	rt_pin_detach_irq(pin_in);
	rt_kprintf("[4] Interrupt Enable Mode Test End\n");
}

static int test_pin_set(u8 has_pin_arg, const char *pin_arg, u8 is_input)
{
	//If params has pin arg, then get pin by arg. Otherwise use pin A28 and pin A29.
	if (has_pin_arg == 1) {
		rt_kprintf("please make sure pinmux is correct first\n [Set output pin: %s]\n", pin_arg);
		return rt_pin_get(pin_arg);
	} else if (is_input == 1) {
		rt_kprintf("[Set input pin: A29]\n");
		PINMUX_CONFIG(IIC0_SDA, XGPIOA_29);
		return rt_pin_get("A29");
	} else {
		rt_kprintf("[Set output pin: A28]\n");
		PINMUX_CONFIG(IIC0_SCL, XGPIOA_28);
		return rt_pin_get("A28");
	}
}

int gpio_test(int argc, char **argv)
{
	char *cmd;

	if (argc < 2) {
		goto Usage;
    }

	cmd = argv[1];

	rt_kprintf("\n--- GPIO_TEST: %s ---\n", cmd);

	if (strcmp(cmd, "all") == 0) {
		rt_base_t pin_out, pin_in;
		u8 has_pin_out = 0, has_pin_in = 0;
		const char *pin_arg_out = NULL, *pin_arg_in = NULL;
		for (int i = 2; i < argc; ++i) {
			const char *a = argv[i];

			if (strcmp(a, "-o") == 0) {
				if (i + 1 >= argc) {
					rt_kprintf("Error: -o requires a value\n");
					return -1;
				}
				pin_arg_out = argv[++i];
				has_pin_out = 1;
				continue;
			}

			if (strcmp(a, "-i") == 0) {
				if (i + 1 >= argc) {
					rt_kprintf("Error: -i requires a value\n");
					return -1;
				}
				pin_arg_in = argv[++i];
				has_pin_in = 1;
				continue;
			}

			rt_kprintf("Error: unknown option: %s\n", a);
			goto Usage;
		}

		pin_out = test_pin_set(has_pin_out, pin_arg_out, 0);
		pin_in = test_pin_set(has_pin_in, pin_arg_in, 1);

		if (pin_out < 0 || pin_in < 0)
			return -1;

		rt_kprintf("\n--- Starting ALL GPIO Driver Test ---\n");
		gpio_output_test(pin_out, 2, 1);
		gpio_intput_test(pin_out, pin_in);
		for (int i = 0; i < 5; i++) {
			gpio_interrupt_normal_test(pin_out, pin_in, i);
		}
		gpio_interrupt_enable_test(pin_out, pin_in, 0);

	    rt_kprintf("\n--- ALL GPIO Test Completed ---\n");
		return 0;
	}

	if (strcmp(cmd, "output") == 0) {
		rt_base_t pin_out;
		u8 level = 0, mode = 1;
		u8 has_pin_out = 0;
		const char *pin_arg_out = NULL, *pin_arg_level = NULL;

		for (int i = 2; i < argc; ++i) {
			const char *a = argv[i];

			if (strcmp(a, "-o") == 0) {
				if (i + 1 >= argc) {
					rt_kprintf("Error: -o requires a value\n");
					return -1;
				}
				pin_arg_out = argv[++i];
				has_pin_out = 1;
				continue;
			}

			if (strcmp(a, "-l") == 0) {
				if (i + 1 >= argc) {
					rt_kprintf("Error: -l requires a value\n");
					goto Usage;
				}
				pin_arg_level = argv[++i];
				char *end = NULL;

				level = strtol(pin_arg_level, &end, 10);
				if (*end != '\0' || level < 0 || level > 2) {
					rt_kprintf("Error: -l requires a value 0 or 1 or 2(Automatically switches between high and low levels)\n");
					goto Usage;
				}
				continue;
			}

			rt_kprintf("Error: unknown option: %s\n", a);
			goto Usage;
		}

		pin_out = test_pin_set(has_pin_out, pin_arg_out, 0);

		if (pin_out < 0)
			return -1;

		gpio_output_test(pin_out, level, mode);
		return 0;
	}

	if (strcmp(cmd, "input") == 0) {
		rt_base_t pin_out, pin_in;
		u8 has_pin_out = 0, has_pin_in = 0;
		const char *pin_arg_out = NULL, *pin_arg_in = NULL;

		for (int i = 2; i < argc; ++i) {
			const char *a = argv[i];

			if (strcmp(a, "-o") == 0) {
				if (i + 1 >= argc) {
					rt_kprintf("Error: -o requires a value\n");
					return -1;
				}
				pin_arg_out = argv[++i];
				has_pin_out = 1;
				continue;
			}

			if (strcmp(a, "-i") == 0) {
				if (i + 1 >= argc) {
					rt_kprintf("Error: -i requires a value\n");
					return -1;
				}
				pin_arg_in = argv[++i];
				has_pin_in = 1;
				continue;
			}

			rt_kprintf("Error: unknown option: %s\n", a);
			goto Usage;
		}

		pin_out = test_pin_set(has_pin_out, pin_arg_out, 0);
		pin_in = test_pin_set(has_pin_in, pin_arg_in, 1);

		if (pin_out < 0 || pin_in < 0)
			return -1;

		gpio_intput_test(pin_out, pin_in);
		return 0;
	}

	if (strcmp(cmd, "intr") == 0) {
		rt_base_t pin_out, pin_in;
		u8 has_pin_out = 0, has_pin_in = 0;
		const char *pin_arg_out = NULL, *pin_arg_in = NULL, *pin_arg_mode = NULL;
		u8 mode = 0, is_enable_test = 0;

		for (int i = 2; i < argc; ++i) {
			const char *a = argv[i];

			if (strcmp(a, "-m") == 0) {
				if (i + 1 >= argc) {
					rt_kprintf("Error: -m requires a value\n");
					return -1;
				}
				pin_arg_mode = argv[++i];
				char *end = NULL;

				mode = strtol(pin_arg_mode, &end, 10);
				if (*end != '\0' || mode < 0 || mode > 4) {
					rt_kprintf("Error: -m requires a value 0~4(0:rising edge, 1:falling edge, 2:both edges, 3:high level, 4:low level)\n");
					goto Usage;
				}
				continue;
			}

			if (strcmp(a, "-o") == 0) {
				if (i + 1 >= argc) {
					rt_kprintf("Error: -o requires a value\n");
					return -1;
				}
				pin_arg_out = argv[++i];
				has_pin_out = 1;
				continue;
			}

			if (strcmp(a, "-e") == 0) {
				is_enable_test = 1;
				continue;
			}

			if (strcmp(a, "-i") == 0) {
				if (i + 1 >= argc) {
					rt_kprintf("Error: -i requires a value\n");
					return -1;
				}
				pin_arg_in = argv[++i];
				has_pin_in = 1;
				continue;
			}

			rt_kprintf("Error: unknown option: %s\n", a);
			goto Usage;
		}

		pin_out = test_pin_set(has_pin_out, pin_arg_out, 0);
		pin_in = test_pin_set(has_pin_in, pin_arg_in, 1);

		if (pin_out < 0 || pin_in < 0)
			return -1;

		if (is_enable_test)
			gpio_interrupt_enable_test(pin_out, pin_in, mode);
		else
			gpio_interrupt_normal_test(pin_out, pin_in, mode);
		return 0;
	}

	if (strcmp(cmd, "help") == 0)
		goto Usage;

Usage:
    rt_kprintf("Usage:\n");
	rt_kprintf("**please make sure pinmux is correct first**\n");
	rt_kprintf("gpio_test all -- all gpio test\n");
	rt_kprintf("gpio_test output [-o output pin name] [-l 0~2]\n");
	rt_kprintf("	-l: level: 0: low(default), 1: high, 2:High–low level auto-switching test\n");
	rt_kprintf("	-- e.g. gpio_test output -o A28 -l 0\n");
    rt_kprintf("gpio_test input [-o output pin name] [-i input pin name]\n");
	rt_kprintf("	[pin name] is optional, default is output: A28 and input: A29\n");
	rt_kprintf("	-- e.g. gpio_test input -o A28 -i E29\n");
	rt_kprintf("gpio_test intr  [-o output pin name] [-i input pin name] [-m 0~4] [-e]\n");
	rt_kprintf("	-e: enable and disable test\n");
	rt_kprintf("	-m: mode: 0:rising edge, 1:falling edge, 2:both edges, 3:high level, 4:low level\n");
	rt_kprintf("	-- e.g. gpio_test intr -o A28 -i A29 -m 0\n");
	return 1;
}

MSH_CMD_EXPORT(gpio_test, GPIO driver test);
