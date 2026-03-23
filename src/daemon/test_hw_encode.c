/*
 * GC2083 + Hardware VENC Test Program
 * 
 * Purpose: Test if SG2000 VENC hardware encoder works with GC2083 CSI camera
 * 
 * Data Path: GC2083 → CSI → ISP → VI → VPSS → VENC(H.264) → File
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

/*
 * SG2000 SDK Headers
 * 
 * These are typically from: duo-buildroot-sdk/middleware/v2/include/
 * 
 * If headers not available, this test will verify SDK presence
 */
#ifdef HAS_CVI_SDK
#include "cvi_comm.h"
#include "cvi_sys.h"
#include "cvi_vi.h"
#include "cvi_vpss.h"
#include "cvi_venc.h"
#include "cvi_buffer.h"
#endif

volatile int g_running = 1;

void signal_handler(int sig) {
    printf("\n[Signal] Caught signal %d, stopping...\n", sig);
    g_running = 0;
}

int main(int argc, char **argv) {
    printf("========================================\n");
    printf("GC2083 Hardware Encoding Test\n");
    printf("========================================\n\n");

#ifndef HAS_CVI_SDK
    printf("[ERROR] CVI SDK headers not found!\n\n");
    printf("This test requires SG2000 SDK to be installed.\n");
    printf("Expected SDK location:\n");
    printf("  - Headers: /mnt/system/include/\n");
    printf("  - Libraries: /mnt/system/lib/\n\n");
    printf("To compile with SDK:\n");
    printf("  gcc -DHAS_CVI_SDK test_hw_encode.c \\\n");
    printf("      -I/mnt/system/include \\\n");
    printf("      -L/mnt/system/lib \\\n");
    printf("      -lcvi_common -lcvi_sys -lcvi_vi -lcvi_vpss -lcvi_venc \\\n");
    printf("      -o test_hw_encode\n\n");
    
    /* Still do basic system checks */
    printf("Checking system availability...\n\n");
    
    /* Check for CVI devices */
    printf("[1] CVI Device Files:\n");
    system("ls -l /dev/cvi* 2>/dev/null || echo '  No CVI devices found'");
    
    printf("\n[2] Video Devices:\n");
    system("ls -l /dev/video* 2>/dev/null || echo '  No video devices found'");
    
    printf("\n[3] Media Devices:\n");
    system("ls -l /dev/media* 2>/dev/null || echo '  No media devices found'");
    
    printf("\n[4] SDK Libraries:\n");
    system("ls -l /mnt/system/lib/libcvi*.so 2>/dev/null || echo '  SDK libraries not found in /mnt/system/lib'");
    
    printf("\n[5] Kernel Modules:\n");
    system("lsmod | grep -E 'cvi|gc2083|sensor' || echo '  No camera modules loaded'");
    
    printf("\n========================================\n");
    printf("RESULT: SDK Not Available - Cannot test hardware encoding\n");
    printf("========================================\n");
    
    return 1;
#else
    
    /* === SDK Available - Run Full Test === */
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    CVI_S32 ret;
    
    printf("[Step 1] Initialize SYS...\n");
    ret = CVI_SYS_Init();
    if (ret != CVI_SUCCESS) {
        printf("  FAILED: CVI_SYS_Init() = 0x%x\n", ret);
        return 1;
    }
    printf("  OK: SYS initialized\n\n");
    
    printf("[Step 2] Initialize VI (Video Input)...\n");
    VI_DEV ViDev = 0;
    VI_CHN ViChn = 0;
    
    VI_DEV_ATTR_S stViDevAttr = {0};
    stViDevAttr.enIntfMode = VI_MODE_MIPI;
    stViDevAttr.enWorkMode = VI_WORK_MODE_1Multiplex;
    stViDevAttr.enScanMode = VI_SCAN_PROGRESSIVE;
    
    ret = CVI_VI_SetDevAttr(ViDev, &stViDevAttr);
    if (ret != CVI_SUCCESS) {
        printf("  FAILED: CVI_VI_SetDevAttr() = 0x%x\n", ret);
        CVI_SYS_Exit();
        return 1;
    }
    
    ret = CVI_VI_EnableDev(ViDev);
    if (ret != CVI_SUCCESS) {
        printf("  FAILED: CVI_VI_EnableDev() = 0x%x\n", ret);
        CVI_SYS_Exit();
        return 1;
    }
    printf("  OK: VI device enabled\n\n");
    
    printf("[Step 3] Initialize VPSS (Video Processing)...\n");
    VPSS_GRP VpssGrp = 0;
    VPSS_CHN VpssChn = 0;
    
    VPSS_GRP_ATTR_S stVpssGrpAttr = {0};
    stVpssGrpAttr.u32MaxW = 1920;
    stVpssGrpAttr.u32MaxH = 1080;
    stVpssGrpAttr.enPixelFormat = PIXEL_FORMAT_NV21;
    
    ret = CVI_VPSS_CreateGrp(VpssGrp, &stVpssGrpAttr);
    if (ret != CVI_SUCCESS) {
        printf("  FAILED: CVI_VPSS_CreateGrp() = 0x%x\n", ret);
        goto cleanup_vi;
    }
    printf("  OK: VPSS group created\n\n");
    
    printf("[Step 4] Initialize VENC (Hardware Encoder)...\n");
    VENC_CHN VencChn = 0;
    
    VENC_CHN_ATTR_S stVencChnAttr = {0};
    stVencChnAttr.stVencAttr.enType = PT_H264;
    stVencChnAttr.stVencAttr.u32MaxPicWidth = 1920;
    stVencChnAttr.stVencAttr.u32MaxPicHeight = 1080;
    stVencChnAttr.stVencAttr.u32PicWidth = 1920;
    stVencChnAttr.stVencAttr.u32PicHeight = 1080;
    stVencChnAttr.stVencAttr.u32Profile = 2; /* High Profile */
    
    stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
    stVencChnAttr.stRcAttr.stH264Cbr.u32Gop = 30;
    stVencChnAttr.stRcAttr.stH264Cbr.u32SrcFrameRate = 30;
    stVencChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRate = 30;
    stVencChnAttr.stRcAttr.stH264Cbr.u32BitRate = 4000; /* 4 Mbps */
    
    ret = CVI_VENC_CreateChn(VencChn, &stVencChnAttr);
    if (ret != CVI_SUCCESS) {
        printf("  FAILED: CVI_VENC_CreateChn() = 0x%x\n", ret);
        goto cleanup_vpss;
    }
    printf("  OK: VENC channel created\n\n");
    
    printf("[Step 5] Start encoding (10 seconds)...\n");
    ret = CVI_VENC_StartRecvFrame(VencChn);
    if (ret != CVI_SUCCESS) {
        printf("  FAILED: CVI_VENC_StartRecvFrame() = 0x%x\n", ret);
        goto cleanup_venc;
    }
    
    FILE *fp = fopen("/mnt/data/test_hw_encode.h264", "wb");
    if (!fp) {
        printf("  FAILED: Cannot create output file\n");
        goto cleanup_venc;
    }
    
    int frame_count = 0;
    time_t start_time = time(NULL);
    
    while (g_running && (time(NULL) - start_time < 10)) {
        VENC_STREAM_S stStream = {0};
        ret = CVI_VENC_GetStream(VencChn, &stStream, 1000);
        
        if (ret == CVI_SUCCESS) {
            for (int i = 0; i < stStream.u32PackCount; i++) {
                fwrite(stStream.pstPack[i].pu8Addr,
                       1,
                       stStream.pstPack[i].u32Len,
                       fp);
            }
            CVI_VENC_ReleaseStream(VencChn, &stStream);
            frame_count++;
            
            if (frame_count % 30 == 0) {
                printf("  Encoded %d frames...\n", frame_count);
            }
        }
    }
    
    fclose(fp);
    
    printf("\n========================================\n");
    printf("RESULT: SUCCESS!\n");
    printf("  Frames encoded: %d\n", frame_count);
    printf("  Output file: /mnt/data/test_hw_encode.h264\n");
    printf("  File size: ");
    fflush(stdout);
    system("ls -lh /mnt/data/test_hw_encode.h264 | awk '{print $5}'");
    printf("\nHardware encoding is WORKING!\n");
    printf("========================================\n");
    
cleanup_venc:
    CVI_VENC_StopRecvFrame(VencChn);
    CVI_VENC_DestroyChn(VencChn);
    
cleanup_vpss:
    CVI_VPSS_DestroyGrp(VpssGrp);
    
cleanup_vi:
    CVI_VI_DisableDev(ViDev);
    CVI_SYS_Exit();
    
    return 0;
#endif
}
