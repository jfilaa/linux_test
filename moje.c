#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <linux/fb.h>

static void msleep(int ms) {
    struct timespec ts;
    ts.tv_sec = ms/1000;
    ts.tv_nsec = (ms%1000)*1000000;
    nanosleep(&ts, NULL);
}

static int safe_write_file(const char *path, const char *buf, size_t len) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open('%s') failed: %s\n", path, strerror(errno));
        return -1;
    }
    ssize_t w = write(fd, buf, len);
    if (w < 0) {
        fprintf(stderr, "write('%s') failed: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
	const char *fbpath = "/dev/fb0";
    const char *sys_update = "/sys/devices/platform/sw-epdc.0/update";
    int wf = 2, mode = 1, wait_s = 5;
    if (argc > 1) wf = atoi(argv[1]);
    if (argc > 2) mode = atoi(argv[2]);
    if (argc > 3) wait_s = atoi(argv[3]);
	
	int fb = open(fbpath, O_RDWR);
    if (fb < 0) {
        perror("open /dev/fb0");
        return 1;
    }
	struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fb, FBIOGET_FSCREENINFO, &finfo) == -1) { perror("FBIOGET_FSCREENINFO"); close(fb); return 1; }
    if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) == -1) { perror("FBIOGET_VSCREENINFO"); close(fb); return 1; }

    unsigned W = vinfo.xres, H = vinfo.yres;
    unsigned stride = finfo.line_length;
    if (vinfo.bits_per_pixel != 8) {
        fprintf(stderr, "Warning: framebuffer bpp=%u, this program assumes 8bpp (1 byte/pixel)\n", vinfo.bits_per_pixel);
    }

    size_t mapsize = finfo.smem_len;
    if (mapsize == 0) mapsize = (size_t)stride * vinfo.yres_virtual;
    unsigned char *fbmem = mmap(NULL, mapsize, PROT_READ|PROT_WRITE, MAP_SHARED, fb, 0);
    if (fbmem == MAP_FAILED) {
        perror("mmap");
        close(fb);
        return 1;
    }

    unsigned char BLACK = 0x00, WHITE = 0xFF, GRAY = 0x80;

    // fill screen black
    memset(fbmem, BLACK, stride * H);
    msleep(50);
    msync(fbmem, stride * H, MS_SYNC);
	
	// make sure memory is synced
    msync(fbmem, stride * H, MS_SYNC);

    // trigger epdc update (full screen)
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "0 0 %u %u %d %d\n", W, H, mode, wf);
    if (safe_write_file(sys_update, cmd, strlen(cmd)) == 0) {
        printf("Wrote update: %s", cmd);
    } else {
        fprintf(stderr, "Failed to write update; check permissions or path %s\n", sys_update);
    }

    // wait a bit to let update run / be visible
    for (int i=0;i<wait_s; ++i) {
        msleep(1000);
    }

    // cleanup
    munmap(fbmem, mapsize);
    close(fb);
    printf("Done.\n");
    return 0;
    
}