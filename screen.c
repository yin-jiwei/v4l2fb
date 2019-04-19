//
// Created by yjw on 18-3-13.
//

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <string.h>
#include <assert.h>
#include "screen.h"
#include "opt.h"
#include "video.h"

extern struct options opt;
extern struct video video;
struct screen screen;
static char *image_rgba;

static int original_vt = -1;
static int console_fd = -1;
#define SIGUSR1 10

static void tty_init();

static void tty_quit();

static void write_frameBuffer(const char *yuyv);

static void yuyv2bgra(const char *yuyv, char *bgra, int length);

static int clamp(double x);

void screen_init()
{
    image_rgba = (char *) malloc(opt.width * opt.height * 4);
    assert(image_rgba != NULL);

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    screen.fd = open("/dev/fb0", O_RDWR);
    if (-1 == screen.fd)
    {
        perror("open frame buffer fail");
        return;
    }

    // Get fixed screen information
    if (ioctl(screen.fd, FBIOGET_FSCREENINFO, &finfo))
    {
        printf("Error reading fixed information.\n");
        return;
    }

    // Get variable screen information
    int ret = ioctl(screen.fd, FBIOGET_VSCREENINFO, &vinfo);
    if (-1 == ret)
    {
        printf("Error reading variable information.\n");
        close(screen.fd);
        return;
    }

    //这里把整个显存一起初始化（xres_virtual 表示显存的x，比实际的xres大,bits_per_pixel位深）
    screen.screensize = vinfo.xres_virtual * vinfo.yres_virtual * vinfo.bits_per_pixel / 8;
    screen.width = vinfo.xres_virtual;
    screen.height = vinfo.yres_virtual;

    if (opt.verbose)
    {
        //获取实际的位色，这里很关键，后面转换和填写的时候需要
        printf("%dx%d, %dbpp  screensize is %d\n", vinfo.xres_virtual, vinfo.yres_virtual, vinfo.bits_per_pixel,
               screen.screensize);
    }

    //映射出来，用户直接操作
    screen.buffer = mmap(NULL, screen.screensize, PROT_READ | PROT_WRITE, MAP_SHARED, screen.fd, 0);
    if (screen.buffer == MAP_FAILED)
    {
        perror("memory map fail");
        close(screen.fd);
        exit(EXIT_FAILURE);
    }

    memset(screen.buffer, 0, screen.screensize);

    tty_init();
}

void screen_quit()
{
    munmap(screen.buffer, screen.screensize);
    close(screen.fd);
    free(image_rgba);
    tty_quit();
    return;
}

void screen_mainloop()
{
    while (1)
    {
        int buffer_index = buffer_dequeue();

        write_frameBuffer(video.buffer.buf[buffer_index].start);

        buffer_enqueue(buffer_index);
    }
}

static void tty_init()
{
    int vt_number;

    /* open /dev/tty0 and get the vt number */
    if ((console_fd = open("/dev/tty0", O_WRONLY, 0)) < 0)
    {
        perror("error opening /dev/tty0");
        exit(1);
    }

    if (ioctl(console_fd, VT_OPENQRY, &vt_number) < 0 || vt_number < 0)
    {
        perror("error: couldn't get a free vt");
        exit(1);
    }

    close(console_fd);
    console_fd = -1;

    char tty_name[128];

    /* open the console tty */
    sprintf(tty_name, "/dev/tty%d", vt_number);  /* /dev/tty1-64 */

    console_fd = open(tty_name, O_RDWR | O_NDELAY, 0);
    if (console_fd < 0)
    {
        fprintf(stderr, "error couldn't open console fd %d\n", vt_number);
        exit(1);
    }

    /* save current vt number */
    {
        struct vt_stat vts;
        if (ioctl(console_fd, VT_GETSTATE, &vts) == 0)
        {
            original_vt = vts.v_active;
        }
    }

    /* disconnect from controlling tty */
    int tty_fd;

    tty_fd = open("/dev/tty", O_RDWR);
    if (tty_fd >= 0)
    {
        ioctl(tty_fd, TIOCNOTTY, 0);
        close(tty_fd);
    }

    /* some magic to restore the vt when we exit */
    {
        struct vt_mode vt;
        if (ioctl(console_fd, VT_ACTIVATE, vt_number) != 0)
        {
            perror("ioctl VT_ACTIVATE");
        }

        if (ioctl(console_fd, VT_WAITACTIVE, vt_number) != 0)
        {
            perror("ioctl VT_WAITACTIVE");
        }

        if (ioctl(console_fd, VT_GETMODE, &vt) < 0)
        {
            perror("error: ioctl VT_GETMODE");
            exit(1);
        }

        vt.mode = VT_PROCESS;
        vt.relsig = SIGUSR1;
        vt.acqsig = SIGUSR1;

        if (ioctl(console_fd, VT_SETMODE, &vt) < 0)
        {
            perror("error: ioctl(VT_SETMODE) failed");
            exit(1);
        }
    }

    /* go into graphics mode */
    if (ioctl(console_fd, KDSETMODE, KD_GRAPHICS) < 0)
    {
        perror("error: ioctl(KDSETMODE, KD_GRAPHICS) failed");
        exit(1);
    }
    return;
}

static void tty_quit()
{
    struct vt_mode vt;

    /* restore text mode */
    ioctl(console_fd, KDSETMODE, KD_TEXT);

    /* set vt */
    if (ioctl(console_fd, VT_GETMODE, &vt) != -1)
    {
        vt.mode = VT_AUTO;
        ioctl(console_fd, VT_SETMODE, &vt);
    }

    /* restore original vt */
    if (original_vt >= 0)
    {
        ioctl(console_fd, VT_ACTIVATE, original_vt);
        original_vt = -1;
    }

    close(console_fd);
    return;
}

static void yuyv2bgra(const char *yuyv, char *bgra, int length)
{
    int count;
    int y0, u0, y1, v0;
    for (count = 0; count < length / 4; count++)
    {
        // rgb2yuv yuv2rgb
        /*Y = 0.257R + 0.504G + 0.098B + 16;
        U = 0.148R - 0.291G + 0.439B + 128;
        V = 0.439R - 0.368G - 0.071B + 128;
        B = 1.164(Y - 16) + 2.018(U - 128);
        G = 1.164(Y - 16) - 0.813(V - 128) - 0.391(U - 128);
        R = 1.164(Y - 16) + 1.596(V - 128);*/

        y0 = yuyv[count * 4 + 0];
        u0 = yuyv[count * 4 + 1];
        y1 = yuyv[count * 4 + 2];
        v0 = yuyv[count * 4 + 3];

        bgra[count * 8 + 0] = clamp(1.164 * (y0 - 16) + 2.018 * (u0 - 128)); //b
        bgra[count * 8 + 1] = clamp(1.164 * (y0 - 16) - 0.391 * (u0 - 128) - 0.813 * (v0 - 128)); //g
        bgra[count * 8 + 2] = clamp(1.164 * (y0 - 16) + 1.596 * (v0 - 128)); //r
        bgra[count * 8 + 3] = 255; //透明度

        bgra[count * 8 + 4] = clamp(1.164 * (y1 - 16) + 2.018 * (u0 - 128)); //b
        bgra[count * 8 + 5] = clamp(1.164 * (y1 - 16) - 0.391 * (u0 - 128) - 0.813 * (v0 - 128)); //g
        bgra[count * 8 + 6] = clamp(1.164 * (y1 - 16) + 1.596 * (v0 - 128)); //r
        bgra[count * 8 + 7] = 255; //透明度
    }
}

static int clamp(double x)
{
    int r = x;
    if (r < 0)
    {
        return 0;
    } else if (r > 255)
    {
        return 255;
    } else
    {
        return r;
    }
}

static void write_frameBuffer(const char *yuyv)
{
    int img_width = video.format.fmt.pix.width;
    int img_height = video.format.fmt.pix.height;

    if (screen.screensize < img_width * img_height * 4)
    {
        printf("the imgsize is too large\n");
        return;
    }

    yuyv2bgra(yuyv, image_rgba, img_width * img_height * 2);

    for (int row = 0; row < img_height; row++)
    {
        //由于摄像头分辨率没有帧缓冲大，完成显示后，需要强制换行，帧缓冲是线性的，使用row * vinfo.xres_virtual换行
        memcpy(screen.buffer + row * screen.width * 4, image_rgba + row * img_width * 4, img_width * 4);
    }
}