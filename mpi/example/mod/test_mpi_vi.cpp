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

RK_U8 test_edid[2][128] = {
    {
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x49, 0x70, 0x88, 0x35, 0x01, 0x00, 0x00, 0x00,
        0x2d, 0x1f, 0x01, 0x03, 0x80, 0x78, 0x44, 0x78, 0x0a, 0xcf, 0x74, 0xa3, 0x57, 0x4c, 0xb0, 0x23,
        0x09, 0x48, 0x4c, 0x21, 0x08, 0x00, 0x61, 0x40, 0x01, 0x01, 0x81, 0x00, 0x95, 0x00, 0xa9, 0xc0,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x08, 0xe8, 0x00, 0x30, 0xf2, 0x70, 0x5a, 0x80, 0xb0, 0x58,
        0x8a, 0x00, 0xc4, 0x8e, 0x21, 0x00, 0x00, 0x1e, 0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40,
        0x58, 0x2c, 0x45, 0x00, 0xb9, 0xa8, 0x42, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x52,
        0x4b, 0x2d, 0x55, 0x48, 0x44, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xfd,
        0x00, 0x3b, 0x46, 0x1f, 0x8c, 0x3c, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0xa3
    },
    {
        0x02, 0x03, 0x39, 0xd2, 0x51, 0x07, 0x16, 0x14, 0x05, 0x01, 0x03, 0x12, 0x13, 0x84, 0x22, 0x1f,
        0x90, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x23, 0x09, 0x07, 0x07, 0x83, 0x01, 0x00, 0x00, 0x66, 0x03,
        0x0c, 0x00, 0x30, 0x00, 0x10, 0x67, 0xd8, 0x5d, 0xc4, 0x01, 0x78, 0xc8, 0x07, 0xe3, 0x05, 0x03,
        0x01, 0xe4, 0x0f, 0x00, 0xf0, 0x01, 0xe2, 0x00, 0xff, 0x08, 0xe8, 0x00, 0x30, 0xf2, 0x70, 0x5a,
        0x80, 0xb0, 0x58, 0x8a, 0x00, 0xc4, 0x8e, 0x21, 0x00, 0x00, 0x1e, 0x02, 0x3a, 0x80, 0x18, 0x71,
        0x38, 0x2d, 0x40, 0x58, 0x2c, 0x45, 0x00, 0xb9, 0xa8, 0x42, 0x00, 0x00, 0x9e, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd1
    }
};

static RK_S32 create_vi(TEST_VI_CTX_S *ctx);
static RK_S32 destroy_vi(TEST_VI_CTX_S *ctx);

static RK_S32 create_ivs(TEST_VI_CTX_S *ctx) {
    IVS_CHN_ATTR_S attr;
    memset(&attr, 0, sizeof(attr));
    attr.enMode = IVS_MODE_MD_OD;
    attr.u32PicWidth = ctx->width;
    attr.u32PicHeight = ctx->height;
    attr.u32MaxWidth = ctx->width;
    attr.u32MaxHeight = ctx->height;
    attr.enPixelFormat = ctx->stChnAttr.enPixelFormat;
    attr.s32Gop = 30;
    attr.bSmearEnable = RK_FALSE;
    attr.bWeightpEnable = RK_FALSE;
    attr.bMDEnable = RK_TRUE;
    attr.s32MDInterval = 1;
    attr.bMDNightMode = RK_FALSE;
    attr.u32MDSensibility = ctx->u32Ivs;
    attr.bODEnable = RK_TRUE;
    attr.s32ODInterval = 1;
    attr.s32ODPercent = 7;

    return RK_MPI_IVS_CreateChn(0, &attr);
}

static RK_S32 destroy_ivs(RK_VOID) {
    RK_MPI_IVS_DestroyChn(0);
    return RK_SUCCESS;
}

static RK_S32 create_vpss(VPSS_CFG_S *pstVpssCfg, RK_S32 s32Grp, RK_S32 s32OutChnNum) {
    RK_S32 s32Ret = RK_SUCCESS;
    VPSS_CHN VpssChn[VPSS_MAX_CHN_NUM] = { VPSS_CHN0, VPSS_CHN1, VPSS_CHN2, VPSS_CHN3 };
    VPSS_CROP_INFO_S stCropInfo;

    s32Ret = RK_MPI_VPSS_CreateGrp(s32Grp, &pstVpssCfg->stGrpVpssAttr);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    for (RK_S32 i = 0; i < s32OutChnNum; i++) {
        s32Ret = RK_MPI_VPSS_SetChnAttr(s32Grp, VpssChn[i], &pstVpssCfg->stVpssChnAttr[i]);
        if (s32Ret != RK_SUCCESS) {
            return s32Ret;
        }
        s32Ret = RK_MPI_VPSS_EnableChn(s32Grp, VpssChn[i]);
        if (s32Ret != RK_SUCCESS) {
            return s32Ret;
        }
    }

    s32Ret = RK_MPI_VPSS_EnableBackupFrame(s32Grp);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    s32Ret = RK_MPI_VPSS_StartGrp(s32Grp);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    return  RK_SUCCESS;
}

static RK_S32 destory_vpss(RK_S32 s32Grp, RK_S32 s32OutChnNum) {
    RK_S32 s32Ret = RK_SUCCESS;
    VPSS_CHN VpssChn[VPSS_MAX_CHN_NUM] = { VPSS_CHN0, VPSS_CHN1, VPSS_CHN2, VPSS_CHN3 };

    s32Ret = RK_MPI_VPSS_StopGrp(s32Grp);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    for (RK_S32 i = 0; i < s32OutChnNum; i++) {
        s32Ret = RK_MPI_VPSS_DisableChn(s32Grp, VpssChn[i]);
        if (s32Ret != RK_SUCCESS) {
            return s32Ret;
        }
    }

    s32Ret = RK_MPI_VPSS_DisableBackupFrame(s32Grp);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    s32Ret = RK_MPI_VPSS_DestroyGrp(s32Grp);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    return  RK_SUCCESS;
}

static RK_S32 create_overlay(TEST_VI_CTX_S *ctx, RK_U32 u32DstCount) {
    RK_S32 s32Ret = RK_SUCCESS;
    MPP_CHN_S stDestChn[TEST_VENC_MAX];
    RGN_CANVAS_INFO_S stCanvasInfo;
    RGN_HANDLE RgnHandle = 0;

    ctx->stViRgn.stRgnAttr.enType = OVERLAY_RGN;
    ctx->stViRgn.stRgnAttr.unAttr.stOverlay.u32ClutNum = 0;
    ctx->stViRgn.stRgnAttr.unAttr.stOverlay.stSize.u32Width = 512;
    ctx->stViRgn.stRgnAttr.unAttr.stOverlay.stSize.u32Height = 512;
    ctx->stViRgn.stRgnAttr.unAttr.stOverlay.enPixelFmt = RK_FMT_ARGB8888;
    s32Ret = RK_MPI_RGN_Create(RgnHandle, &ctx->stViRgn.stRgnAttr);
    if (RK_SUCCESS != s32Ret) {
        RK_LOGE("RK_MPI_RGN_Create (%d) failed with %#x!", RgnHandle, s32Ret);
        RK_MPI_RGN_Destroy(RgnHandle);
        return RK_FAILURE;
    }
    RK_LOGI("The handle: %d, create success!", RgnHandle);


    for (RK_S32 i = 0; i < u32DstCount; i++) {
        stDestChn[i].enModId   = RK_ID_VENC;
        stDestChn[i].s32DevId  = 0;
        stDestChn[i].s32ChnId  = ctx->stVencCfg[i].s32ChnId;
        ctx->stViRgn.stRgnChnAttr.bShow = RK_TRUE;
        ctx->stViRgn.stRgnChnAttr.enType = OVERLAY_RGN;
        ctx->stViRgn.stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = 0;
        ctx->stViRgn.stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = 0;
        ctx->stViRgn.stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 128;
        ctx->stViRgn.stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 128;
        ctx->stViRgn.stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = i;
        ctx->stViRgn.stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.bEnable = RK_FALSE;
        ctx->stViRgn.stRgnChnAttr.unChnAttr.stOverlayChn.stInvertColor.bInvColEn = RK_FALSE;
        s32Ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stDestChn[i], &ctx->stViRgn.stRgnChnAttr);
        if (RK_SUCCESS != s32Ret) {
            RK_LOGE("RK_MPI_RGN_AttachToChn (%d) failed with %#x!", RgnHandle, s32Ret);
            return RK_FAILURE;
        }
    }

    memset(&stCanvasInfo, 0, sizeof(RGN_CANVAS_INFO_S));

    s32Ret = RK_MPI_RGN_GetCanvasInfo(RgnHandle, &stCanvasInfo);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_RGN_GetCanvasInfo failed with %#x!", s32Ret);
        return RK_FAILURE;
    }
    memset(reinterpret_cast<void *>(stCanvasInfo.u64VirAddr), 0xff,
            stCanvasInfo.u32VirWidth * stCanvasInfo.u32VirHeight * 4);
    s32Ret = RK_MPI_RGN_UpdateCanvas(RgnHandle);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_RGN_UpdateCanvas failed with %#x!", s32Ret);
        return RK_FAILURE;
    }

    return RK_SUCCESS;
}

static RK_S32 resize_overlay(TEST_VI_CTX_S *ctx, RK_U32 u32DstCount, SIZE_S size) {
    RK_S32 s32Ret = RK_SUCCESS;
    MPP_CHN_S stDestChn[TEST_VENC_MAX];
    RGN_CANVAS_INFO_S stCanvasInfo;
    RGN_ATTR_S stRgnAttr;
    RGN_HANDLE RgnHandle = 0;

    for (RK_S32 i = 0; i < u32DstCount; i++) {
        stDestChn[i].enModId   = RK_ID_VENC;
        stDestChn[i].s32DevId  = 0;
        stDestChn[i].s32ChnId  = ctx->stVencCfg[i].s32ChnId;
        s32Ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stDestChn[i]);
        if (RK_SUCCESS != s32Ret) {
            RK_LOGE("RK_MPI_RGN_DetachFrmChn (%d) failed with %#x!", RgnHandle, s32Ret);
            return RK_FAILURE;
        }
    }

    s32Ret = RK_MPI_RGN_GetAttr(RgnHandle, &stRgnAttr);
    if (RK_SUCCESS != s32Ret) {
        RK_LOGE("RK_MPI_RGN_GetAttr (%d) failed with %#x!", RgnHandle, s32Ret);
        return RK_FAILURE;
    }

    stRgnAttr.unAttr.stOverlay.stSize.u32Width  = size.u32Width;
    stRgnAttr.unAttr.stOverlay.stSize.u32Height = size.u32Height;
    s32Ret = RK_MPI_RGN_SetAttr(RgnHandle, &stRgnAttr);
    if (RK_SUCCESS != s32Ret) {
        RK_LOGE("RK_MPI_RGN_SetAttr (%d) failed with %#x!", RgnHandle, s32Ret);
        return RK_FAILURE;
    }

    for (RK_S32 i = 0; i < u32DstCount; i++) {
        stDestChn[i].enModId   = RK_ID_VENC;
        stDestChn[i].s32DevId  = 0;
        stDestChn[i].s32ChnId  = ctx->stVencCfg[i].s32ChnId;
        s32Ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stDestChn[i], &ctx->stViRgn.stRgnChnAttr);
        if (RK_SUCCESS != s32Ret) {
            RK_LOGE("RK_MPI_RGN_AttachToChn (%d) failed with %#x!", RgnHandle, s32Ret);
            return RK_FAILURE;
        }
    }

    memset(&stCanvasInfo, 0, sizeof(RGN_CANVAS_INFO_S));
    s32Ret = RK_MPI_RGN_GetCanvasInfo(RgnHandle, &stCanvasInfo);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_RGN_GetCanvasInfo failed with %#x!", s32Ret);
        return RK_FAILURE;
    }
    memset(reinterpret_cast<void *>(stCanvasInfo.u64VirAddr), 0xff,
            stCanvasInfo.u32VirWidth * stCanvasInfo.u32VirHeight * 4);
    s32Ret = RK_MPI_RGN_UpdateCanvas(RgnHandle);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_RGN_UpdateCanvas failed with %#x!", s32Ret);
        return RK_FAILURE;
    }

    return RK_SUCCESS;
}

static RK_S32 destroy_overlay(TEST_VI_CTX_S *ctx, RK_U32 u32DstCount) {
    RK_S32 s32Ret = RK_SUCCESS;
    MPP_CHN_S stDestChn[TEST_VENC_MAX];
    RGN_HANDLE RgnHandle = 0;

    for (RK_S32 i = 0; i < u32DstCount; i++) {
        stDestChn[i].enModId   = RK_ID_VENC;
        stDestChn[i].s32DevId  = 0;
        stDestChn[i].s32ChnId  = ctx->stVencCfg[i].s32ChnId;
        s32Ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stDestChn[i]);
        if (RK_SUCCESS != s32Ret) {
            RK_LOGE("RK_MPI_RGN_DetachFrmChn (%d) failed with %#x!", RgnHandle, s32Ret);
            return RK_FAILURE;
        }
    }

    s32Ret = RK_MPI_RGN_Destroy(RgnHandle);
    if (RK_SUCCESS != s32Ret) {
        RK_LOGE("RK_MPI_RGN_Destroy [%d] failed with %#x", RgnHandle, s32Ret);
    }

    return RK_SUCCESS;
}

static RK_S32 create_venc(TEST_VI_CTX_S *ctx, RK_U32 u32Ch) {
#ifdef HAVE_API_MPI_VENC
    VENC_RECV_PIC_PARAM_S stRecvParam;
    VENC_CHN_BUF_WRAP_S stVencChnBufWrap;
    VENC_CHN_REF_BUF_SHARE_S stVencChnRefBufShare;

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
        ctx->stVencCfg[u32Ch].stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
        ctx->stVencCfg[u32Ch].stAttr.stRcAttr.stH264Vbr.u32BitRate = ctx->u32BitRateKb;
        ctx->stVencCfg[u32Ch].stAttr.stRcAttr.stH264Vbr.u32MinBitRate = ctx->u32BitRateKbMin;
        ctx->stVencCfg[u32Ch].stAttr.stRcAttr.stH264Vbr.u32MaxBitRate = ctx->u32BitRateKbMax;
    } else {
        ctx->stVencCfg[u32Ch].stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
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

static RK_S32 create_vo(TEST_VI_CTX_S *ctx, RK_U32 u32Ch) {
#ifdef HAVE_API_MPI_VO
    /* Enable VO */
    VO_PUB_ATTR_S VoPubAttr;
    VO_VIDEO_LAYER_ATTR_S stLayerAttr;
    RK_S32 s32Ret = RK_SUCCESS;
    VO_CHN_ATTR_S stChnAttr;
    VO_LAYER VoLayer = ctx->s32VoLayer;
    VO_DEV VoDev = ctx->s32VoDev;

    RK_MPI_VO_DisableLayer(VoLayer);
    RK_MPI_VO_DisableLayer(RK356X_VOP_LAYER_ESMART_0);
    RK_MPI_VO_DisableLayer(RK356X_VOP_LAYER_ESMART_1);
    RK_MPI_VO_DisableLayer(RK356X_VOP_LAYER_SMART_0);
    RK_MPI_VO_DisableLayer(RK356X_VOP_LAYER_SMART_1);
    RK_MPI_VO_Disable(VoDev);

    memset(&VoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
    memset(&stLayerAttr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));

    stLayerAttr.enPixFormat = RK_FMT_YUV420SP;
    stLayerAttr.stDispRect.s32X = 0;
    stLayerAttr.stDispRect.s32Y = 0;
    stLayerAttr.u32DispFrmRt = 30;
    stLayerAttr.stDispRect.u32Width = 1920;
    stLayerAttr.stDispRect.u32Height = 1080;
    stLayerAttr.stImageSize.u32Width = 1920;
    stLayerAttr.stImageSize.u32Height = 1080;

    s32Ret = RK_MPI_VO_GetPubAttr(VoDev, &VoPubAttr);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    VoPubAttr.enIntfType = VO_INTF_HDMI;
    VoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;

    s32Ret = RK_MPI_VO_SetPubAttr(VoDev, &VoPubAttr);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }
    s32Ret = RK_MPI_VO_Enable(VoDev);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    s32Ret = RK_MPI_VO_BindLayer(VoLayer, VoDev, VO_LAYER_MODE_VIDEO);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VO_BindLayer failed,s32Ret:%d\n", s32Ret);
        return RK_FAILURE;
    }

    s32Ret = RK_MPI_VO_SetLayerAttr(VoLayer, &stLayerAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VO_SetLayerAttr failed,s32Ret:%d\n", s32Ret);
        return RK_FAILURE;
    }

    s32Ret = RK_MPI_VO_EnableLayer(VoLayer);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VO_EnableLayer failed,s32Ret:%d\n", s32Ret);
        return RK_FAILURE;
    }

    stChnAttr.stRect.s32X = 0;
    stChnAttr.stRect.s32Y = 0;
    stChnAttr.stRect.u32Width = stLayerAttr.stImageSize.u32Width;
    stChnAttr.stRect.u32Height = stLayerAttr.stImageSize.u32Height;
    stChnAttr.u32Priority = 0;
    stChnAttr.u32FgAlpha = 128;
    stChnAttr.u32BgAlpha = 0;

    s32Ret = RK_MPI_VO_SetChnAttr(VoLayer, u32Ch, &stChnAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("set chn Attr failed,s32Ret:%d\n", s32Ret);
        return RK_FAILURE;
    }

    return s32Ret;
#else
    return 0;
#endif
}

RK_S32 test_vi_poll_event(RK_S32 timeoutMsec, RK_S32 fd) {
    RK_S32 num_fds = 1;
    struct pollfd pollFds[num_fds];
    RK_S32 ret = 0;

    RK_ASSERT(fd > 0);
    memset(pollFds, 0, sizeof(pollFds));
    pollFds[0].fd = fd;
    pollFds[0].events = (POLLPRI | POLLIN | POLLERR | POLLNVAL | POLLHUP);

    ret = poll(pollFds, num_fds, timeoutMsec);

    if (ret > 0 && (pollFds[0].revents & (POLLERR | POLLNVAL | POLLHUP))) {
        RK_LOGE("fd: %d polled error", fd);
        return -1;
    }

    return ret;
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

static RK_S32 test_vi_hdmi_rx_infomation(TEST_VI_CTX_S *ctx) {
    RK_S32 s32Ret = RK_FAILURE;

    /* test get connect info before enable chn(need after RK_MPI_VI_SetChnAttr)*/
    if (ctx->bGetConnecInfo) {
        VI_CONNECT_INFO_S stConnectInfo;
        s32Ret = RK_MPI_VI_GetChnConnectInfo(ctx->pipeId, ctx->channelId, &stConnectInfo);
        RK_LOGE("RK_MPI_VI_GetChnConnectInfo %x, w:%d,h:%d, fmt:0x%x, connect:%d, frameRate:%d",
                 s32Ret,
                 stConnectInfo.u32Width, stConnectInfo.u32Height,
                 stConnectInfo.enPixFmt, stConnectInfo.enConnect,
                 stConnectInfo.f32FrameRate);
    }
    if (ctx->bGetEdid) {
        VI_EDID_S stEdid;
        memset(&stEdid, 0, sizeof(VI_EDID_S));
        stEdid.u32Blocks = 3;
        stEdid.pu8Edid = (RK_U8 *)calloc(128 * stEdid.u32Blocks, 1);
        s32Ret = RK_MPI_VI_GetChnEdid(ctx->pipeId, ctx->channelId, &stEdid);
        RK_LOGE("RK_MPI_VI_GetChnEdid %x, stEdid.u32Blocks=%d "
                 "edid[0]:0x%x,edid[1]:0x%x,edid[2]:0x%x,edid[3]:0x%x,edid[126]:0x%x edid[127]:0x%x",
                 s32Ret, stEdid.u32Blocks,
                 stEdid.pu8Edid[0], stEdid.pu8Edid[1],
                 stEdid.pu8Edid[2], stEdid.pu8Edid[3],
                 stEdid.pu8Edid[126], stEdid.pu8Edid[127]);
        free(stEdid.pu8Edid);
    }
    if (ctx->bSetEdid) {
        VI_EDID_S stEdid;
        memset(&stEdid, 0, sizeof(VI_EDID_S));
        stEdid.u32Blocks = 2;
        stEdid.pu8Edid = (RK_U8 *)test_edid;
        s32Ret = RK_MPI_VI_SetChnEdid(ctx->pipeId, ctx->channelId, &stEdid);
        RK_LOGE("RK_MPI_VI_SetChnEdid %x, edid[0]:0x%x,edid[1]:0x%x,edid[2]:0x%x,edid[3]:0x%x,"
                "edid[126]:0x%x edid[127]:0x%x",
                 s32Ret,
                 stEdid.pu8Edid[0], stEdid.pu8Edid[1],
                 stEdid.pu8Edid[2], stEdid.pu8Edid[3],
                 stEdid.pu8Edid[126], stEdid.pu8Edid[127]);
    }

    return s32Ret;
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

static void test_vi_event_source_change(TEST_VI_CTX_S *ctx) {
    if (ctx) {
        VI_CONNECT_INFO_S stConnectInfo;
        RK_MPI_VI_GetChnConnectInfo(ctx->pipeId, ctx->channelId, &stConnectInfo);
        if (stConnectInfo.u32Width == ctx->width &&
            stConnectInfo.u32Height == ctx->height &&
            stConnectInfo.enPixFmt == ctx->stChnAttr.enPixelFormat) {  // not change, just need pause/resume.
            RK_LOGI("reset vi!");
            RK_MPI_VI_PauseChn(ctx->pipeId, ctx->channelId);
            RK_MPI_VI_ResumeChn(ctx->pipeId, ctx->channelId);
        } else {  // TODO(user): need rebuild vi when w/h/format change.
            RK_LOGE("need signal to rebuild vi!");
        }
    }
}

static void test_vi_event_call_back(RK_VOID *pPrivateData, VI_CB_INFO_S *info) {
    TEST_VI_CTX_S *ctx = (TEST_VI_CTX_S *)pPrivateData;

    if (info) {
        if (info->u32Event & VI_EVENT_CONNECT_CHANGE)
            RK_LOGI("event connect change");
        if (info->u32Event & VI_EVENT_SOURCE_CHANGE) {
            RK_LOGI("event source change");
            test_vi_event_source_change(ctx);
        }
    }
}

static RK_S32 test_vi_set_event_call_back(TEST_VI_CTX_S *ctx) {
    RK_S32 s32Ret = RK_FAILURE;
    VI_EVENT_CALL_BACK_S stCallbackFunc;

    stCallbackFunc.pfnCallback = test_vi_event_call_back;
    stCallbackFunc.pPrivateData = ctx;

    /* test set change event call back(need after RK_MPI_VI_SetChnAttr)*/
    s32Ret = RK_MPI_VI_RegChnEventCallback(ctx->pipeId, ctx->channelId, &stCallbackFunc);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VI_RegChnEventCallback %x", s32Ret);
        return s32Ret;
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

    test_vi_hdmi_rx_infomation(ctx);
    test_vi_set_stream_codec(ctx);
    // test_vi_set_event_call_back(ctx);

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
    if (ctx->stDebugFile.bCfg) {
        s32Ret = RK_MPI_VI_ChnSaveFile(ctx->pipeId, ctx->channelId, &ctx->stDebugFile);
        if (s32Ret)
            RK_LOGE("RK_MPI_VI_ChnSaveFile %x", s32Ret);
    }

__FAILED:
    return s32Ret;
}

static RK_S32 test_vi_eptz_vi_only(TEST_VI_CTX_S *ctx) {
    RK_S32 s32Ret;
    RK_S32 loopCount = 0;
    RK_S32 waitTime = 100;

    /* test use getframe&release_frame */
    s32Ret = test_vi_init(ctx);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("vi %d:%d init failed:%x", ctx->devId, ctx->channelId, s32Ret);
        goto __FAILED;
    }

    while (loopCount < ctx->loopCountSet) {
        // 5.get the frame
        s32Ret = RK_MPI_VI_GetChnFrame(ctx->pipeId, ctx->channelId, &ctx->stViFrame, waitTime);
        if (s32Ret == RK_SUCCESS) {
            RK_U64 nowUs = TEST_COMM_GetNowUs();
            void *data = RK_MPI_MB_Handle2VirAddr(ctx->stViFrame.stVFrame.pMbBlk);
            RK_LOGD("RK_MPI_VI_GetChnFrame ok:data %p loop:%d seq:%d pts:%lld ms len=%d", data, loopCount,
                     ctx->stViFrame.stVFrame.u32TimeRef, ctx->stViFrame.stVFrame.u64PTS / 1000,
                     ctx->stViFrame.stVFrame.u64PrivateData);
            // 6.get the channel status
            s32Ret = RK_MPI_VI_QueryChnStatus(ctx->pipeId, ctx->channelId, &ctx->stChnStatus);
            RK_LOGD("RK_MPI_VI_QueryChnStatus ret %x, w:%d,h:%d,enable:%d," \
                    "current frame id:%d,input lost:%d,output lost:%d," \
                    "framerate:%d,vbfail:%d delay=%lldus",
                     s32Ret,
                     ctx->stChnStatus.stSize.u32Width,
                     ctx->stChnStatus.stSize.u32Height,
                     ctx->stChnStatus.bEnable,
                     ctx->stChnStatus.u32CurFrameID,
                     ctx->stChnStatus.u32InputLostFrame,
                     ctx->stChnStatus.u32OutputLostFrame,
                     ctx->stChnStatus.u32FrameRate,
                     ctx->stChnStatus.u32VbFail,
                     nowUs - ctx->stViFrame.stVFrame.u64PTS);
            // 7.release the frame
            s32Ret = RK_MPI_VI_ReleaseChnFrame(ctx->pipeId, ctx->channelId, &ctx->stViFrame);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
            }
            loopCount++;

            {   // do eptz process
                RK_BOOL bDoCrop = RK_FALSE;
                VI_CROP_INFO_S stCropInfo;
                memset(&stCropInfo, 0, sizeof(VI_CROP_INFO_S));
                stCropInfo.bEnable = RK_TRUE;
                stCropInfo.enCropCoordinate = VI_CROP_ABS_COOR;
                RK_U32 x, y, w, h;

                switch (loopCount) {
                    case 2:    // crop to 1080p
                        x = 0; y = 0; w = 1920; h = 1080;
                        bDoCrop = RK_TRUE;
                    break;

                    case 4:    // crop to 720
                        x = 0; y = 0; w = 1280; h = 720;
                        bDoCrop = RK_TRUE;
                    break;

                    case 6:    // crop to 576p
                        x = 0; y = 0; w = 720;  h = 576;
                        bDoCrop = RK_TRUE;
                    break;

                    case 8:    // crop to 480p
                        x = 0; y = 0; w = 640;  h = 480;
                        bDoCrop = RK_TRUE;
                    break;
                }

                if (bDoCrop) {
                    stCropInfo.stCropRect.s32X = x;
                    stCropInfo.stCropRect.s32Y = y;
                    stCropInfo.stCropRect.u32Width  = w;
                    stCropInfo.stCropRect.u32Height = h;
                    if (ctx->bEptz) {
                        RK_LOGD("change eptz and set start");
                        RK_LOGD("------ Crop to: w x h (%d x %d) ------", w, h);
                        s32Ret = RK_MPI_VI_SetEptz(ctx->pipeId, ctx->channelId, stCropInfo);
                        RK_LOGD("change eptz and set end result:%#X", s32Ret);
                    }

                    bDoCrop = RK_FALSE;
                }
            }
        } else {
            RK_LOGE("RK_MPI_VI_GetChnFrame timeout %x", s32Ret);
        }

        usleep(10*1000);
    }

    {
        /* restore crops setting as beginning resolution,
           avoiding getting video frame fail when run again.
        */
        VI_CROP_INFO_S stCropInfo;
        if (ctx->bEptz) {
            RK_MPI_VI_GetEptz(ctx->pipeId, ctx->channelId, &stCropInfo);
            if (stCropInfo.stCropRect.u32Width != ctx->width ||
                    stCropInfo.stCropRect.u32Height != ctx->height) {
                stCropInfo.stCropRect.u32Width  = ctx->width;
                stCropInfo.stCropRect.u32Height = ctx->height;
                RK_LOGD("<<< change eptz and set start >>>");
                RK_LOGD("------ Crop to: w x h (%d x %d) ------", ctx->width, ctx->height);
                RK_MPI_VI_SetEptz(ctx->pipeId, ctx->channelId, stCropInfo);
                RK_LOGD("<<< change eptz and set end >>>");
            }
        }
    }

__FAILED:
    // if enable rgn,deinit it and destory it.
    if (ctx->bEnRgn) {
        destory_rgn(ctx);
    }

    if (ctx->bAttachPool) {
        RK_MPI_VI_DetachMbPool(ctx->pipeId, ctx->channelId);
        RK_MPI_MB_DestroyPool(ctx->attachPool);
    }

    // 9. disable one chn
    s32Ret = RK_MPI_VI_DisableChn(ctx->pipeId, ctx->channelId);
    RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);

    // 10.disable dev(will diabled all chn)
    s32Ret = RK_MPI_VI_DisableDev(ctx->devId);
    RK_LOGE("RK_MPI_VI_DisableDev %x", s32Ret);
    return s32Ret;
}

static void *venc_jpeg_get_packet(void *arg) {
    RK_S32 s32Ret = RK_SUCCESS;
    VENC_STREAM_S stFrame;
    memset(&stFrame, 0, sizeof(VENC_STREAM_S));
    stFrame.pstPack = reinterpret_cast<VENC_PACK_S *>(malloc(sizeof(VENC_PACK_S)));
    struct combo_th *combo_th = (struct combo_th*)arg;
    RK_S32 s32ChnId = combo_th->chn;
    RK_S32 loopCount = 0;

    while (combo_th->run) {
        s32Ret = RK_MPI_VENC_GetStream(s32ChnId, &stFrame, 1000);
        if (s32Ret == RK_SUCCESS) {
            if (1) {
                char filename[128];
                FILE *fp;
                snprintf(filename, sizeof(filename), "/data/venc_ch%d_%lld.jpg", s32ChnId, stFrame.pstPack->u64PTS);
                fp = fopen(filename, "wb");
                if (fp) {
                    RK_VOID *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
                    fwrite(pData, 1, stFrame.pstPack->u32Len, fp);
                    fflush(fp);
                    fclose(fp);
                }
            }
            RK_U64 nowUs = TEST_COMM_GetNowUs();

            RK_LOGD("chn:%d, loopCount:%d enc->seq:%d wd:%d pts=%lld delay=%lldus", s32ChnId, loopCount,
                    stFrame.u32Seq, stFrame.pstPack->u32Len,
                    stFrame.pstPack->u64PTS,
                    nowUs - stFrame.pstPack->u64PTS);
            s32Ret = RK_MPI_VENC_ReleaseStream(s32ChnId, &stFrame);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
            }
            loopCount++;
        } else {
            // RK_LOGE("RK_MPI_VENC_GetChnFrame fail %x", s32Ret);
        }
    }
    pthread_exit(NULL);
}

static RK_S32 create_venc_combo(TEST_VI_CTX_S *ctx, RK_U32 u32Ch, RK_U32 s32ComboChnId) {
    VENC_CHN_ATTR_S stAttr;
    VENC_RECV_PIC_PARAM_S stRecvParam;
    VENC_CHN_BUF_WRAP_S stVencChnBufWrap;
    memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));
    memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
    memset(&stVencChnBufWrap, 0, sizeof(stVencChnBufWrap));

    stAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
    stAttr.stRcAttr.stH264Cbr.u32Gop = 30;
    stAttr.stRcAttr.stH264Cbr.u32BitRate = 1024;

    stAttr.stVencAttr.enType = RK_VIDEO_ID_JPEG;
    stAttr.stVencAttr.enPixelFormat = ctx->stChnAttr.enPixelFormat;
    stAttr.stVencAttr.u32PicWidth = ctx->width;
    stAttr.stVencAttr.u32PicHeight = ctx->height;
    stAttr.stVencAttr.u32VirWidth = ctx->width;
    stAttr.stVencAttr.u32VirHeight = ctx->height;
    stAttr.stVencAttr.u32StreamBufCnt = 1;
    stAttr.stVencAttr.u32BufSize = ctx->width * ctx->height * 3 / 2;

    if (stAttr.stVencAttr.enType == RK_VIDEO_ID_JPEG) {
        stAttr.stVencAttr.stAttrJpege.bSupportDCF = RK_FALSE;
        stAttr.stVencAttr.stAttrJpege.stMPFCfg.u8LargeThumbNailNum = 0;
        stAttr.stVencAttr.stAttrJpege.enReceiveMode = VENC_PIC_RECEIVE_SINGLE;
    }

    RK_MPI_VENC_CreateChn(u32Ch, &stAttr);

    stVencChnBufWrap.bEnable = ctx->stChnWrap.bEnable;
    RK_MPI_VENC_SetChnBufWrapAttr(u32Ch, &stVencChnBufWrap);

    stRecvParam.s32RecvPicNum = ctx->s32Snap;
    RK_MPI_VENC_StartRecvFrame(u32Ch, &stRecvParam);

    VENC_COMBO_ATTR_S stComboAttr;
    memset(&stComboAttr, 0, sizeof(VENC_COMBO_ATTR_S));
    stComboAttr.bEnable = RK_TRUE;
    stComboAttr.s32ChnId = s32ComboChnId;
    RK_MPI_VENC_SetComboAttr(u32Ch, &stComboAttr);

    VENC_JPEG_PARAM_S stJpegParam;
    memset(&stJpegParam, 0, sizeof(stJpegParam));
    stJpegParam.u32Qfactor = 77;
    RK_MPI_VENC_SetJpegParam(u32Ch, &stJpegParam);

    g_combo_th[u32Ch].run = RK_TRUE;
    g_combo_th[u32Ch].chn = u32Ch;
    pthread_create(&g_combo_th[u32Ch].th, NULL, venc_jpeg_get_packet, &g_combo_th[u32Ch]);

    return RK_SUCCESS;
}

static RK_S32 destroy_venc_combo(TEST_VI_CTX_S *ctx, RK_U32 u32Ch) {
    RK_S32 s32Ret = RK_SUCCESS;
    g_combo_th[u32Ch].run = RK_FALSE;
    pthread_join(g_combo_th[u32Ch].th, NULL);
    s32Ret = RK_MPI_VENC_StopRecvFrame(u32Ch);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }
    RK_LOGE("destroy enc chn:%d", u32Ch);
    s32Ret = RK_MPI_VENC_DestroyChn(u32Ch);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VENC_DestroyChn fail %x", s32Ret);
    }
    return RK_SUCCESS;
}

static int ParseSensorInfo() {
    int ret = 0;
    int fd = -1;
    char buf[1024];
    ssize_t nbytes;
    char *pBuf;

    fd = open(RKISP_DEV, O_RDONLY);
    if (fd < 0) {
        goto __FAIL;
    }

    nbytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (nbytes < 0) {
        goto __FAIL;
    }
    buf[nbytes] = '\0';
    /* Parse the contents of the file, which looks like:
     *
     *     # cat /proc/rkisp-vir0
     *     rkisp-vir0 Version:v01.09.00
     *     clk_isp_core 339428572
     *     aclk_isp   339428572
     *     hclk_isp   148500000
     *     clk_isp_core_vicap 0
     *     Interrupt  Cnt:0 ErrCnt:0
     *     Input      rkcif-mipi-lvds Format:SBGGR10_1X10 Size:2880x1616@30fps Offset(0,0)
     */
    pBuf = buf;
    while (nbytes > 0) {
        int matches;
        char format[32], sizeStr[32], offSet[8];
        matches = sscanf(pBuf, "Input      rkcif-mipi-lvds %s %s %s",
                format, sizeStr, offSet);
        if (matches > 0) {
            // printf("//////////////// matches:%d ///////////////\n", matches);
            if (strstr(sizeStr, "Size")) {
                int w, h, fps;
                sscanf(sizeStr, "Size:%dx%d@%dfps", &w, &h, &fps);
                // printf("//////////////// w:%d, h:%d, fps:%d ///////////////\n", w, h, fps);
                g_sensorInfo.width  = w;
                g_sensorInfo.height = h;
                g_sensorInfo.fps    = fps;
                if (w == 2880 && h == 1616) {
                    g_sensorInfo.type = _5MEGA;
                } else if (w == 2560 && h == 1440) {
                    g_sensorInfo.type = _4MEGA;
                } else if (w == 2304 && h == 1296) {
                    g_sensorInfo.type = _3MEGA;
                } else if (w == 1920 && h == 1080) {
                    g_sensorInfo.type = _2MEGA;
                } else if (w == 1280 && h == 720) {
                    g_sensorInfo.type = _1MEGA;
                } else if (w == 3140 && h == 2160) {
                    g_sensorInfo.type = _8MEGA;
                }
                break;
            }
        }
        /* Eat the line.
         */
        while (nbytes > 0 && *pBuf != '\n') {
            pBuf++;
            nbytes--;
        }
        if (nbytes > 0) {
            pBuf++;
            nbytes--;
        }
    }

    return 0;

__FAIL:
    return -1;
}

static int SetViChnReso(int chn, uint32_t &w, uint32_t &h) {
    if (chn < 0 || chn > TEST_VI_CHANNEL_NUM) {
        printf("Error: chn %d invalid or exceed the TEST_VI_CHANNEL_NUM", chn);
        return -1;
    }
    switch (g_sensorInfo.type) {
        case _1MEGA:
            w = g_sensorInfo.width; h = g_sensorInfo.height;
            break;
        case _2MEGA:
            if (chn == VI_CHN0) {
                w = g_sensorInfo.width; h = g_sensorInfo.height;
            } else if (chn == VI_CHN1 || chn == VI_CHN2) {
                w = 1280; h = 720;
            } break;
        case _3MEGA:
            if (chn == VI_CHN0) {
                w = g_sensorInfo.width; h = g_sensorInfo.height;
            } else if (chn == VI_CHN1) {
                w = 1920; h = 1080;
            } else if(chn == VI_CHN2) {
                 w = 1280; h = 720;
            } break;
        case _4MEGA:
            if (chn == VI_CHN0) {
                w = g_sensorInfo.width; h = g_sensorInfo.height;
            } else if (chn == VI_CHN1) {
                w = 1920; h = 1080;
            } else if(chn == VI_CHN2) {
                 w = 1280; h = 720;
            } break;
        case _5MEGA:
            if (chn == VI_CHN0) {
                w = g_sensorInfo.width; h = g_sensorInfo.height;
            } else if (chn == VI_CHN1) {
                w = 2560; h = 1440;
            } else if(chn == VI_CHN2) {
                 w = 1920; h = 1080;
            } break;
        case _8MEGA:
            if (chn == VI_CHN0) {
                w = g_sensorInfo.width; h = g_sensorInfo.height;
            } else {
                w = 1920; h = 1080;
            } break;
        case _4K:
            if (chn == VI_CHN0) {
                w = g_sensorInfo.width; h = g_sensorInfo.height;
            } else {
                w = 1920; h = 1080;
            } break;
        default:
            w = g_sensorInfo.width; h = g_sensorInfo.height;
            break;
    }

    return 0;
}

static void wait_module_test_switch_success(void) {
    for (RK_U32 i = 0; i < TEST_VI_CHANNEL_NUM; i++) {
        sem_wait(&g_sem_module_test[i]);
        pthread_mutex_lock(&g_frame_count_mutex[i]);
        g_u32VIGetFrameCnt[i] = 0;
        pthread_mutex_unlock(&g_frame_count_mutex[i]);
    }
}

static void *ViFuncModeTestThread(void *arg) {
    RK_S32 s32Ret = RK_FAILURE;
    TEST_VI_CTX_S **pstViCtx = (TEST_VI_CTX_S **)arg;
    RK_U32 u32TestCnt = 0;

    wait_module_test_switch_success();

    while (!bquit) {
        switch (g_ModeTestType) {
            case MODE_TYPE_PAUSE_RESUME: {
                RK_MPI_VI_PauseChn(pstViCtx[0]->pipeId, pstViCtx[0]->channelId);
                RK_MPI_VI_PauseChn(pstViCtx[1]->pipeId, pstViCtx[1]->channelId);
                RK_MPI_VI_PauseChn(pstViCtx[2]->pipeId, pstViCtx[2]->channelId);

                usleep(200*1000);

                RK_MPI_VI_ResumeChn(pstViCtx[0]->pipeId, pstViCtx[0]->channelId);
                RK_MPI_VI_ResumeChn(pstViCtx[1]->pipeId, pstViCtx[1]->channelId);
                RK_MPI_VI_ResumeChn(pstViCtx[2]->pipeId, pstViCtx[2]->channelId);
                RK_LOGE("----------------- Pause / Resume mode switch success");
                wait_module_test_switch_success();

                u32TestCnt++;
                RK_LOGI("-----------------moduleTest switch success total:%d, now_count:%d",
                        g_ModeTestCnt, u32TestCnt);
                if (g_ModeTestCnt > 0 && u32TestCnt >= g_ModeTestCnt) {
                    RK_LOGI("------------------moduleTest: Pause / Resume end");
                    g_ExitModeTest = RK_TRUE;
                    bquit = RK_TRUE;
                }
            } break;

            case MODE_TYPE_MIRR_FLIP: {
                VI_ISP_MIRROR_FLIP_S mirr_flip;
                RK_U32 chos = u32TestCnt % 2;
                switch (chos) {
                case 0:
                    mirr_flip.flip = 1;
                    mirr_flip.mirror = 1;
                    break;
                case 1:
                    mirr_flip.flip = 0;
                    mirr_flip.mirror = 0;
                    break;
                default:
                    break;
                }
                RK_MPI_VI_SetChnMirrorFlip(pstViCtx[0]->pipeId, pstViCtx[0]->channelId, mirr_flip);
                RK_MPI_VI_SetChnMirrorFlip(pstViCtx[1]->pipeId, pstViCtx[1]->channelId, mirr_flip);
                RK_MPI_VI_SetChnMirrorFlip(pstViCtx[2]->pipeId, pstViCtx[2]->channelId, mirr_flip);
                usleep(200*1000);

                RK_LOGE("----------------- Mirror / Flip mode config success");
                wait_module_test_switch_success();

                u32TestCnt++;
                RK_LOGI("-----------------moduleTest success total:%d, now_count:%d",
                        g_ModeTestCnt, u32TestCnt);
                if (g_ModeTestCnt > 0 && u32TestCnt >= g_ModeTestCnt) {
                    RK_LOGI("------------------moduleTest: %d end", g_ModeTestType);
                    g_ExitModeTest = RK_TRUE;
                    bquit = RK_TRUE;
                }
            } break;

            default:
                break;
        }
    }

    RK_LOGE("ViFuncModeTestThread exit!!!");
    return RK_NULL;
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

static void *GetViBuffer(void *arg) {
    void *pData = RK_NULL;
    int loopCount = 0;
    int s32Ret;
    int chn, pipe;
#if TEST_WITH_FD
    TEST_VI_CTX_S **pstViCtx = (TEST_VI_CTX_S **)arg;
    RK_S32 num_fds = 3;
    RK_S32 timeoutMsec = 20;
    struct pollfd pollFds[3];
    RK_S32 pollResult = 0;
    int loopCnt[3] = {0};
    RK_S32 i = 0;
    RK_S32 totalCnt = pstViCtx[0]->loopCountSet;
#else
    TEST_VI_CTX_S *ctx = (TEST_VI_CTX_S *)arg;
    pipe = ctx->pipeId;
    chn = ctx->channelId;
#endif

#if TEST_WITH_FD
    memset(pollFds, 0, sizeof(pollFds));
    for (i = 0; i < num_fds; i++)
    {
        pollFds[i].fd = pstViCtx[i]->selectFd;
        pollFds[i].events = (POLLPRI| POLLIN | POLLERR | POLLNVAL | POLLHUP);
    }

    while (1) {
        if (loopCnt[0] > pstViCtx[0]->loopCountSet &&
            loopCnt[1] > pstViCtx[1]->loopCountSet &&
            loopCnt[2] > pstViCtx[2]->loopCountSet) {
                break;
            }

        pollResult = poll(pollFds, num_fds, timeoutMsec);
        if (pollResult == 0) {
            RK_LOGW("Poll timed out with no file descriptors ready");
            usleep(1000ul);
            continue;
        } else if (pollResult == -1) {
            RK_LOGE("polled error");
            usleep(1000ul);
            continue;
        } else {
            for (i = 0; i < num_fds; i++) {
                if (loopCnt[i] > pstViCtx[i]->loopCountSet) // have reach loopcnt, don't get frame for this chn
                    continue;
                if (pollFds[i].revents & POLLIN) {
                    RK_LOGI("fd: %d polled success, data is available to read on", pollFds[i].fd);
                    // do get chn frame
                    TEST_VI_CTX_S *ctx = pstViCtx[i];
                    pipe = ctx->pipeId;
                     chn = ctx->channelId;
                    s32Ret = RK_MPI_VI_GetChnFrame(pipe, chn, &ctx->stViFrame, 1000);
                    if (s32Ret == RK_SUCCESS) {
                        RK_U64 nowUs = TEST_COMM_GetNowUs();
                        void *data = RK_MPI_MB_Handle2VirAddr(ctx->stViFrame.stVFrame.pMbBlk);
                        RK_LOGD("RK_MPI_VI_GetChnFrame ok:data %p [%d : %d] loop:%d seq:%d pts:%lld ms len=%d",
                                data, pipe, chn,
                                loopCnt[i],
                                ctx->stViFrame.stVFrame.u32TimeRef,
                                ctx->stViFrame.stVFrame.u64PTS/1000,
                                ctx->stViFrame.stVFrame.u64PrivateData);
                        s32Ret = RK_MPI_VI_ReleaseChnFrame(pipe, chn, &ctx->stViFrame);
                        RK_ASSERT(s32Ret==RK_SUCCESS);
                        loopCnt[i]++;
                    } else {
                        RK_LOGE("Chn:%d, RK_MPI_VI_GetChnFrame timeout %x", chn, s32Ret);
                    }
                }
            }
        }
    }
#else
    while (loopCount < ctx->loopCountSet) {
        s32Ret = RK_MPI_VI_GetChnFrame(pipe, chn, &ctx->stViFrame, 1000);
        if (s32Ret == RK_SUCCESS) {
            RK_U64 nowUs = TEST_COMM_GetNowUs();
            void *data = RK_MPI_MB_Handle2VirAddr(ctx->stViFrame.stVFrame.pMbBlk);
            RK_LOGD("RK_MPI_VI_GetChnFrame ok:data %p [%d: %d] loop:%d seq:%d pts:%lld ms len=%d",
                     data, pipe, chn,
                     loopCount,
                     ctx->stViFrame.stVFrame.u32TimeRef,
                     ctx->stViFrame.stVFrame.u64PTS/1000,
                     ctx->stViFrame.stVFrame.u64PrivateData);
            s32Ret = RK_MPI_VI_ReleaseChnFrame(pipe, chn, &ctx->stViFrame);
            RK_ASSERT(s32Ret==RK_SUCCESS);
            loopCount++;
        } else {
            RK_LOGE("Chn:%d, RK_MPI_VI_GetChnFrame timeout %x", chn, s32Ret);
        }
    }
#endif
    return NULL;
}

static RK_S32 test_vi_func_mode(TEST_VI_CTX_S *ctx) {
    RK_S32 s32Ret;
    RK_S32 loopCount = 0;
    RK_S32 waitTime = 100;
    RK_S32 i = 0;
    RK_U32 chn_w = 0, chn_h = 0;
    TEST_VI_CTX_S *pstViCtx[TEST_VI_CHANNEL_NUM];

    for (i = 0; i < TEST_VI_CHANNEL_NUM; i++) {
        sem_init(&g_sem_module_test[i], 0, 0);
    }

    ParseSensorInfo();

    for (i = 0; i < TEST_VI_CHANNEL_NUM; i++) {
        pstViCtx[i] = reinterpret_cast<TEST_VI_CTX_S *>(malloc(sizeof(TEST_VI_CTX_S)));
        memset(pstViCtx[i], 0, sizeof(TEST_VI_CTX_S));

        /* vi config init */
        pstViCtx[i]->devId = 0;
        pstViCtx[i]->pipeId = pstViCtx[i]->devId;
        pstViCtx[i]->channelId = i;
        pstViCtx[i]->loopCountSet = ctx->loopCountSet;

        //ctx->stDebugFile.bCfg
        pstViCtx[i]->stDebugFile.bCfg = ctx->stDebugFile.bCfg;
        if (ctx->stDebugFile.bCfg) {
            memcpy(&pstViCtx[i]->stDebugFile.aFilePath, "/data", strlen("/data"));
            snprintf(pstViCtx[i]->stDebugFile.aFileName, MAX_VI_FILE_PATH_LEN,
                    "test_%d_%d_%d.bin", pstViCtx[i]->devId, pstViCtx[i]->pipeId, i);
        }

        // TODO(@Chad): according to sensor type to config vi channels resolution.
        if (SetViChnReso(i, chn_w, chn_h)) goto __FAILED_VI;
        RK_LOGD("+++VI_CHN%d: reso: %u x %u +++", i, chn_w, chn_h);
        pstViCtx[i]->stChnAttr.stSize.u32Width = chn_w;
        pstViCtx[i]->stChnAttr.stSize.u32Height = chn_h;
        pstViCtx[i]->stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
        pstViCtx[i]->stChnAttr.stIspOpt.u32BufCount = 3;
        pstViCtx[i]->stChnAttr.stIspOpt.bNoUseLibV4L2 = RK_TRUE;
        pstViCtx[i]->stChnAttr.u32Depth = 1;
        pstViCtx[i]->stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
        pstViCtx[i]->stChnAttr.enCompressMode = COMPRESS_MODE_NONE;
        pstViCtx[i]->stChnAttr.stFrameRate.s32SrcFrameRate = ctx->stChnAttr.stFrameRate.s32SrcFrameRate;
        pstViCtx[i]->stChnAttr.stFrameRate.s32DstFrameRate = ctx->stChnAttr.stFrameRate.s32DstFrameRate;

        /* vi create */
        s32Ret = create_vi(pstViCtx[i]);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("vi [%d, %d] init failed: %x",
                    pstViCtx[i]->devId, pstViCtx[i]->channelId,
                    s32Ret);
            goto __FAILED_VI;
        }
    }

    pthread_t chn0_thread, chn1_thread, chn2_thread, fun_thread;
    pthread_create(&fun_thread,  NULL, ViFuncModeTestThread, pstViCtx);
    pthread_create(&chn0_thread, NULL, ViGetFrameThread, pstViCtx[0]);
    pthread_create(&chn1_thread, NULL, ViGetFrameThread, pstViCtx[1]);
    pthread_create(&chn2_thread, NULL, ViGetFrameThread, pstViCtx[2]);

    pthread_join(chn0_thread, RK_NULL);
    pthread_join(chn1_thread, RK_NULL);
    pthread_join(chn2_thread, RK_NULL);
    pthread_join(fun_thread,  RK_NULL);

__FAILED_VI:
    for (i = 0; i < TEST_VI_CHANNEL_NUM; i++) {
        sem_destroy(&g_sem_module_test[i]);
    }

    // if enable rgn,deinit it and destory it.
    if (ctx->bEnRgn) {
        destory_rgn(ctx);
    }

    if (ctx->bAttachPool) {
        RK_MPI_VI_DetachMbPool(ctx->pipeId, ctx->channelId);
        RK_MPI_MB_DestroyPool(ctx->attachPool);
    }

    /* destroy vi*/
    for (i = 0; i < TEST_VI_CHANNEL_NUM; i++) {
        s32Ret = RK_MPI_VI_DisableChn(pstViCtx[i]->pipeId, pstViCtx[i]->channelId);
        RK_LOGE("RK_MPI_VI_DisableChn pipe=%d, chn:%d, ret:%x", pstViCtx[i]->pipeId, pstViCtx[i]->channelId, s32Ret);

        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("disable vi dev:%d, chn %d error %x",pstViCtx[i]->devId, pstViCtx[i]->channelId, s32Ret);
        }
    }

    s32Ret = RK_MPI_VI_DisableDev(0);
    RK_LOGE("RK_MPI_VI_DisableDev dev 0: ret:%x", s32Ret);

    for (i = 0; i < TEST_VI_CHANNEL_NUM; i++) {
        RK_SAFE_FREE(pstViCtx[i]);
    }

    return s32Ret;
}

/* 
 @brief get frame and release it from one sensor multiple channels for rv1106/rv1103
 @param ctx
 @return 0 - Success, < 0 - Failure
*/
static int test_vi_one_sensor_multi_chn_loop(const TEST_VI_CTX_S *ctx) {
    RK_S32 s32Ret = RK_FAILURE;
    RK_S32 loopCount = 0;
    RK_S32 waitTime = 100;
    RK_S32 i = 0;
    RK_U32 chn_w = 0, chn_h = 0;
    TEST_VI_CTX_S *pstViCtx[TEST_VI_CHANNEL_NUM];

    ParseSensorInfo();

    for (i = 0; i < TEST_VI_CHANNEL_NUM; i++) {
        pstViCtx[i] = reinterpret_cast<TEST_VI_CTX_S *>(malloc(sizeof(TEST_VI_CTX_S)));
        memset(pstViCtx[i], 0, sizeof(TEST_VI_CTX_S));

        /* vi config init */
        pstViCtx[i]->devId = 0;
        pstViCtx[i]->pipeId = pstViCtx[i]->devId;
        pstViCtx[i]->channelId = i;
        pstViCtx[i]->loopCountSet = ctx->loopCountSet;
        // pstViCtx[i]->bDevDataOffline = ctx->bDevDataOffline;
        // pstViCtx[i]->bUserStartPipe = ctx->bUserStartPipe;

        // TODO(@Chad): according to sensor type to config vi channels resolution.
        if (SetViChnReso(i, chn_w, chn_h)) goto __FAILED_VI;
        RK_LOGD("+++VI_CHN%d: reso: %u x %u +++", i, chn_w, chn_h);
        pstViCtx[i]->stChnAttr.stSize.u32Width = chn_w;
        pstViCtx[i]->stChnAttr.stSize.u32Height = chn_h;
        pstViCtx[i]->stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
        pstViCtx[i]->stChnAttr.stIspOpt.u32BufCount = 3;
        pstViCtx[i]->stChnAttr.stIspOpt.bNoUseLibV4L2 = RK_TRUE;
        pstViCtx[i]->stChnAttr.u32Depth = 1;
        pstViCtx[i]->stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
        pstViCtx[i]->stChnAttr.enCompressMode = COMPRESS_MODE_NONE;
        pstViCtx[i]->stChnAttr.stFrameRate.s32SrcFrameRate = ctx->stChnAttr.stFrameRate.s32SrcFrameRate;
        pstViCtx[i]->stChnAttr.stFrameRate.s32DstFrameRate = ctx->stChnAttr.stFrameRate.s32DstFrameRate;

        /* vi create */
        s32Ret = create_vi(pstViCtx[i]);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("vi [%d, %d] init failed: %x",
                    pstViCtx[i]->devId, pstViCtx[i]->channelId,
                    s32Ret);
            goto __FAILED_VI;
        }
    }

#if TEST_WITH_FD
    pthread_t chn_thread;
    pthread_create(&chn_thread, NULL, GetViBuffer, pstViCtx);
    pthread_join(chn_thread, RK_NULL);
#else
    pthread_t chn0_thread, chn1_thread, chn2_thread;
    pthread_create(&chn0_thread, NULL, GetViBuffer, pstViCtx[0]);
    pthread_create(&chn1_thread, NULL, GetViBuffer, pstViCtx[1]);
    pthread_create(&chn2_thread, NULL, GetViBuffer, pstViCtx[2]);

    pthread_join(chn0_thread, RK_NULL);
    pthread_join(chn1_thread, RK_NULL);
    pthread_join(chn2_thread, RK_NULL);
#endif
__FAILED_VI:
    /* destroy vi*/
    for (i = 0; i < TEST_VI_CHANNEL_NUM && pstViCtx[i]; i++) {
        s32Ret = RK_MPI_VI_DisableChn(pstViCtx[i]->pipeId, pstViCtx[i]->channelId);
        RK_LOGE("RK_MPI_VI_DisableChn pipe=%d, chn:%d, ret:%x", pstViCtx[i]->pipeId, pstViCtx[i]->channelId, s32Ret);

        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("disable vi dev:%d, chn %d error %x",pstViCtx[i]->devId, pstViCtx[i]->channelId, s32Ret);
        }
    }

    s32Ret = RK_MPI_VI_DisableDev(0);
    RK_LOGE("RK_MPI_VI_DisableDev dev 0: ret:%x", s32Ret);

    for (i = 0; i < TEST_VI_CHANNEL_NUM; i++) {
        RK_SAFE_FREE(pstViCtx[i]);
    }

__FAILED:
    return s32Ret;
}

/// @brief vi bind venc wrap switch to non-wrap test
/// @param ctx
/// @return 0 -success, 1 - fail
static RK_S32 test_vi_bind_venc_wrap_switch(TEST_VI_CTX_S *ctx) {
    MPP_CHN_S stSrcChn, stDestChn[TEST_VENC_MAX];
    RK_S32 loopCount = 0;
    void *pData = RK_NULL;
    RK_S32 s32Ret = RK_FAILURE;
    RK_U32 i;
    RK_U32 u32DstCount = ((ctx->enMode == TEST_VI_MODE_BIND_VENC_MULTI) ? 2 : 1);
    RK_U32 idx = 0;
    RK_U32 resCnt;
    static int wrapSwi = 0;

    ctx->stDebugFile.bCfg = RK_FALSE;

BEGIN:
    /* vi init */
    s32Ret = test_vi_init(ctx);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("vi %d:%d init failed:%x", ctx->devId, ctx->channelId, s32Ret);
        goto __FAILED;
    }

    stSrcChn.enModId    = RK_ID_VI;
    stSrcChn.s32DevId   = ctx->devId;
    stSrcChn.s32ChnId   = ctx->channelId;

    /* venc */
    for (i = 0; i < u32DstCount; i++) {
        // venc  init and create
        init_venc_cfg(ctx, i, (RK_CODEC_ID_E)ctx->u32DstCodec);
        s32Ret = create_venc(ctx, i);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("create %d ch venc failed", ctx->stVencCfg[i].s32ChnId);
            return s32Ret;
        }
         // bind vi to venc
        stDestChn[i].enModId   = RK_ID_VENC;
        stDestChn[i].s32DevId  = 0;
        stDestChn[i].s32ChnId  = ctx->stVencCfg[i].s32ChnId;

        s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn[i]);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("bind %d ch venc failed", ctx->stVencCfg[i].s32ChnId);
            goto __FAILED;
        }
        ctx->stFrame[i].pstPack = reinterpret_cast<VENC_PACK_S *>(malloc(sizeof(VENC_PACK_S)));
#if TEST_WITH_FD
        ctx->stVencCfg[i].selectFd = RK_MPI_VENC_GetFd(ctx->stVencCfg[i].s32ChnId);
        RK_LOGE("venc chn:%d, ctx->selectFd:%d ", ctx->stVencCfg[i].s32ChnId, ctx->stVencCfg[i].selectFd);
#endif

        if (ctx->bCombo)
            create_venc_combo(ctx, ctx->stVencCfg[i].s32ChnId + COMBO_START_CHN, ctx->stVencCfg[i].s32ChnId);
    }

    /* creat overlay */
    if (ctx->bEnOverlay) {
        s32Ret = create_overlay(ctx, u32DstCount);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("create overlay failed");
            goto __FAILED;
        }
    }

    while (loopCount < ctx->loopCountSet) {
        for (i = 0; i < u32DstCount; i++) {
            // freeze test
            RK_MPI_VI_SetChnFreeze(ctx->pipeId, ctx->channelId, ctx->bFreeze);

            s32Ret = RK_MPI_VENC_GetStream(ctx->stVencCfg[i].s32ChnId, &ctx->stFrame[i], -1);
            if (s32Ret == RK_SUCCESS) {
                if (ctx->stVencCfg[i].bOutDebugCfg) {
                    pData = RK_MPI_MB_Handle2VirAddr(ctx->stFrame[i].pstPack->pMbBlk);
                    fwrite(pData, 1, ctx->stFrame[i].pstPack->u32Len, ctx->stVencCfg[i].fp);
                    fflush(ctx->stVencCfg[i].fp);
                }
                RK_U64 nowUs = TEST_COMM_GetNowUs();

                RK_LOGD("chn:%d, loopCount:%d enc->seq:%d wd:%d pts=%lld delay=%lldus", i, loopCount,
                         ctx->stFrame[i].u32Seq, ctx->stFrame[i].pstPack->u32Len,
                         ctx->stFrame[i].pstPack->u64PTS,
                         nowUs - ctx->stFrame[i].pstPack->u64PTS);
                usleep(ctx->u32DelayMsGet * 1000);
                s32Ret = RK_MPI_VENC_ReleaseStream(ctx->stVencCfg[i].s32ChnId, &ctx->stFrame[i]);
                if (s32Ret != RK_SUCCESS) {
                    RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
                }
                loopCount++;
            } else {
                RK_LOGE("RK_MPI_VI_GetChnFrame fail %x", s32Ret);
            }
        }
    }

__FAILED:
    for (i = 0; i < u32DstCount; i++) {
        s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn[i]);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
        }
    }

    // 5. disable one chn
    s32Ret = RK_MPI_VI_DisableChn(ctx->pipeId, ctx->channelId);
    RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);

    // destroy overlay
    if (ctx->bEnOverlay) {
        s32Ret = destroy_overlay(ctx, u32DstCount);
        if (RK_SUCCESS != s32Ret) {
            RK_LOGE("destroy_overlay failed!");
        }
    }

    for (i = 0; i < u32DstCount; i++) {
        if (ctx->bCombo)
            destroy_venc_combo(ctx, ctx->stVencCfg[i].s32ChnId + COMBO_START_CHN);

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

    /*************************************************************************/
    // switch to non-wrap mode
    loopCount = 0;
    if (wrapSwi == 0) {
        wrapSwi = 1;
        ctx->stChnWrap.bEnable = RK_FALSE;
        ctx->stChnWrap.u32BufLine = 0;
        goto BEGIN;
    }

    return s32Ret;
}

static RK_S32 test_vi_bind_vdec_bind_vo_loop(TEST_VI_CTX_S *ctx) {
#if (defined HAVE_API_MPI_VO) && (defined HAVE_API_MPI_VDEC)
    MPP_CHN_S stSrcChn, stDestChn;
    RK_S32 loopCount = 0;
    void *pData = RK_NULL;
    RK_S32 s32Ret = RK_FAILURE;
    RK_U32 i;
    TEST_VDEC_CFG_S stVdecCfg;

    if (ctx->enCodecId == RK_VIDEO_ID_Unused) {
        RK_LOGE("this mode need set codec id", ctx->enCodecId);
        return RK_FAILURE;
    }

    s32Ret = test_vi_init(ctx);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("vi %d:%d init failed:%x", ctx->devId, ctx->channelId, s32Ret);
        goto __FAILED;
    }

    // vo  init and create
    ctx->s32VoLayer = RK356X_VOP_LAYER_CLUSTER_0;
    ctx->s32VoDev = RK356X_VO_DEV_HD0;
    s32Ret = create_vo(ctx, 0);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("create vo ch:%d failed", ctx->channelId);
        goto __FAILED;
    }
    // bind vi to vdec to vo
    stVdecCfg.s32VdecChn = 0;
    stVdecCfg.u32CompressMode = 0;
    if (ctx->enCodecId == RK_VIDEO_ID_MJPEG)  // mjpeg vdec only support frame mode
        stVdecCfg.enVideoMode = VIDEO_MODE_FRAME;
    else
        stVdecCfg.enVideoMode = VIDEO_MODE_STREAM;
    stVdecCfg.enVdecSource = TEST_MPI_SOURCE_BIND;
    stVdecCfg.stStream.pSrcFilePath = RK_NULL;
    stVdecCfg.stStream.s32ReadLoopCnt = 1;
    // stVdecCfg.bUseSeqAsPts = RK_FALSE;
    stVdecCfg.stBindSrc.enCodecId = ctx->enCodecId;
    stVdecCfg.stBindSrc.u32PicWidth = ctx->width;
    stVdecCfg.stBindSrc.u32PicHeight = ctx->height;
    stVdecCfg.stBindSrc.stSrcChn.enModId = RK_ID_VI;
    stVdecCfg.stBindSrc.stSrcChn.s32DevId = ctx->devId;
    stVdecCfg.stBindSrc.stSrcChn.s32ChnId = ctx->channelId;
    stDestChn.enModId   = RK_ID_VO;
    stDestChn.s32DevId  = ctx->s32VoLayer;
    stDestChn.s32ChnId  = 0;
    TEST_COMM_APP_VDEC_StartProcWithDstChn(&stVdecCfg, &stDestChn);

    // enable vo
    s32Ret = RK_MPI_VO_EnableChn(ctx->s32VoLayer, 0);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("Enalbe vo chn failed, s32Ret = %d\n", s32Ret);
        goto __FAILED;
    }

    while (loopCount < ctx->loopCountSet) {
        loopCount++;
        RK_LOGE("loopCount:%d", loopCount);
        // can not get the vo frameout count . so here regard as 33ms one frame.
        usleep(33*1000);
    }

__FAILED:
    if (ctx->bAttachPool) {
        RK_MPI_VI_DetachMbPool(ctx->pipeId, ctx->channelId);
        RK_MPI_MB_DestroyPool(ctx->attachPool);
    }
    TEST_COMM_APP_VDEC_StopProcWithDstChn(&stVdecCfg, &stDestChn);
    TEST_COMM_APP_VDEC_Stop(&stVdecCfg);
    // disable vo
    RK_MPI_VO_DisableChn(ctx->s32VoLayer, 0);
    RK_MPI_VO_DisableLayer(ctx->s32VoLayer);
    RK_MPI_VO_Disable(ctx->s32VoDev);

    // 5. disable one chn
    s32Ret = RK_MPI_VI_DisableChn(ctx->pipeId, ctx->channelId);
    RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);

    RK_MPI_VO_DisableChn(ctx->s32VoLayer, 0);

    // 6.disable dev(will diabled all chn)
    s32Ret = RK_MPI_VI_DisableDev(ctx->devId);
    RK_LOGE("RK_MPI_VI_DisableDev %x", s32Ret);

    return s32Ret;
#else
    return 0;
#endif
}

static RK_S32 test_vi_bind_ivs_loop(TEST_VI_CTX_S *ctx) {
#ifdef HAVE_API_MPI_IVS
    MPP_CHN_S stSrcChn, stIvsChn;
    RK_S32 loopCount = 0;
    void *pData = RK_NULL;
    RK_S32 s32Ret = RK_FAILURE;
    RK_U32 i;

    s32Ret = test_vi_init(ctx);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("vi %d:%d init failed:%x", ctx->devId, ctx->channelId, s32Ret);
        goto __FAILED;
    }
    stSrcChn.enModId    = RK_ID_VI;
    stSrcChn.s32DevId   = ctx->devId;
    stSrcChn.s32ChnId   = ctx->channelId;

    if (ctx->u32Ivs) {
        s32Ret = create_ivs(ctx);
        if (s32Ret) {
            RK_LOGE("ivs init failed:%x", s32Ret);
            goto __FAILED;
        }

        IVS_MD_ATTR_S stMdAttr;
        memset(&stMdAttr, 0, sizeof(stMdAttr));
        s32Ret = RK_MPI_IVS_GetMdAttr(0, &stMdAttr);
        if (s32Ret) {
            RK_LOGE("ivs get mdattr failed:%x", s32Ret);
            goto __FAILED;
        }
        stMdAttr.s32ThreshSad = 40;
        stMdAttr.s32ThreshMove = 2;
        stMdAttr.s32SwitchSad = 0;
        s32Ret = RK_MPI_IVS_SetMdAttr(0, &stMdAttr);
        if (s32Ret) {
            RK_LOGE("ivs set mdattr failed:%x", s32Ret);
            goto __FAILED;
        }

        IVS_OD_ATTR_S stOdAttr;
        memset(&stOdAttr, 0, sizeof(stOdAttr));
        s32Ret = RK_MPI_IVS_GetOdAttr(0, &stOdAttr);
        if (s32Ret) {
            RK_LOGE("ivs get odattr failed:%x", s32Ret);
            goto __FAILED;
        }
        stOdAttr.s32ODPercent = 6;
        stOdAttr.bODUserRectEnable = RK_TRUE;
        stOdAttr.stODUserRect.s32X = (ctx->width - ctx->width/8)/2;
        stOdAttr.stODUserRect.s32Y = (ctx->height - ctx->height/8)/2;
        stOdAttr.stODUserRect.u32Width = ctx->width/8;
        stOdAttr.stODUserRect.u32Height = ctx->height/8;
        s32Ret = RK_MPI_IVS_SetOdAttr(0, &stOdAttr);
        if (s32Ret) {
            RK_LOGE("ivs set odattr failed:%x", s32Ret);
            goto __FAILED;
        }

        stIvsChn.enModId   = RK_ID_IVS;
        stIvsChn.s32DevId  = 0;
        stIvsChn.s32ChnId  = 0;
        s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stIvsChn);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("bind vi to ivs failed");
            goto __FAILED;
        }
    }

    while (loopCount < ctx->loopCountSet) {
        if (ctx->u32Ivs) {
            IVS_RESULT_INFO_S stResults;
            memset(&stResults, 0, sizeof(IVS_RESULT_INFO_S));
            s32Ret = RK_MPI_IVS_GetResults(0, &stResults, -1);
            if (s32Ret == RK_SUCCESS) {
                if (stResults.s32ResultNum == 1) {
                    printf("MD u32RectNum: %u, u32Square %u\n", stResults.pstResults->stMdInfo.u32RectNum,
                           stResults.pstResults->stMdInfo.u32Square);
                    printf("OD u32Flag: %d\n", stResults.pstResults->stOdInfo.u32Flag);
                    for (int i = 0; i < stResults.pstResults->stMdInfo.u32RectNum; i++) {
                        printf("%d: [%d, %d, %d, %d]\n", i,
                               stResults.pstResults->stMdInfo.stRect[i].s32X,
                               stResults.pstResults->stMdInfo.stRect[i].s32Y,
                               stResults.pstResults->stMdInfo.stRect[i].u32Width,
                               stResults.pstResults->stMdInfo.stRect[i].u32Height);
                    }
                }
                RK_MPI_IVS_ReleaseResults(0, &stResults);
                loopCount++;
            } else {
                RK_LOGE("RK_MPI_IVS_GetResults fail %x", s32Ret);
            }
        }

        usleep(10*1000);
    }

__FAILED:
    if (ctx->bAttachPool) {
        RK_MPI_VI_DetachMbPool(ctx->pipeId, ctx->channelId);
        RK_MPI_MB_DestroyPool(ctx->attachPool);
    }
    if (ctx->u32Ivs) {
        s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stIvsChn);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_SYS_UnBind vi ivs fail %x", s32Ret);
        }
        destroy_ivs();
    }

    // 5. disable one chn
    s32Ret = RK_MPI_VI_DisableChn(ctx->pipeId, ctx->channelId);
    RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);

    // 6.disable dev(will diabled all chn)
    s32Ret = RK_MPI_VI_DisableDev(ctx->devId);
    RK_LOGE("RK_MPI_VI_DisableDev %x", s32Ret);
    return s32Ret;
#else
    return 0;
#endif
}

static RK_S32 test_vi_get_release_stream_loop(TEST_VI_CTX_S *ctx) {
    RK_S32 s32Ret;
    RK_S32 loopCount = 0;
    RK_S32 waitTime = 33;
    RK_S64 s64FirstStreamTime = 0;
    RK_S64 s64StartTime;
    VI_STREAM_S stStream;

    if (ctx->enCodecId == RK_VIDEO_ID_Unused) {
        RK_LOGE("this mode need set codec id", ctx->enCodecId);
        return RK_FAILURE;
    }

    s64StartTime = TEST_COMM_GetNowUs();

    /* test use getframe&release_stream */
    s32Ret = test_vi_init(ctx);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("vi %d:%d init failed:%x", ctx->devId, ctx->channelId, s32Ret);
        goto __FAILED;
    }

    while (loopCount < ctx->loopCountSet) {
        // get the stream
        s32Ret = RK_MPI_VI_GetChnStream(ctx->pipeId, ctx->channelId, &stStream, waitTime);
        if (s32Ret == RK_SUCCESS) {
            RK_U64 nowUs = TEST_COMM_GetNowUs();
            if (!s64FirstStreamTime) {
                s64FirstStreamTime = nowUs - s64StartTime;
                RK_LOGE("get first stream use time:%d ms", s64FirstStreamTime / 1000);
            }
            void *data = RK_MPI_MB_Handle2VirAddr(stStream.pMbBlk);

            RK_LOGD("RK_MPI_VI_GetChnFrame ok:data %p loop:%d seq:%d pts:%lld ms len=%d", data, loopCount,
                     stStream.u32Seq, stStream.u64PTS/1000,
                     stStream.u32Len);
            // 6.get the channel status
            s32Ret = RK_MPI_VI_QueryChnStatus(ctx->pipeId, ctx->channelId, &ctx->stChnStatus);
            RK_LOGD("RK_MPI_VI_QueryChnStatus ret %x, w:%d,h:%d,enable:%d," \
                    "current frame id:%d,input lost:%d,output lost:%d," \
                    "framerate:%d,vbfail:%d delay=%lldus",
                     s32Ret,
                     ctx->stChnStatus.stSize.u32Width,
                     ctx->stChnStatus.stSize.u32Height,
                     ctx->stChnStatus.bEnable,
                     ctx->stChnStatus.u32CurFrameID,
                     ctx->stChnStatus.u32InputLostFrame,
                     ctx->stChnStatus.u32OutputLostFrame,
                     ctx->stChnStatus.u32FrameRate,
                     ctx->stChnStatus.u32VbFail,
                     nowUs - stStream.u64PTS);

            // 7.release the stream
            s32Ret = RK_MPI_VI_ReleaseChnStream(ctx->pipeId, ctx->channelId, &stStream);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
            }
            loopCount++;
        } else {
            RK_LOGE("RK_MPI_VI_GetChnFrame timeout %x", s32Ret);
        }

        usleep(10*1000);
    }

__FAILED:
    if (ctx->bAttachPool) {
        RK_MPI_VI_DetachMbPool(ctx->pipeId, ctx->channelId);
        RK_MPI_MB_DestroyPool(ctx->attachPool);
    }
    // 9. disable one chn
    s32Ret = RK_MPI_VI_DisableChn(ctx->pipeId, ctx->channelId);
    // 10.disable dev(will diabled all chn)
    s32Ret = RK_MPI_VI_DisableDev(ctx->devId);

    return s32Ret;
}

static RK_S32 test_vi_bind_vo_loop(TEST_VI_CTX_S *ctx) {
#ifdef HAVE_API_MPI_VO
    MPP_CHN_S stSrcChn, stDestChn;
    RK_S32 loopCount = 0;
    void *pData = RK_NULL;
    RK_S32 s32Ret = RK_FAILURE;
    RK_U32 i;

    s32Ret = test_vi_init(ctx);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("vi %d:%d init failed:%x", ctx->devId, ctx->channelId, s32Ret);
        goto __FAILED;
    }

    // vo  init and create
    ctx->s32VoLayer = RK356X_VOP_LAYER_CLUSTER_0;
    ctx->s32VoDev = RK356X_VO_DEV_HD0;
    s32Ret = create_vo(ctx, 0);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("create vo ch:%d failed", ctx->channelId);
        goto __FAILED;
    }
    // bind vi to vo
    stSrcChn.enModId    = RK_ID_VI;
    stSrcChn.s32DevId   = ctx->devId;
    stSrcChn.s32ChnId   = ctx->channelId;

    stDestChn.enModId   = RK_ID_VO;
    stDestChn.s32DevId  = ctx->s32VoLayer;
    stDestChn.s32ChnId  = 0;

    s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("vi band vo fail:%x", s32Ret);
        goto __FAILED;
    }

    // enable vo
    s32Ret = RK_MPI_VO_EnableChn(ctx->s32VoLayer, 0);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("Enalbe vo chn failed, s32Ret = %d\n", s32Ret);
        goto __FAILED;
    }

    while (loopCount < ctx->loopCountSet) {
        loopCount++;
        RK_LOGE("loopCount:%d", loopCount);
        // can not get the vo frameout count . so here regard as 33ms one frame.
        usleep(33*1000);
    }

__FAILED:
    if (ctx->bAttachPool) {
        RK_MPI_VI_DetachMbPool(ctx->pipeId, ctx->channelId);
        RK_MPI_MB_DestroyPool(ctx->attachPool);
    }

    s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
    }
    // disable vo
    RK_MPI_VO_DisableChn(ctx->s32VoLayer, 0);
    RK_MPI_VO_DisableLayer(ctx->s32VoLayer);
    RK_MPI_VO_Disable(ctx->s32VoDev);

    // 5. disable one chn
    s32Ret = RK_MPI_VI_DisableChn(ctx->pipeId, ctx->channelId);
    RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);

    RK_MPI_VO_DisableChn(ctx->s32VoLayer, 0);

    // 6.disable dev(will diabled all chn)
    s32Ret = RK_MPI_VI_DisableDev(ctx->devId);
    RK_LOGE("RK_MPI_VI_DisableDev %x", s32Ret);

    return s32Ret;
#else
    return 0;
#endif
}

static RK_S32 test_vi_bind_vpss_venc_loop(TEST_VI_CTX_S *ctx) {
#if (defined HAVE_API_MPI_VPSS) && (defined HAVE_API_MPI_VENC)
    MPP_CHN_S stViChn, stVencChn[TEST_VENC_MAX], stVpssChn;
    RK_S32 loopCount = 0;
    void *pData = RK_NULL;
    RK_S32 s32Ret = RK_FAILURE;
    RK_U32 i;
    RK_U32 u32DstCount = 1;

    s32Ret = test_vi_init(ctx);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("vi %d:%d init failed:%x", ctx->devId, ctx->channelId, s32Ret);
        goto __FAILED;
    }

    /* vpss */
    ctx->stVpssCfg.u32VpssChnCnt = 1;
    ctx->stVpssCfg.stGrpVpssAttr.u32MaxW = 4096;
    ctx->stVpssCfg.stGrpVpssAttr.u32MaxH = 4096;
    ctx->stVpssCfg.stGrpVpssAttr.enPixelFormat = RK_FMT_YUV420SP;
    ctx->stVpssCfg.stGrpVpssAttr.stFrameRate.s32SrcFrameRate = -1;
    ctx->stVpssCfg.stGrpVpssAttr.stFrameRate.s32DstFrameRate = -1;
    ctx->stVpssCfg.stGrpVpssAttr.enCompressMode = COMPRESS_MODE_NONE;
    for (i = 0; i < VPSS_MAX_CHN_NUM; i ++) {
        ctx->stVpssCfg.stVpssChnAttr[i].enChnMode = VPSS_CHN_MODE_PASSTHROUGH;
        ctx->stVpssCfg.stVpssChnAttr[i].enDynamicRange = DYNAMIC_RANGE_SDR8;
        ctx->stVpssCfg.stVpssChnAttr[i].enPixelFormat = RK_FMT_YUV420SP;
        ctx->stVpssCfg.stVpssChnAttr[i].stFrameRate.s32SrcFrameRate = -1;
        ctx->stVpssCfg.stVpssChnAttr[i].stFrameRate.s32DstFrameRate = -1;
        ctx->stVpssCfg.stVpssChnAttr[i].u32Width = ctx->width;
        ctx->stVpssCfg.stVpssChnAttr[i].u32Height = ctx->height;
        ctx->stVpssCfg.stVpssChnAttr[i].enCompressMode = COMPRESS_MODE_NONE;
    }

    // init vpss
    s32Ret = create_vpss(&ctx->stVpssCfg, 0, ctx->stVpssCfg.u32VpssChnCnt);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("creat 0 grp vpss failed!");
        goto __FAILED;
    }
    // bind vi to vpss
    stViChn.enModId    = RK_ID_VI;
    stViChn.s32DevId   = ctx->devId;
    stViChn.s32ChnId   = ctx->channelId;
    stVpssChn.enModId = RK_ID_VPSS;
    stVpssChn.s32DevId = 0;
    stVpssChn.s32ChnId = 0;

    RK_LOGD("vi to vpss ch %d vpss group %d", stVpssChn.s32ChnId , stVpssChn.s32DevId);
    s32Ret = RK_MPI_SYS_Bind(&stViChn, &stVpssChn);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("vi and vpss bind error ");
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
        // bind vpss to venc
        stVencChn[i].enModId   = RK_ID_VENC;
        stVencChn[i].s32DevId  = i;
        stVencChn[i].s32ChnId  = ctx->stVencCfg[i].s32ChnId;

        s32Ret = RK_MPI_SYS_Bind(&stVpssChn, &stVencChn[i]);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("create %d ch venc failed", ctx->stVencCfg[i].s32ChnId);
            goto __FAILED;
        }
        ctx->stFrame[i].pstPack = reinterpret_cast<VENC_PACK_S *>(malloc(sizeof(VENC_PACK_S)));
#if TEST_WITH_FD
        ctx->stVencCfg[i].selectFd = RK_MPI_VENC_GetFd(ctx->stVencCfg[i].s32ChnId);
        RK_LOGE("venc chn:%d, ctx->selectFd:%d ", ctx->stVencCfg[i].s32ChnId, ctx->stVencCfg[i].selectFd);
#endif
    }

    while (loopCount < ctx->loopCountSet) {
		RK_LOGE("loopCountSet: %u\n", loopCount);
        for (i = 0; i < u32DstCount; i++) {
#if TEST_WITH_FD
            test_vi_poll_event(-1, ctx->stVencCfg[i].selectFd);
#endif

#ifdef FREEZE_TEST
            // freeze test
            RK_MPI_VI_SetChnFreeze(ctx->pipeId, ctx->channelId, ctx->bFreeze);
#endif

            s32Ret = RK_MPI_VENC_GetStream(ctx->stVencCfg[i].s32ChnId, &ctx->stFrame[i], -1);
            if (s32Ret == RK_SUCCESS) {
                if (ctx->stVencCfg[i].bOutDebugCfg) {
                    pData = RK_MPI_MB_Handle2VirAddr(ctx->stFrame[i].pstPack->pMbBlk);
                    fwrite(pData, 1, ctx->stFrame[i].pstPack->u32Len, ctx->stVencCfg[i].fp);
                    fflush(ctx->stVencCfg[i].fp);
                }
                RK_LOGD("chn:%d, loopCount:%d enc->seq:%d wd:%d\n", i, loopCount,
                         ctx->stFrame[i].u32Seq, ctx->stFrame[i].pstPack->u32Len);
                s32Ret = RK_MPI_VENC_ReleaseStream(ctx->stVencCfg[i].s32ChnId, &ctx->stFrame[i]);
                if (s32Ret != RK_SUCCESS) {
                    RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
                }
                loopCount++;
            } else {
                RK_LOGE("RK_MPI_VI_GetChnFrame fail %x", s32Ret);
            }
        }
    }

__FAILED:
    if (ctx->bAttachPool) {
        RK_MPI_VI_DetachMbPool(ctx->pipeId, ctx->channelId);
        RK_MPI_MB_DestroyPool(ctx->attachPool);
    }
    // unbind vi->vpss
    s32Ret = RK_MPI_SYS_UnBind(&stViChn, &stVpssChn);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
    }
    // unbind vpss->venc
    for (i = 0; i < u32DstCount; i++) {
        s32Ret = RK_MPI_SYS_UnBind(&stVpssChn, &stVencChn[i]);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
        }
    }
    // destory vpss
    s32Ret = destory_vpss(0, ctx->stVpssCfg.u32VpssChnCnt);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("destory vpss error");
        return s32Ret;
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
    return s32Ret;
#else
    return 0;
#endif
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

    while (loopCount < ctx->loopCountSet) {
		RK_LOGE("loopCountSet: %u\n", loopCount);
        for (i = 0; i < u32DstCount; i++) {
#if TEST_WITH_FD
            test_vi_poll_event(-1, ctx->stVencCfg[i].selectFd);
#endif

#ifdef FREEZE_TEST
            // freeze test
            RK_MPI_VI_SetChnFreeze(ctx->pipeId, ctx->channelId, ctx->bFreeze);
#endif

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
                loopCount++;
            } else {
                RK_LOGE("RK_MPI_VI_GetChnFrame fail %x", s32Ret);
            }
        }
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
    return s32Ret;
#else
    return 0;
#endif
}

static RK_S32 test_vi_get_release_frame_loop(TEST_VI_CTX_S *ctx) {
    RK_S32 s32Ret;
    RK_S32 loopCount = 0;
    RK_S32 waitTime = -1;
    RK_S64 s64FirstFrameTime = 0;
    RK_S64 s64StartTime;

    s64StartTime = TEST_COMM_GetNowUs();

    /* test use getframe&release_frame */
    s32Ret = test_vi_init(ctx);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("vi %d:%d init failed:%x", ctx->devId, ctx->channelId, s32Ret);
        goto __FAILED;
    }

    if (!ctx->bGetStream) { // get frames in another process.
        while (!bquit) {
            usleep(1000);
        }
    }

    /*below test for second thread to get video frame*/
    pthread_t another_getframe_thread;
    if (ctx->bSecondThr) {
        pthread_create(&another_getframe_thread, RK_NULL, SecondGetFrame, (void *)ctx);
    }

    while (loopCount < ctx->loopCountSet && ctx->bGetStream && !bquit) {
#if TEST_WITH_FD_SWITCH
        if (loopCount % 10 == 0 && ctx->selectFd != -1) {  // test close/reopen the fd
            RK_MPI_VI_CloseChnFd(ctx->pipeId, ctx->channelId);
            RK_LOGE("close ctx->pipeId=%d, ctx->channelId=%d, ctx->selectFd:%d",
                     ctx->pipeId, ctx->channelId, ctx->selectFd);
            ctx->selectFd = -1;
        } else {
            if (ctx->selectFd < 0) {
                ctx->selectFd = RK_MPI_VI_GetChnFd(ctx->pipeId, ctx->channelId);
                RK_LOGE("regetfd ctx->pipeId=%d, ctx->channelId=%d, ctx->selectFd:%d",
                         ctx->pipeId, ctx->channelId, ctx->selectFd);
                // do not use non-block polling for the first time after switching fd
                test_vi_poll_event(33, ctx->selectFd);
            } else {
                test_vi_poll_event(-1, ctx->selectFd);
            }
        }
#elif TEST_WITH_FD
        test_vi_poll_event(-1, ctx->selectFd);
#endif

        /* test user picture */
        if (loopCount > 5 && ctx->bUserPicEnabled) {
            ctx->bUserPicEnabled = RK_FALSE;
            RK_MPI_VI_DisableUserPic(ctx->pipeId, ctx->channelId);
            if (ctx->stUsrPic.enUsrPicMode == VI_USERPIC_MODE_PIC &&
                ctx->stUsrPic.unUsrPic.stUsrPicFrm.stVFrame.pMbBlk) {
                RK_MPI_MMZ_Free(ctx->stUsrPic.unUsrPic.stUsrPicFrm.stVFrame.pMbBlk);
                ctx->stUsrPic.unUsrPic.stUsrPicFrm.stVFrame.pMbBlk = RK_NULL;
            }
        }
#ifdef FREEZE_TEST
        // freeze test
        RK_MPI_VI_SetChnFreeze(ctx->pipeId, ctx->channelId, ctx->bFreeze);
#endif

        // 5.get the frame
        s32Ret = RK_MPI_VI_GetChnFrame(ctx->pipeId, ctx->channelId, &ctx->stViFrame, waitTime);
        if (s32Ret == RK_SUCCESS) {
            RK_U64 nowUs = TEST_COMM_GetNowUs();
            if (!s64FirstFrameTime) {
                s64FirstFrameTime = nowUs - s64StartTime;
                RK_LOGI("get first frame use time:%lld ms", s64FirstFrameTime / 1000);
            }

            void *data = RK_MPI_MB_Handle2VirAddr(ctx->stViFrame.stVFrame.pMbBlk);

            RK_LOGD("RK_MPI_VI_GetChnFrame ok:data %p loop:%d seq:%d pts:%lld ms len=%d", data, loopCount,
                     ctx->stViFrame.stVFrame.u32TimeRef, ctx->stViFrame.stVFrame.u64PTS/1000,
                     RK_MPI_MB_GetLength(ctx->stViFrame.stVFrame.pMbBlk));
            // 6.get the channel status
            s32Ret = RK_MPI_VI_QueryChnStatus(ctx->pipeId, ctx->channelId, &ctx->stChnStatus);
            RK_LOGD("RK_MPI_VI_QueryChnStatus ret %x, w:%d,h:%d,enable:%d," \
                    "current frame id:%d,input lost:%d,output lost:%d," \
                    "framerate:%d,vbfail:%d delay=%lldus",
                     s32Ret,
                     ctx->stChnStatus.stSize.u32Width,
                     ctx->stChnStatus.stSize.u32Height,
                     ctx->stChnStatus.bEnable,
                     ctx->stChnStatus.u32CurFrameID,
                     ctx->stChnStatus.u32InputLostFrame,
                     ctx->stChnStatus.u32OutputLostFrame,
                     ctx->stChnStatus.u32FrameRate,
                     ctx->stChnStatus.u32VbFail,
                     nowUs - ctx->stViFrame.stVFrame.u64PTS);

            // 7.release the frame
            s32Ret = RK_MPI_VI_ReleaseChnFrame(ctx->pipeId, ctx->channelId, &ctx->stViFrame);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
            }
            loopCount++;
        } else {
            RK_LOGE("RK_MPI_VI_GetChnFrame timeout %x", s32Ret);
        }

        usleep(10*1000);
    }

    if (ctx->bSecondThr)
        pthread_join(another_getframe_thread, RK_NULL);

__FAILED:
    if (ctx->bAttachPool) {
        RK_MPI_VI_DetachMbPool(ctx->pipeId, ctx->channelId);
        RK_MPI_MB_DestroyPool(ctx->attachPool);
    }
    // if enable rgn,deinit it and destory it.
    if (ctx->bEnRgn) {
        destory_rgn(ctx);
    }

    // 9. disable one chn
    if (ctx->bUseExt) {
        s32Ret = RK_MPI_VI_StopPipe(ctx->pipeId);
        RK_LOGD("RK_MPI_VI_StopPipe %x", s32Ret);

        s32Ret = RK_MPI_VI_DisableChnExt(ctx->pipeId, ctx->channelId);
        RK_LOGD("RK_MPI_VI_DisableChnExt %x", s32Ret);
    } else {
        s32Ret = RK_MPI_VI_DisableChn(ctx->pipeId, ctx->channelId);
        RK_LOGD("RK_MPI_VI_DisableChn %x", s32Ret);
    }

    // 10.disable dev(will diabled all chn)
    s32Ret = RK_MPI_VI_DisableDev(ctx->devId);
    RK_LOGD("RK_MPI_VI_DisableDev %x", s32Ret);
    return s32Ret;
}

static RK_S32 create_vi(TEST_VI_CTX_S *ctx) {
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
        s32Ret = RK_MPI_VI_SetDevBindPipe(ctx->devId, &ctx->stBindPipe);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_SetDevBindPipe %x", s32Ret);
            goto __FAILED;
        }
    } else {
        RK_LOGE("RK_MPI_VI_EnableDev already");
    }
    // 2.config channel
    s32Ret = RK_MPI_VI_SetChnAttr(ctx->pipeId, ctx->channelId, &ctx->stChnAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VI_SetChnAttr %x", s32Ret);
        goto __FAILED;
    }
    // 3.enable channel
    RK_LOGD("RK_MPI_VI_EnableChn %x %d %d", ctx->devId, ctx->pipeId, ctx->channelId);
    s32Ret = RK_MPI_VI_EnableChn(ctx->pipeId, ctx->channelId);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VI_EnableChn %x", s32Ret);
        goto __FAILED;
    }
    // 4.save debug file
    if (ctx->stDebugFile.bCfg) {
        s32Ret = RK_MPI_VI_ChnSaveFile(ctx->pipeId, ctx->channelId, &ctx->stDebugFile);
        RK_LOGD("RK_MPI_VI_ChnSaveFile %x", s32Ret);
    }

__FAILED:
    return s32Ret;
}

static RK_S32 destroy_vi(TEST_VI_CTX_S *ctx) {
    RK_S32 s32Ret = RK_FAILURE;
    s32Ret = RK_MPI_VI_DisableChn(ctx->pipeId, ctx->channelId);
    RK_LOGE("RK_MPI_VI_DisableChn pipe=%d ret:%x", ctx->pipeId, s32Ret);

    s32Ret = RK_MPI_VI_DisableDev(ctx->devId);
    RK_LOGE("RK_MPI_VI_DisableDev pipe=%d ret:%x", ctx->pipeId, s32Ret);

    RK_SAFE_FREE(ctx);
    return s32Ret;
}

static RK_S32 test_vi_muti_vi_loop(TEST_VI_CTX_S *ctx_out) {
    RK_S32 s32Ret = RK_FAILURE;
    TEST_VI_CTX_S *pstViCtx[TEST_VI_SENSOR_NUM];
    RK_S32 loopCount = 0;
    RK_S32 waitTime = 33;
    RK_S32 i = 0;

    for (i = 0; i < TEST_VI_SENSOR_NUM; i++) {
        pstViCtx[i] = reinterpret_cast<TEST_VI_CTX_S *>(malloc(sizeof(TEST_VI_CTX_S)));
        memset(pstViCtx[i], 0, sizeof(TEST_VI_CTX_S));

        /* vi config init */
        pstViCtx[i]->devId = i;
        pstViCtx[i]->pipeId = pstViCtx[i]->devId;
        pstViCtx[i]->channelId = 2;

        if (TEST_VI_SENSOR_NUM == 2) {
            pstViCtx[i]->stChnAttr.stSize.u32Width = 2688;
            pstViCtx[i]->stChnAttr.stSize.u32Height = 1520;
        } else if (TEST_VI_SENSOR_NUM == 4 || TEST_VI_SENSOR_NUM == 6) {
            pstViCtx[i]->stChnAttr.stSize.u32Width = 2560;
            pstViCtx[i]->stChnAttr.stSize.u32Height = 1520;
        }
        pstViCtx[i]->stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;

        pstViCtx[i]->stChnAttr.stIspOpt.u32BufCount = 10;
        pstViCtx[i]->stChnAttr.u32Depth = 2;
        pstViCtx[i]->stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
        pstViCtx[i]->stChnAttr.enCompressMode = COMPRESS_AFBC_16x16;
        pstViCtx[i]->stChnAttr.stFrameRate.s32SrcFrameRate = -1;
        pstViCtx[i]->stChnAttr.stFrameRate.s32DstFrameRate = -1;

        /* vi create */
        s32Ret = create_vi(pstViCtx[i]);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("vi [%d, %d] init failed: %x",
                    pstViCtx[i]->devId, pstViCtx[i]->channelId,
                    s32Ret);
            goto __FAILED_VI;
        }
    }

    while (loopCount < ctx_out->loopCountSet) {
        for (i = 0; i < TEST_VI_SENSOR_NUM; i++) {
            // get the frame
            s32Ret = RK_MPI_VI_GetChnFrame(pstViCtx[i]->pipeId, pstViCtx[i]->channelId,
                                           &pstViCtx[i]->stViFrame, waitTime);
            if (s32Ret == RK_SUCCESS) {
                RK_U64 nowUs = TEST_COMM_GetNowUs();
                void *data = RK_MPI_MB_Handle2VirAddr(pstViCtx[i]->stViFrame.stVFrame.pMbBlk);
                RK_LOGD("dev %d GetChnFrame Success!", i);
                RK_LOGD("RK_MPI_VI_GetChnFrame ok:data %p loop:%d seq:%d pts:%lld ms len=%d",
                        data, loopCount, pstViCtx[i]->stViFrame.stVFrame.u32TimeRef,
                        pstViCtx[i]->stViFrame.stVFrame.u64PTS/1000,
                        RK_MPI_MB_GetLength(pstViCtx[i]->stViFrame.stVFrame.pMbBlk));
                // get the channel status
                s32Ret = RK_MPI_VI_QueryChnStatus(pstViCtx[i]->pipeId, pstViCtx[i]->channelId,
                                                  &pstViCtx[i]->stChnStatus);
                RK_LOGD("RK_MPI_VI_QueryChnStatus ret %x, w:%d,h:%d,enable:%d," \
                        "input lost:%d, output lost:%d, framerate:%d,vbfail:%d delay=%lldus",
                        s32Ret,
                        pstViCtx[i]->stChnStatus.stSize.u32Width,
                        pstViCtx[i]->stChnStatus.stSize.u32Height,
                        pstViCtx[i]->stChnStatus.bEnable,
                        pstViCtx[i]->stChnStatus.u32InputLostFrame,
                        pstViCtx[i]->stChnStatus.u32OutputLostFrame,
                        pstViCtx[i]->stChnStatus.u32FrameRate,
                        pstViCtx[i]->stChnStatus.u32VbFail,
                        nowUs - pstViCtx[i]->stViFrame.stVFrame.u64PTS);
                // release the frame
                s32Ret = RK_MPI_VI_ReleaseChnFrame(pstViCtx[i]->pipeId, pstViCtx[i]->channelId,
                                                   &pstViCtx[i]->stViFrame);
                if (s32Ret != RK_SUCCESS) {
                    RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
                }
            } else {
                RK_LOGE("dev %d RK_MPI_VI_GetChnFrame timeout %x", i, s32Ret);
            }
            usleep(3*1000);
        }
        loopCount++;
        usleep(10*1000);
    }

__FAILED_VI:
    /* destroy vi*/
    for (RK_S32 i = 0; i < TEST_VI_SENSOR_NUM; i++) {
        s32Ret = destroy_vi(pstViCtx[i]);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("destroy vi [%d, %d] error %x",
                    pstViCtx[i]->devId, pstViCtx[i]->channelId,
                    s32Ret);
            goto __FAILED;
        }
    }
__FAILED:
    return s32Ret;
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
    "\t0)./rk_mpi_vi_test -w 1920 -h 1080 -t 4 -n rkispp_scale0  (command only for rv1126)",
    "\t0)./rk_mpi_vi_test -w 1920 -h 1080 -t 4 -c 0 -d 0 -m 0 -l 10 -o 1  (not limit)",
    "\t1)./rk_mpi_vi_test -w 1920 -h 1080 -m 1 -t 4 -c 1 -l 10  (not limit)",
    "\t2)./rk_mpi_vi_test -w 640 -h 480 -m 2 -t 4 -c 1 -d 0 -l 10  (not limit)",
    "\t3)./rk_mpi_vi_test -w 1920 -h 1080 -m 3 -t 4 -c 1 -l 10  (not limit)",
    "\t4)./rk_mpi_vi_test -w 1920 -h 1080 -m 3 -t 4 -c 1 -l 10  (not limit)",
    "\t13)./rk_mpi_vi_test -w 1920 -h 1080 -d 0 -m 13 -l 100 (not limit)",
    "\t14)./rk_mpi_vi_test -w 2880 -h 1616 -c 0 -d 0 -m 14 --en_eptz 1 -o 1 (not limit)",
    "\t15)./rk_mpi_vi_test -w 1920 -h 1080 -d 0 -m 15 -l 10 --module_test_loop 100 --module_test_type 0",
    "\t16)./rk_mpi_vi_test -w 1920 -h 1080 -d 0 -c 0 -m 16 -l 10 --wrap=1 --wrap_line=540",
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
    ctx->width = 0;
    ctx->height = 0;
    ctx->devId = 0;
    ctx->pipeId = ctx->devId;
    ctx->channelId = 1;
    ctx->loopCountSet = 100;
    ctx->enMode = TEST_VI_MODE_BIND_VENC;
    ctx->stChnAttr.stIspOpt.u32BufCount = 3;
    ctx->stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
    ctx->stChnAttr.stIspOpt.enCaptureType = VI_V4L2_CAPTURE_TYPE_VIDEO_CAPTURE;
    ctx->stChnAttr.u32Depth = 2;
    ctx->aEntityName = RK_NULL;
    ctx->stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
    ctx->stChnAttr.stFrameRate.s32SrcFrameRate = -1;
    ctx->stChnAttr.stFrameRate.s32DstFrameRate = -1;
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
    ctx->u32DstCodec = RK_VIDEO_ID_HEVC;
    ctx->bEnOverlay = RK_FALSE;
    ctx->s32Snap = 1;
    ctx->maxWidth = 0;
    ctx->maxHeight = 0;
    ctx->bSaveVlogPath = NULL;
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

    if (ctx->bEnRgn) {
        if ((ctx->mosaicBlkSize < MOSAIC_BLK_SIZE_8) ||
            (ctx->mosaicBlkSize > MOSAIC_BLK_SIZE_64)) {
            printf("Error: RGN mosaic block size config, config it at [0 - 3]!\n");
            goto __FAILED2;
        }
    }

    if (ctx->stDebugFile.bCfg) {
        if (ctx->enMode == TEST_VI_MODE_BIND_VENC || ctx->enMode == TEST_VI_MODE_BIND_VPSS_BIND_VENC
            || TEST_VI_BIND_VENC_WRAP_SWITCH) {
            ctx->stVencCfg[0].bOutDebugCfg = ctx->stDebugFile.bCfg;
        } else if (ctx->enMode == TEST_VI_MODE_BIND_VENC_MULTI) {
            ctx->stVencCfg[0].bOutDebugCfg = ctx->stDebugFile.bCfg;
            ctx->stVencCfg[1].bOutDebugCfg = ctx->stDebugFile.bCfg;
        }
        if (ctx->pSavePath)
            memcpy(&ctx->stDebugFile.aFilePath, ctx->pSavePath, strlen(ctx->pSavePath));
        else
            memcpy(&ctx->stDebugFile.aFilePath, "/data/", strlen("/data/"));
        snprintf(ctx->stDebugFile.aFileName, MAX_VI_FILE_PATH_LEN,
                 "test_%d_%d_%d.bin", ctx->devId, ctx->pipeId, ctx->channelId);
    }
    for (i = 0; i < TEST_VENC_MAX; i++) {
        if (ctx->stVencCfg[i].bOutDebugCfg) {
            char name[256] = {0};
            snprintf(ctx->stVencCfg[i].dstFileName, sizeof(ctx->stVencCfg[i].dstFileName),
                   "venc_%d.bin", i);
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
    do {
        switch (ctx->enMode) {
            case TEST_VI_MODE_VI_ONLY:
                signal(SIGINT, sigterm_handler);
                if (!ctx->stChnAttr.u32Depth) {
                    RK_LOGE("depth need > 0 when vi not bind any other module!");
                    ctx->stChnAttr.u32Depth = ctx->stChnAttr.stIspOpt.u32BufCount;
                }
                s32Ret = test_vi_get_release_frame_loop(ctx);
            break;
            case TEST_VI_MODE_BIND_VENC:
            case TEST_VI_MODE_BIND_VENC_MULTI:
                s32Ret = test_vi_bind_venc_loop(ctx);
            break;
            case TEST_VI_MODE_BIND_VPSS_BIND_VENC:
                s32Ret = test_vi_bind_vpss_venc_loop(ctx);
            break;
            case TEST_VI_MODE_BIND_VO:
                s32Ret = test_vi_bind_vo_loop(ctx);
            break;
            case TEST_VI_MODE_MULTI_VI:
                s32Ret = test_vi_muti_vi_loop(ctx);
            break;
            case TEST_VI_MODE_VI_STREAM_ONLY:
                if (!ctx->stChnAttr.u32Depth) {
                    RK_LOGE("depth need > 0 when vi not bind any other module!");
                    ctx->stChnAttr.u32Depth = ctx->stChnAttr.stIspOpt.u32BufCount;
                }
                s32Ret = test_vi_get_release_stream_loop(ctx);
            break;
            case TEST_VI_MODE_BIND_VDEC_BIND_VO:
                s32Ret = test_vi_bind_vdec_bind_vo_loop(ctx);
            break;
            case TEST_VI_MODE_BIND_IVS:
                s32Ret = test_vi_bind_ivs_loop(ctx);
            break;
            case TEST_VI_MODE_MULTI_CHN:    // rv1106/ 1103
                s32Ret = test_vi_one_sensor_multi_chn_loop(ctx);
            break;
            case TEST_VI_EPTZ_VI_ONLY:
                s32Ret = test_vi_eptz_vi_only(ctx);
            break;
            case TEST_VI_FUNC_MODE:
                s32Ret = test_vi_func_mode(ctx);
            break;
            case TEST_VI_BIND_VENC_WRAP_SWITCH:
                s32Ret = test_vi_bind_venc_wrap_switch(ctx);
            break;
            default:
                RK_LOGE("no support such test mode:%d", ctx->enMode);
            break;
        }

        s32TestCount++;
        RK_LOGI("-------------------mode %d Test success Total: %d, Now Count:%d-------------------\n",
                ctx->enMode, ctx->modTestCnt, s32TestCount);
        sleep(1);
    } while (s32TestCount < ctx->modTestCnt);

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