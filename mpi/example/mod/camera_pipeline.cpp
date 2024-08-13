/*
 * Copyright 2020 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include <errno.h>
#include <cstring>
#include <cstdlib>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "rk_defines.h"
#include "rk_debug.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vpss.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_rgn.h"
#include "rk_common.h"
#include "rk_comm_rgn.h"
#include "rk_comm_vi.h"
#include "rk_comm_vo.h"
#include "test_common.h"
#include "test_comm_utils.h"
#include "test_comm_argparse.h"
#include "test_comm_app_vdec.h"
#include "rk_mpi_cal.h"
#include "rk_mpi_mmz.h"
#include "rk_mpi_ivs.h"

#define TEST_VENC_MAX 2
#define TEST_WITH_FD 0
#define TEST_WITH_FD_SWITCH 0

#define HAVE_API_MPI_VENC 1
#define HAVE_API_MPI_VPSS 1

#ifdef DBG_MOD_ID
#undef DBG_MOD_ID
#endif
#define DBG_MOD_ID  RK_ID_VI

// for 356x vo
#define RK356X_VO_DEV_HD0 0
#define RK356X_VO_DEV_HD1 1
#define RK356X_VOP_LAYER_CLUSTER_0 0
#define RK356X_VOP_LAYER_CLUSTER_1 2
#define RK356X_VOP_LAYER_ESMART_0 4
#define RK356X_VOP_LAYER_ESMART_1 5
#define RK356X_VOP_LAYER_SMART_0 6
#define RK356X_VOP_LAYER_SMART_1 7
#define STRING_BUFF_LEN  64

#define TEST_VI_SENSOR_NUM 6
#define COMBO_START_CHN 4
#define TEST_VI_CHANNEL_NUM     3

#define RKISP_DEV  "/proc/rkisp-vir0"

typedef struct _SENSOR_INFO {
    int width;
    int height;
    int fps;
    int type;   //2MP,3MP,4MP,5MP
} SENSOR_INFO;

typedef struct _rkTestVencCfg {
    RK_BOOL bOutDebugCfg;
    VENC_CHN_ATTR_S stAttr;
    RK_CHAR dstFilePath[128];
    RK_CHAR dstFileName[128];
    RK_S32 s32ChnId;
    FILE *fp;
    RK_S32 selectFd;
} TEST_VENC_CFG;

typedef struct rkVPSS_CFG_S {
    const char *dstFilePath;
    RK_S32 s32DevId;
    RK_S32 s32ChnId;
    RK_U32 u32VpssChnCnt;
    VPSS_GRP_ATTR_S stGrpVpssAttr;
    VPSS_CHN_ATTR_S stVpssChnAttr[VPSS_MAX_CHN_NUM];
} VPSS_CFG_S;

typedef struct rkRGN_CFG_S {
    RGN_ATTR_S stRgnAttr;
    RGN_CHN_ATTR_S stRgnChnAttr;
} RGN_CFG_S;

typedef enum sensorResoType_E {
    _1MEGA = 0,
    _2MEGA,
    _3MEGA,
    _4MEGA,
    _5MEGA,
    _8MEGA,
    _4K,
} SENSOR_RESO_TYPE_E;

typedef enum rkTestVIMODE_E {
    TEST_VI_MODE_VI_ONLY = 0,
    TEST_VI_MODE_BIND_VENC = 1,
    TEST_VI_MODE_BIND_VENC_MULTI = 2,
    TEST_VI_MODE_BIND_VPSS_BIND_VENC = 3,
    TEST_VI_MODE_BIND_VO = 4,
    TEST_VI_MODE_MULTI_VI = 5,
    TEST_VI_MODE_VI_STREAM_ONLY = 6,
    TEST_VI_MODE_BIND_VDEC_BIND_VO = 7,
    TEST_VI_MODE_BIND_IVS = 8,
    TEST_VI_MODE_MULTI_CHN = 13,
    TEST_VI_EPTZ_VI_ONLY = 14,
    TEST_VI_FUNC_MODE = 15,
    TEST_VI_BIND_VENC_WRAP_SWITCH = 16,
} TEST_VI_MODE_E;

typedef enum rkTestModuleType_E {
    MODE_TYPE_PAUSE_RESUME,
    MODE_TYPE_MIRR_FLIP,
    MODE_TYPE_BUTT,
} TEST_VI_MODULE_TYPE_E;

struct combo_th {
    pthread_t th;
    RK_BOOL run;
    RK_S32 chn;
};

typedef struct _rkMpiVICtx {
    RK_S32 width;
    RK_S32 height;
    RK_S32 devId;
    RK_S32 pipeId;
    RK_S32 channelId;
    RK_S32 vencChId;
    RK_S32 loopCountSet;
    RK_S32 selectFd;
    RK_BOOL bFreeze;
    RK_BOOL bEnRgn;
    RK_S32 s32RgnCnt;
    RK_S32 rgnType;
    RK_BOOL bMirror;
    RK_BOOL bFlip;
    RK_BOOL bAttachPool;
    MB_POOL attachPool;
    RK_BOOL bEptz;
    RK_BOOL bUseExt;
    RK_BOOL bRgnOnPipe;
    RK_U32  mosaicBlkSize;
    RK_BOOL bUserPicEnabled;
    RK_BOOL bGetConnecInfo;
    RK_BOOL bGetEdid;
    RK_BOOL bSetEdid;
    RK_BOOL bNoUseLibv4l2;
    RK_BOOL bEnSwcac;
    RK_BOOL bGetStream;
    RK_BOOL bSecondThr;
    COMPRESS_MODE_E enCompressMode;
    VI_DEV_ATTR_S stDevAttr;
    VI_DEV_BIND_PIPE_S stBindPipe;
    VI_CHN_ATTR_S stChnAttr;
    VI_SAVE_FILE_INFO_S stDebugFile;
    VIDEO_FRAME_INFO_S stViFrame;
    VI_CHN_STATUS_S stChnStatus;
    VI_USERPIC_ATTR_S stUsrPic;
    RK_CHAR *pSavePath;
    RK_S32 modTestCnt;
    RK_BOOL bRefBufShare;
    RK_U32 u32DeBreath;
    RK_U32 u32BitRateKb;
    RK_U32 u32BitRateKbMin;
    RK_U32 u32BitRateKbMax;
    RK_U32 u32DelayMsGet;
    RK_U32 u32GopSize;
    RK_U32 maxWidth;
    RK_U32 maxHeight;
    RK_U32 u32DstCodec;
    RK_BOOL bSvc;
    RK_BOOL bCombo;
    RK_S32  s32Snap;
    RK_BOOL bEnOverlay;
    TEST_VI_MODE_E enMode;
    const char *aEntityName;
    VI_CHN_BUF_WRAP_S stChnWrap;
    // for vi
    RGN_CFG_S stViRgn;
	FILE * fpViChn;
    // for venc
    TEST_VENC_CFG stVencCfg[TEST_VENC_MAX];
    VENC_STREAM_S stFrame[TEST_VENC_MAX];
    VPSS_CFG_S stVpssCfg;
    // for vo
    VO_LAYER s32VoLayer;
    VO_DEV s32VoDev;
    // for stream
    RK_CODEC_ID_E enCodecId;
    RK_U32 u32Ivs;
    RK_BOOL bIvsDebug;
    RK_CHAR *bSaveVlogPath;
} TEST_VI_CTX_S;

static struct combo_th g_combo_th[VENC_MAX_CHN_NUM] = {0};
static SENSOR_INFO g_sensorInfo;
static RK_BOOL bquit = RK_FALSE;
static RK_BOOL bBlock = RK_TRUE;
static RK_U32 g_ModeTestCnt = 100;
static sem_t g_sem_module_test[3];
static pthread_mutex_t g_frame_count_mutex[3];
static RK_U32 g_u32VIGetFrameCnt[3];
static RK_BOOL g_ExitModeTest = RK_FALSE;
static RK_U32 g_ModeTestType = MODE_TYPE_BUTT;
static void sigterm_handler(int sig) {
    bquit = RK_TRUE;
    bBlock = RK_FALSE;
}

static RK_S32 create_venc(TEST_VI_CTX_S *ctx, RK_U32 u32Ch) {
#ifdef HAVE_API_MPI_VENC
    VENC_RECV_PIC_PARAM_S stRecvParam;
    VENC_CHN_BUF_WRAP_S stVencChnBufWrap;
    VENC_CHN_REF_BUF_SHARE_S stVencChnRefBufShare;
	VENC_CHN_PARAM_S        stParam;

    memset(&stVencChnBufWrap, 0, sizeof(stVencChnBufWrap));
    stVencChnBufWrap.bEnable = ctx->stChnWrap.bEnable;
    stVencChnBufWrap.u32BufLine = ctx->stChnWrap.u32BufLine;

    memset(&stVencChnRefBufShare, 0, sizeof(VENC_CHN_REF_BUF_SHARE_S));
    stVencChnRefBufShare.bEnable = ctx->bRefBufShare;

    //stRecvParam.s32RecvPicNum = ctx->loopCountSet;
    stRecvParam.s32RecvPicNum = -1;
    RK_MPI_VENC_CreateChn(ctx->stVencCfg[u32Ch].s32ChnId, &ctx->stVencCfg[u32Ch].stAttr);
    RK_MPI_VENC_SetChnBufWrapAttr(ctx->stVencCfg[u32Ch].s32ChnId, &stVencChnBufWrap);
    RK_MPI_VENC_SetChnRefBufShareAttr(ctx->stVencCfg[u32Ch].s32ChnId, &stVencChnRefBufShare);

    if (ctx->u32DeBreath > 0) {
        VENC_DEBREATHEFFECT_S stDeBreathEffect;
        memset(&stDeBreathEffect, 0, sizeof(stDeBreathEffect));
        stDeBreathEffect.bEnable = RK_TRUE;
        stDeBreathEffect.s32Strength1 = ctx->u32DeBreath;
        RK_MPI_VENC_SetDeBreathEffect(ctx->stVencCfg[u32Ch].s32ChnId, &stDeBreathEffect);
    }

    if (ctx->bSvc)
        RK_MPI_VENC_EnableSvc(ctx->stVencCfg[u32Ch].s32ChnId, ctx->bSvc);

	memset(&stParam, 0, sizeof(VENC_CHN_PARAM_S));
	RK_MPI_VENC_GetChnParam(ctx->stVencCfg[u32Ch].s32ChnId, &stParam);

    stParam.stFrameRate.bEnable = RK_TRUE;
    stParam.stFrameRate.s32SrcFrmRateNum = 60;
    stParam.stFrameRate.s32SrcFrmRateDen = 1;
    stParam.stFrameRate.s32DstFrmRateNum = 60;
    stParam.stFrameRate.s32DstFrmRateDen = 1;

	RK_MPI_VENC_SetChnParam(ctx->stVencCfg[u32Ch].s32ChnId, &stParam);
	

    RK_MPI_VENC_StartRecvFrame(ctx->stVencCfg[u32Ch].s32ChnId, &stRecvParam);

    return RK_SUCCESS;
#endif
    return RK_SUCCESS;
}

void init_venc_cfg(TEST_VI_CTX_S *ctx, RK_U32 u32Ch, RK_CODEC_ID_E enType) {
    ctx->stVencCfg[u32Ch].stAttr.stVencAttr.enType = enType;
    ctx->stVencCfg[u32Ch].s32ChnId = ctx->vencChId + u32Ch;
    ctx->stVencCfg[u32Ch].stAttr.stVencAttr.enPixelFormat = ctx->stChnAttr.enPixelFormat;
    if (ctx->bSvc) {
		if (12 == ctx->u32DstCodec) {
			ctx->stVencCfg[u32Ch].stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
		} else {
			ctx->stVencCfg[u32Ch].stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
		}
        ctx->stVencCfg[u32Ch].stAttr.stRcAttr.stH264Vbr.u32BitRate = ctx->u32BitRateKb;
        ctx->stVencCfg[u32Ch].stAttr.stRcAttr.stH264Vbr.u32MinBitRate = ctx->u32BitRateKbMin;
        ctx->stVencCfg[u32Ch].stAttr.stRcAttr.stH264Vbr.u32MaxBitRate = ctx->u32BitRateKbMax;
    } else {
		if (12 == ctx->u32DstCodec) {
			ctx->stVencCfg[u32Ch].stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
		} else {
			ctx->stVencCfg[u32Ch].stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
		}
        ctx->stVencCfg[u32Ch].stAttr.stRcAttr.stH264Cbr.u32BitRate = ctx->u32BitRateKb;
    }
    ctx->stVencCfg[u32Ch].stAttr.stRcAttr.stH264Cbr.u32Gop = ctx->u32GopSize;
    ctx->stVencCfg[u32Ch].stAttr.stVencAttr.u32MaxPicWidth = ctx->maxWidth;
    ctx->stVencCfg[u32Ch].stAttr.stVencAttr.u32MaxPicHeight = ctx->maxHeight;
    ctx->stVencCfg[u32Ch].stAttr.stVencAttr.u32PicWidth = ctx->width;
    ctx->stVencCfg[u32Ch].stAttr.stVencAttr.u32PicHeight = ctx->height;
    ctx->stVencCfg[u32Ch].stAttr.stVencAttr.u32VirWidth = ctx->width;
    ctx->stVencCfg[u32Ch].stAttr.stVencAttr.u32VirHeight = ctx->height;
    ctx->stVencCfg[u32Ch].stAttr.stVencAttr.u32StreamBufCnt = 5;
    ctx->stVencCfg[u32Ch].stAttr.stVencAttr.u32BufSize = ctx->maxWidth * ctx->maxHeight / 2;
    ctx->stVencCfg[u32Ch].stAttr.stGopAttr.u32MaxLtrCount = 1;
}

static RK_S32 readFromPic(TEST_VI_CTX_S *ctx, VIDEO_FRAME_S *buffer) {
    FILE            *fp    = NULL;
    RK_S32          s32Ret = RK_SUCCESS;
    MB_BLK          srcBlk = MB_INVALID_HANDLE;
    PIC_BUF_ATTR_S  stPicBufAttr;
    MB_PIC_CAL_S    stMbPicCalResult;
    const char      *user_picture_path = "/data/test_vi_user.yuv";

    stPicBufAttr.u32Width = ctx->width;
    stPicBufAttr.u32Height = ctx->height;
    stPicBufAttr.enCompMode = COMPRESS_MODE_NONE;
    stPicBufAttr.enPixelFormat = RK_FMT_YUV420SP;
    s32Ret = RK_MPI_CAL_VGS_GetPicBufferSize(&stPicBufAttr, &stMbPicCalResult);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("get picture buffer size failed. err 0x%x", s32Ret);
        return RK_NULL;
    }

    RK_MPI_MMZ_Alloc(&srcBlk, stMbPicCalResult.u32MBSize, RK_MMZ_ALLOC_CACHEABLE);

    fp = fopen(user_picture_path, "rb");
    if (NULL == fp) {
        RK_LOGE("open %s fail", user_picture_path);
        return RK_FAILURE;
    } else {
        fread(RK_MPI_MB_Handle2VirAddr(srcBlk), 1 , stMbPicCalResult.u32MBSize, fp);
        fclose(fp);
        RK_MPI_SYS_MmzFlushCache(srcBlk, RK_FALSE);
        RK_LOGD("open %s success", user_picture_path);
    }

    buffer->u32Width = ctx->width;
    buffer->u32Height = ctx->height;
    buffer->u32VirWidth = ctx->width;
    buffer->u32VirHeight = ctx->height;
    buffer->enPixelFormat = RK_FMT_YUV420SP;
    buffer->u32TimeRef = 0;
    buffer->u64PTS = 0;
    buffer->enCompressMode = COMPRESS_MODE_NONE;
    buffer->pMbBlk = srcBlk;

    RK_LOGD("readFromPic width = %d, height = %d size = %d pixFormat = %d",
            ctx->width, ctx->height, stMbPicCalResult.u32MBSize, RK_FMT_YUV420SP);
    return RK_SUCCESS;
}

static RK_S32 create_rgn(TEST_VI_CTX_S *ctx) {
#ifdef HAVE_API_MPI_RGN
    RK_S32 i;
    RK_S32 s32Ret = RK_SUCCESS;
    RK_S32 attachChnId = 0;
    RGN_ATTR_S stRgnAttr;
    RGN_CHN_ATTR_S stRgnChnAttr;
    RGN_HANDLE RgnHandle = 0;
    MPP_CHN_S stMppChn;
    RGN_TYPE_E rgnType = (RGN_TYPE_E)ctx->rgnType;

    /****************************************
     step 1: create overlay regions
    ****************************************/
    for (i = 0; i < ctx->s32RgnCnt; i++) {
        ctx->stViRgn.stRgnAttr.enType = (RGN_TYPE_E)ctx->rgnType;
        ctx->stViRgn.stRgnAttr.unAttr.stOverlay.u32ClutNum = 0;
        ctx->stViRgn.stRgnAttr.unAttr.stOverlay.stSize.u32Width = 128;
        ctx->stViRgn.stRgnAttr.unAttr.stOverlay.stSize.u32Height = 128;
        RgnHandle = i;
        s32Ret = RK_MPI_RGN_Create(RgnHandle, &ctx->stViRgn.stRgnAttr);
        if (RK_SUCCESS != s32Ret) {
            RK_LOGE("RK_MPI_RGN_Create (%d) failed with %#x!", RgnHandle, s32Ret);
            RK_MPI_RGN_Destroy(RgnHandle);
            return RK_FAILURE;
        }
        RK_LOGI("The handle: %d, create success!", RgnHandle);
    }

    /*********************************************
     step 2: display overlay regions to vi
    *********************************************/
    if (ctx->bRgnOnPipe) {
        /* rgn attach to pipe */
        attachChnId = VI_MAX_CHN_NUM;
    } else {
        attachChnId = ctx->channelId;
    }
    for (i = 0; i < ctx->s32RgnCnt; i++) {
        stMppChn.enModId = RK_ID_VI;
        stMppChn.s32DevId = ctx->devId;
        stMppChn.s32ChnId = attachChnId;
        RgnHandle = i;

        memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
        stRgnChnAttr.bShow = RK_TRUE;
        stRgnChnAttr.enType = rgnType;

        BITMAP_S stBitmap;
        switch (rgnType) {
            case COVER_RGN: {
                RGN_CHN_ATTR_S stCoverChnAttr;
                stCoverChnAttr.bShow = RK_TRUE;
                stCoverChnAttr.enType = COVER_RGN;
                stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X = 128 * i;
                stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y = 128 * i;
                stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width = 128;
                stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height = 128;
                stCoverChnAttr.unChnAttr.stCoverChn.u32Color = 0xffffff;  // white
                stCoverChnAttr.unChnAttr.stCoverChn.enCoordinate = RGN_ABS_COOR;
                stCoverChnAttr.unChnAttr.stCoverChn.u32Layer = i;

                s32Ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stCoverChnAttr);
                if (RK_SUCCESS != s32Ret) {
                    RK_LOGE("failed with %#x!", s32Ret);
                    goto __EXIT;
                }
                s32Ret = RK_MPI_RGN_GetDisplayAttr(RgnHandle, &stMppChn, &stCoverChnAttr);
                if (RK_SUCCESS != s32Ret) {
                    RK_LOGE("failed with %#x!", s32Ret);
                    goto __EXIT;
                }
                stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X = 64 * i;
                stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y = 64 * i;
                stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width = 64;
                stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height = 64;
                stCoverChnAttr.unChnAttr.stCoverChn.u32Color = 0x0000ff;  // blue
                stCoverChnAttr.unChnAttr.stCoverChn.enCoordinate = RGN_ABS_COOR;
                stCoverChnAttr.unChnAttr.stCoverChn.u32Layer = i;
                // change cover channel attribute below.
                s32Ret = RK_MPI_RGN_SetDisplayAttr(RgnHandle, &stMppChn, &stCoverChnAttr);
                if (RK_SUCCESS != s32Ret) {
                    RK_LOGE("RK_MPI_RGN_SetDisplayAttr failed with %#x!", s32Ret);
                    goto __EXIT;
                }

                RK_LOGI("the cover region:%d to <%d, %d, %d, %d>",
                        RgnHandle,
                        stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X,
                        stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y,
                        stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width,
                        stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height);
            } break;
            case MOSAIC_RGN: {
                RGN_CHN_ATTR_S stMoscaiChnAttr;
                stMoscaiChnAttr.bShow = RK_TRUE;
                stMoscaiChnAttr.enType = MOSAIC_RGN;
                stMoscaiChnAttr.unChnAttr.stMosaicChn.stRect.s32X = 128 * i;
                stMoscaiChnAttr.unChnAttr.stMosaicChn.stRect.s32Y = 128 * i;
                stMoscaiChnAttr.unChnAttr.stMosaicChn.stRect.u32Width = 128;
                stMoscaiChnAttr.unChnAttr.stMosaicChn.stRect.u32Height = 128;
                stMoscaiChnAttr.unChnAttr.stMosaicChn.enBlkSize = (MOSAIC_BLK_SIZE_E)ctx->mosaicBlkSize;
                stMoscaiChnAttr.unChnAttr.stMosaicChn.u32Layer = i;

                s32Ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stMoscaiChnAttr);
                if (RK_SUCCESS != s32Ret) {
                    RK_LOGE("failed with %#x!", s32Ret);
                    goto __EXIT;
                }
                s32Ret = RK_MPI_RGN_GetDisplayAttr(RgnHandle, &stMppChn, &stMoscaiChnAttr);
                if (RK_SUCCESS != s32Ret) {
                    RK_LOGE("failed with %#x!", s32Ret);
                    goto __EXIT;
                }

                stMoscaiChnAttr.unChnAttr.stMosaicChn.stRect.s32X = 64 * i;
                stMoscaiChnAttr.unChnAttr.stMosaicChn.stRect.s32Y = 64 * i;
                stMoscaiChnAttr.unChnAttr.stMosaicChn.stRect.u32Width = 64;
                stMoscaiChnAttr.unChnAttr.stMosaicChn.stRect.u32Height = 64;
                stMoscaiChnAttr.unChnAttr.stMosaicChn.enBlkSize = (MOSAIC_BLK_SIZE_E)ctx->mosaicBlkSize;;
                stMoscaiChnAttr.unChnAttr.stMosaicChn.u32Layer = i;

                // change mosaic channel attribute below.
                s32Ret = RK_MPI_RGN_SetDisplayAttr(RgnHandle, &stMppChn, &stMoscaiChnAttr);
                if (RK_SUCCESS != s32Ret) {
                    RK_LOGE("failed with %#x!", s32Ret);
                    goto __EXIT;
                }
                RK_LOGI("the mosaic region:%d to <%d, %d, %d, %d>",
                        RgnHandle,
                        stMoscaiChnAttr.unChnAttr.stMosaicChn.stRect.s32X,
                        stMoscaiChnAttr.unChnAttr.stMosaicChn.stRect.s32Y,
                        stMoscaiChnAttr.unChnAttr.stMosaicChn.stRect.u32Width,
                        stMoscaiChnAttr.unChnAttr.stMosaicChn.stRect.u32Height);
            } break;
            default:
                break;
        }
    }

    return RK_SUCCESS;
__EXIT:
    for (i = 0; i < ctx->s32RgnCnt; i++) {
        stMppChn.enModId = RK_ID_VI;
        stMppChn.s32DevId = ctx->devId;
        stMppChn.s32ChnId = attachChnId;
        RgnHandle = i;
        s32Ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
        if (RK_SUCCESS != s32Ret) {
            RK_LOGE("RK_MPI_RGN_DetachFrmChn (%d) failed with %#x!", RgnHandle, s32Ret);
            return RK_FAILURE;
        }
    }
#endif
    return RK_FAILURE;
}

static RK_S32 destory_rgn(TEST_VI_CTX_S *ctx) {
#ifdef HAVE_API_MPI_RGN
    RK_S32 i;
    RK_S32 s32Ret = RK_SUCCESS;
    RGN_HANDLE RgnHandle = 0;

    MPP_CHN_S stMppChn;

    stMppChn.enModId = RK_ID_VI;
    stMppChn.s32DevId = ctx->devId;
    // NOTE: rv1106 rgn chn use max chn num as distinguish acting on pipe or chn.
    if (ctx->bRgnOnPipe)
        stMppChn.s32ChnId = VI_MAX_CHN_NUM;
    else
        stMppChn.s32ChnId = ctx->channelId;

    // detach rgn
    for (RK_S32 i = 0; i < ctx->s32RgnCnt; i++) {
        RgnHandle = i;
        s32Ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
        if (RK_SUCCESS != s32Ret) {
            RK_LOGE("RK_MPI_RGN_DetachFrmChn (%d) failed with %#x!", RgnHandle, s32Ret);
            return RK_FAILURE;
        }

        RK_LOGI("Detach handle:%d from chn success", RgnHandle);
    }

    for (RK_S32 i = 0; i < ctx->s32RgnCnt; i++) {
        RgnHandle = i;
        s32Ret = RK_MPI_RGN_Destroy(RgnHandle);
        if (RK_SUCCESS != s32Ret) {
            RK_LOGE("RK_MPI_RGN_Destroy (%d) failed with %#x!", RgnHandle, s32Ret);
        }
        RK_LOGI("Destory handle:%d success", RgnHandle);
    }

    RK_LOGD("vi RK_MPI_RGN_Destroy OK");
#endif
    return RK_SUCCESS;
}

static RK_S32 test_vi_set_stream_codec(TEST_VI_CTX_S *ctx) {
    RK_S32 s32Ret = RK_FAILURE;

    /* test set stream codec before enable chn(need after RK_MPI_VI_SetChnAttr)*/
    if (ctx->enCodecId != RK_VIDEO_ID_Unused) {
        RK_CODEC_ID_E enCodecId;

        s32Ret = RK_MPI_VI_SetChnStreamCodec(ctx->pipeId, ctx->channelId, ctx->enCodecId);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_SetChnStreamCodec %x", s32Ret);
            return s32Ret;
        }
    }

    return s32Ret;
}

static MB_POOL create_pool(TEST_VI_CTX_S *pstCtx) {
    MB_POOL_CONFIG_S stMbPoolCfg;
    PIC_BUF_ATTR_S stPicBufAttr;
    MB_PIC_CAL_S stMbPicCalResult;
    RK_S32 s32Ret = RK_SUCCESS;

    if (pstCtx->stChnAttr.stIspOpt.stMaxSize.u32Width && \
         pstCtx->stChnAttr.stIspOpt.stMaxSize.u32Height) {
        stPicBufAttr.u32Width = pstCtx->stChnAttr.stIspOpt.stMaxSize.u32Width;
        stPicBufAttr.u32Height = pstCtx->stChnAttr.stIspOpt.stMaxSize.u32Height;
    } else {
        stPicBufAttr.u32Width = pstCtx->width;
        stPicBufAttr.u32Height = pstCtx->height;
    }
    stPicBufAttr.enCompMode = (COMPRESS_MODE_E)pstCtx->enCompressMode;
    stPicBufAttr.enPixelFormat = (PIXEL_FORMAT_E)pstCtx->stChnAttr.enPixelFormat;
    s32Ret = RK_MPI_CAL_COMM_GetPicBufferSize(&stPicBufAttr, &stMbPicCalResult);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("get picture buffer size failed. err 0x%x", s32Ret);
        return MB_INVALID_POOLID;
    }

    memset(&stMbPoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
    stMbPoolCfg.u64MBSize = stMbPicCalResult.u32MBSize;
    stMbPoolCfg.u32MBCnt  = 3;
    stMbPoolCfg.enRemapMode = MB_REMAP_MODE_CACHED;
    stMbPoolCfg.bPreAlloc = RK_TRUE;
    stMbPoolCfg.bNotDelete = RK_TRUE;

    return RK_MPI_MB_CreatePool(&stMbPoolCfg);
}

static void *SecondGetFrame(void *arg) {
    int s32Ret;
    int loopCnt = 0;
    RK_S32 waitTime = 100;
    VIDEO_FRAME_INFO_S stViFrame;
    TEST_VI_CTX_S *ctx = (TEST_VI_CTX_S*)arg;

    if (!ctx->bGetStream) { // get frames in another process.
        while (!bquit) {
            usleep(1000);
        }
    }

    while (loopCnt < ctx->loopCountSet && ctx->bGetStream) {
        // get the frame
        s32Ret = RK_MPI_VI_GetChnFrame(ctx->pipeId, ctx->channelId, &stViFrame, waitTime);
        if (s32Ret == RK_SUCCESS) {
            RK_U64 nowUs = TEST_COMM_GetNowUs();
            void *data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
            RK_LOGD("RK_MPI_VI_GetChnFrame ok:data %p >>> loop:%d <<< \tseq:%d pts:%lld ms len=%d", data, loopCnt,
                     stViFrame.stVFrame.u32TimeRef, stViFrame.stVFrame.u64PTS / 1000,
                     stViFrame.stVFrame.u64PrivateData);

            // release the frame
            s32Ret = RK_MPI_VI_ReleaseChnFrame(ctx->pipeId, ctx->channelId, &stViFrame);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
            } else {
                RK_LOGD("RK_MPI_VI_ReleaseChnFrame OK pid = %lu", pthread_self());
            }
            loopCnt++;
        } else {
            RK_LOGE("RK_MPI_VI_GetChnFrame timeout %x", s32Ret);
        }

        usleep(10*1000);
    }
    return RK_NULL;
}

static void * ViChnGetFrame(void *arg) {
#if 1
    int s32Ret;
    int loopCnt = 0;
    RK_S32 waitTime = -1;
    VIDEO_FRAME_INFO_S stViFrame;
    TEST_VI_CTX_S *ctx = (TEST_VI_CTX_S*)arg;

    while (loopCnt < ctx->loopCountSet) {
        // get the frame
        s32Ret = RK_MPI_VI_GetChnFrame(ctx->pipeId, ctx->channelId, &stViFrame, waitTime);
        if (s32Ret == RK_SUCCESS) {
            RK_U64 nowUs = TEST_COMM_GetNowUs();
            void * data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
            RK_LOGD("RK_MPI_VI_GetChnFrame ok:data %p >>> loop:%d <<< \tseq:%d pts:%lld ms len=%d", data, loopCnt,
                     stViFrame.stVFrame.u32TimeRef, stViFrame.stVFrame.u64PTS / 1000,
                     RK_MPI_MB_GetLength(stViFrame.stVFrame.pMbBlk));

			//fwrite(data, 1, RK_MPI_MB_GetLength(stViFrame.stVFrame.pMbBlk), ctx->fpViChn);
            //fflush(ctx->fpViChn);

            // release the frame
            s32Ret = RK_MPI_VI_ReleaseChnFrame(ctx->pipeId, ctx->channelId, &stViFrame);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
            } else {
                RK_LOGD("RK_MPI_VI_ReleaseChnFrame OK pid = %lu", pthread_self());
            }
        } else {
            RK_LOGE("RK_MPI_VI_GetChnFrame timeout %x", s32Ret);
        }
		loopCnt++;
    }

	fclose(ctx->fpViChn);
    return RK_NULL;
#else
	int s32Ret = -1;
    RK_S32 waitTime = -1;
    VIDEO_FRAME_INFO_S stViFrame;
	TEST_VI_CTX_S * ctx = (TEST_VI_CTX_S*)arg;

	s32Ret = RK_MPI_VI_GetChnFrame(ctx->pipeId, ctx->channelId, &stViFrame, waitTime);
	if (s32Ret == RK_SUCCESS) {
		RK_U64 nowUs = TEST_COMM_GetNowUs();
		void * data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
		            RK_LOGD("RK_MPI_VI_GetChnFrame ok:data %p  <<< \tseq:%d pts:%lld ms len=%d", data,
                     stViFrame.stVFrame.u32TimeRef, stViFrame.stVFrame.u64PTS / 1000,
                     RK_MPI_MB_GetLength(stViFrame.stVFrame.pMbBlk));

		fwrite(data, 1, RK_MPI_MB_GetLength(stViFrame.stVFrame.pMbBlk), ctx->fpViChn);
        fflush(ctx->fpViChn);

		// release the frame
        s32Ret = RK_MPI_VI_ReleaseChnFrame(ctx->pipeId, ctx->channelId, &stViFrame);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x\n", s32Ret);
        } else {
            RK_LOGD("RK_MPI_VI_ReleaseChnFrame OK pid = %lu\n", pthread_self());
        }
	}
#endif
}

static RK_S32 test_vi_init(TEST_VI_CTX_S *ctx) {
    RK_S32 s32Ret = RK_FAILURE;

    // 0. get dev config status
    s32Ret = RK_MPI_VI_GetDevAttr(ctx->devId, &ctx->stDevAttr);
    if (s32Ret == RK_ERR_VI_NOT_CONFIG) {
        // 0-1.config dev
        s32Ret = RK_MPI_VI_SetDevAttr(ctx->devId, &ctx->stDevAttr);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_SetDevAttr %x", s32Ret);
            goto __FAILED;
        }
    } else {
        RK_LOGE("RK_MPI_VI_SetDevAttr already");
    }
    RK_LOGI("RK_MPI_VI_SetDevAttr success");
    // 1.get  dev enable status
    s32Ret = RK_MPI_VI_GetDevIsEnable(ctx->devId);
    if (s32Ret != RK_SUCCESS) {
        // 1-2.enable dev
        s32Ret = RK_MPI_VI_EnableDev(ctx->devId);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_EnableDev %x", s32Ret);
            goto __FAILED;
        }
        // 1-3.bind dev/pipe
        ctx->stBindPipe.u32Num = 1;
        ctx->stBindPipe.PipeId[0] = ctx->pipeId;
        if (ctx->bUseExt)
            ctx->stBindPipe.bUserStartPipe[0] = RK_TRUE;
        s32Ret = RK_MPI_VI_SetDevBindPipe(ctx->devId, &ctx->stBindPipe);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_SetDevBindPipe %x", s32Ret);
            goto __FAILED;
        }
    } else {
        RK_LOGE("RK_MPI_VI_EnableDev already");
    }
    RK_LOGI("RK_MPI_VI_EnableDev success");
    // 2.config channel
    ctx->stChnAttr.bMirror         = ctx->bMirror;
    ctx->stChnAttr.bFlip           = ctx->bFlip;
    ctx->stChnAttr.stSize.u32Width = ctx->width;
    ctx->stChnAttr.stSize.u32Height = ctx->height;
    ctx->stChnAttr.stIspOpt.stMaxSize.u32Width  = RK_MAX(ctx->maxWidth, ctx->width);
    ctx->stChnAttr.stIspOpt.stMaxSize.u32Height = RK_MAX(ctx->maxHeight, ctx->height);
    ctx->stChnAttr.enCompressMode = ctx->enCompressMode;
    ctx->stChnAttr.stIspOpt.bNoUseLibV4L2 = ctx->bNoUseLibv4l2;
    s32Ret = RK_MPI_VI_SetChnAttr(ctx->pipeId, ctx->channelId, &ctx->stChnAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VI_SetChnAttr %x", s32Ret);
        goto __FAILED;
    }
    RK_LOGI("RK_MPI_VI_SetChnAttr success");

    if (ctx->bAttachPool) {
        ctx->attachPool = create_pool(ctx);
        if (ctx->attachPool == MB_INVALID_POOLID) {
            RK_LOGE("create pool failure");
            s32Ret = RK_FAILURE;
            goto __FAILED;
        }

        s32Ret = RK_MPI_VI_AttachMbPool(ctx->pipeId, ctx->channelId, ctx->attachPool);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("pipe:%d ch:%d attach pool fail", ctx->pipeId, ctx->channelId);
            goto __FAILED;
        }
    }

    // set wrap mode attr
    VI_CHN_BUF_WRAP_S stViWrap;
    memset(&stViWrap, 0, sizeof(VI_CHN_BUF_WRAP_S));
    if (ctx->stChnWrap.bEnable) {
        if (ctx->stChnWrap.u32BufLine < 128 || ctx->stChnWrap.u32BufLine > ctx->height) {
            RK_LOGE("wrap mode buffer line must between [128, H]");
            goto __FAILED;
        }
        RK_U32                maxW = RK_MAX(ctx->width, ctx->maxWidth);
        stViWrap.bEnable           = ctx->stChnWrap.bEnable;
        stViWrap.u32BufLine        = ctx->stChnWrap.u32BufLine;
        stViWrap.u32WrapBufferSize = stViWrap.u32BufLine * maxW * 3 / 2;  // nv12 (w * wrapLine *3 / 2)
        RK_LOGD("set channel wrap line: %d, wrapBuffSize = %d", ctx->stChnWrap.u32BufLine, stViWrap.u32WrapBufferSize);
        s32Ret = RK_MPI_VI_SetChnWrapBufAttr(ctx->pipeId, ctx->channelId, &stViWrap);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_SetChnWrapBufAttr fail:%x", s32Ret);
            goto __FAILED;
        }
    }

    /* test user picture */
    if (ctx->bUserPicEnabled) {
        ctx->stUsrPic.enUsrPicMode = VI_USERPIC_MODE_BGC;
        // ctx->stUsrPic.enUsrPicMode = VI_USERPIC_MODE_PIC;

        if (ctx->stUsrPic.enUsrPicMode == VI_USERPIC_MODE_PIC) {
            s32Ret = readFromPic(ctx, &ctx->stUsrPic.unUsrPic.stUsrPicFrm.stVFrame);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VI_SetUserPic fail:%x", s32Ret);
                goto __FAILED;
            }
        } else if (ctx->stUsrPic.enUsrPicMode == VI_USERPIC_MODE_BGC) {
            /* set background color */
            ctx->stUsrPic.unUsrPic.stUsrPicBg.u32BgColor = RGB(0, 0, 128);
        }

        s32Ret = RK_MPI_VI_SetUserPic(ctx->pipeId, ctx->channelId, &ctx->stUsrPic);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_SetUserPic fail:%x", s32Ret);
            goto __FAILED;
        }

        s32Ret = RK_MPI_VI_EnableUserPic(ctx->pipeId, ctx->channelId);
    }

    test_vi_set_stream_codec(ctx);

    // do swcac
    if (ctx->bEnSwcac) {
        SWCAC_CONFIG_S stSwcacCfg;
        memset(&stSwcacCfg, 0, sizeof(SWCAC_CONFIG_S));
        stSwcacCfg.bEnable = RK_TRUE;
        stSwcacCfg.stCacEffectAttr.u32AutoHighLightDetect = 1;
        stSwcacCfg.stCacEffectAttr.u32AutoHighLightOffset = 0;
        stSwcacCfg.stCacEffectAttr.u32FixHighLightBase    = 0;
        stSwcacCfg.stCacEffectAttr.fYCompensate         = 0.0f;
        stSwcacCfg.stCacEffectAttr.fAutoStrengthU       = 1.0f;
        stSwcacCfg.stCacEffectAttr.fAutoStrengthV       = 1.0f;
        stSwcacCfg.stCacEffectAttr.fGrayStrengthU       = 0.6f;
        stSwcacCfg.stCacEffectAttr.fGrayStrengthV       = 0.2f;

        s32Ret = RK_MPI_VI_SetSwcacConfig(ctx->pipeId, ctx->channelId, &stSwcacCfg);
        RK_LOGD("RK_MPI_VI_SetSwcacConfig ret = %x", s32Ret);
    } else {
        SWCAC_CONFIG_S stSwcacCfg;
        memset(&stSwcacCfg, 0, sizeof(SWCAC_CONFIG_S));
        stSwcacCfg.bEnable = RK_FALSE;
        s32Ret = RK_MPI_VI_SetSwcacConfig(ctx->pipeId, ctx->channelId, &stSwcacCfg);
        RK_LOGD("RK_MPI_VI_SetSwcacConfig ret = %x", s32Ret);
    }

    // open fd before enable chn will be better
#if TEST_WITH_FD
     ctx->selectFd = RK_MPI_VI_GetChnFd(ctx->pipeId, ctx->channelId);
     RK_LOGE("ctx->pipeId=%d, ctx->channelId=%d, ctx->selectFd:%d ", ctx->pipeId, ctx->channelId, ctx->selectFd);
#endif
    // 3.enable channel
    if (ctx->bUseExt) {
        s32Ret = RK_MPI_VI_EnableChnExt(ctx->pipeId, ctx->channelId);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_EnableChnExt %x", s32Ret);
            goto __FAILED;
        }

        s32Ret = RK_MPI_VI_StartPipe(ctx->pipeId);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_StartPipe %x", s32Ret);
            goto __FAILED;
        }
    } else {
        s32Ret = RK_MPI_VI_EnableChn(ctx->pipeId, ctx->channelId);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_EnableChn %x", s32Ret);
            goto __FAILED;
        }
    }

    if (ctx->bEnRgn) {
        s32Ret = create_rgn(ctx);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("Error:create rgn failed!");
            goto __FAILED;
        }
    }

    // 4.save debug file
//    if (ctx->stDebugFile.bCfg) {
//        s32Ret = RK_MPI_VI_ChnSaveFile(ctx->pipeId, ctx->channelId, &ctx->stDebugFile);
//        if (s32Ret)
//            RK_LOGE("RK_MPI_VI_ChnSaveFile %x", s32Ret);
//    }

__FAILED:
    return s32Ret;
}

static void *ViGetFrameThread(void *arg) {
    TEST_VI_CTX_S *ctx = (TEST_VI_CTX_S *)arg;
    void *pData = RK_NULL;
    int loopCount = 0;
    int s32Ret;
    int chn, pipe;
    pipe = ctx->pipeId;
    chn = ctx->channelId;

    while (!g_ExitModeTest) {
        if (loopCount < ctx->loopCountSet) {
            s32Ret = RK_MPI_VI_GetChnFrame(pipe, chn, &ctx->stViFrame, 100);
            if (s32Ret == RK_SUCCESS) {
                RK_U64 nowUs = TEST_COMM_GetNowUs();
                void *data = RK_MPI_MB_Handle2VirAddr(ctx->stViFrame.stVFrame.pMbBlk);
                RK_LOGD("RK_MPI_VI_GetChnFrame ok:data %p,Ch:%d >>> loop:%d <<< seq:%d pts:%lld ms len=%d",
                        data, chn, loopCount,
                        ctx->stViFrame.stVFrame.u32TimeRef,
                        ctx->stViFrame.stVFrame.u64PTS/1000,
                        ctx->stViFrame.stVFrame.u64PrivateData);
                s32Ret = RK_MPI_VI_ReleaseChnFrame(pipe, chn, &ctx->stViFrame);
                RK_ASSERT(s32Ret == RK_SUCCESS);

                pthread_mutex_lock(&g_frame_count_mutex[chn]);
                g_u32VIGetFrameCnt[chn]++;
                pthread_mutex_unlock(&g_frame_count_mutex[chn]);

                if (g_u32VIGetFrameCnt[chn] == ctx->loopCountSet) {
                    sem_post(&g_sem_module_test[chn]);
                }
                loopCount++;

                if (loopCount == ctx->loopCountSet)
                    loopCount = 0;
            } else {
                RK_LOGE("Chn:%d, RK_MPI_VI_GetChnFrame timeout %x", chn, s32Ret);
            }
        }
    }

    return RK_NULL;
}

static void VencCropFrame(TEST_VI_CTX_S * ctx)
{
	static unsigned int index = 1;

	//TEST_VI_CTX_S *ctx = reinterpret_cast<TEST_VI_CTX_S *>(vctx);
	VENC_CHN_PARAM_S        stParam;

	memset(&stParam, 0, sizeof(VENC_CHN_PARAM_S));

	static unsigned int calX = 0;
	static unsigned int calY = 0;

	

	RK_MPI_VENC_GetChnParam(ctx->stVencCfg[1].s32ChnId, &stParam);

	stParam.stCropCfg.enCropType = VENC_CROP_ONLY;
    //stParam.stCropCfg.stCropRect.s32X = 960+2*index;
    //stParam.stCropCfg.stCropRect.s32Y = 540+2*index;
    stParam.stCropCfg.stCropRect.s32X = calX;
    stParam.stCropCfg.stCropRect.s32Y = calY;
	stParam.stCropCfg.stCropRect.u32Width = 1920;
    stParam.stCropCfg.stCropRect.u32Height = 1080;

	RK_MPI_VENC_SetChnParam(ctx->stVencCfg[1].s32ChnId, &stParam);

	index++;

	if ((calX < 1920) && (calY == 0))
		calX += 2;
	else if ((calX == 1920) && (calY < 1080))
		calY += 1;
	else if ((calY == 1080) && (calX <= 1920) && (calX > 0))
		calX -= 2;
	else if ((calX == 0) && (calY <= 1080))
		calY -= 1;
}

static RK_S32 test_vi_bind_venc_loop(TEST_VI_CTX_S *ctx) {
#ifdef HAVE_API_MPI_VENC
    MPP_CHN_S stSrcChn, stDestChn[TEST_VENC_MAX];
    RK_S32 loopCount = 0;
    void *pData = RK_NULL;
    RK_S32 s32Ret = RK_FAILURE;
    RK_U32 i;
    RK_U32 u32DstCount = ((ctx->enMode == TEST_VI_MODE_BIND_VENC_MULTI) ? 2 : 1);

    s32Ret = test_vi_init(ctx);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("vi %d:%d init failed:%x", ctx->devId, ctx->channelId, s32Ret);
        goto __FAILED;
    }

    /* venc */
    for (i = 0; i < u32DstCount; i++) {
        // venc  init and create
        init_venc_cfg(ctx, i, (RK_CODEC_ID_E)ctx->u32DstCodec);
        s32Ret = create_venc(ctx, ctx->stVencCfg[i].s32ChnId);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("create %d ch venc failed", ctx->stVencCfg[i].s32ChnId);
            return s32Ret;
        }
         // bind vi to venc
        stSrcChn.enModId    = RK_ID_VI;
        stSrcChn.s32DevId   = ctx->devId;
        stSrcChn.s32ChnId   = ctx->channelId;

        stDestChn[i].enModId   = RK_ID_VENC;
        stDestChn[i].s32DevId  = 0;
        stDestChn[i].s32ChnId  = ctx->stVencCfg[i].s32ChnId;

        s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn[i]);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("venc:%d bind vi failed:%#X", ctx->stVencCfg[i].s32ChnId, s32Ret);
            goto __FAILED;
        }
        ctx->stFrame[i].pstPack = reinterpret_cast<VENC_PACK_S *>(malloc(sizeof(VENC_PACK_S)));
#if TEST_WITH_FD
        ctx->stVencCfg[i].selectFd = RK_MPI_VENC_GetFd(ctx->stVencCfg[i].s32ChnId);
        RK_LOGE("venc chn:%d, ctx->selectFd:%d ", ctx->stVencCfg[i].s32ChnId, ctx->stVencCfg[i].selectFd);
#endif
    }

	//pthread_t venc_crop_thread;
    //pthread_create(&venc_crop_thread, RK_NULL, VencCropFrame, (void *)ctx);

	pthread_t vi_getframe_thread;
    pthread_create(&vi_getframe_thread, RK_NULL, ViChnGetFrame, (void *)ctx);

    while (loopCount < ctx->loopCountSet) {
		RK_LOGE("loopCountSet: %u\n", loopCount);
		VencCropFrame(ctx);

        for (i = 0; i < u32DstCount; i++) {

            s32Ret = RK_MPI_VENC_GetStream(ctx->stVencCfg[i].s32ChnId, &ctx->stFrame[i], -1);
            if (s32Ret == RK_SUCCESS) {
                if (ctx->stVencCfg[i].bOutDebugCfg) {
                    pData = RK_MPI_MB_Handle2VirAddr(ctx->stFrame[i].pstPack->pMbBlk);
                    fwrite(pData, 1, ctx->stFrame[i].pstPack->u32Len, ctx->stVencCfg[i].fp);
                    fflush(ctx->stVencCfg[i].fp);
                }
                RK_U64 nowUs = TEST_COMM_GetNowUs();

                RK_LOGI("chn:%d, loopCount:%d enc->seq:%d wd:%d pts=%lld delay=%lldus\n", i, loopCount,
                         ctx->stFrame[i].u32Seq, ctx->stFrame[i].pstPack->u32Len,
                         ctx->stFrame[i].pstPack->u64PTS,
                         nowUs - ctx->stFrame[i].pstPack->u64PTS);
                s32Ret = RK_MPI_VENC_ReleaseStream(ctx->stVencCfg[i].s32ChnId, &ctx->stFrame[i]);
                if (s32Ret != RK_SUCCESS) {
                    RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
                }
            } else {
                RK_LOGE("RK_MPI_VENC_GetFrame fail %x", s32Ret);
            }
        }

		//ViChnGetFrame(ctx);

		loopCount++;
    }

__FAILED:
    if (ctx->bAttachPool) {
        RK_MPI_VI_DetachMbPool(ctx->pipeId, ctx->channelId);
        RK_MPI_MB_DestroyPool(ctx->attachPool);
    }
    for (i = 0; i < u32DstCount; i++) {
        s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn[i]);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
        }
    }
    // 5. disable one chn
    s32Ret = RK_MPI_VI_DisableChn(ctx->pipeId, ctx->channelId);
    RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);

    for (i = 0; i < u32DstCount; i++) {
        s32Ret = RK_MPI_VENC_StopRecvFrame(ctx->stVencCfg[i].s32ChnId);
        if (s32Ret != RK_SUCCESS) {
            return s32Ret;
        }
        RK_LOGE("destroy enc chn:%d", ctx->stVencCfg[i].s32ChnId);
        s32Ret = RK_MPI_VENC_DestroyChn(ctx->stVencCfg[i].s32ChnId);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VENC_DestroyChn fail %x", s32Ret);
        }
    }

    // 6.disable dev(will diabled all chn)
    s32Ret = RK_MPI_VI_DisableDev(ctx->devId);
    RK_LOGE("RK_MPI_VI_DisableDev %x", s32Ret);
    for (i = 0; i < u32DstCount; i++) {
      if (ctx->stFrame[i].pstPack)
          free(ctx->stFrame[i].pstPack);
      if (ctx->stVencCfg[i].fp)
          fclose(ctx->stVencCfg[i].fp);
    }

	pthread_join(vi_getframe_thread, RK_NULL);
	//fclose(ctx->fpViChn);
    return s32Ret;
#else
    return 0;
#endif
}

static void mpi_vi_test_show_options(const TEST_VI_CTX_S *ctx) {
    RK_PRINT("cmd parse result:\n");

    RK_PRINT("output file open      : %d\n", ctx->stDebugFile.bCfg);
    RK_PRINT("yuv output file name  : %s/%s\n", ctx->stDebugFile.aFilePath, ctx->stDebugFile.aFileName);
    RK_PRINT("enc0 output file path : /%s/%s\n", ctx->stVencCfg[0].dstFilePath, ctx->stVencCfg[0].dstFileName);
    RK_PRINT("enc1 output file path : /%s/%s\n", ctx->stVencCfg[1].dstFilePath, ctx->stVencCfg[1].dstFileName);
    RK_PRINT("loop count            : %d\n", ctx->loopCountSet);
    RK_PRINT("enMode                : %d\n", ctx->enMode);
    RK_PRINT("dev                   : %d\n", ctx->devId);
    RK_PRINT("pipe                  : %d\n", ctx->pipeId);
    RK_PRINT("channel               : %d\n", ctx->channelId);
    RK_PRINT("width                 : %d\n", ctx->width);
    RK_PRINT("height                : %d\n", ctx->height);
    RK_PRINT("enCompressMode        : %d\n", ctx->enCompressMode);
    RK_PRINT("enMemoryType          : %d\n", ctx->stChnAttr.stIspOpt.enMemoryType);
    RK_PRINT("aEntityName           : %s\n", ctx->stChnAttr.stIspOpt.aEntityName);
    RK_PRINT("depth                 : %d\n", ctx->stChnAttr.u32Depth);
    RK_PRINT("enPixelFormat         : %d\n", ctx->stChnAttr.enPixelFormat);
    RK_PRINT("bFreeze               : %d\n", ctx->bFreeze);
    RK_PRINT("src_frame rate        : %d\n", ctx->stChnAttr.stFrameRate.s32SrcFrameRate);
    RK_PRINT("dst frame rate        : %d\n", ctx->stChnAttr.stFrameRate.s32DstFrameRate);
    RK_PRINT("out buf count         : %d\n", ctx->stChnAttr.stIspOpt.u32BufCount);
    RK_PRINT("bUserPicEnabled       : %d\n", ctx->bUserPicEnabled);
    RK_PRINT("bEnRgn                : %d\n", ctx->bEnRgn);
    RK_PRINT("rgn count             : %d\n", ctx->s32RgnCnt);
    RK_PRINT("rgn type              : %d\n", ctx->rgnType);
    RK_PRINT("mosaic block size     : %d\n", ctx->mosaicBlkSize);
    RK_PRINT("bGetConnecInfo        : %d\n", ctx->bGetConnecInfo);
    RK_PRINT("bGetEdid              : %d\n", ctx->bGetEdid);
    RK_PRINT("bSetEdid              : %d\n", ctx->bSetEdid);
    RK_PRINT("enCodecId             : %d\n", ctx->enCodecId);
    RK_PRINT("bNoUseLibv4l2         : %d\n", ctx->bNoUseLibv4l2);
    RK_PRINT("enable swcac          : %d\n", ctx->bEnSwcac);
    RK_PRINT("enable mirror         : %d\n", ctx->bMirror);
    RK_PRINT("enable flip           : %d\n", ctx->bFlip);
    RK_PRINT("enable get stream     : %d\n", ctx->bGetStream);
    RK_PRINT("enable second thread  : %d\n", ctx->bSecondThr);
    RK_PRINT("moduleLoopCnt         : %d\n", ctx->modTestCnt);
    RK_PRINT("wrap mode enable      : %d\n", ctx->stChnWrap.bEnable);
    RK_PRINT("wrap line             : %d\n", ctx->stChnWrap.u32BufLine);
    RK_PRINT("enable venc ref buf share : %d\n", ctx->bRefBufShare);
    RK_PRINT("enable combo          : %d\n", ctx->bCombo);
    RK_PRINT("enable eptz           : %d\n", ctx->bEptz);
    RK_PRINT("enable use ext chn API: %d\n", ctx->bUseExt);
}

static const char *const usages[] = {
    "\t0)./camera_pipeline or camera_pipeline -l xxx",
    RK_NULL,
};

int main(int argc, const char **argv) {
    RK_S32 i;
    RK_S32 s32Ret = RK_FAILURE;
    RK_CHAR strBuff[128] = {0};
    TEST_VI_CTX_S *ctx;
    ctx = reinterpret_cast<TEST_VI_CTX_S *>(malloc(sizeof(TEST_VI_CTX_S)));
    memset(ctx, 0, sizeof(TEST_VI_CTX_S));

    RK_S32 s32TestCount = 0;
    RK_S32 s32TestFinishQuit = 1;
    ctx->stChnAttr.stIspOpt.enCaptureType = VI_V4L2_CAPTURE_TYPE_VIDEO_CAPTURE;
    ctx->aEntityName = RK_NULL;
    ctx->stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
    ctx->bEnRgn = RK_FALSE;
    ctx->s32RgnCnt = 1;
    ctx->rgnType = RGN_BUTT;
    ctx->bRgnOnPipe = RK_FALSE;
    ctx->bEnSwcac = RK_FALSE;
    ctx->bGetStream = RK_TRUE;
    ctx->bSecondThr = RK_FALSE;
    ctx->modTestCnt = 1;
    ctx->u32BitRateKb = 10 * 1024;
    ctx->u32BitRateKbMin = 10 * 1024;
    ctx->u32BitRateKbMax = 10 * 1024;
    ctx->u32GopSize = 60;
    ctx->mosaicBlkSize = MOSAIC_BLK_SIZE_64;
    ctx->bEnOverlay = RK_FALSE;
    ctx->s32Snap = 1;
    ctx->maxWidth = 0;
    ctx->maxHeight = 0;
    ctx->bSaveVlogPath = NULL;

	ctx->width = 3840;
	ctx->height = 2160;
	ctx->stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
	ctx->channelId = 0;
	ctx->devId = 0;
	ctx->pipeId = ctx->devId;
	ctx->enMode = TEST_VI_MODE_BIND_VENC_MULTI;
	ctx->loopCountSet = 10000;
	ctx->u32DstCodec = RK_VIDEO_ID_HEVC;
	ctx->stDebugFile.bCfg = RK_TRUE;
	ctx->stChnAttr.u32Depth = 1;
	ctx->stChnAttr.stIspOpt.u32BufCount = 6;
	ctx->stChnAttr.stFrameRate.s32SrcFrameRate = 60;
	ctx->stChnAttr.stFrameRate.s32DstFrameRate = 60;

	RK_LOGE("test running enter!");

    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_GROUP("basic options:"),
        OPT_INTEGER('w', "width", &(ctx->width),
                    "set capture channel width(required, default 0)", NULL, 0, 0),
        OPT_INTEGER('h', "height", &(ctx->height),
                    "set capture channel height(required, default 0)", NULL, 0, 0),
        OPT_INTEGER('d', "dev", &(ctx->devId),
                    "set dev id(default 0)", NULL, 0, 0),
        OPT_INTEGER('p', "pipe", &(ctx->pipeId),
                    "set pipe id(default 0)", NULL, 0, 0),
        OPT_INTEGER('c', "channel", &(ctx->channelId),
                    "set channel id(default 1)", NULL, 0, 0),
        OPT_INTEGER('l', "loopcount", &(ctx->loopCountSet),
                    "set capture frame count(default 100)", NULL, 0, 0),
        OPT_INTEGER('C', "compressmode", &(ctx->enCompressMode),
                    "set capture compressmode(default 0; 0:MODE_NONE 1:AFBC_16x16)", NULL, 0, 0),
        OPT_INTEGER('o', "output", &(ctx->stDebugFile.bCfg),
                    "save output file, file at /data/test_<devid>_<pipeid>_<channelid>.bin"
                    " (default 0; 0:no save 1:save)", NULL, 0, 0),
        OPT_STRING('\0', "savepath", &(ctx->pSavePath),
                    "save output path, default:/data/"),
        OPT_INTEGER('m', "mode", &(ctx->enMode),
                    "test mode(default 1; 0:vi get&release frame 1:vi bind one venc(h264) \n\t"
                    "2:vi bind two venc(h264)) 3:vi bind vpss bind venc \n\t"
                    "4:vi bind vo 5:multi vi 6:vi get&release stream \n\t"
                    "7:vi bind vdec bind vo 8: vi bind ivs 13: vi multi chn, \n\t"
                    "14: vi eptz, 15: vi func test, 16:vi wrap mode switch.\n\t", NULL, 0, 0),
        OPT_INTEGER('t', "memorytype", &(ctx->stChnAttr.stIspOpt.enMemoryType),
                    "set the buf memorytype(required, default 4; 1:mmap(hdmiin/bt1120/sensor input) "
                    "2:userptr(invalid) 3:overlay(invalid) 4:dma(sensor))", NULL, 0, 0),
        OPT_STRING('n', "name", &(ctx->aEntityName),
                   "set the entityName (required, default null;\n\t"
                   "rv1126 sensor:rkispp_m_bypass rkispp_scale0 rkispp_scale1 rkispp_scale2;\n\t"
                   "rv1126 hdmiin/bt1120/sensor:/dev/videox such as /dev/video19 /dev/video20;\n\t"
                   "rk356x hdmiin/bt1120/sensor:/dev/videox such as /dev/video0 /dev/video1", NULL, 0, 0),
        OPT_INTEGER('D', "depth", &(ctx->stChnAttr.u32Depth),
                    "channel output depth, default{u32BufCount(not bind) or 0(bind venc/vpss/...)}", NULL, 0, 0),
        OPT_INTEGER('f', "format", &(ctx->stChnAttr.enPixelFormat),
                    "set the format(default 0; 0:RK_FMT_YUV420SP 10:RK_FMT_YUV422_UYVY"
                    "131080:RK_FMT_RGB_BAYER_SBGGR_12BPP)", NULL, 0, 0),
        OPT_INTEGER('\0', "freeze", &(ctx->bFreeze),
                    "freeze output(default 0; 0:disable freeze 1:enable freeze)", NULL, 0, 0),
        OPT_INTEGER('\0', "src_rate", &(ctx->stChnAttr.stFrameRate.s32SrcFrameRate),
                    "src frame rate(default -1; -1:not control; other:1-max_fps<isp_out_fps>)", NULL, 0, 0),
        OPT_INTEGER('\0', "dst_rate", &(ctx->stChnAttr.stFrameRate.s32DstFrameRate),
                    "dst frame rate(default -1; -1:not control; other:1-src_fps<src_rate>)", NULL, 0, 0),
        OPT_INTEGER('\0', "buf_count", &(ctx->stChnAttr.stIspOpt.u32BufCount),
                    "out buf count, range[1-8] default(3)", NULL, 0, 0),
        OPT_INTEGER('U', "user_pic", &(ctx->bUserPicEnabled),
                    "enable using user specify picture as vi input.", NULL, 0, 0),
        OPT_INTEGER('e', "use_ext", &(ctx->bUseExt),
                    "enable use channel ext api and pipe (default(0))", NULL, 0, 0),
        OPT_INTEGER('\0', "rgn_pipe", &(ctx->bRgnOnPipe),
                    "enable rgn on pipe (default(0))", NULL, 0, 0),
        OPT_INTEGER('\0', "rgn_type", &(ctx->rgnType),
                    "rgn type. 0:overlay, 1:overlayEx,2:cover,3:coverEx,4:mosaic,5:moscaiEx", NULL, 0, 0),
        OPT_INTEGER('\0', "rgn_cnt", &(ctx->s32RgnCnt),
                    "rgn count. default(1),max:8", NULL, 0, 0),
        OPT_INTEGER('\0', "en_rgn", &(ctx->bEnRgn),
                    "enable rgn. default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "get_connect_info", &(ctx->bGetConnecInfo),
                    "get connect info. default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "get_edid", &(ctx->bGetEdid),
                    "get edid. default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "set_edid", &(ctx->bSetEdid),
                    "set edid. default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "stream_codec", &(ctx->enCodecId),
                    "stream codec(0:disable 8:h264, 9:mjpeg, 12:h265). default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "bNoUseLibv4l2", &(ctx->bNoUseLibv4l2), "vi if no use libv4l2, 0: use, 1: no use."
                    "dafault(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "en_swcac", &(ctx->bEnSwcac),
                    "enable swcac. default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "ivs", &(ctx->u32Ivs),
                    "ivs(MD, OD) enable, default(0: disable, 1: low, 2: mid, 3: high)", NULL, 0, 0),
        OPT_INTEGER('\0', "ivs_debug", &(ctx->bIvsDebug),
                    "ivs debug enable, default(0: disable, 1: enable)", NULL, 0, 0),
        OPT_INTEGER('\0', "mirror", &(ctx->bMirror),
                    "picture mirror, default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "flip", &(ctx->bFlip),
                    "picture flip, default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "en_overlay", &(ctx->bEnOverlay),
                    "enable overlay. default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "wrap", &(ctx->stChnWrap.bEnable),
                    "wrap enable(0: disable, 1: enable). default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "wrap_line", &(ctx->stChnWrap.u32BufLine),
                    "set wrap line, range [128-H]", NULL, 0, 0),
        OPT_INTEGER('\0', "en_eptz", &(ctx->bEptz),
                    "enable eptz (default(0))", NULL, 0, 0),
        OPT_INTEGER('\0', "maxWidth", &(ctx->maxWidth),
                    "config max resolution width(<= sensor max resolution width)", NULL, 0, 0),
        OPT_INTEGER('\0', "maxHeight", &(ctx->maxHeight),
                    "config max resolution height(<= sensor max resolution height)", NULL, 0, 0),
        OPT_INTEGER('\0', "combo", &(ctx->bCombo),
                    "combo enable(0: disable, 1: enable). default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "snap", &(ctx->s32Snap),
                    "snap jpeg num(-1: save all, > 0: save pic num). default(1)", NULL, 0, 0),
        OPT_INTEGER('\0', "venc_ref_buf_share", &(ctx->bRefBufShare),
                    "enable venc ref buf share or not, default(0), 0: RK_FALSE, 1: RK_TRUE", NULL, 0, 0),
        OPT_INTEGER('\0', "de_breath", &(ctx->u32DeBreath),
                    "debreath[0, 35] default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "svc", &(ctx->bSvc),
                    "svc(smart video coding) enable, default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "gop_size", &(ctx->u32GopSize),
                    "gop size(range >= 1 default(60))", NULL, 0, 0),
        OPT_INTEGER('\0', "codec", &(ctx->u32DstCodec),
                     "venc encode codec(8:h264, 9:mjpeg, 12:h265, 15:jpeg, ...). default(12)", NULL, 0, 0),
         OPT_INTEGER('b', "bit_rate", &(ctx->u32BitRateKb),
                    "bit rate kbps(h264/h265:range[3-200000],jpeg/mjpeg:range[5-800000] default(10*1024kb))",
                    NULL, 0, 0),
        OPT_INTEGER('\0', "bit_rate_max", &(ctx->u32BitRateKbMax),
                    "bit rate kbps max(vbr/avbr valid)(h264/h265:range[3-200000];"
                    "jpeg/mjpeg:range[5-800000] default(0:auto calcu))",
                     NULL, 0, 0),
        OPT_INTEGER('\0', "bit_rate_min", &(ctx->u32BitRateKbMin),
                    "bit rate kbps min(vbr/avbr valid)(h264/h265:range[3-200000];"
                    "jpeg/mjpeg:range[5-800000] default(0:auto calcu))",
                     NULL, 0, 0),
        OPT_INTEGER('\0', "snap", &(ctx->s32Snap),
                    "snap jpeg num(-1: save all, > 0: save pic num). default(1)", NULL, 0, 0),
        OPT_INTEGER('\0', "attach_pool", &(ctx->bAttachPool),
                    "enable attach usr pool, default(0), 0: RK_FALSE, 1: RK_TRUE", NULL, 0, 0),
        OPT_INTEGER('\0', "getstream", &(ctx->bGetStream),
                    "ebable get vi stream,default(1)", NULL, 0, 0),
        OPT_INTEGER('\0', "en_thr", &(ctx->bSecondThr),
                    "ebable second thread to get vi stream,default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "test_finish_quit", &(s32TestFinishQuit),
                    "0: block, 1: quit, default: 1", NULL, 0, 0),
        OPT_INTEGER('\0', "module_test_type", &g_ModeTestType,
                    "module test type (0:pause/resume;1:mirr/flip)", NULL, 0, 0),
        OPT_INTEGER('\0', "blksize", &(ctx->mosaicBlkSize),
                    "rgn mosaic effect ctl: block size"
                    " (default(0) 0:8x8,1:16x16,2:32x32,3:64x64)", NULL, 0, 0),
        OPT_INTEGER('L', "vi_init_deinit_loop", &(ctx->modTestCnt),
                    "vi init deinit test loop, default(1)", NULL, 0, 0),
        OPT_INTEGER('\0', "module_test_loop", &g_ModeTestCnt,
                    "module test loop count (default(100))", NULL, 0, 0),
        OPT_STRING('\0', "save_vlog_path", &(ctx->bSaveVlogPath), \
                    "when process exit, get vlog and save to <save_vlog_path>", NULL, 0, 0),
        OPT_END(),
    };

    struct argparse argparse;
    argparse_init(&argparse, options, usages, 0);
    argparse_describe(&argparse, "\nselect a test case to run.",
                                 "\nuse --help for details.");
    argc = argparse_parse(&argparse, argc, argv);

    if (!ctx->width || !ctx->height) {
        argparse_usage(&argparse);
        goto __FAILED2;
    }

    if (ctx->pipeId != ctx->devId)
        ctx->pipeId = ctx->devId;

    if (ctx->stDebugFile.bCfg) {
        ctx->stVencCfg[0].bOutDebugCfg = ctx->stDebugFile.bCfg;
        ctx->stVencCfg[1].bOutDebugCfg = ctx->stDebugFile.bCfg;
        if (ctx->pSavePath)
            memcpy(&ctx->stDebugFile.aFilePath, ctx->pSavePath, strlen(ctx->pSavePath));
        else
            memcpy(&ctx->stDebugFile.aFilePath, "/data/", strlen("/data/"));
        snprintf(ctx->stDebugFile.aFileName, MAX_VI_FILE_PATH_LEN,
                 "test_%d_%d_%d.bin", ctx->devId, ctx->pipeId, ctx->channelId);

		ctx->fpViChn = fopen("/data/vi_chn.yuv", "wb");
		if (ctx->fpViChn == RK_NULL) {
			RK_LOGE("can not open /data/vi_chn.yuv\n");
            goto __FAILED;
		}
		
    }
    for (i = 0; i < TEST_VENC_MAX; i++) {
        if (ctx->stVencCfg[i].bOutDebugCfg) {			
            char name[256] = {0};
            snprintf(ctx->stVencCfg[i].dstFileName, sizeof(ctx->stVencCfg[i].dstFileName),
                   "venc_%d_%s.h265", i, i==0? "4K60":"1080P60");
            snprintf(name, sizeof(name), "/%s/%s",
                     ctx->stDebugFile.aFilePath, ctx->stVencCfg[i].dstFileName);
            ctx->stVencCfg[i].fp = fopen(name, "wb");
            if (ctx->stVencCfg[i].fp == RK_NULL) {
                RK_LOGE("chn %d can't open file %s in get picture thread!\n", i, name);
                goto __FAILED;
            }
        }
    }

    RK_LOGE("test running enter ctx->aEntityName=%s!", ctx->aEntityName);
    if (ctx->aEntityName != RK_NULL)
        memcpy(ctx->stChnAttr.stIspOpt.aEntityName, ctx->aEntityName, strlen(ctx->aEntityName));

    if (ctx->pipeId != ctx->devId)
        ctx->pipeId = ctx->devId;

    mpi_vi_test_show_options(ctx);

    if (RK_MPI_SYS_Init() != RK_SUCCESS) {
        RK_LOGE("rk mpi sys init fail!");
        goto __FAILED;
    }

    s32Ret = test_vi_bind_venc_loop(ctx);

    if (!s32TestFinishQuit) {
        while(bBlock) {
            sleep(1);
        }
    }
__FAILED:
    RK_LOGE("test running exit:%d", s32Ret);
    if (ctx->bSaveVlogPath) {
        memset(strBuff, 0, sizeof(strBuff));
        sprintf(strBuff, "dumpsys cat /dev/mpi/vlog > %s", ctx->bSaveVlogPath);
        system(strBuff);
        RK_LOGI("save vlog info to %s", ctx->bSaveVlogPath);
    }
    RK_MPI_SYS_Exit();
__FAILED2:
    if (ctx) {
        free(ctx);
        ctx = RK_NULL;
    }

    return 0;
}