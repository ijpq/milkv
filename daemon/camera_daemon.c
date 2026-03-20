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

#define CAMERA_DEV      "/dev/video0"
#define CAP_WIDTH       1920
#define CAP_HEIGHT      1080
#define CAP_FPS         30
#define RECORD_DIR      "/mnt/data/recordings"
#define SOCKET_PATH     "/tmp/camera_daemon.sock"
#define HTTP_PORT       8554
#define V4L2_BUF_COUNT  4
#define MAX_CLIENTS     8

struct v4l2_buffer_info {
    void   *start;
    size_t  length;
};

typedef struct {
    unsigned char *data;
    size_t         size;
    uint64_t       seq;
} MjpegFrame;

static volatile int    g_running     = 1;
static volatile int    g_recording   = 0;
static int             g_cam_fd      = -1;
static FILE           *g_mjpeg_fp    = NULL;
static char            g_current_file[256] = {0};
static uint32_t        g_frame_count = 0;

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

    /* 确认实际分辨率 */
    printf("[v4l2] Actual format: %dx%d\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height);

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
 * MJPEG HTTP server
 * ================================================================ */
static void *mjpeg_client_thread(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

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
        pthread_mutex_lock(&g_frame_mutex);
        while (g_frame.seq == last_seq && g_running)
            pthread_cond_wait(&g_frame_cond, &g_frame_mutex);

        if (!g_running) {
            pthread_mutex_unlock(&g_frame_mutex);
            break;
        }

        size_t sz = g_frame.size;
        unsigned char *buf = malloc(sz);
        memcpy(buf, g_frame.data, sz);
        last_seq = g_frame.seq;
        pthread_mutex_unlock(&g_frame_mutex);

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
                     "%s/rec_%s.mjpeg", RECORD_DIR, ts);
            g_mjpeg_fp = fopen(g_current_file, "wb");
            if (!g_mjpeg_fp) {
                snprintf(resp, sizeof(resp), "ERR:open_file_failed");
            } else {
                g_frame_count = 0;
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
            if (g_mjpeg_fp) { fclose(g_mjpeg_fp); g_mjpeg_fp = NULL; }
            snprintf(resp, sizeof(resp), "STOPPED:%s:%u",
                     g_current_file, g_frame_count);
            printf("[daemon] Recording stopped: %s (%u frames)\n",
                   g_current_file, g_frame_count);
            g_current_file[0] = '\0';
            g_frame_count = 0;
        }
        pthread_mutex_unlock(&g_record_mutex);

    } else if (strncmp(buf, "status", 6) == 0) {
        pthread_mutex_lock(&g_record_mutex);
        if (g_recording)
            snprintf(resp, sizeof(resp), "RECORDING:%s:%u",
                     g_current_file, g_frame_count);
        else
            snprintf(resp, sizeof(resp), "IDLE");
        pthread_mutex_unlock(&g_record_mutex);

    } else {
        snprintf(resp, sizeof(resp), "ERR:unknown_command");
    }

    send(client_fd, resp, strlen(resp), 0);
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
    signal(SIGPIPE, SIG_IGN);


    mkdir(RECORD_DIR, 0755);

    g_cam_fd = v4l2_open_camera();
    if (g_cam_fd < 0) {
        fprintf(stderr, "[main] Camera open failed\n");
        return 1;
    }

    g_frame.data = malloc(CAP_WIDTH * CAP_HEIGHT * 2);

    pthread_t http_tid, sock_tid;
    pthread_create(&http_tid, NULL, http_server_thread, NULL);
    pthread_create(&sock_tid, NULL, socket_thread, NULL);

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

        /* 更新预览帧 */
        pthread_mutex_lock(&g_frame_mutex);
        memcpy(g_frame.data, jpeg_data, jpeg_size);
        g_frame.size = jpeg_size;
        g_frame.seq++;
        pthread_cond_broadcast(&g_frame_cond);
        pthread_mutex_unlock(&g_frame_mutex);

        /* 录像：限速到 30fps */

        pthread_mutex_lock(&g_record_mutex);
        if (g_recording && g_mjpeg_fp) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double now_ms = now.tv_sec * 1000.0 + now.tv_nsec / 1e6;
            static double last_frame_ms = 0;
            if (last_frame_ms == 0 || now_ms - last_frame_ms >= 33.3) {
                fwrite(jpeg_data, 1, jpeg_size, g_mjpeg_fp);
                g_frame_count++;
                last_frame_ms = now_ms;
            }
        }
        pthread_mutex_unlock(&g_record_mutex);

        ioctl(g_cam_fd, VIDIOC_QBUF, &buf);
    }

    printf("[main] Shutting down...\n");
    pthread_cond_broadcast(&g_frame_cond);

    pthread_mutex_lock(&g_record_mutex);
    if (g_recording && g_mjpeg_fp) {
        fclose(g_mjpeg_fp);
        g_mjpeg_fp = NULL;
    }
    pthread_mutex_unlock(&g_record_mutex);

    free(g_frame.data);
    v4l2_close_camera(g_cam_fd);
    pthread_join(http_tid, NULL);
    pthread_join(sock_tid, NULL);
    return 0;
}
