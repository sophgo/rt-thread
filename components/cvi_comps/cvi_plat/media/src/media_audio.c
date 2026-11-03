#include "media_audio.h"
#include <rtthread.h>
#include <stddef.h>
#include <stdio.h>
#include "cvi_audio.h"
#include "cvi_comm_aio.h"
#include "cvi_type.h"

#define PERIOD_FRAMES_SIZE 160

static AUDIO_DEV aiDev;
static AI_CHN aiChn;
static AUDIO_DEV aoDev;
static AI_CHN aoChn;
static int ai_channel = 1;
static int ao_channel = 1;

static void audio_capture_init(void)
{
	int ret = 0;
	AIO_ATTR_S ai_attr;
#ifdef CVI_AUDIO_3A_ENABLE
	AI_TALKVQE_CONFIG_S  stVqeConfig;
#endif
	ai_attr.enSamplerate = AUDIO_FREQ;
	ai_attr.enBitwidth = AUDIO_BIT_WIDTH_16;
	ai_attr.enSoundmode =  (ai_channel == 1 ? AUDIO_SOUND_MODE_MONO : AUDIO_SOUND_MODE_STEREO);
	ai_attr.u32FrmNum = 4;
	ai_attr.u32PtNumPerFrm = PERIOD_FRAMES_SIZE;
	ai_attr.u32ChnCnt = 1;
#ifdef CVI_AUDIO_3A_ENABLE
	stVqeConfig.para_client_config = 0;
	stVqeConfig.u32OpenMask = AI_TALKVQE_MASK_AEC | AI_TALKVQE_MASK_AGC | AI_TALKVQE_MASK_ANR;
	stVqeConfig.s32WorkSampleRate = ai_attr.enSamplerate;
	stVqeConfig.s32RevMask = 0;
	stVqeConfig.para_notch_freq = 0;
	/* AEC */
	stVqeConfig.stAecCfg.para_aec_filter_len = 13;
	stVqeConfig.stAecCfg.para_aes_std_thrd = 37;
	stVqeConfig.stAecCfg.para_aes_supp_coeff = 60;
	/* ANR */
	stVqeConfig.stAnrCfg.para_nr_snr_coeff = 15;
	stVqeConfig.stAnrCfg.para_nr_init_sile_time = 0;

	/* AGC */
	stVqeConfig.stAgcCfg.para_agc_max_gain = 0;
	stVqeConfig.stAgcCfg.para_agc_target_high = 2;
	stVqeConfig.stAgcCfg.para_agc_target_low = 72;
	stVqeConfig.stAgcCfg.para_agc_vad_ena = 0;

	stVqeConfig.stAecDelayCfg.para_aec_init_filter_len = 2;
	stVqeConfig.stAecDelayCfg.para_dg_target = 1;
	stVqeConfig.stAecDelayCfg.para_delay_sample = 1;
#endif

	ret = CVI_AI_SetPubAttr(aiDev, &ai_attr);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AI_SetPubAttr error ret = %d\n", __func__, ret);
	}
#ifdef CVI_AUDIO_3A_ENABLE
	ret = CVI_AI_SetTalkVqeAttr(aiDev, aiChn, 0, 0, &stVqeConfig);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AI_SetTalkVqeAttr error ret = %d\n", __func__, ret);
	}
#endif
	ret = CVI_AI_Enable(aiDev);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AI_Enable error ret = %d\n", __func__, ret);
	}
#ifdef CVI_AUDIO_3A_ENABLE
	ret = CVI_AI_EnableVqe(aiDev, aiChn);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AI_EnableVqe error ret = %d\n", __func__, ret);
	}
#endif
	ret = CVI_AI_EnableChn(aiDev, aiChn);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AI_EnableChn error ret = %d\n", __func__, ret);
	}
}

static void audio_play_init(void)
{
	int ret = 0;
	AIO_ATTR_S ao_attr;

	ao_attr.enSamplerate = AUDIO_FREQ;
	ao_attr.enBitwidth = AUDIO_BIT_WIDTH_16;
	ao_attr.enSoundmode = (ao_channel == 1 ? AUDIO_SOUND_MODE_MONO : AUDIO_SOUND_MODE_STEREO);
	ao_attr.u32FrmNum = 4;
	ao_attr.u32PtNumPerFrm = PERIOD_FRAMES_SIZE;
	ao_attr.u32ChnCnt = 1;


	ret = CVI_AO_SetPubAttr(aoDev, &ao_attr);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AO_SetPubAttr error ret = %d\n", __func__, ret);
	}
	ret = CVI_AO_Enable(aoDev);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AO_Enable error ret = %d\n", __func__, ret);
	}
	ret = CVI_AO_EnableChn(aoDev, aoChn);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AO_EnableChn error ret = %d\n", __func__, ret);
	}
}

int MEDIA_AUDIO_PcmRead(unsigned char *buf)
{
	AUDIO_FRAME_S stFrm;
	AEC_FRAME_S stAecFrm;
	int ret = 0;

	ret = CVI_AI_GetFrame(aiDev, aiChn, &stFrm, &stAecFrm, 1000);
	if (ret != CVI_SUCCESS) {
		printf("%s fail, ret:%d\n", __func__, ret);
	}

	ret = stFrm.u32Len * ai_channel * 2;
	memcpy(buf, stFrm.u64VirAddr[0], ret);

	return ret;
}

int MEDIA_AUDIO_PcmWrite(const unsigned char *buf, int len)
{
	AUDIO_FRAME_S stData;
	int ret = 0;

	stData.enBitwidth = AUDIO_BIT_WIDTH_16;
	stData.enSoundmode =  (ao_channel == 1 ? AUDIO_SOUND_MODE_MONO : AUDIO_SOUND_MODE_STEREO);
	stData.u64VirAddr[0] = buf;
	stData.u32Len = len / ao_channel / 2;
	ret = CVI_AO_SendFrame(aoDev, aoChn, &stData, 1000);
	if (ret != CVI_SUCCESS) {
		printf("%s fail, ret:%d\n", __func__, ret);
	}

	return PERIOD_FRAMES_SIZE;
}

int MEDIA_AUDIO_GetPcmLen(enum MEDIA_PCM_TYPE type)
{
	if (type == MEDIA_PCM_TYPE_MIC)
		return PERIOD_FRAMES_SIZE * ai_channel * 2;
	else
		return PERIOD_FRAMES_SIZE * ao_channel * 2;
}

int MEDIA_AUDIO_AlgoIsEnable(void)
{
#ifdef CVI_AUDIO_3A_ENABLE
	return 1;
#else
	return 0;
#endif
}

void *MEDIA_AUDIO_AlgoHandle(void)
{
	return NULL;
}

int MEDIA_AUDIO_Init()
{
	audio_play_init();
	audio_capture_init();
	printf("RTT audio init success\r\n");
	return 0;
}

int MEDIA_AUDIO_DeInit()
{
	int ret = 0;

#ifdef CVI_AUDIO_3A_ENABLE
	ret = CVI_AI_EnableVqe(aiDev, aiChn);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AI_EnableVqe error ret = %d\n", __func__, ret);
	}
#endif
	ret = CVI_AI_DisableChn(aiDev, aiChn);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AI_DisableChn error ret = %d\n", __func__, ret);
	}
	ret = CVI_AI_Disable(aiDev);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AI_Disable error ret = %d\n", __func__, ret);
	}
	ret =  CVI_AO_DisableChn(aoDev, aoChn);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AO_DisableChn error ret = %d\n", __func__, ret);
	}
	ret = CVI_AO_Disable(aoDev);
	if (ret != CVI_SUCCESS) {
		printf("%s: CVI_AO_Disable error ret = %d\n", __func__, ret);
	}
	printf("RTT audio deinit success\r\n");
	return 0;
}
