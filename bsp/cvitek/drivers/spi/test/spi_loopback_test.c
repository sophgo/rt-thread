#include <rtdevice.h>
#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

#include "drv_pinmux.h"
#include "hal_debug.h"
#include "hal_def.h"
#include "hal_dma.h"
#include "hal_spi.h"

#define DATA_LEN 40960
#define MAXPRINT_LEN 4096

static uint8_t g_send_buffer[DATA_LEN] __attribute__((aligned(64)));
static uint8_t g_receive_buffer[DATA_LEN] __attribute__((aligned(64)));
static int send_finished = 1;
static int receive_finished = 1;

static void hex_dump(char *buf, int len, int addr) {
  int i, j, k, extra_len;
  char binstr[128];

  extra_len = 0;
  for (i = 0; i < len; i++) {
    if ((i % 16) == 0) {
      extra_len = 0;
      extra_len += sprintf(binstr + extra_len, "%08x -", i + addr);
      extra_len += sprintf(binstr + extra_len, " %02x", (unsigned char)buf[i]);
    } else if ((i % 16) == 15) {
      extra_len += sprintf(binstr + extra_len, " %02x", (unsigned char)buf[i]);
      extra_len += sprintf(binstr + extra_len, "  ");
      for (j = i - 15; j <= i; j++)
        extra_len += sprintf(binstr + extra_len, "%c",
                             ('!' < buf[j] && buf[j] <= '~') ? buf[j] : '.');
      printf("%s\n", binstr);
    } else {
      extra_len += sprintf(binstr + extra_len, " %02x", (unsigned char)buf[i]);
    }
  }

  if ((i % 16) != 0) {
    k = 16 - (i % 16);
    for (j = 0; j < k; j++)
      extra_len += sprintf(binstr + extra_len, "   ");
    extra_len += sprintf(binstr + extra_len, "  ");
    k = 16 - k;
    for (j = i - k; j < i; j++)
      extra_len += sprintf(binstr + extra_len, "%c",
                           ('!' < buf[j] && buf[j] <= '~') ? buf[j] : '.');
    printf("%s\n", binstr);
  }
}

#ifdef BSP_USING_SPI0
static const char *pinname_whitelist_spi0_sck[] = {
    "SD0_CLK",
    NULL,
};
static const char *pinname_whitelist_spi0_sdo[] = {
    "SD0_CMD",
    NULL,
};
static const char *pinname_whitelist_spi0_sdi[] = {
    "SD0_D0",
    NULL,
};
static const char *pinname_whitelist_spi0_cs[] = {
    "SD0_D3",
    NULL,
};
#endif

#ifdef BSP_USING_SPI1
static const char *pinname_whitelist_spi1_sck[] = {
    "PAD_MIPIRX4N",
    "MUX_SPI1_SCK",
    NULL,
};
static const char *pinname_whitelist_spi1_sdo[] = {
    "PAD_MIPIRX3P",
    "MUX_SPI1_MOSI",
    NULL,
};
static const char *pinname_whitelist_spi1_sdi[] = {
    "PAD_MIPIRX3N",
    "MUX_SPI1_MISO",
    NULL,
};
static const char *pinname_whitelist_spi1_cs[] = {
    "PAD_MIPIRX4P",
    "MUX_SPI1_CS",
    NULL,
};
#endif

#ifdef BSP_USING_SPI2
static const char *pinname_whitelist_spi2_sck[] = {
    "SD1_CLK",
    NULL,
};
static const char *pinname_whitelist_spi2_sdo[] = {
    "SD1_CMD",
    NULL,
};
static const char *pinname_whitelist_spi2_sdi[] = {
    "SD1_D0",
    NULL,
};
static const char *pinname_whitelist_spi2_cs[] = {
    "SD1_D3",
    NULL,
};
#endif

#ifdef BSP_USING_SPI3
static const char *pinname_whitelist_spi3_sck[] = {
    NULL,
};
static const char *pinname_whitelist_spi3_sdo[] = {
    NULL,
};
static const char *pinname_whitelist_spi3_sdi[] = {
    NULL,
};
static const char *pinname_whitelist_spi3_cs[] = {
    NULL,
};
#endif

static bool initialized = false;
static void cvi_spi_pinmux_config() {
  if (initialized)
    return;

  initialized = true;

#ifdef BSP_USING_SPI0
  pinmux_config(BSP_SPI0_SCK_PINNAME, SPI0_SCK, pinname_whitelist_spi0_sck);
  pinmux_config(BSP_SPI0_SDO_PINNAME, SPI0_SDO, pinname_whitelist_spi0_sdo);
  pinmux_config(BSP_SPI0_SDI_PINNAME, SPI0_SDI, pinname_whitelist_spi0_sdi);
  pinmux_config(BSP_SPI0_CS_PINNAME, SPI0_CS_X, pinname_whitelist_spi0_cs);
#endif /* BSP_USING_SPI0 */

#ifdef BSP_USING_SPI1
  pinmux_config("PAD_MIPIRX4N", MUX_SPI1_SCK, pinname_whitelist_spi1_sck);
  pinmux_config("PAD_MIPIRX3P", MUX_SPI1_MOSI, pinname_whitelist_spi1_sdo);
  pinmux_config("PAD_MIPIRX3N", MUX_SPI1_MISO, pinname_whitelist_spi1_sdi);
  pinmux_config("PAD_MIPIRX4P", MUX_SPI1_CS, pinname_whitelist_spi1_cs);
  pinmux_config("MUX_SPI1_SCK", SPI1_SCK, pinname_whitelist_spi1_sck);
  pinmux_config("MUX_SPI1_MOSI", SPI1_SDO, pinname_whitelist_spi1_sdo);
  pinmux_config("MUX_SPI1_MISO", SPI1_SDI, pinname_whitelist_spi1_sdi);
  pinmux_config("MUX_SPI1_CS", SPI1_CS_X, pinname_whitelist_spi1_cs);
#endif /* BSP_USING_SPI1 */

#ifdef BSP_USING_SPI2
  pinmux_config(BSP_SPI2_SCK_PINNAME, SPI2_SCK, pinname_whitelist_spi2_sck);
  pinmux_config(BSP_SPI2_SDO_PINNAME, SPI2_SDO, pinname_whitelist_spi2_sdo);
  pinmux_config(BSP_SPI2_SDI_PINNAME, SPI2_SDI, pinname_whitelist_spi2_sdi);
  pinmux_config(BSP_SPI2_CS_PINNAME, SPI2_CS_X, pinname_whitelist_spi2_cs);
#endif /* BSP_USING_SPI2 */

#ifdef BSP_USING_SPI3
  pinmux_config(BSP_SPI3_SCK_PINNAME, SPI3_SCK, pinname_whitelist_spi3_sck);
  pinmux_config(BSP_SPI3_SDO_PINNAME, SPI3_SDO, pinname_whitelist_spi3_sdo);
  pinmux_config(BSP_SPI3_SDI_PINNAME, SPI3_SDI, pinname_whitelist_spi3_sdi);
  pinmux_config(BSP_SPI3_CS_PINNAME, SPI3_CS_X, pinname_whitelist_spi3_cs);
#endif /* BSP_USING_SPI3 */
}

void spi_callback_func(hal_spi_t *spi, hal_spi_event_t event, void *arg) {
  if (event == SPI_EVENT_RECEIVE_COMPLETE) {
    printf("[CALLBACK] RX complete\n");
    receive_finished = 0;
  }

  if (event == SPI_EVENT_SEND_COMPLETE) {
    printf("[CALLBACK] TX complete\n");
    send_finished = 0;
  }

  if (event == SPI_EVENT_ERROR) {
    printf("[CALLBACK] Transfer error\n");
  }
}

int config_spi_transfer(int bus, hal_spi_t *spi_handler, hal_spi_cp_format_t format,
                        hal_spi_frame_len_t frame_len, uint32_t baud_hz) {
  int spi_ret = 0;

    hal_spi_init(spi_handler, bus);

  // test hal_spi_mode
  spi_ret = hal_spi_mode(spi_handler, SPI_MASTER);
  if (spi_ret != HAL_OK) {
    printf("set spi mode failed, actual ret %d\n", spi_ret);
    return spi_ret;
  }

  // test hal_spi_cp_format
  spi_ret = hal_spi_cp_format(spi_handler, format);
  if (spi_ret != HAL_OK) {
    printf("set spi format failed returned, actual ret %d\n", spi_ret);
    return spi_ret;
  }

  // test hal_spi_frame_len
  spi_ret = hal_spi_frame_len(spi_handler, frame_len);
  if (spi_ret != HAL_OK) {
    printf("set spi frame len failed, actual ret %d\n", spi_ret);
    return spi_ret;
  }

  // test hal_spi_baud
  hal_spi_baud(spi_handler, baud_hz);

  return spi_ret;
}

int32_t spi_send_receive_async(hal_spi_t *spi, const void *data_out,
                               void *data_in, uint32_t size, uint32_t timeout) {
  return hal_spi_send_receive_async(spi, data_out, data_in, size);
}

int32_t spi_send_receive_dma(hal_spi_t *spi, const void *data_out,
                             void *data_in, uint32_t size, uint32_t timeout) {
  return hal_spi_send_receive_dma(spi, data_out, data_in, size);
}

struct spi_test_function {
  char *desc;
  int32_t (*func)(hal_spi_t *spi, const void *data_out, void *data_in,
                  uint32_t size, uint32_t timeout);
};

const struct spi_test_function spi_test_case[6] = {
    {.desc = "poll mode", .func = hal_spi_send_receive},
    {.desc = "irq mode", .func = spi_send_receive_async},
    {.desc = "dma mode", .func = spi_send_receive_dma},
    {.desc = NULL, .func = NULL}};

int test_spi(int argc, char **argv) {
  uint8_t *send_buffer = g_send_buffer;
  uint8_t *receive_buffer = g_receive_buffer;
  char op[16] = {0};
  int spi_ret = 0;
  hal_spi_t spi_handler;
  int i;
  int bus = 1;

  for (int i = 1; i < argc; i++) {
      char *arg = argv[i];
      if (!strncmp(arg, "bus=", 4))
          bus = atoi(arg + 4);
  }
  if (bus < 0 || bus > 3) {
      printf("Invalid bus=%d (must be >= 0 and <= 3)\\n", bus);
      return -1;
  }

  const struct spi_test_function *t_case = spi_test_case;
  const uint32_t bauds[] = {100000, 1000000, 30000000, 46000000};
  const hal_spi_cp_format_t formats[] = {
      SPI_FORMAT_CPOL0_CPHA0,
      SPI_FORMAT_CPOL0_CPHA1,
      SPI_FORMAT_CPOL1_CPHA0,
      SPI_FORMAT_CPOL1_CPHA1,
  };
  const hal_spi_frame_len_t frames[] = {SPI_FRAME_LEN_8, SPI_FRAME_LEN_16};
  const int sizes[] = {1, 13, 128, 40960};

  rt_kprintf("\n--- Starting SPI Driver Test ---\n");
  cvi_spi_pinmux_config();
  for (i = 0; i < DATA_LEN; i++)
    send_buffer[i] = i;

  for (; t_case->desc; t_case++) {
    for (int fi = 0; fi < (int)(sizeof(formats) / sizeof(formats[0])); fi++) {
      for (int bi = 0; bi < (int)(sizeof(bauds) / sizeof(bauds[0])); bi++) {
        for (int fr = 0; fr < (int)(sizeof(frames) / sizeof(frames[0])); fr++) {
          for (int sz = 0; sz < (int)(sizeof(sizes) / sizeof(sizes[0])); sz++) {
            int cur_size = sizes[sz];
            printf("################ %s fmt=%d baud=%u frame=%d size=%d "
                   "################\n",
                   t_case->desc, formats[fi], bauds[bi], frames[fr], cur_size);

            memset(receive_buffer, 0x0, DATA_LEN);
            spi_ret = config_spi_transfer(bus, &spi_handler, formats[fi], frames[fr],
                                          bauds[bi]);
            if (spi_ret) {
              printf("[FAIL] set spi config failed, ret %d\n", spi_ret);
              goto un_init_loop;
            }

            if (!strcmp(t_case->desc, "irq mode") ||
                !strcmp(t_case->desc, "dma mode")) {
              spi_ret = hal_spi_attach_callback(&spi_handler, spi_callback_func,
                                                NULL);
              if (spi_ret) {
                printf("[FAIL] attach cb failed, ret %d\n", spi_ret);
                goto un_init_loop;
              }
            }
            int dma_ok = 0;
            /* padding scheme: 16-bit odd length padding 0x00 to even length */
            int effective_size = cur_size;
            if (frames[fr] == SPI_FRAME_LEN_16 && (effective_size & 1)) {
              printf(
                  "[INFO] 16-bit frame odd size=%d: padding one 0x00 to even\n",
                  cur_size);
              if (effective_size + 1 <= DATA_LEN) {
                send_buffer[effective_size] = 0x00;
                effective_size += 1;
              } else {
                /* extreme case: drop 1 byte */
                effective_size -= 1;
              }
            }

            if (!strcmp(t_case->desc, "dma mode") && effective_size > 16) {
              spi_ret = hal_spi_link_dma(&spi_handler, NULL, NULL);
              if (spi_ret) {
                printf("[WARN] link dma failed, fallback to poll. ret=%d\n",
                       spi_ret);
                dma_ok = 0;
              } else {
                dma_ok = 1;
              }
            }

            /* determine whether to wait for callback based on mode and length:
             * - dma mode and size>16：wait for TX/RX callback clear (with
             * timeout)
             * - others (dma and size<=16 → POLL；or poll/irq mode) follow the
             * original logic
             */
            if (!strcmp(t_case->desc, "dma mode")) {
              if (effective_size > 16 && dma_ok) {
                receive_finished = send_finished = 1;
                spi_ret = t_case->func(&spi_handler, send_buffer,
                                       receive_buffer, effective_size, 1000);
                if (spi_ret) {
                  printf("[FAIL] xfer failed, ret %d\n", spi_ret);
                  goto un_init_loop;
                }
                int timeout_ms = 5000, elapsed = 0;
                while ((receive_finished || send_finished) &&
                       elapsed < timeout_ms) {
                  rt_thread_mdelay(1);
                  elapsed++;
                }
                printf("receive_finished:%d, send_finished:%d\n",
                       receive_finished, send_finished);
                if (receive_finished || send_finished) {
                  hal_spi_uninit(&spi_handler);
                  printf("[WARN] dma xfer timeout\n");
                }
                receive_finished = send_finished = 1;
              } else {
                /* size<=16 or dma link failed: follow the POLL path, no
                 * callback wait */
                spi_ret = t_case->func(&spi_handler, send_buffer,
                                       receive_buffer, effective_size, 1000);
                if (spi_ret) {
                  printf("[FAIL] xfer failed, ret %d\n", spi_ret);
                  goto un_init_loop;
                }
              }
            } else {
              /* other mode follow the original logic */
              spi_ret = t_case->func(&spi_handler, send_buffer, receive_buffer,
                                     effective_size, 1000);
              if (spi_ret) {
                printf("[FAIL] xfer failed, ret %d\n", spi_ret);
                goto un_init_loop;
              }
              if (!strcmp(t_case->desc, "irq mode")) {
                int timeout_ms = 8000, elapsed = 0;
                while ((receive_finished || send_finished) &&
                       elapsed < timeout_ms) {
                  rt_thread_mdelay(1);
                  elapsed++;
                }
                printf("receive_finished:%d, send_finished:%d\n",
                       receive_finished, send_finished);
                if (receive_finished || send_finished) {
                  printf("[WARN] irq xfer timeout\n");
                }
                receive_finished = send_finished = 1;
              }
            }

            /* validation: loopback self-test by comparing the effective even
             * length (including padding) */
            int cmp_len =
                (effective_size > DATA_LEN) ? DATA_LEN : effective_size;
            strcpy(op, "success");
            for (i = 0; i < cmp_len; i++) {
              if (send_buffer[i] != receive_buffer[i]) {
                strcpy(op, "failed");
                if (cmp_len < MAXPRINT_LEN) {
                  printf("send_buffer:\n");
                  hex_dump((char *)send_buffer, cmp_len, 0);
                  printf("recv_buffer:\n");
                  hex_dump((char *)receive_buffer, cmp_len, 0);
                  break;
                }
                printf("[FAIL] data mismatch at 0x%x\n", i);
                break;
              }
            }
            printf("\n################ %s fmt=%d baud=%u frame=%d size=%d %s "
                   "################\n\n",
                   t_case->desc, formats[fi], bauds[bi], frames[fr], cur_size,
                   op);

          un_init_loop:
            hal_spi_uninit(&spi_handler);
          }
        }
      }
    }
  }
  rt_kprintf("\n--- END SPI Driver Test ---\n");

  return spi_ret;
}
MSH_CMD_EXPORT(test_spi, cvi spi loopback auto test);

/* ----------------------------- user configurable spi_test
 * ----------------------------- */

static int hex_to_val(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F')
    return 10 + (c - 'A');
  return -1;
}

static int parse_hex_string(const char *hex, uint8_t *out, int max_out) {
  int out_len = 0;
  int hi = -1;
  if (!hex || !out)
    return -1;
  /* skip optional 0x/0X */
  if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
    hex += 2;
  for (const char *p = hex; *p; p++) {
    if (*p == ' ' || *p == ',' || *p == ':')
      continue;
    int v = hex_to_val(*p);
    if (v < 0)
      return -1;
    if (hi < 0) {
      hi = v;
    } else {
      if (out_len >= max_out)
        return -1;
      out[out_len++] = (uint8_t)((hi << 4) | v);
      hi = -1;
    }
  }
  if (hi >= 0) {
    /* odd digits */
    return -1;
  }
  return out_len;
}

static void print_spi_test_usage(void) {
  printf("Usage: spi_test [baud] [len] [frame] [bus] [xfer]\\n");
  printf("       - baud: Hz, e.g. 1000000\\n");
  printf("       - len:  transfer bytes count (<= %d)\\n", DATA_LEN);
  printf("       - frame: 8 or 16\\n");
  printf("       - bus: SPI controller index (default 1)\\n");
  printf("       - xfer: polling|irq|dma (default polling)\\n");
  printf("Compat: spi_test baud=<Hz> len=<N> frame=<8|16> [bus=<idx>] "
         "[xfer=polling|irq|dma]\\n");
}

static int has_equal(const char *s) {
  if (!s)
    return 0;
  while (*s) {
    if (*s == '=')
      return 1;
    s++;
  }
  return 0;
}

int spi_test(int argc, char **argv) {
  /* defaults */
  uint32_t baud = 1000000;  /* Hz */
  int size = 16;            /* bytes */
  int frame_bits = 8;       /* 8 or 16 */
  int bus = 1;              /* SPI bus idx */
  const char *xfer = "dma"; /* polling|irq|dma */
  hal_spi_cp_format_t formatmode = SPI_FORMAT_CPOL0_CPHA0;
  /* detect positional vs kv */
  int positional = (argc >= 2 && !has_equal(argv[1]) && strcmp(argv[1], "-h") &&
                    strcmp(argv[1], "--help"));

  if (argc == 1 || (!positional && (argc == 2) &&
                    (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))) {
    print_spi_test_usage();
    return 0;
  }

  if (positional) {
    if (argc >= 2)
      baud = (uint32_t)strtoul(argv[1], NULL, 0);
    if (argc >= 3)
      size = atoi(argv[2]);
    if (argc >= 4)
      frame_bits = atoi(argv[3]);
    if (argc >= 5)
      bus = atoi(argv[4]);
    if (argc >= 6)
      xfer = argv[5];
    if (argc >= 7)
      formatmode = (hal_spi_cp_format_t)atoi(argv[6]);
  } else {
    for (int i = 1; i < argc; i++) {
      char *arg = argv[i];
      if (!strncmp(arg, "baud=", 5))
        baud = (uint32_t)strtoul(arg + 5, NULL, 0);
      else if (!strncmp(arg, "len=", 4) || !strncmp(arg, "size=", 5))
        size = atoi(arg + (arg[3] == '=' ? 4 : 5));
      else if (!strncmp(arg, "bus=", 4))
        bus = atoi(arg + 4);
      else if (!strncmp(arg, "xfer=", 5))
        xfer = arg + 5;
      else if (!strncmp(arg, "frame=", 6))
        frame_bits = atoi(arg + 6);
      else if (!strncmp(arg, "fmt=", 4))
        formatmode = (hal_spi_cp_format_t)atoi(arg + 4);
      else if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
        print_spi_test_usage();
        return 0;
      }
    }
  }
  printf("cfg: baud=%u size=%d frame=%d bus=%d xfer=%s fmt=%d\n", baud, size,
         frame_bits, bus, xfer, formatmode);
  if (size <= 0 || size > DATA_LEN) {
    if (size <= 0)
      size = 16;
    if (size > DATA_LEN)
      size = DATA_LEN;
  }
  if (baud <= 0) {
    printf("Invalid baud=%u (must be > 0)\\n", baud);
    return -1;
  }
  if (frame_bits != 8 && frame_bits != 16) {
    printf("Invalid frame=%d (must be 8 or 16)\\n", frame_bits);
    return -1;
  }
  if (bus < 0 || bus > 3) {
    printf("Invalid bus=%d (must be >= 0 and <= 3)\\n", bus);
    return -1;
  }
  if (strcmp(xfer, "poll") && strcmp(xfer, "irq") && strcmp(xfer, "dma")) {
    printf("Invalid xfer=%s (must be poll, irq or dma)\\n", xfer);
    return -1;
  }
  if (formatmode < 0 || formatmode > 3) {
    printf("Invalid format=%d (must be >= 0 and <= 3)\\n", formatmode);
    return -1;
  }
  /* prepare pattern 0..N-1 */
  uint8_t *send_buffer = g_send_buffer;
  uint8_t *recv_buffer = g_receive_buffer;

  for (int i = 0; i < size; i++)
    send_buffer[i] = (uint8_t)i;
  memset(recv_buffer, 0x00, size);

  int effective_size = size;
  if (frame_bits == 16 && (effective_size & 1)) {
    printf("[INFO] 16-bit frame odd size=%d: padding one 0x00 to even.\\n",
           effective_size);
    if (effective_size + 1 <= DATA_LEN) {
      send_buffer[effective_size] = 0x00;
      effective_size += 1;
    } else {
      effective_size -= 1;
    }
  }

  cvi_spi_pinmux_config();

  hal_spi_t spi;
  int ret;

  hal_spi_frame_len_t frame_enum = (hal_spi_frame_len_t)frame_bits;

  /* configure */
  hal_spi_init(&spi, (uint32_t)bus);

  ret = hal_spi_cp_format(&spi, formatmode);
  if (ret != HAL_OK) {
    printf("set spi format failed, ret=%d\\n", ret);
    hal_spi_uninit(&spi);
    return ret;
  }
  ret = hal_spi_frame_len(&spi, frame_enum);
  if (ret != HAL_OK) {
    printf("set frame len failed, ret=%d\\n", ret);
    hal_spi_uninit(&spi);
    return ret;
  }
  hal_spi_baud(&spi, baud);

  int is_irq =
      (!strcmp(xfer, "irq") || !strcmp(xfer, "INT") || !strcmp(xfer, "IRQ"));
  int is_dma = (!strcmp(xfer, "dma") || !strcmp(xfer, "DMA"));
  int is_poll = (!strcmp(xfer, "poll") || !strcmp(xfer, "POLL") ||
                 !strcmp(xfer, "polling") || !strcmp(xfer, "POLLING"));
  if (!is_irq && !is_dma)
    is_poll = 1; /* default polling */

  if (is_irq || is_dma) {
    ret = hal_spi_attach_callback(&spi, spi_callback_func, NULL);
    if (ret) {
      printf("attach cb failed, ret=%d\\n", ret);
      hal_spi_uninit(&spi);
      return ret;
    }
  }

  int dma_ok = 0;
  if (is_dma && effective_size > 16) {
    ret = hal_spi_link_dma(&spi, NULL, NULL);
    if (ret) {
      printf("[WARN] link dma failed, fallback to poll. ret=%d\\n", ret);
      dma_ok = 0;
    } else
      dma_ok = 1;
  }

  int timeout_ms = 8000;
  const char *result = "success";

  if (is_dma) {
    if (effective_size > 0) {
      if (effective_size > 16 && dma_ok) {
        receive_finished = send_finished = 1;
        ret = hal_spi_send_receive_dma(&spi, send_buffer, recv_buffer,
                                       effective_size);
        if (ret) {
          printf("[FAIL] xfer failed, ret=%d\\n", ret);
          hal_spi_uninit(&spi);
          return ret;
        }
        int elapsed = 0;
        while ((receive_finished) && elapsed < timeout_ms) {
          rt_thread_mdelay(1);
          elapsed++;
        }
        if (receive_finished) {
          printf("[WARN] dma xfer timeout, fallback to polling\\n");
          hal_spi_detach_callback(&spi);
          /* try a best-effort poll receive to unblock */
          ret = hal_spi_send_receive(&spi, send_buffer, recv_buffer,
                                     effective_size, 2000);
          if (ret) {
            printf("[FAIL] poll fallback failed, ret=%d\\n", ret);
          }
          result = "failed";
        }
        receive_finished = send_finished = 1;
      } else {
        ret = hal_spi_send_receive(&spi, send_buffer, recv_buffer,
                                   effective_size, timeout_ms);
        if (ret) {
          printf("[FAIL] xfer failed, ret=%d\\n", ret);
          hal_spi_uninit(&spi);
          return ret;
        }
      }
    }
  } else if (is_irq) {
    if (effective_size > 0) {
      receive_finished = send_finished = 1;
      ret = hal_spi_send_receive_async(&spi, send_buffer, recv_buffer,
                                       effective_size);
      if (ret) {
        printf("[FAIL] xfer failed, ret=%d\\n", ret);
        hal_spi_uninit(&spi);
        return ret;
      }
      int elapsed = 0;
      while ((receive_finished) && elapsed < timeout_ms) {
        rt_thread_mdelay(1);
        elapsed++;
      }
      if (receive_finished) {
        printf("[WARN] irq xfer timeout\\n");
        result = "failed";
      }
      receive_finished = send_finished = 1;
    }
  } else {
    if (effective_size > 0) {
      ret = hal_spi_send_receive(&spi, send_buffer, recv_buffer, effective_size,
                                 timeout_ms);
      if (ret) {
        printf("[FAIL] xfer failed, ret=%d\\n", ret);
        hal_spi_uninit(&spi);
        return ret;
      }
    }
  }

  /* verify: compare only original requested size, ignore padding */
  for (int i = 0; i < size; i++) {
    if (send_buffer[i] != recv_buffer[i]) {
      printf("[FAIL] data mismatch at 0x%x: tx=%02x rx=%02x\\n", i,
             send_buffer[i], recv_buffer[i]);
      result = "failed";
      if (size < MAXPRINT_LEN) {
        printf("send_buffer:\n");
        hex_dump((char *)send_buffer, size, 0);
        printf("recv_buffer:\n");
        hex_dump((char *)recv_buffer, size, 0);
        break;
      }
    }
  }

  printf("spi_test done: baud=%u frame=%d size=%d bus=%d xfer=%s => %s\n", baud,
         frame_bits, size, bus, is_dma ? "dma" : (is_irq ? "irq" : "polling"),
         result);

  hal_spi_uninit(&spi);
  return (result[0] == 's') ? 0 : -1;
}

MSH_CMD_EXPORT(spi_test, cvi spi user - configurable test);