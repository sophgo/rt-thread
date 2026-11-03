#include <rtdevice.h>
#include <rtthread.h>
#include <string.h>

#include "drv_pinmux.h"
#include "hal_debug.h"
#include "hal_def.h"
#include "hal_dma.h"
#include "hal_spi.h"

static int check_dst_memory(void *src, void *dst, unsigned int len)
{
    return rt_memcmp(src, dst, len);
}

static int dma_mem_to_mem(void *src, void *dst, uint32_t len)
{
	hal_dma_ch_config_t chan_config;
	uint8_t             dma_data_width;
	hal_dma_ch_t * dma_chan;
	int ret = HAL_OK;
	rt_tick_t ticks_start = 0, ticks_end = 0;
	dma_chan = (hal_dma_ch_t *)rt_malloc(sizeof(hal_dma_ch_t));\
	rt_memset(src, 0xc9, len);
	ret = hal_dma_channel_request(dma_chan, 5, 0);
	
	if (ret != HAL_OK) {
		printf("dma allocate tx channel failed!\n");
		return ret;
	}

	dma_data_width = DMA_DATA_WIDTH_16_BITS;

	if (dma_chan) {
		memset(&chan_config, 0, sizeof(hal_dma_ch_config_t));
		/* configure tx dma channel */
		chan_config.src_tw    = DMA_DATA_WIDTH_32_BITS;
		chan_config.dst_tw    = dma_data_width;
		chan_config.src_inc   = DMA_ADDR_INC;
		chan_config.dst_inc   = DMA_ADDR_INC;
		chan_config.group_len = 8;
		chan_config.trans_dir = DMA_MEM2MEM;
		chan_config.handshake = 5; /* dma channel 0 */
		hal_dma_ch_config(dma_chan, &chan_config);
		// dw_writel(dws, CVI_DW_SPI_DMATDLR, 8);
		// spi_enable_dma(dws, 1, 1);
		rt_hw_cpu_dcache_ops(RT_HW_CACHE_FLUSH | RT_HW_CACHE_INVALIDATE,
							 (void *)src, len);
	}

	/* rx must be started before tx due to spi instinct */
    ticks_start = rt_tick_get();
	if (dst) {
		hal_dma_ch_start(dma_chan, (void *)src,
						 (void *)dst, len);
	}
	hal_dma_ch_free(dma_chan);
	ticks_end = rt_tick_get();
	rt_kprintf("start %d, end %d, System ticks: %d ms\n", ticks_start, ticks_end, ticks_end - ticks_start);

	if (!check_dst_memory(src, dst, len)) {
		printf("dma mem to mem success!\n");
	}
	rt_free(dma_chan);
	return 0;
}

int dma_test(int argc, char **argv)
{
    if (argc < 4) {
        return -1;
    }

    void  *src_addr = (void *)strtoul(argv[1], NULL, 16);
    void  *dst_addr = (void *)strtoul(argv[2], NULL, 16);
    size_t size     = (size_t)strtoul(argv[3], NULL, 16);

    int dma_ret = 0;
    dma_ret     = dma_mem_to_mem(src_addr, dst_addr, size);
    return dma_ret;
}
MSH_CMD_EXPORT(dma_test, cvi dma test);
