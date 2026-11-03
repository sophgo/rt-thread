#include "usbd_uac.h"

#include <finsh.h>
#include <rtdevice.h>
#include <rtthread.h>
#include <stdio.h>

#include "cvi_comm_aio.h"
#include "cvi_type.h"
#include "cviaudio_algo_interface.h"
#include "media_audio.h"
#include "ringfifo.h"
#include "usbd_audio.h"
#include "usbd_comp.h"
#include "usbd_core.h"
#include "usbd_uac_descriptor.h"

#define CAPTURE_SIZE 4096

#define USBD_VID           0xffff
#define USBD_PID           0xffff
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

#define UAC_MIC_INDEX     0
#define UAC_SPEAKER_INDEX 1

#define UAC_TIMER_TICK (RT_TICK_PER_SECOND * 500 / 1000000)  // 500us → 0.5 tick

static volatile bool tx_flag               = 0;
static volatile bool rx_flag               = 0;
static volatile bool ep_tx_busy_flag       = false;
static volatile bool ep_rx_busy_flag       = false;
static int           uac_session_init_flag = CVI_FALSE;
static u32           uac_tx_cnt            = 0;
static u32           uac_rx_cnt            = 0;

static struct uac_device_info uac_info;
static uint8_t               *uac_descriptor = NULL;

static void usbd_audio_out_callback(uint8_t ep, uint32_t nbytes);
static void usbd_audio_in_callback(uint8_t ep, uint32_t nbytes);

#define UAC_RINGFIFO_SIZE (8 * 1024)
typedef struct uac_timer {
    rt_timer_t tmr;
    bool       StartFlag;
} uac_timer_t;

pthread_mutex_t    *f_lock[2];
struct ring_buffer *g_ring_buf[2];
void               *g_buffer[2];

static rt_event_t    _gslEvent[2];
static volatile bool g_event_flag[2];

uac_timer_t g_utimer;

#define UAC_HOST_READ_64BYTE (20)
#define UAC_HOST_OUT_BUF_LEN AUDIO_OUT_PACKET *UAC_HOST_READ_64BYTE  // default 64*20
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t out_buffer[UAC_HOST_OUT_BUF_LEN];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t in_buffer[AUDIO_IN_PACKET];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t write_pcm_buf[CAPTURE_SIZE];

void usbd_audio_open(uint8_t intf)
{
    USB_LOG_INFO("interface number:%u opened,uac_info.uac_intf1.intf_num:%u\r\n", intf,
                 uac_info.uac_intf1.intf_num);

    if (intf == uac_info.uac_intf1.intf_num) {
        // out
        rx_flag = 1;
        usbd_ep_start_read(uac_info.audio_out_ep.ep_addr, out_buffer, AUDIO_OUT_PACKET);
        ep_rx_busy_flag = false;
        uac_rx_cnt      = 0;
    } else {
        // in
        tx_flag         = 1;
        ep_tx_busy_flag = false;
        uac_tx_cnt      = 0;
    }

    if (!g_utimer.StartFlag) {
        if (rt_timer_start(g_utimer.tmr)) {
            USB_LOG_ERR("rt timer starting failed!\n");
        }
        g_utimer.StartFlag = true;
        USB_LOG_INFO("start uac usb write/read timer\r\n");
    }
}

void usbd_audio_close(uint8_t intf)
{
    USB_LOG_INFO("interface number:%u closed\r\n", intf);

    if (intf == uac_info.uac_intf1.intf_num) {
        rx_flag         = 0;
        ep_rx_busy_flag = false;
    } else {
        tx_flag         = 0;
        ep_tx_busy_flag = false;
    }

    if (g_utimer.StartFlag && !rx_flag && !tx_flag) {
        g_utimer.StartFlag = false;
        rt_timer_stop(g_utimer.tmr);
        USB_LOG_INFO("stop uac usb write/read timer\r\n");
    }
}

static volatile uint32_t total_len = 0;
static volatile uint32_t offset    = 0;

static rt_sem_t g_audio_write_sem;

static void usbd_audio_out_callback(uint8_t ep, uint32_t nbytes)
{
    // USB_LOG_DBG("actual out len:%u\r\n", nbytes);

    if (nbytes == 0) {
        goto EXIT;
    }

    ring_buffer_put(g_ring_buf[UAC_SPEAKER_INDEX], (void *)out_buffer, nbytes);
    if (ring_buffer_len(g_ring_buf[UAC_SPEAKER_INDEX]) >= 1024) {
        rt_sem_release(g_audio_write_sem);
    }

    uac_rx_cnt++;

EXIT:
    ep_rx_busy_flag = true;
    usbd_ep_start_read(uac_info.audio_out_ep.ep_addr, out_buffer, AUDIO_OUT_PACKET);
}

static void usbd_audio_in_callback(uint8_t ep, uint32_t nbytes)
{
    // USB_LOG_DBG("actual in len:%d\r\n", nbytes);
    ep_tx_busy_flag = false;
}

static int uac_ringfifo_init(void)
{
    for (int i = 0; i < 2; i++) {
        f_lock[i] = (pthread_mutex_t *)rt_malloc(sizeof(pthread_mutex_t));
        if (pthread_mutex_init(f_lock[i], NULL) != 0) {
            USB_LOG_ERR("Failed init mutex\r\n");
            return -1;
        }
        g_buffer[i] = (void *)rt_malloc(UAC_RINGFIFO_SIZE);
        if (!g_buffer[i]) {
            USB_LOG_ERR("Failed to malloc memory.\r\n");
            return -1;
        }

        g_ring_buf[i] = ring_buffer_init(g_buffer[i], UAC_RINGFIFO_SIZE, f_lock[i]);
        if (!g_ring_buf[i]) {
            USB_LOG_ERR("Failed to init ring buffer\r\n");
            return -1;
        }
    }
    USB_LOG_INFO("uac_ringfifo_init init success\r\n");
    return 0;
}

static int uac_ringfifo_deinit(void)
{
    for (int i = 0; i < 2; i++) {
        ring_buffer_free(g_ring_buf[i]);
    }
    return 0;
}

/***************************************************************/
uint64_t ep2_out_cnt;
uint64_t ep3_in_cnt;
uint64_t ep_all_cnt;
uint64_t ep_out_cnt;

extern void usbd_dump_reg_info();

static void uac_timer_send_data_cb(void *timer, void *arg)
{
    uint32_t      ret     = 0;
    unsigned char mic_idx = UAC_MIC_INDEX;

    if (tx_flag && !ep_tx_busy_flag && ring_buffer_len(g_ring_buf[mic_idx]) >= AUDIO_IN_PACKET) {
        ret = ring_buffer_get(g_ring_buf[mic_idx], (void *)in_buffer, AUDIO_IN_PACKET);
        if (ret > 0) {
            ep_tx_busy_flag = true;
            usbd_ep_start_write(uac_info.audio_in_ep.ep_addr, in_buffer, ret);
            uac_tx_cnt++;
        }
    }

    if (g_utimer.StartFlag) {
        rt_timer_start(g_utimer.tmr);
    }
}

#define AUDIO_AEC_LENGTH 160

static void audio_write(void *arg)
{
    int play_len = MEDIA_AUDIO_GetPcmLen(MEDIA_PCM_TYPE_SPEAKER);

    while (1) {
        rt_sem_take(g_audio_write_sem, RT_WAITING_FOREVER);

        if (ring_buffer_len(g_ring_buf[UAC_SPEAKER_INDEX]) >= play_len) {
            int ret =
                ring_buffer_get(g_ring_buf[UAC_SPEAKER_INDEX], (void *)write_pcm_buf, play_len);
            if (ret > 0) {
                MEDIA_AUDIO_PcmWrite(write_pcm_buf, play_len);
                ep_rx_busy_flag = false;
            }
        }
    }
}

static void audio_read(void *arg)
{
    int           ret      = 0;
    int           data_len = 0;
    unsigned char index    = UAC_MIC_INDEX;

    unsigned char *buf = rt_malloc(CAPTURE_SIZE);
    if (buf == NULL) {
        USB_LOG_ERR("rt_malloc buf fail\n");
        return;
    }

    // USB_LOG_INFO("start audio_read thread\r\n");
    while (uac_session_init_flag) {
        if (tx_flag) {
            ret = MEDIA_AUDIO_PcmRead(buf);
            USB_LOG_INFO("audio read len:%d\r\n", ret);
            if (ret > 0) {
                data_len = ret;
                if (MEDIA_AUDIO_AlgoIsEnable()) {
                    ring_buffer_put(g_ring_buf[index], (void *)buf, data_len);
                }
            }

        } else {
            msleep(1);
        }
        msleep(1);
    }

    rt_free(buf);
}

static void uac_event_sem_init(void)
{
    for (int i = 0; i < 2; i++) {
        char name[8];
        rt_snprintf(name, sizeof(name), "evt%d", i);
        _gslEvent[i] = rt_event_create(name, RT_IPC_FLAG_PRIO);
    }
}

static void uac_event_sem_deinit(void)
{
    for (int i = 0; i < 2; i++) {
        rt_event_delete(_gslEvent[i]);
    }
}

static void uac_timer_init(void)
{
    g_utimer.tmr =
        rt_timer_create("uac_timer", uac_timer_send_data_cb, RT_NULL, RT_TICK_PER_SECOND / 1000,
                        RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_HARD_TIMER);
}

static void uac_timer_deinit(void)
{
    rt_timer_stop(g_utimer.tmr);
    rt_timer_delete(g_utimer.tmr);
}

static void uac_desc_register_cb()
{
    uac_destroy_descriptor(uac_descriptor);
}

int uac_desc_register(void)
{
    uint32_t desc_len;

    uac_info.audio_out_ep.ep_cb   = usbd_audio_out_callback;
    uac_info.audio_out_ep.ep_addr = comp_get_available_ep(0);
    uac_info.audio_in_ep.ep_cb    = usbd_audio_in_callback;
    uac_info.audio_in_ep.ep_addr  = comp_get_available_ep(1);
    uac_info.interface_nums       = comp_get_interfaces_num();
    USB_LOG_INFO("uac out ep:%#x\n", uac_info.audio_out_ep.ep_addr);
    USB_LOG_INFO("uac int ep:%#x\n", uac_info.audio_in_ep.ep_addr);
    USB_LOG_INFO("uac interface_nums:%d\n", uac_info.interface_nums);

    uac_descriptor = uac_build_descriptor(&uac_info, &desc_len);
    comp_register_descriptors(USBD_TYPE_UAC, uac_descriptor, desc_len, 3, uac_desc_register_cb);

    usbd_add_interface(usbd_audio_init_intf(&uac_info.uac_intf0));
    usbd_add_interface(usbd_audio_init_intf(&uac_info.uac_intf1));
    usbd_add_interface(usbd_audio_init_intf(&uac_info.uac_intf2));
    usbd_add_endpoint(&uac_info.audio_in_ep);
    usbd_add_endpoint(&uac_info.audio_out_ep);
    usbd_audio_add_entity(0x02, AUDIO_CONTROL_FEATURE_UNIT);
    usbd_audio_add_entity(0x05, AUDIO_CONTROL_FEATURE_UNIT);

    USB_LOG_INFO("intf_num %d,%d\n", uac_info.uac_intf1.intf_num, uac_info.uac_intf2.intf_num);
    return 0;
}

int uac_init(void)
{
    rt_thread_t read_thread, write_thread;

    uac_event_sem_init();
    // media_audio_init();
    uac_ringfifo_init();

    uac_timer_init();
    uac_session_init_flag = CVI_TRUE;

    read_thread = rt_thread_create("audio_read", audio_read, RT_NULL, 6 * 1024, 30, 10);

    if (read_thread == RT_NULL) {
        rt_kprintf("create audio_read thread fail\n");
        return -1;
    }
    rt_thread_startup(read_thread);

    g_audio_write_sem = rt_sem_create("uac", 0, RT_IPC_FLAG_PRIO);
    if (g_audio_write_sem == RT_NULL) {
        rt_kprintf("create audio_write sem fail\n");
        return -1;
    }

    write_thread = rt_thread_create("audio_write", audio_write, RT_NULL, 6 * 1024, 30, 10);

    if (write_thread == RT_NULL) {
        rt_kprintf("create audio_write thread fail\n");
        return -1;
    }
    rt_thread_startup(write_thread);
    return 0;
}

int uac_deinit(void)
{
    // media_audio_deinit();
    uac_ringfifo_deinit();
    uac_event_sem_deinit();
    uac_timer_deinit();
    return 0;
}

#define UAC_DBG_BUF_SIZE 1 * 1024
void _uac_dbg_proc_show(int32_t argc, char **argv)
{
    int     pos         = 0;
    char   *buf         = NULL;
    uint8_t ep_interval = HS_EP_INTERVAL;
    buf                 = calloc(1, UAC_DBG_BUF_SIZE);
    if (!buf) {
        printf("fail to malloc\n");
        return;
    }

    if (usbd_comp_get_speed() != USB_SPEED_HIGH) {
        ep_interval = FS_EP_INTERVAL;
    }

    pos += sprintf(buf + pos, "[UAC Debug Info]\n");
    pos += sprintf(buf + pos, "AudioFreq\t\t:%d\t\tAudioChnNum\t\t:%d\n", AUDIO_FREQ,
                   AUDIO_CHANNEL_NUM);
    pos += sprintf(buf + pos, "AudioAlgoStatus\t\t:%d\t\tEpInterval\t\t:%d\n",
                   MEDIA_AUDIO_AlgoIsEnable(), ep_interval);
    pos += sprintf(buf + pos, "[FLAG Info]\n");
    pos += sprintf(buf + pos, "TxFlag\t\t\t:%d\t\tRxFlag\t\t:%d\n", tx_flag, rx_flag);
    pos += sprintf(buf + pos, "[EP Flag Info]\n");
    pos += sprintf(buf + pos, "EpTxBusyFlag\t\t:%d\t\tEpRxBusyFlag\t:%d\n", ep_tx_busy_flag,
                   ep_rx_busy_flag);
    pos += sprintf(buf + pos, "[UAC Cnt Info]\n");
    pos += sprintf(buf + pos, "UacTxCnt\t\t:%d\t\tUacRxCnt\t:%d\n", uac_tx_cnt, uac_rx_cnt);

    printf(buf);
    free(buf);
}

MSH_CMD_EXPORT_ALIAS(_uac_dbg_proc_show, proc_uac, dump uac debug info);