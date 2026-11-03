#include "drv_dma.h"

#include <rthw.h>
#include <stdlib.h>

#include "cache.h"
#include "hal_debug.h"
#include "hal_dma.h"

int cvi_set_ch_hw_param(dw_dma_channel_t *dwc, uint8_t master, uint8_t hs_polarity)
{
    HAL_PARAM_CHK(dwc, HAL_ERROR);

    dw_dma_t *dw_dma = dma_array[dwc->ctrl_id];

    if (master >= dw_dma->nr_masters || hs_polarity > 1) {
        dma_err("master or hs_polarity error\r\n");
        return HAL_UNSUPPORTED;
    }

    dwc->cfg.master      = master;
    dwc->cfg.hs_polarity = hs_polarity;

    return 0;
}

int set_dw_config(struct dw_dma_cfg *dw_cfg, hal_dma_ch_config_t *config)
{
    if (config->src_reload_en || config->dst_reload_en) {
        dma_err("src/dst reload_en not supported\r\n");
        return HAL_UNSUPPORTED;
    }

    if (config->half_int_en) {
        dma_err("half_int_en not supported\r\n");
        return HAL_UNSUPPORTED;
    }

    if (config->src_inc == DMA_ADDR_DEC || config->dst_inc == DMA_ADDR_DEC) {
        dma_err("DMA_ADDR_DEC not supported\r\n");
        return HAL_UNSUPPORTED;
    }

    dw_cfg->dst_inc   = (dma_addr_inc_t)config->dst_inc;
    dw_cfg->dst_tw    = (dma_data_width_t)config->dst_tw;
    dw_cfg->group_len = config->group_len;
    dw_cfg->handshake = config->handshake;
    dw_cfg->src_inc   = (dma_addr_inc_t)config->src_inc;
    dw_cfg->src_tw    = (dma_data_width_t)config->src_tw;
    dw_cfg->trans_dir = (dma_trans_dir_t)config->trans_dir;

    return HAL_OK;
}

int cvi_dma_ch_config(int ctrl_idx, int ch_idx, struct dw_dma_cfg *dw_cfg)
{
    return dw_dma_set_ch_config(ctrl_idx, ch_idx, dw_cfg);
}

/*--------------------------HAL--------------------------------*/
/**
 * @brief Initialize the DMA controller.
 *
 * @note
 * This function initializes the DMA controller for the specified control ID.
 * Currently, only DMA_0 is supported. It sets the base register and IRQ number,
 * allocates memory for the DMA private structure, and initializes the DMA
 * controller.
 *
 * @param dma Pointer to the HAL DMA structure.
 * @param ctrl_id Control ID of the DMA to initialize.
 * @return HAL_OK on success, HAL_UNSUPPORTED for unsupported control IDs.
 */
int hal_dma_init(hal_dma_t *dma, uint32_t ctrl_id)
{
    HAL_PARAM_CHK(dma, HAL_ERROR);
    int       ret = HAL_OK;
    dw_dma_t *dw_dma;

    // Check if the control ID is unsupported
    if (ctrl_id != 0) {
        dma_err("Cvitek DMA Only Support DMA_0");
        return HAL_UNSUPPORTED;
    }

    // Set base register and IRQ number for DMA
    dma->reg_base = DW_DMA_BASE;
    dma->irq_num  = DW_DMA_IRQn;
    dma_dbg("DW_DMA: Irq_num=%d, Base_Reg=0x%x\n", dma->irq_num, dma->reg_base);

    // Allocate and initialize the DMA private structure
    dw_dma = rt_malloc(sizeof(dw_dma_t));
    HAL_PARAM_CHK(dw_dma, HAL_ERROR);
    memset(dw_dma, 0, sizeof(dw_dma_t));

    dma->priv   = dw_dma;
    dma->ch_num = DW_DMA_MAX_NR_CHANNELS;

    // Initialize the DMA controller
    dw_dma_controller_init(dw_dma, ctrl_id, dma->reg_base, dma->irq_num);

    return ret;
}

/**
 * @brief Deinitialize the DMA controller.
 *
 * @note
 * This function deinitializes the DMA controller for the specified control ID.
 * Currently, only DMA_0 is supported. It releases resources and cleans up
 * the DMA private structure associated with the given control ID.
 *
 * @param dma Pointer to the HAL DMA structure to be deinitialized.
 * @param ctrl_id Control ID of the DMA to deinitialize.
 */
void hal_dma_deinit(hal_dma_t *dma, uint32_t ctrl_id)
{
    HAL_PARAM_CHK_NORETVAL(dma);

    if (ctrl_id != 0) {
        dma_err("Cvitek DMA Only Support DMA_0\n");
        return;
    }

    dw_dma_t *dw_dma = dma->priv;

    // Uninitialize the DMA controller
    dw_dma_controller_uninit(dw_dma);

    // Release resources
    rt_free(dw_dma);
}

/**
 * @brief Request a DMA channel from the DMA controller.
 *
 * @note
 * This function requests a DMA channel from the DMA controller for the
 * specified control ID. The channel ID is used to identify the requested
 * channel. If the request is successful, the channel ID and control ID are
 * stored in the HAL DMA channel structure. If the request fails, an error code
 * is returned.
 *
 * @param hal_dma_ch Pointer to the HAL DMA channel structure.
 * @param ch_id Channel ID of the DMA channel to request.
 * @param ctrl_id Control ID of the DMA controller.
 * @return HAL_OK on success, HAL_ERROR on failure.
 */
int hal_dma_channel_request(hal_dma_ch_t *hal_dma_ch, uint32_t ch_id, uint32_t ctrl_id)
{
    HAL_PARAM_CHK(hal_dma_ch, HAL_ERROR);

    int ret = HAL_ERROR;

    // Request the DMA channel
    ret = dw_dma_lock_ch(ctrl_id, ch_id);
    if (ret) {
        // If the request fails, print an error message
        dma_err("DMA request channel fail\n");
        return ret;
    }

    // Store the channel ID and control ID in the HAL DMA channel structure
    hal_dma_ch->ch_id     = ch_id;
    hal_dma_ch->ctrl_id   = ctrl_id;
    hal_dma_ch->etb_ch_id = -1;

    return ret;
}

/**
 * @brief Stop the DMA channel.
 *
 * @note
 * This function stops the DMA channel specified by the HAL DMA channel
 * structure.
 *
 * @param hal_dma_ch Pointer to the HAL DMA channel structure.
 */
void hal_dma_ch_stop(hal_dma_ch_t *hal_dma_ch)
{
    HAL_PARAM_CHK_NORETVAL(hal_dma_ch);

    dw_dma_ch_stop(hal_dma_ch->ctrl_id, hal_dma_ch->ch_id);
}

/**
 * @brief Release the DMA channel.
 *
 * @note
 * This function releases the DMA channel specified by the HAL DMA channel
 * structure. The channel ID and control ID are used to identify the channel
 * to be released.
 *
 * @param hal_dma_ch Pointer to the HAL DMA channel structure.
 */
void hal_dma_ch_free(hal_dma_ch_t *hal_dma_ch)
{
    HAL_PARAM_CHK_NORETVAL(hal_dma_ch);

    dw_dma_ch_free_ch(hal_dma_ch->ctrl_id, hal_dma_ch->ch_id);
}

/**
 * @brief Pause the DMA channel.
 *
 * @note
 * This function pauses the DMA channel specified by the HAL DMA channel
 * structure.
 *
 * @param hal_dma_ch Pointer to the HAL DMA channel structure.
 */
void hal_dma_ch_pause(hal_dma_ch_t *hal_dma_ch)
{
    HAL_PARAM_CHK_NORETVAL(hal_dma_ch);

    dw_dma_ch_pause_ch(hal_dma_ch->ctrl_id, hal_dma_ch->ch_id);
}

/**
 * @brief Resume the DMA channel.
 *
 * @note
 * This function resumes the DMA channel specified by the HAL DMA channel
 * structure. The channel ID and control ID are used to identify the channel to
 * be resumed.
 *
 * @param hal_dma_ch Pointer to the HAL DMA channel structure.
 */
void hal_dma_ch_resume(hal_dma_ch_t *hal_dma_ch)
{
    HAL_PARAM_CHK_NORETVAL(hal_dma_ch);

    dw_dma_ch_resume_ch(hal_dma_ch->ctrl_id, hal_dma_ch->ch_id);
}

/**
 * @brief Default callback function for DMA channel.
 *
 * @note
 * This function is called after a DMA transfer is completed. It will call the
 * callback function registered by the user with the DMA_EVENT_TRANSFER_DONE
 * event and the user-provided argument.
 *
 * @param ch_id Channel ID of the DMA channel.
 * @param args  The user-provided argument.
 */
static void cvi_dma_ch_callback_default(uint32_t ch_id, void *args)
{
    hal_dma_ch_t *hal_dma_ch = args;

    if (hal_dma_ch->callback) {
        hal_dma_ch->callback(hal_dma_ch, DMA_EVENT_TRANSFER_DONE, hal_dma_ch->arg);
    }
}

/**
 * @brief Attach a callback function to a DMA channel.
 *
 * @note
 * This function attaches a callback function to a DMA channel. The callback
 * function is called after a DMA transfer is completed. The callback function
 * receives two arguments: the HAL DMA channel structure and a user-provided
 * argument.
 *
 * @param hal_dma_ch Pointer to the HAL DMA channel structure.
 * @param callback    The callback function to attach.
 * @param arg         The user-provided argument to pass to the callback
 * function.
 *
 * @return HAL_OK on success, HAL_ERROR on failure.
 */
int hal_dma_ch_attach_callback(hal_dma_ch_t *hal_dma_ch, void *callback, void *arg)
{
    int ret = HAL_OK;
    HAL_PARAM_CHK(hal_dma_ch, HAL_ERROR);

    // Store the callback function and user-provided argument in the HAL DMA
    // channel structure.
    hal_dma_ch->callback = callback;
    hal_dma_ch->arg      = arg;

    // Attach the callback function to the DMA channel using the default
    // callback function.
    dw_dma_ch_attach_cb(hal_dma_ch->ctrl_id, hal_dma_ch->ch_id, cvi_dma_ch_callback_default,
                        hal_dma_ch);

    return ret;
}

/**
 * @brief Detach a callback function from a DMA channel.
 *
 * @note
 * This function detaches the callback function from a DMA channel. The callback
 * function is called after a DMA transfer is completed. The callback function
 * receives two arguments: the HAL DMA channel structure and a user-provided
 * argument.
 *
 * @param hal_dma_ch Pointer to the HAL DMA channel structure.
 */
void hal_dma_ch_detach_callback(hal_dma_ch_t *hal_dma_ch)
{
    HAL_PARAM_CHK_NORETVAL(hal_dma_ch);
    hal_dma_ch->callback = NULL;
    hal_dma_ch->arg      = NULL;

    dw_dma_ch_detach_cb(hal_dma_ch->ctrl_id, hal_dma_ch->ch_id);
}

/**
 * @brief Configure a DMA channel.
 *
 * @note
 * This function configures a DMA channel using the specified configuration
 * parameters. It validates the input parameters, converts the HAL DMA channel
 * configuration to a DW DMA configuration, and applies the configuration to the
 * specified channel.
 *
 * @param hal_dma_ch Pointer to the HAL DMA channel structure.
 * @param config Pointer to the configuration structure containing the desired
 * settings for the DMA channel.
 * @return HAL_OK on success, HAL_ERROR if any parameter is invalid or if the
 * configuration fails.
 */
int hal_dma_ch_config(hal_dma_ch_t *hal_dma_ch, hal_dma_ch_config_t *config)
{
    // Validate the input parameters
    HAL_PARAM_CHK(hal_dma_ch, HAL_ERROR);
    HAL_PARAM_CHK(config, HAL_ERROR);

    int               ret = HAL_OK;
    struct dw_dma_cfg dw_cfg;

    // Initialize the DW DMA configuration structure to zero
    memset(&dw_cfg, 0, sizeof(dw_cfg));

    // Set the DW DMA configuration based on the provided HAL DMA configuration
    ret = set_dw_config(&dw_cfg, config);
    if (ret) {
        // Return if setting configuration fails
        return ret;
    }

    // Apply the configuration to the DMA channel
    cvi_dma_ch_config(hal_dma_ch->ctrl_id, hal_dma_ch->ch_id, &dw_cfg);

    return ret;
}

/**
 * @brief Start a DMA channel.
 *
 * @note
 * This function starts a DMA channel using the configuration and parameters
 * set by the hal_dma_ch_config() function. It begins the DMA transfer based on
 * the provided source and destination addresses and length.
 *
 * @param hal_dma_ch Pointer to the HAL DMA channel structure.
 * @param srcaddr     Source address of the DMA transfer.
 * @param dstaddr     Destination address of the DMA transfer.
 * @param length      Length of the DMA transfer in bytes.
 */
void hal_dma_ch_start(hal_dma_ch_t *hal_dma_ch, void *srcaddr, void *dstaddr, uint32_t length)
{
    HAL_PARAM_CHK_NORETVAL(hal_dma_ch);

    // Print debug information about the DMA transfer
    dma_dbg(
        "hal_dma_ch_start: ctrl_id=%d, ch_id=%d, srcaddr=%p, dstaddr=%p, "
        "length=%u\r\n",
        hal_dma_ch->ctrl_id, hal_dma_ch->ch_id, srcaddr, dstaddr, length);

    // Start the DMA channel
    dw_dma_ch_start_xfer(hal_dma_ch->ctrl_id, hal_dma_ch->ch_id, srcaddr, dstaddr, length);
}

int target_get_optimal_dma_channel(void *dma_list, uint32_t ctrl_num, void *ch_info)
{
    uint32_t          spt_id, ch_info_id, ctrl_id, ch_id;
    int32_t           is_find     = 0;
    int               ret         = HAL_OK;
    dw_dma_t        **list        = (dw_dma_t **)dma_list;
    dw_dma_ch_desc_t *dma_ch_info = (dw_dma_ch_desc_t *)ch_info;

    for (ctrl_id = 0U; ctrl_id < ctrl_num; ctrl_id++) {
        for (ch_id = 0U; ch_id < list[ctrl_id]->nr_channels; ch_id++) {
            if (!(list[ctrl_id]->alloc_status & ((uint32_t)1 << ch_id))) {
                dma_ch_info->ch_idx   = (uint8_t)ch_id;
                dma_ch_info->ctrl_idx = (uint8_t)ctrl_id;
                /* find the channel */
                is_find = 1;
                break;
            }
        }

        if (is_find) {
            break;
        }
    }

    if (is_find == 0) {
        ret = HAL_ERROR;
    }
    return ret;
}

int hal_dma_ch_alloc(hal_dma_ch_t *dma_ch, int8_t ch_id, int8_t ctrl_id)
{
    int ret = HAL_OK;

    dw_dma_ch_desc_t ch_info = {0};
    // uint32_t result = csi_irq_save();

    if ((ctrl_id == -1) && (ch_id == -1)) {
        ret = target_get_optimal_dma_channel(dma_array, 1, &ch_info);

        if (ret == HAL_OK) {
            // dma_array[ch_info.ctrl_idx]->alloc_status |=
            //         (uint32_t)((uint32_t)1U << (uint32_t)ch_info.ch_idx);

            dma_ch->ch_id   = ch_info.ch_idx;
            dma_ch->ctrl_id = ch_info.ctrl_idx;
        }
    } else if ((ctrl_id >= 0) && (ch_id >= 0)) {
        if (dma_array[ctrl_id]->alloc_status & (uint32_t)((uint32_t)1U << (uint32_t)ch_id)) {
            ret = HAL_ERROR;
        } else {
            // dma_array[ctrl_id]->alloc_status |=
            //         (uint32_t)((uint32_t)1U << (uint32_t)ch_id);
            dma_ch->ch_id   = ch_id;
            dma_ch->ctrl_id = ctrl_id;
        }
    } else {
        ret = HAL_ERROR;
    }

    if (ret == HAL_ERROR) {
        // csi_irq_restore(result);

    } else {
        hal_dma_channel_request(dma_ch, dma_ch->ch_id, dma_ch->ctrl_id);
        // csi_irq_restore(result);
    }
    return ret;
}
