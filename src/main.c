/* xpad-ps5: a wired Xbox One / Series controller plugged into a jailbroken
 * PS5 becomes a virtual DualSense, seen by the system menu, PS4/PS5 games and
 * emulators like a Sony controller.
 *
 *   gip.c          the controller's protocol (unit-tested on the host)
 *   main.c         the console's USB, the main loop, the log
 *   anypad/        the virtual DualSense, taken from AnyPad PS5 (GPL-3.0)
 *
 * The controller is looked for among /dev/ugen* every second, so it can be
 * unplugged and plugged back at any time. The log is in
 * /data/xpad-ps5/xpad-ps5.log and, read-only, at http://<console>:8096/
 * (the port doubles as a lock: one instance at a time).
 *
 * Stops on rest mode, SIGTERM/SIGINT, or the file /data/xpad-ps5/stop.
 * On the way out it removes its virtual DualSense and gives its rights back.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "anypad/log.h"
#include "anypad/pad.h"
#include "anypad/ps5_power.h"
#include "anypad/ps5_vpad.h"
#include "anypad/util.h"
#include "gip.h"
#include "weblog.h"

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <netinet/in.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define VERSION     "1.0.2"
#define STATE_DIR   "/data/xpad-ps5"
#define LOG_PATH    STATE_DIR "/xpad-ps5.log"
#define STOP_FLAG   STATE_DIR "/stop"
#define WEB_PORT    8096

#define N_IN        4           /* reads kept pending */
#define X_OUT       0
#define X_IN0       1
#define N_XFER      (X_IN0 + N_IN)
#define BUF         64
/* FreeBSD transfer statuses (dev/usb/usbdi.h, kernel-only header). */
#define ST_CANCELLED      5
#define ST_IOERROR       18
#define ST_NOT_CONFIGURED 19
#define RESEND_MS   16          /* the state is resent at this pace, even unchanged */

static const char g_version_tag[] __attribute__((used)) = "xpad-ps5-version " VERSION;

static volatile sig_atomic_t g_stop;

/* ---- the USB controller --------------------------------------------------- */

typedef struct {
    int fd;
    char node[32];
    uint16_t vid, pid;
    gip_iface gi;
    struct usb_fs_endpoint ep[N_XFER];
    void *bufp[N_XFER][1];
    uint32_t lenp[N_XFER][1];
    uint8_t buf[N_XFER][BUF];
    uint8_t busy[N_XFER];
    uint8_t seq;
    int dead;
    unsigned packets;
    gip_state gs;
    uint8_t outq[8][16];
    int outq_len[8], outq_n;
} xbox;

static xbox g_x = { .fd = -1 };

static void x_close(xbox *x)
{
    struct usb_fs_uninit un;
    struct usb_fs_stop st;
    int i;

    if (x->fd < 0) return;
    for (i = 0; i < N_XFER; i++) {
        if (!x->busy[i]) continue;
        memset(&st, 0, sizeof st);
        st.ep_index = (uint8_t)i;
        ioctl(x->fd, USB_FS_STOP, &st);
        x->busy[i] = 0;
    }
    memset(&un, 0, sizeof un);
    ioctl(x->fd, USB_FS_UNINIT, &un);
#ifdef USB_RELEASE_INTERFACE
    {
        int ifc = x->gi.iface;
        ioctl(x->fd, USB_RELEASE_INTERFACE, &ifc);
    }
#endif
    close(x->fd);
    x->fd = -1;
}

static int x_start(xbox *x, int index, int length)
{
    struct usb_fs_start st;

    x->lenp[index][0] = (uint32_t)length;
    x->ep[index].nFrames = 1;
    x->ep[index].aFrames = 0;
    x->ep[index].status = 0;
    memset(&st, 0, sizeof st);
    st.ep_index = (uint8_t)index;
    if (ioctl(x->fd, USB_FS_START, &st) != 0) {
        log_line("usb: start transfer %d: errno %d", index, errno);
        if (errno == ENXIO || errno == ENODEV || errno == EIO || errno == EBADF) x->dead = 1;
        return 0;
    }
    x->busy[index] = 1;
    return 1;
}

static int x_open_ep(xbox *x, int index, int addr, int size)
{
    struct usb_fs_open op;

    memset(&op, 0, sizeof op);
    op.ep_index = (uint8_t)index;
    op.ep_no = (uint8_t)addr;
    op.max_bufsize = (uint32_t)size;
    op.max_frames = 1;
    if (ioctl(x->fd, USB_FS_OPEN, &op) != 0) {
        log_line("usb: open endpoint %#x (transfer %d): errno %d", addr, index, errno);
        return 0;
    }
    return 1;
}

/* Outgoing packets are queued: there is a single OUT transfer. */
static void x_send(xbox *x, const uint8_t *p, int n)
{
    if (x->outq_n >= 8 || n > 16) {
        log_line("usb: send queue full, packet %#x dropped", p[0]);
        return;
    }
    memcpy(x->outq[x->outq_n], p, (size_t)n);
    x->outq_len[x->outq_n++] = n;
}

static void x_flush(xbox *x)
{
    if (x->busy[X_OUT] || !x->outq_n || x->dead) return;
    memcpy(x->buf[X_OUT], x->outq[0], (size_t)x->outq_len[0]);
    if (x_start(x, X_OUT, x->outq_len[0])) {
        x->outq_n--;
        memmove(x->outq[0], x->outq[1], sizeof x->outq[0] * (size_t)x->outq_n);
        memmove(x->outq_len, x->outq_len + 1, sizeof x->outq_len[0] * (size_t)x->outq_n);
    }
}

static void log_hex(const char *what, const uint8_t *p, int n)
{
    char line[3 * 32 + 1];
    int k, m = n < 32 ? n : 32;

    line[0] = 0;
    for (k = 0; k < m; k++) snprintf(line + 3 * k, 4, "%02x ", p[k]);
    log_line("%s (%d bytes) %s", what, n, line);
}

/* What a ugen node says about itself: identity, configuration, drivers. Read-only. */
static void describe(int fd, const char *node, const struct usb_device_descriptor *dd)
{
    struct usb_device_info di;
    struct usb_gen_descriptor gd;
    char name[64];
    int j;

    memset(&di, 0, sizeof di);
    if (ioctl(fd, USB_GET_DEVICEINFO, &di) == 0)
        log_line("usb: %s %04x:%04x \"%.40s\" by \"%.40s\", config %u, speed %u",
                 node, UGETW(dd->idVendor), UGETW(dd->idProduct), di.udi_product, di.udi_vendor,
                 di.udi_config_no, di.udi_speed);
    else
        log_line("usb: %s %04x:%04x", node, UGETW(dd->idVendor), UGETW(dd->idProduct));
    for (j = 0; j < 4; j++) {
        memset(&gd, 0, sizeof gd);
        memset(name, 0, sizeof name);
        gd.ugd_data = name;
        gd.ugd_maxlen = sizeof name - 1;
        gd.ugd_iface_index = (uint8_t)j;
        if (ioctl(fd, USB_GET_IFACE_DRIVER, &gd) == 0)
            log_line("usb: %s interface %d held by \"%.40s\"", node, j, name);
    }
}

/* Opens the controller on `node` if it is one. Returns 1 when it is ready. */
static int x_try(xbox *x, const char *node, int verbose)
{
    struct usb_device_descriptor dd;
    struct usb_gen_descriptor gd;
    struct usb_fs_init init;
    uint8_t cfg[512];
    uint8_t pkt[GIP_INIT_MAX][16];
    int len[GIP_INIT_MAX], n, i, fd, total;

    fd = open(node, O_RDWR);
    if (fd < 0) {
        if (verbose) log_line("usb: %s does not open: errno %d", node, errno);
        return 0;
    }
    if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) != 0) {
        close(fd);
        return 0;
    }
    memset(&gd, 0, sizeof gd);
    gd.ugd_data = cfg;
    gd.ugd_maxlen = sizeof cfg;
    gd.ugd_config_index = 0xFF;             /* the current configuration */
    if (ioctl(fd, USB_GET_FULL_DESC, &gd) != 0) {
        if (verbose) {
            describe(fd, node, &dd);
            log_line("usb: %s: configuration unreadable (errno %d)", node, errno);
        }
        close(fd);
        return 0;
    }
    total = gd.ugd_actlen < (int)sizeof cfg ? gd.ugd_actlen : (int)sizeof cfg;
    memset(x, 0, sizeof *x);
    x->fd = -1;
    if (!gip_find_interface(cfg, total, &x->gi)) {
        if (verbose) describe(fd, node, &dd);
        close(fd);
        return 0;
    }

    /* It is a GIP controller. */
    describe(fd, node, &dd);
    x->fd = fd;
    snprintf(x->node, sizeof x->node, "%s", node);
    x->vid = UGETW(dd.idVendor);
    x->pid = UGETW(dd.idProduct);
    log_line("pad: %s %04x:%04x, interface %d, IN %#x (%u), OUT %#x (%u)", node, x->vid, x->pid,
             x->gi.iface, x->gi.ep_in, x->gi.mps_in, x->gi.ep_out, x->gi.mps_out);

    {
        int ifc = x->gi.iface;
        if (ioctl(fd, USB_IFACE_DRIVER_ACTIVE, &ifc) == 0) {
            ifc = x->gi.iface;
            log_line("pad: a system driver holds the interface, detaching: %s",
                     ioctl(fd, USB_IFACE_DRIVER_DETACH, &ifc) == 0 ? "done" : strerror(errno));
        }
#ifdef USB_CLAIM_INTERFACE
        ifc = x->gi.iface;
        if (ioctl(fd, USB_CLAIM_INTERFACE, &ifc) != 0)
            log_line("pad: interface claim refused (errno %d), carrying on", errno);
#endif
    }

    for (i = 0; i < N_XFER; i++) {
        x->bufp[i][0] = x->buf[i];
        x->lenp[i][0] = BUF;
        x->ep[i].ppBuffer = x->bufp[i];
        x->ep[i].pLength = x->lenp[i];
        x->ep[i].nFrames = 1;
        x->ep[i].flags = USB_FS_FLAG_SINGLE_SHORT_OK;
    }
    memset(&init, 0, sizeof init);
    init.pEndpoints = x->ep;
    init.ep_index_max = N_XFER;
    if (ioctl(fd, USB_FS_INIT, &init) != 0) {
        log_line("pad: USB_FS_INIT: errno %d", errno);
        x_close(x);
        return 0;
    }
    if (!x_open_ep(x, X_OUT, x->gi.ep_out, BUF)) { x_close(x); return 0; }
    for (i = 0; i < N_IN; i++)
        if (!x_open_ep(x, X_IN0 + i, x->gi.ep_in, BUF)) { x_close(x); return 0; }
    for (i = 0; i < N_IN; i++) x_start(x, X_IN0 + i, BUF);

    gip_reset(&x->gs);
    n = gip_init_packets(x->vid, x->pid, &x->seq, pkt, len);
    for (i = 0; i < n; i++) x_send(x, pkt[i], len[i]);
    x_flush(x);
    log_line("pad: open, %d start-up packets sent", n);
    return 1;
}

/* Looks for a controller among /dev/ugen*. */
static int x_scan(xbox *x, int verbose)
{
    DIR *d = opendir("/dev");
    struct dirent *e;
    char node[32];
    int found = 0;

    if (!d) {
        if (verbose) log_line("usb: /dev unreadable: errno %d", errno);
        return 0;
    }
    while (!found && (e = readdir(d))) {
        if (strncmp(e->d_name, "ugen", 4) != 0) continue;
        snprintf(node, sizeof node, "/dev/%s", e->d_name);
        found = x_try(x, node, verbose);
    }
    closedir(d);
    return found;
}

/* Reaps finished transfers. Returns 1 if the controller state changed. */
static int x_reap(xbox *x)
{
    struct usb_fs_complete done;
    int changed = 0;

    for (;;) {
        int i;
        memset(&done, 0, sizeof done);
        if (ioctl(x->fd, USB_FS_COMPLETE, &done) != 0) {
            if (errno == ENXIO || errno == ENODEV || errno == EIO || errno == EBADF) x->dead = 1;
            break;
        }
        i = done.ep_index;
        if (i >= N_XFER) continue;
        x->busy[i] = 0;
        if (i == X_OUT) {
            if (x->ep[i].status) log_line("usb: send finished with status %u", (unsigned)x->ep[i].status);
            continue;
        }
        if (x->ep[i].status == ST_CANCELLED || x->ep[i].status == ST_IOERROR ||
            x->ep[i].status == ST_NOT_CONFIGURED) {
            log_line("usb: read %d finished with status %u: controller gone?", i, (unsigned)x->ep[i].status);
            x->dead = 1;
            continue;
        }
        if (x->ep[i].status == 0 && x->ep[i].aFrames) {
            uint8_t ack[16];
            int ack_len, r, n = (int)x->lenp[i][0];
            x->packets++;
            if (x->packets <= 12 || x->buf[i][0] != GIP_CMD_INPUT) log_hex("pad: received", x->buf[i], n);
            r = gip_parse(&x->gs, x->buf[i], n, ack, &ack_len);
            if (ack_len) x_send(x, ack, ack_len);
            if (r == GIP_CHANGED) changed = 1;
        }
        if (!x->dead) x_start(x, i, BUF);
    }
    x_flush(x);
    return changed;
}

/* ---- the read-only log page ------------------------------------------------- */

static int g_web = -1;

static int web_open(void)
{
    struct sockaddr_in sin;
    int fd = socket(AF_INET, SOCK_STREAM, 0), one = 1;

    if (fd < 0) return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(WEB_PORT);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&sin, sizeof sin) != 0 || listen(fd, 4) != 0) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}

static void web_poll(void)
{
    static char raw[64 * 1024], body[64 * 1024];
    char head[160];
    int c;

    if (g_web < 0) return;
    while ((c = accept(g_web, NULL, NULL)) >= 0) {
        struct timeval tv = { 1, 0 };
        char req[512];
        int hl;
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        (void)!read(c, req, sizeof req);        /* the request is ignored: there is one page */
        log_since(0, raw, sizeof raw);
        web_filter(raw, body, sizeof body);
        hl = snprintf(head, sizeof head,
                      "HTTP/1.0 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
                      "Content-Length: %zu\r\nConnection: close\r\n\r\n", strlen(body));
        (void)!write(c, head, (size_t)hl);
        (void)!write(c, body, strlen(body));
        close(c);
    }
}


/* ---- start-up diagnostics (read-only) ---------------------------------------- */

#include <dlfcn.h>

/* The /dev nodes, to know what this process can see. */
static void diag_dev(void)
{
    DIR *d = opendir("/dev");
    struct dirent *e;
    char line[180];
    size_t used = 0;

    if (!d) return;
    line[0] = 0;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (e->d_name[0] == '.') continue;
        if (used + n + 2 >= sizeof line) {
            log_line("dev: %s", line);
            used = 0;
            line[0] = 0;
        }
        memcpy(line + used, e->d_name, n);
        used += n;
        line[used++] = ' ';
        line[used] = 0;
    }
    if (used) log_line("dev: %s", line);
    closedir(d);
}

/* What libSceUsbd, the games' USB library (modelled on libusb), can see. */
typedef struct {
    uint8_t bLength, bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
    uint16_t idVendor, idProduct, bcdDevice;
    uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
} usbd_desc;

static long (*g_usbd_list)(void ***);
static void (*g_usbd_free)(void **, int);
static int (*g_usbd_desc)(void *, usbd_desc *);
static uint8_t (*g_usbd_bus)(void *), (*g_usbd_addr)(void *);

static void diag_usbd_init(void)
{
    static const char *const libs[] = { "/system/common/lib/libSceUsbd.sprx", "libSceUsbd.sprx" };
    int (*init)(void);
    void *lib = NULL;
    size_t i;

    for (i = 0; i < sizeof libs / sizeof libs[0] && !lib; i++) lib = dlopen(libs[i], RTLD_NOW | RTLD_GLOBAL);
    if (!lib) {
        log_line("usbd: libSceUsbd not found");
        return;
    }
    init = (int (*)(void))dlsym(lib, "sceUsbdInit");
    g_usbd_list = (long (*)(void ***))dlsym(lib, "sceUsbdGetDeviceList");
    g_usbd_free = (void (*)(void **, int))dlsym(lib, "sceUsbdFreeDeviceList");
    g_usbd_desc = (int (*)(void *, usbd_desc *))dlsym(lib, "sceUsbdGetDeviceDescriptor");
    g_usbd_bus = (uint8_t (*)(void *))dlsym(lib, "sceUsbdGetBusNumber");
    g_usbd_addr = (uint8_t (*)(void *))dlsym(lib, "sceUsbdGetDeviceAddress");
    if (!init || !g_usbd_list || !g_usbd_desc || init() < 0) {
        log_line("usbd: unavailable");
        g_usbd_list = NULL;
        return;
    }
}

/* Devices seen by libSceUsbd and the ugen nodes, logged whenever they change. */
static void diag_watch(void)
{
    static char last[600];
    char now[600];
    size_t used = 0;
    DIR *d;
    struct dirent *e;

    now[0] = 0;
    if (g_usbd_list) {
        void **devs = NULL;
        long n = g_usbd_list(&devs), i;
        for (i = 0; n > 0 && i < n && used + 40 < sizeof now; i++) {
            usbd_desc dd;
            memset(&dd, 0, sizeof dd);
            g_usbd_desc(devs[i], &dd);
            used += (size_t)snprintf(now + used, sizeof now - used, "%u.%u=%04x:%04x ",
                                     g_usbd_bus ? g_usbd_bus(devs[i]) : 0, g_usbd_addr ? g_usbd_addr(devs[i]) : 0,
                                     dd.idVendor, dd.idProduct);
        }
        if (devs && g_usbd_free) g_usbd_free(devs, 1);
    }
    used += (size_t)snprintf(now + used, sizeof now - used, "| ");
    if ((d = opendir("/dev"))) {
        while ((e = readdir(d)) && used + 40 < sizeof now) {
            if (strncmp(e->d_name, "ugen", 4) && strncmp(e->d_name, "da", 2) && strncmp(e->d_name, "uhid", 4) &&
                strncmp(e->d_name, "usb", 3) && strncmp(e->d_name, "hid", 3))
                continue;
            used += (size_t)snprintf(now + used, sizeof now - used, "%s ", e->d_name);
        }
        closedir(d);
    }
    if (strcmp(now, last) != 0) {
        log_line("usb seen: %s", now);
        memcpy(last, now, sizeof last);
    }
}


/* The kernel log, filtered on USB, while no controller has been found (at
 * most KLOG_MS): it shows why a plugged device does not appear (a charge-only
 * cable gives "giving up port reset - device vanished", or nothing at all).
 * The local log server (127.0.0.1:3232) first; /dev/klog only without one.
 * /dev/klog has a single reader and the virtual DualSense needs it to be
 * identified, so it is given back as soon as a controller arrives. */
#define KLOG_MS (5 * 60 * 1000)
static int g_klog = -1;
static long g_klog_until;
static char g_kline[512];
static size_t g_klen;

static void klog_open(void)
{
    struct sockaddr_in sin;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd >= 0) {
        memset(&sin, 0, sizeof sin);
        sin.sin_family = AF_INET;
        sin.sin_port = htons(3232);
        sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (struct sockaddr *)&sin, sizeof sin) == 0) {
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
            g_klog = fd;
            log_line("klog: local server 3232");
        } else {
            close(fd);
        }
    }
    if (g_klog < 0) {
        g_klog = open("/dev/klog", O_RDONLY | O_NONBLOCK);
        log_line("klog: /dev/klog %s", g_klog >= 0 ? "open" : strerror(errno));
    }
    g_klog_until = now_ms() + KLOG_MS;
}

static int usb_line(const char *l)
{
    static const char *const keys[] = { "usb", "USB", "ugen", "uhub", "xhci", "045e", "Microsoft", "Xbox", "attach", "Usb" };
    size_t i;
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++) if (strstr(l, keys[i])) return 1;
    return 0;
}

static void klog_close(void)
{
    if (g_klog < 0) return;
    close(g_klog);
    g_klog = -1;
    log_line("klog: released");
}

static void klog_poll(long now)
{
    char buf[1024];
    int rounds;

    if (g_klog < 0) return;
    if (now >= g_klog_until) {
        klog_close();
        return;
    }
    for (rounds = 0; rounds < 8; rounds++) {
        ssize_t n = read(g_klog, buf, sizeof buf), i;
        if (n <= 0) return;
        for (i = 0; i < n; i++) {
            if (buf[i] != '\n' && g_klen < sizeof g_kline - 1) {
                g_kline[g_klen++] = buf[i];
                continue;
            }
            g_kline[g_klen] = 0;
            g_klen = 0;
            if (usb_line(g_kline)) log_line("klog| %.180s", g_kline);
        }
    }
}

/* ---- the main loop ------------------------------------------------------------ */

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

int main(void)
{
    long last_scan = -100000, last_power = 0, last_send = 0, last_flag = 0, last_retry = 0;
    int scans = 0, was_live = 0, vpad_ok;
    pad_state ps;
    const char *why = "stop requested";

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);

    g_web = web_open();
    if (g_web < 0) {
        notify("xpad-ps5: already running (port %d in use)", WEB_PORT);
        return 0;
    }
    mkdir(STATE_DIR, 0755);
    unlink(STOP_FLAG);
    log_open(LOG_PATH);
    log_line("xpad-ps5 %s: starting", VERSION);

    vpad_ok = vpad_init();
    if (!vpad_ok) {
        notify("xpad-ps5: cannot create a virtual DualSense (see the log)");
        log_line("vpad: unavailable, the controller will only be read for the log");
    }
    pad_state_reset(&ps);
    diag_dev();
    diag_usbd_init();
    klog_open();

    while (!g_stop) {
        long now = now_ms();

        web_poll();
        klog_poll(now);
        if (vpad_ok) vpad_poll(now);

        if (g_x.fd < 0) {
            if (now - last_scan >= 1000) {
                last_scan = now;
                diag_watch();
                if (x_scan(&g_x, scans++ == 0)) {
                    klog_close();
                    if (vpad_ok) vpad_add(0);
                    last_retry = now;
                    notify("Xbox controller connected");
                } else if (scans == 1) {
                    log_line("usb: no Xbox controller found, looking every second");
                }
            }
            usleep(10000);
        } else {
            int changed = x_reap(&g_x);

            if (g_x.dead) {
                log_line("pad: unplugged (%u packets received)", g_x.packets);
                if (vpad_ok) vpad_remove(0);
                x_close(&g_x);
                was_live = 0;
                notify("Xbox controller disconnected");
                continue;
            }
            /* A failed identification (log busy, slow console) is tried again. */
            if (vpad_ok && !vpad_live(0) && now - last_retry >= 5000) {
                last_retry = now;
                if (vpad_add(0)) log_line("vpad: trying again");
            }
            if (vpad_ok && vpad_live(0)) {
                if (!was_live) {
                    uint8_t rb[16];
                    int n = gip_rumble_packet(&g_x.seq, 160, 160, rb);
                    was_live = 1;
                    x_send(&g_x, rb, n);
                    x_flush(&g_x);
                    log_line("pad: virtual DualSense ready");
                }
                if (changed || now - last_send >= RESEND_MS) {
                    gip_to_pad(&g_x.gs, &ps);
                    vpad_update(0, &ps);
                    last_send = now;
                }
            }
            usleep(changed ? 500 : 2000);
        }

        if (now - last_power >= 200) {
            int p = power_state();
            last_power = now;
            if (p == POWER_GOING_TO_REST || p == POWER_IN_REST) {
                why = "rest mode";
                break;
            }
        }
        if (now - last_flag >= 1000) {
            last_flag = now;
            if (access(STOP_FLAG, F_OK) == 0) {
                unlink(STOP_FLAG);
                why = "stop file";
                break;
            }
        }
    }

    log_line("stopping: %s", why);
    klog_close();
    if (vpad_ok) vpad_remove(0);
    x_close(&g_x);
    if (vpad_ok) vpad_creds_restore();
    if (g_web >= 0) close(g_web);
    log_line("stopped");
    log_close();
    notify("xpad-ps5: stopped (%s)", why);
    return 0;
}
