#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <linux/videodev2.h>
#include <jpeglib.h>

#include "cvi_sys.h"
#include "cvi_vb.h"
#include "cvi_venc.h"
#include "cvi_comm_venc.h"
#include "cvi_comm_video.h"

/* ── 配置 ──────────────────────────────────────────────── */
#define CAMERA_DEV      "/dev/video0"
#define CAP_WIDTH       1280
#define CAP_HEIGHT      720
#define CAP_FPS         30
#define VENC_BITRATE    800000
#define VENC_CHN        0
#define RECORD_DIR      "/mnt/data/recordings"
#define SOCKET_PATH     "/tmp/camera_daemon.sock"
#define HTTP_PORT       8554
#define V4L2_BUF_COUNT  4
#define MAX_CLIENTS     8

/* ── V4L2 buffer ─────────────────────────────────────── */
struct v4l2_buffer_info {
    void   *start;
    size_t  length;
};

/* ── MJPEG 帧缓存 ────────────────────────────────────── */
typedef struct {
    unsigned char *data;
    size_t         size;
    uint64_t       seq;
} MjpegFrame;

/* ── 全局状态 ─────────────────────────────────────────── */
static volatile int    g_running     = 1;
static volatile int    g_recording   = 0;
static int             g_cam_fd      = -1;
static FILE           *g_h264_fp     = NULL;
static char            g_current_file[256] = {0};

static pthread_mutex_t g_record_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_frame_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_frame_cond   = PTHREAD_COND_INITIALIZER;

static MjpegFrame      g_frame        = {NULL, 0, 0};
static struct v4l2_buffer_info g_buffers[V4L2_BUF_COUNT];

/* ================================================================
 * V4L2
 * ================================================================ */
static int v4l2_open_camera(void)
{
    int fd = open(CAMERA_DEV, O_RDWR | O_NONBLOCK);
    if (fd < 0) { perror("[v4l2] open"); return -1; }

    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("[v4l2] QUERYCAP"); close(fd); return -1;
    }
    printf("[v4l2] Device: %s\n", cap.card);

    struct v4l2_format fmt = {0};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = CAP_WIDTH;
    fmt.fmt.pix.height      = CAP_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("[v4l2] S_FMT"); close(fd); return -1;
    }

    struct v4l2_streamparm parm = {0};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = CAP_FPS;
    ioctl(fd, VIDIOC_S_PARM, &parm);

    struct v4l2_requestbuffers req = {0};
    req.count  = V4L2_BUF_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("[v4l2] REQBUFS"); close(fd); return -1;
    }

    for (int i = 0; i < V4L2_BUF_COUNT; i++) {
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("[v4l2] QUERYBUF"); close(fd); return -1;
        }
        g_buffers[i].length = buf.length;
        g_buffers[i].start  = mmap(NULL, buf.length,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, buf.m.offset);
        if (g_buffers[i].start == MAP_FAILED) {
            perror("[v4l2] mmap"); close(fd); return -1;
        }
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("[v4l2] QBUF"); close(fd); return -1;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("[v4l2] STREAMON"); close(fd); return -1;
    }

    printf("[v4l2] Camera started: %dx%d @ %dfps MJPEG\n",
           CAP_WIDTH, CAP_HEIGHT, CAP_FPS);
    return fd;
}

static void v4l2_close_camera(int fd)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (int i = 0; i < V4L2_BUF_COUNT; i++)
        munmap(g_buffers[i].start, g_buffers[i].length);
    close(fd);
}

/* ================================================================
 * MJPEG -> YUV420P
 * ================================================================ */
static int mjpeg_to_yuv420(const unsigned char *jpeg_data, size_t jpeg_size,
                            unsigned char *yuv_buf, int width, int height)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)jpeg_data, jpeg_size);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    cinfo.out_color_space = JCS_YCbCr;
    jpeg_start_decompress(&cinfo);

    unsigned char *row_buf = malloc((size_t)width * 3);
    unsigned char *y_plane = yuv_buf;
    unsigned char *u_plane = yuv_buf + width * height;
    unsigned char *v_plane = yuv_buf + width * height * 5 / 4;

    int y = 0;
    while ((int)cinfo.output_scanline < height) {
        JSAMPROW row_ptr = row_buf;
        jpeg_read_scanlines(&cinfo, &row_ptr, 1);
        for (int x = 0; x < width; x++) {
            y_plane[y * width + x] = row_buf[x * 3];
            if ((y % 2 == 0) && (x % 2 == 0)) {
                u_plane[(y/2)*(width/2)+(x/2)] = row_buf[x*3+1];
                v_plane[(y/2)*(width/2)+(x/2)] = row_buf[x*3+2];
            }
        }
        y++;
    }

    free(row_buf);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return 0;
}

/* ================================================================
 * cvi_mpi VENC
 * ================================================================ */
static int venc_init(void)
{
    CVI_SYS_Init();

    VB_CONFIG_S stVbConf;
    memset(&stVbConf, 0, sizeof(stVbConf));
    stVbConf.u32MaxPoolCnt = 1;
    stVbConf.astCommPool[0].u32BlkSize = CAP_WIDTH * CAP_HEIGHT * 3 / 2;
    stVbConf.astCommPool[0].u32BlkCnt  = 3;
    CVI_VB_SetConfig(&stVbConf);
    CVI_VB_Init();

    VENC_CHN_ATTR_S stChnAttr;
    memset(&stChnAttr, 0, sizeof(stChnAttr));
    stChnAttr.stVencAttr.enType          = PT_H264;
    stChnAttr.stVencAttr.u32MaxPicWidth  = CAP_WIDTH;
    stChnAttr.stVencAttr.u32MaxPicHeight = CAP_HEIGHT;
    stChnAttr.stVencAttr.u32PicWidth     = CAP_WIDTH;
    stChnAttr.stVencAttr.u32PicHeight    = CAP_HEIGHT;
    stChnAttr.stVencAttr.u32BufSize      = CAP_WIDTH * CAP_HEIGHT;
    stChnAttr.stVencAttr.u32Profile      = H264E_PROFILE_MAIN;
    stChnAttr.stVencAttr.bByFrame        = CVI_TRUE;

    stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
    stChnAttr.stRcAttr.stH264Cbr.u32Gop          = CAP_FPS * 2;
    stChnAttr.stRcAttr.stH264Cbr.u32StatTime      = 1;
    stChnAttr.stRcAttr.stH264Cbr.u32SrcFrameRate  = CAP_FPS;
    stChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRate = CAP_FPS;
    stChnAttr.stRcAttr.stH264Cbr.u32BitRate       = VENC_BITRATE / 1000;

    stChnAttr.stGopAttr.enGopMode              = VENC_GOPMODE_NORMALP;
    stChnAttr.stGopAttr.stNormalP.s32IPQpDelta = 2;

    CVI_S32 ret = CVI_VENC_CreateChn(VENC_CHN, &stChnAttr);
    if (ret != CVI_SUCCESS) {
        fprintf(stderr, "[venc] CreateChn failed: 0x%x\n", ret);
        return -1;
    }
    CVI_VENC_StartRecvFrame(VENC_CHN, NULL);
    printf("[venc] HW H.264 encoder ready (%dx%d @ %dkbps)\n",
           CAP_WIDTH, CAP_HEIGHT, VENC_BITRATE / 1000);
    return 0;
}

static void venc_deinit(void)
{
    CVI_VENC_StopRecvFrame(VENC_CHN);
    CVI_VENC_DestroyChn(VENC_CHN);
    CVI_VB_Exit();
    CVI_SYS_Exit();
}

/* ================================================================
 * 编码单帧
 * ================================================================ */
static int encode_and_write_frame(unsigned char *yuv_buf)
{
    size_t frame_size = CAP_WIDTH * CAP_HEIGHT * 3 / 2;

    VB_BLK blk = CVI_VB_GetBlock(VB_INVALID_POOLID, frame_size);
    if (blk == VB_INVALID_HANDLE) {
        fprintf(stderr, "[venc] VB_GetBlock failed\n");
        return -1;
    }

    CVI_U64 phys_addr = CVI_VB_Handle2PhysAddr(blk);
    void   *virt_addr = CVI_SYS_MmapCache(phys_addr, frame_size);

    memcpy(virt_addr, yuv_buf, frame_size);
    CVI_SYS_IonFlushCache(phys_addr, virt_addr, frame_size);

    VIDEO_FRAME_INFO_S stFrame;
    memset(&stFrame, 0, sizeof(stFrame));
    stFrame.stVFrame.enPixelFormat     = PIXEL_FORMAT_YUV_PLANAR_420;
    stFrame.stVFrame.u32Width          = CAP_WIDTH;
    stFrame.stVFrame.u32Height         = CAP_HEIGHT;
    stFrame.stVFrame.u32Stride[0]      = CAP_WIDTH;
    stFrame.stVFrame.u32Stride[1]      = CAP_WIDTH / 2;
    stFrame.stVFrame.u32Stride[2]      = CAP_WIDTH / 2;
    stFrame.stVFrame.u64PhyAddr[0]     = phys_addr;
    stFrame.stVFrame.u64PhyAddr[1]     = phys_addr + CAP_WIDTH * CAP_HEIGHT;
    stFrame.stVFrame.u64PhyAddr[2]     = phys_addr + CAP_WIDTH * CAP_HEIGHT * 5 / 4;
    stFrame.stVFrame.pu8VirAddr[0]     = virt_addr;
    stFrame.stVFrame.pu8VirAddr[1]     = (char *)virt_addr + CAP_WIDTH * CAP_HEIGHT;
    stFrame.stVFrame.pu8VirAddr[2]     = (char *)virt_addr + CAP_WIDTH * CAP_HEIGHT * 5 / 4;

        int retry = 0;
    CVI_S32 ret;
    do {
        ret = CVI_VENC_SendFrame(VENC_CHN, &stFrame, 2000);
        if (ret == CVI_SUCCESS) break;
        if ((ret & 0xFF) == 64) {
            usleep(50000);
            retry++;
        } else {
            break;
        }
    } while (retry < 10);
    CVI_SYS_Munmap(virt_addr, frame_size);
    CVI_VB_ReleaseBlock(blk);

    if (ret != CVI_SUCCESS) {
        fprintf(stderr, "[venc] SendFrame failed: 0x%x\n", ret);
        return -1;
    }

    VENC_STREAM_S stStream;
    memset(&stStream, 0, sizeof(stStream));
    stStream.pstPack = malloc(sizeof(VENC_PACK_S) * 8);

    ret = CVI_VENC_GetStream(VENC_CHN, &stStream, 2000);
    if (ret != CVI_SUCCESS) {
        free(stStream.pstPack);
        return -1;
    }

    if (g_h264_fp) {
        for (CVI_U32 i = 0; i < stStream.u32PackCount; i++) {
            VENC_PACK_S *pack = &stStream.pstPack[i];
            void *data_virt = CVI_SYS_Mmap(pack->u64PhyAddr, pack->u32Len);
            fwrite((char *)data_virt + pack->u32Offset,
                   1, pack->u32Len - pack->u32Offset, g_h264_fp);
            CVI_SYS_Munmap(data_virt, pack->u32Len);
        }
    }

    CVI_VENC_ReleaseStream(VENC_CHN, &stStream);
    free(stStream.pstPack);
    return 0;
}

/* ================================================================
 * 内置 MJPEG HTTP server
 * 每个客户端一个线程，直接推帧
 * ================================================================ */
static void *mjpeg_client_thread(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

    /* 发送 HTTP 响应头 */
    const char *header =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=mjpegframe\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n"
        "\r\n";
    if (write(client_fd, header, strlen(header)) < 0) {
        close(client_fd);
        return NULL;
    }

    uint64_t last_seq = 0;
    char part_header[128];

    while (g_running) {
        /* 等待新帧 */
        pthread_mutex_lock(&g_frame_mutex);
        while (g_frame.seq == last_seq && g_running)
            pthread_cond_wait(&g_frame_cond, &g_frame_mutex);

        if (!g_running) {
            pthread_mutex_unlock(&g_frame_mutex);
            break;
        }

        /* 复制帧数据 */
        size_t sz = g_frame.size;
        unsigned char *buf = malloc(sz);
        memcpy(buf, g_frame.data, sz);
        last_seq = g_frame.seq;
        pthread_mutex_unlock(&g_frame_mutex);

        /* 发送 multipart 帧 */
        int hlen = snprintf(part_header, sizeof(part_header),
            "--mjpegframe\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %zu\r\n"
            "\r\n", sz);

        if (write(client_fd, part_header, hlen) < 0 ||
            write(client_fd, buf, sz) < 0 ||
            write(client_fd, "\r\n", 2) < 0) {
            free(buf);
            break;
        }
        free(buf);
    }

    close(client_fd);
    return NULL;
}

static void *http_server_thread(void *arg)
{
    (void)arg;

    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) { perror("[http] socket"); return NULL; }

    int opt = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(HTTP_PORT);

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[http] bind"); close(srv_fd); return NULL;
    }
    listen(srv_fd, MAX_CLIENTS);
    printf("[http] MJPEG server on port %d\n", HTTP_PORT);

    while (g_running) {
        int *cli_fd = malloc(sizeof(int));
        *cli_fd = accept(srv_fd, NULL, NULL);
        if (*cli_fd < 0) { free(cli_fd); continue; }

        /* 读并忽略 HTTP 请求头 */
        char tmp[512];
        recv(*cli_fd, tmp, sizeof(tmp), 0);

        pthread_t tid;
        pthread_create(&tid, NULL, mjpeg_client_thread, cli_fd);
        pthread_detach(tid);
    }

    close(srv_fd);
    return NULL;
}

/* ================================================================
 * Unix socket 命令处理
 * ================================================================ */
static void handle_command(int client_fd)
{
    char buf[256];
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    buf[n] = '\0';

    char resp[256];

    if (strncmp(buf, "start", 5) == 0) {
        pthread_mutex_lock(&g_record_mutex);
        if (g_recording) {
            snprintf(resp, sizeof(resp), "ERR:already_recording");
        } else {
            time_t t = time(NULL);
            struct tm *tm_info = localtime(&t);
            char ts[32];
            strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", tm_info);
            snprintf(g_current_file, sizeof(g_current_file),
                     "%s/rec_%s.h264", RECORD_DIR, ts);
            g_h264_fp = fopen(g_current_file, "wb");
            if (!g_h264_fp) {
                snprintf(resp, sizeof(resp), "ERR:open_file_failed");
            } else {
                g_recording = 1;
                snprintf(resp, sizeof(resp), "OK:%s", g_current_file);
                printf("[daemon] Recording started: %s\n", g_current_file);
            }
        }
        pthread_mutex_unlock(&g_record_mutex);

    } else if (strncmp(buf, "stop", 4) == 0) {
        pthread_mutex_lock(&g_record_mutex);
        if (!g_recording) {
            snprintf(resp, sizeof(resp), "ERR:not_recording");
        } else {
            g_recording = 0;
            if (g_h264_fp) { fclose(g_h264_fp); g_h264_fp = NULL; }
            snprintf(resp, sizeof(resp), "STOPPED:%s", g_current_file);
            printf("[daemon] Recording stopped: %s\n", g_current_file);
            g_current_file[0] = '\0';
        }
        pthread_mutex_unlock(&g_record_mutex);

    } else if (strncmp(buf, "status", 6) == 0) {
        pthread_mutex_lock(&g_record_mutex);
        if (g_recording)
            snprintf(resp, sizeof(resp), "RECORDING:%s", g_current_file);
        else
            snprintf(resp, sizeof(resp), "IDLE");
        pthread_mutex_unlock(&g_record_mutex);

    } else {
        snprintf(resp, sizeof(resp), "ERR:unknown_command");
    }

    send(client_fd, resp, strlen(resp), 1);
}

static void *socket_thread(void *arg)
{
    (void)arg;
    unlink(SOCKET_PATH);

    int srv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv_fd < 0) { perror("[socket] create"); return NULL; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[socket] bind"); close(srv_fd); return NULL;
    }
    listen(srv_fd, 5);
    printf("[socket] Listening on %s\n", SOCKET_PATH);

    while (g_running) {
        int cli_fd = accept(srv_fd, NULL, NULL);
        if (cli_fd < 0) continue;
        handle_command(cli_fd);
        close(cli_fd);
    }

    close(srv_fd);
    unlink(SOCKET_PATH);
    return NULL;
}

/* ================================================================
 * 信号处理
 * ================================================================ */
static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
    pthread_cond_broadcast(&g_frame_cond);
}

/* ================================================================
 * main
 * ================================================================ */
int main(void)
{
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    mkdir(RECORD_DIR, 0755);

    if (venc_init() < 0) {
        fprintf(stderr, "[main] VENC init failed\n");
        return 1;
    }

    g_cam_fd = v4l2_open_camera();
    if (g_cam_fd < 0) {
        fprintf(stderr, "[main] Camera open failed\n");
        venc_deinit();
        return 1;
    }

    /* 分配帧缓存 */
    g_frame.data = malloc(CAP_WIDTH * CAP_HEIGHT * 2);

    /* 启动线程 */
    pthread_t http_tid, sock_tid;
    pthread_create(&http_tid, NULL, http_server_thread, NULL);
    pthread_create(&sock_tid, NULL, socket_thread, NULL);

    unsigned char *yuv_buf = malloc(CAP_WIDTH * CAP_HEIGHT * 3 / 2);
    printf("[main] Camera daemon running...\n");

    while (g_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_cam_fd, &fds);
        struct timeval tv = {1, 0};
        if (select(g_cam_fd + 1, &fds, NULL, NULL, &tv) <= 0) continue;

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(g_cam_fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            perror("[v4l2] DQBUF"); break;
        }

        unsigned char *jpeg_data = g_buffers[buf.index].start;
        size_t         jpeg_size = buf.bytesused;

        /* ① 更新预览帧，通知所有 HTTP 客户端 */
        pthread_mutex_lock(&g_frame_mutex);
        memcpy(g_frame.data, jpeg_data, jpeg_size);
        g_frame.size = jpeg_size;
        g_frame.seq++;
        pthread_cond_broadcast(&g_frame_cond);
        pthread_mutex_unlock(&g_frame_mutex);

        /* ② 录像 */
        pthread_mutex_lock(&g_record_mutex);
        int recording = g_recording;
        pthread_mutex_unlock(&g_record_mutex);

        if (recording) {
            if (mjpeg_to_yuv420(jpeg_data, jpeg_size,
                                yuv_buf, CAP_WIDTH, CAP_HEIGHT) == 0) {
                encode_and_write_frame(yuv_buf);
            }
        }

        ioctl(g_cam_fd, VIDIOC_QBUF, &buf);
    }

    printf("[main] Shutting down...\n");
    pthread_cond_broadcast(&g_frame_cond);

    if (g_recording) {
        g_recording = 0;
        if (g_h264_fp) fclose(g_h264_fp);
    }

    free(yuv_buf);
    free(g_frame.data);
    v4l2_close_camera(g_cam_fd);
    venc_deinit();
    pthread_join(http_tid, NULL);
    pthread_join(sock_tid, NULL);
    return 0;
}