/* TI's hub driver seen from outside: what it knows of each port of the hub
 * on the calculator's port, and its own "look at the hubs now" (2026-11-17).
 * Read from the CX II's OS 6.4.0.74 code (the 512 KB capture of
 * 109c0000-10a40000); the addresses below are
 * that OS's.
 *
 * The structures, as the hub's explore (10a040cc) and the explore task
 * (10a05404) use them:
 *  - the controller's bus structure: + 48 the root hub's device;
 *  - a device: + 0 its bus, + 96 its hub structure (0: not a hub);
 *  - a hub structure: + 4 the hub driver's softc, + 10 the number of ports,
 *    + 48 the ports, 20 bytes each: + 0 wPortStatus, + 2 wPortChange as the
 *    hub last answered (get port status, 10a042c0), + 8 the device on the
 *    port (0: none), + 16 flags;
 *  - the hub driver's softc: + 4 the hub's device, + 25 running, + 26
 *    suspended, + 27 its active children.
 * Every hub is asked for all its ports' status each time the explore task
 * explores (10a04160..10a04360: no port is skipped), and the task explores
 * only when its flag is set (usbshim_explore.h): by a hub's status pipe, or
 * by 10a053d0 called for any device of the bus.
 *
 * What this header gives:
 *  - usbshim_hub_read(bus, &info): the hub on a port of the root hub, as the
 *    OS last saw it: its ports' state, a byte each (USBSHIM_HUBP_*). 1 a hub
 *    is there, 0 none, -1 not known (another OS, or a pointer that is not
 *    what the code leaves: each one is checked to lie in RAM and to point
 *    back where it should). Reads only. With the CPU's interrupts masked by
 *    the caller or not: a port's bytes are read one by one (a torn pair of
 *    status and change is possible and harmless: the next look corrects it).
 *  - usbshim_hub_explore(bus): TI's own "explore now" (10a053d0) for this
 *    bus: the flag set and the explore task woken, exactly as a hub's status
 *    pipe does it. For a hub whose status pipe says nothing any more (the
 *    hub after a controller's unplug: two more
 *    controllers plugged in, no news, no offer): the task then asks every
 *    port itself. CPU interrupts are masked around the call (it is called
 *    from the interrupt's own context by the OS). 0 asked, -1 not known.
 *  - usbshim_hub_device(bus) (2026-11-18): that hub's own device structure,
 *    checked as usbshim_hub_read checks it (in RAM, of this bus, its hub
 *    structure's softc pointing back at it), or NULL (none, another OS).
 *    For a request to the hub itself (a program's look at whether a hub
 *    whose power fell still answers). The OS frees it when the hub leaves:
 *    use it at once, from the task that asked, never keep it.
 *  - usbshim_dev_default_pipe(dev), usbshim_ctl_request(xfer, req)
 *    (2026-11-18): a control request without waiting. TI exports no
 *    usbd_setup_default_xfer, and its usbd_do_request sleeps until the reply
 *    (its own 5 s limit, 10a08b2c -> 10a088e0). Read from that code: a
 *    request is an ordinary transfer block on the device's default pipe
 *    (device + 4) with the 8 request bytes at block + 41 and bit 0 of block +
 *    68 set ("a request"); usbd_setup_xfer (10a07a34) fills everything else
 *    (pipe + 0, priv + 4, buffer + 12, length + 16, flags + 24, timeout in ms
 *    + 28, callback + 36) and clears that bit; without flag 2 (synchronous,
 *    10a088d0) usbd_transfer returns at once and the callback comes at the
 *    end. So: usbd_alloc_xfer(dev), usbd_setup_xfer(xfer, the default pipe,
 *    priv, NULL, 0, 0, timeout, cb), usbshim_ctl_request(xfer, req),
 *    usbd_transfer(xfer); its callback says how it went (a request with no
 *    data stage only: nothing else is checked). NULL / -1: not the OS whose
 *    code is known.
 *  - usbshim_xfer_rehome(xfer, holder) (2026-11-18): before
 *    usbd_free_xfer of a block whose device may be gone. usbd_alloc_xfer
 *    keeps the device at block + 60 (10a083e8) and usbd_free_xfer reaches
 *    the controller's methods through it (dev -> bus -> bus + 8 -> + 20,
 *    10a082d8-10a082f4; and + 12 for a DMA buffer, 10a082fc): nothing else of
 *    the device. holder is a word of the program's own holding the bus
 *    (the controller's: it is never freed); written at + 60, the free
 *    never touches the device. 0, -1 not the known OS. Every reader of + 60
 *    in that code stops at the bus (the transfer's buffer path 10a08584, the
 *    time limit 10a08040, the abort 10a108f4, the free): re-homed right
 *    after usbd_alloc_xfer, the block never needs its device again.
 *
 * Tests: with USBSHIM_TEST the fake OS provides the hooks below. */
#ifndef USBSHIM_HUB_H
#define USBSHIM_HUB_H

#include <stdint.h>
#include "usbshim_explore.h"

#define USBSHIM_HUB_PORTS 8u
/* a port's byte */
#define USBSHIM_HUBP_CONNECTED 0x01u   /* wPortStatus bit 0 */
#define USBSHIM_HUBP_ENABLED   0x02u   /* bit 1 */
#define USBSHIM_HUBP_SUSPENDED 0x04u   /* bit 2 */
#define USBSHIM_HUBP_OVERCUR   0x08u   /* bit 3 */
#define USBSHIM_HUBP_RESET     0x10u   /* bit 4 */
#define USBSHIM_HUBP_POWER     0x20u   /* bit 8 */
#define USBSHIM_HUBP_DEVICE    0x40u   /* the OS has a device structure for the port */
#define USBSHIM_HUBP_CHANGE    0x80u   /* a change bit the OS has not cleared */

struct usbshim_hub_info {
    uint8_t nports;                     /* of the hub (USBSHIM_HUB_PORTS at the most are told) */
    uint8_t root_port;                  /* the root hub's port it is on */
    uint8_t running, suspended, active; /* the hub driver's own: softc + 25, + 26, + 27 */
    uint8_t port[USBSHIM_HUB_PORTS];
};

#ifdef USBSHIM_TEST

/* The fake OS's: the hub as its "OS" sees it (1, 0, -1), and its explore
 * (0 or -1). Weak: a fake OS without them is one that is not known. */
int usbshim_test_hub_read(const void *bus, struct usbshim_hub_info *out) __attribute__((weak));
int usbshim_test_hub_explore(const void *bus) __attribute__((weak));
void *usbshim_test_hub_device(const void *bus) __attribute__((weak));
void *usbshim_test_default_pipe(void *dev) __attribute__((weak));
int usbshim_test_ctl_request(void *xfer, const uint8_t req[8]) __attribute__((weak));
int usbshim_test_xfer_rehome(void *xfer, void *const *holder) __attribute__((weak));

static inline int usbshim_hub_known(void)
{
    return usbshim_explore_known() && usbshim_test_hub_read && usbshim_test_hub_explore;
}

static inline int usbshim_hub_read(const void *bus, struct usbshim_hub_info *out)
{
    return bus && out && usbshim_hub_known() ? usbshim_test_hub_read(bus, out) : -1;
}

static inline int usbshim_hub_explore(const void *bus)
{
    return bus && usbshim_hub_known() ? usbshim_test_hub_explore(bus) : -1;
}

static inline void *usbshim_hub_device(const void *bus)
{
    return bus && usbshim_hub_known() && usbshim_test_hub_device ? usbshim_test_hub_device(bus) : 0;
}

static inline void *usbshim_dev_default_pipe(void *dev)
{
    return dev && usbshim_hub_known() && usbshim_test_default_pipe ? usbshim_test_default_pipe(dev) : 0;
}

static inline int usbshim_ctl_request(void *xfer, const uint8_t req[8])
{
    return xfer && req && usbshim_hub_known() && usbshim_test_ctl_request ? usbshim_test_ctl_request(xfer, req) : -1;
}

static inline int usbshim_xfer_rehome(void *xfer, void *const *holder)
{
    return xfer && holder && usbshim_hub_known() && usbshim_test_xfer_rehome ? usbshim_test_xfer_rehome(xfer, holder) : -1;
}

#else

static inline int usbshim_hub_known(void)
{
    /* The instructions the offsets come from, as the capture has them. */
    static const struct { uint32_t at, w; } sig[] = {
        { 0x10a040d0u, 0xe5902060u },   /* explore: ldr r2, [r0, #96]: the device's hub structure */
        { 0x10a040e0u, 0xe5927004u },   /* ldr r7, [r2, #4]: its softc */
        { 0x10a040e8u, 0xe5d7301au },   /* ldrb r3, [r7, #26]: suspended */
        { 0x10a0411cu, 0xe5d71019u },   /* ldrb r1, [r7, #25]: running */
        { 0x10a0413cu, 0xe5dc200au },   /* ldrb r2, [ip, #10]: the ports' number */
        { 0x10a04174u, 0xe2831030u },   /* add r1, r3, #48: the ports, 20 bytes each */
        { 0x10a04198u, 0xe59e3038u },   /* ldr r3, [lr, #56]: a port's device (48 + 8) */
        { 0x10a041acu, 0xe5d01040u },   /* ldrb r1, [r0, #64]: a port's flags (48 + 16) */
        { 0x10a041ccu, 0x15d8301bu },   /* ldrbne r3, [r8, #27]: the active children */
        { 0x10a042d4u, 0xe5dae002u },   /* ldrb lr, [sl, #2]: wPortChange's low byte */
        { 0x10a042e0u, 0xe5d3c030u },   /* ldrb ip, [r3, #48]: wPortStatus's low byte */
        { 0x10a03cd4u, 0xe5900004u },   /* the hub's resume: ldr r0, [r0, #4]: softc -> device */
        { 0x10a0544cu, 0xe592e030u },   /* the explore task: ldr lr, [r2, #48]: the root hub's device */
        { 0x10a05450u, 0xe59e3060u },   /* ldr r3, [lr, #96] */
    };
    static int8_t known = -1;
    unsigned i;
    if (known >= 0)
        return known;
    known = usbshim_explore_known() ? 1 : 0;
    for (i = 0; known && i < sizeof sig / sizeof sig[0]; i++)
        if (*(const volatile uint32_t *) (uintptr_t) sig[i].at != sig[i].w)
            known = 0;
    return known;
}

static inline uint32_t usbshim_hub_word(const volatile void *p, unsigned off)
{
    return *(const volatile uint32_t *) ((const volatile uint8_t *) p + off);
}

/* A device structure of this bus that is a hub: its hub structure, checked
 * (in RAM, a sane port count, its softc in RAM and pointing back at the
 * device). NULL: not a hub, or not what the code leaves. */
static inline const volatile uint8_t *usbshim_hub_of(const void *bus, const volatile void *dev)
{
    const volatile uint8_t *hub, *sc;
    if (!usbshim_explore_ram(dev, 100) || (const void *) (uintptr_t) usbshim_hub_word(dev, 0) != bus)
        return 0;
    hub = (const volatile uint8_t *) (uintptr_t) usbshim_hub_word(dev, 96);
    if (!usbshim_explore_ram(hub, 48) || !hub[10] || hub[10] > 15 ||
        !usbshim_explore_ram(hub, 48u + 20u * hub[10]))
        return 0;
    sc = (const volatile uint8_t *) (uintptr_t) usbshim_hub_word(hub, 4);
    if (!usbshim_explore_ram(sc, 40) || (const volatile void *) (uintptr_t) usbshim_hub_word(sc, 4) != dev)
        return 0;
    return hub;
}

/* The hub on a port of the root hub: its hub structure (*dev its device,
 * *port the root port), 0 none, -1 not known. */
static inline int usbshim_hub_find(const void *bus, const volatile uint8_t **hub_out, const volatile void **dev,
                                   unsigned *port)
{
    const volatile uint8_t *root, *hub = 0;
    const volatile void *rootdev;
    unsigned p;

    if (!usbshim_hub_known() || !usbshim_explore_ram(bus, USBSHIM_BUS_EXPLORE + 4))
        return -1;
    rootdev = (const volatile void *) (uintptr_t) usbshim_hub_word(bus, 48);
    root = usbshim_hub_of(bus, rootdev);
    if (!root)
        return -1;
    for (p = 1; p <= root[10] && !hub; p++) {
        const volatile void *child = (const volatile void *) (uintptr_t) usbshim_hub_word(root, 48u + 20u * (p - 1) + 8u);
        if (child && (hub = usbshim_hub_of(bus, child)) != 0) {
            *dev = child;
            *port = p;
        }
    }
    *hub_out = hub;
    return hub ? 1 : 0;
}

static inline void *usbshim_hub_device(const void *bus)
{
    const volatile uint8_t *hub = 0;
    const volatile void *dev = 0;
    unsigned port = 0;
    return usbshim_hub_find(bus, &hub, &dev, &port) == 1 ? (void *) (uintptr_t) dev : 0;
}

/* The code the request's offsets come from, as the capture has it. */
static inline int usbshim_ctl_known(void)
{
    static const struct { uint32_t at, w; } sig[] = {
        { 0x10a08aecu, 0xe92d4010u },   /* usbd_do_request_flags: push {r4, lr} */
        { 0x10a08af0u, 0xe5904004u },   /* ldr r4, [r0, #4]: the device's default pipe */
        { 0x10a08b14u, 0xe1a01004u },   /* mov r1, r4: ...is the pipe of the request */
        { 0x10a08978u, 0xe2860029u },   /* add r0, r6, #41: the request's 8 bytes */
        { 0x10a0897cu, 0xebe87099u },   /* bl memcpy */
        { 0x10a08980u, 0xe5d60044u },   /* ldrb r0, [r6, #68] */
        { 0x10a08988u, 0xe3803001u },   /* orr r3, r0, #1: "a request" */
        { 0x10a0898cu, 0xe5c63044u },   /* strb r3, [r6, #68] */
        { 0x10a088d0u, 0xe1d011b8u },   /* the synchronous transfer: ldrh r1, [r0, #24] */
        { 0x10a088d4u, 0xe3812002u },   /* orr r2, r1, #2: flag 2, synchronous */
        { 0x10a07a5cu, 0xe3c44001u },   /* usbd_setup_xfer: bic r4, r4, #1 (not a request) */
        { 0x10a07a78u, 0xe5803024u },   /* str r3, [r0, #36]: the callback */
        { 0x10a07a70u, 0xe580201cu },   /* str r2, [r0, #28]: the timeout */
        { 0x10a084f0u, 0xe5953004u },   /* usbd_transfer: ldr r3, [r5, #4]: the pipe's device */
        { 0x10a083e8u, 0xe584503cu },   /* usbd_alloc_xfer: str r5, [r4, #60]: the block's device */
        { 0x10a082d8u, 0xe594103cu },   /* usbd_free_xfer: ldr r1, [r4, #60] */
        { 0x10a082e0u, 0xe5913000u },   /* ldr r3, [r1]: its bus, and nothing else of it */
        { 0x10a082ecu, 0xe5932008u },   /* ldr r2, [r3, #8]: the bus's methods */
        { 0x10a082fcu, 0xe590c03cu },   /* (a DMA buffer) ldr ip, [r0, #60] */
        { 0x10a08304u, 0xe59c5000u },   /* ldr r5, [ip] */
        { 0x10a08584u, 0xe594003cu },   /* usbd_transfer (a buffer): ldr r0, [r4, #60]: block + 60... */
        { 0x10a08040u, 0xe590203cu },   /* the time limit: ldr r2, [r0, #60] */
        { 0x10a08044u, 0xe5920000u },   /* ldr r0, [r2]: ...only its bus */
        { 0x10a108f4u, 0xe594003cu },   /* the abort: ldr r0, [r4, #60] */
        { 0x10a108f8u, 0xe5902000u },   /* ldr r2, [r0]: only its bus */
    };
    static int8_t known = -1;
    unsigned i;
    if (known >= 0)
        return known;
    known = usbshim_hub_known() ? 1 : 0;
    for (i = 0; known && i < sizeof sig / sizeof sig[0]; i++)
        if (*(const volatile uint32_t *) (uintptr_t) sig[i].at != sig[i].w)
            known = 0;
    return known;
}

static inline void *usbshim_dev_default_pipe(void *dev)
{
    void *pipe;
    if (!usbshim_ctl_known() || !usbshim_explore_ram(dev, 8))
        return 0;
    pipe = (void *) (uintptr_t) usbshim_hub_word(dev, 4);
    /* (a pipe's device is at + 4: usbd_transfer, 10a084f0 / 10a0853c) */
    return usbshim_explore_ram(pipe, 20) && (const void *) (uintptr_t) usbshim_hub_word(pipe, 4) == dev ? pipe : 0;
}

static inline int usbshim_xfer_rehome(void *xfer, void *const *holder)
{
    if (!holder || !usbshim_ctl_known() || !usbshim_explore_ram(xfer, 64) ||
        !usbshim_explore_ram(*holder, USBSHIM_BUS_EXPLORE + 4))
        return -1;
    *(volatile uint32_t *) ((volatile uint8_t *) xfer + 60) = (uint32_t) (uintptr_t) holder;
    return 0;
}

static inline int usbshim_ctl_request(void *xfer, const uint8_t req[8])
{
    volatile uint8_t *x = (volatile uint8_t *) xfer;
    unsigned i;
    if (!req || !usbshim_ctl_known() || !usbshim_explore_ram(xfer, 72))
        return -1;
    for (i = 0; i < 8; i++)
        x[41 + i] = req[i];
    x[68] = (uint8_t) (x[68] | 1u);
    return 0;
}

static inline int usbshim_hub_read(const void *bus, struct usbshim_hub_info *out)
{
    const volatile uint8_t *hub = 0, *sc;
    const volatile void *dev = 0;
    unsigned p, n, port = 0;
    int r;

    if (!out)
        return -1;
    r = usbshim_hub_find(bus, &hub, &dev, &port);
    if (r != 1)
        return r;
    out->root_port = (uint8_t) port;
    sc = (const volatile uint8_t *) (uintptr_t) usbshim_hub_word(hub, 4);
    out->nports = hub[10];
    out->running = sc[25];
    out->suspended = sc[26];
    out->active = sc[27];
    n = hub[10] < USBSHIM_HUB_PORTS ? hub[10] : USBSHIM_HUB_PORTS;
    for (p = 0; p < USBSHIM_HUB_PORTS; p++)
        out->port[p] = 0;
    for (p = 0; p < n; p++) {
        const volatile uint8_t *e = hub + 48u + 20u * p;
        uint8_t st = e[0], st1 = e[1], ch = e[2];
        out->port[p] = (uint8_t) ((st & 0x1fu) | (st1 & 1u ? USBSHIM_HUBP_POWER : 0) |
                                  (usbshim_hub_word(e, 8) ? USBSHIM_HUBP_DEVICE : 0) |
                                  (ch & 0x1fu ? USBSHIM_HUBP_CHANGE : 0));
    }
    return 1;
}

static inline int usbshim_hub_explore(const void *bus)
{
    const void *fake_dev[1];
    uint32_t cpsr, off;
    uint8_t b;

    if (!usbshim_hub_known() || !usbshim_explore_ram(bus, USBSHIM_BUS_EXPLORE + 4))
        return -1;
    b = ((const volatile uint8_t *) bus)[USBSHIM_BUS_EXPLORE];
    if (!(b & 1u))
        return -1;      /* no explore task there */
    /* 10a053d0 reads its argument's first word (the bus) and nothing else of it */
    fake_dev[0] = bus;
    __asm__ volatile("mrs %0, cpsr\n\torr %1, %0, #0x80\n\tmsr cpsr_c, %1" : "=&r"(cpsr), "=&r"(off) : : "memory");
    ((void (*)(const void *)) (uintptr_t) 0x10a053d0u)(fake_dev);
    __asm__ volatile("msr cpsr_c, %0" : : "r"(cpsr) : "memory");
    return 0;
}

#endif
#endif
