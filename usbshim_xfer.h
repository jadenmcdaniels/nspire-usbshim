/* TI's "being aborted" mark on a transfer block, and the host controller's
 * pool of free blocks (2026-11-16). Read from the CX II's OS 6.4.0.74 code
 * (a 512 KB capture of 109c0000-10a40000);
 * the addresses below are that OS's.
 *
 * The defect. A transfer block is 136 bytes (10a0f88c). Byte 84 holds two
 * flags: bit 0 "being aborted", bit 1 "a second abort waits for the first".
 *  - The host controller driver's abort of a control, bulk or interrupt
 *    transfer sets bit 0 at its start (10a10960) and clears both at its end
 *    (10a10a34). If it finds bit 0 already set it takes that for an abort
 *    under way in another task, sets bit 1 and sleeps until bit 0 clears
 *    (10a10908-10a1093c: jmsleep(xfer + 96, forever) in a loop).
 *  - Its abort of an ISOCHRONOUS transfer sets bit 0 (10a0f578-10a0f588)
 *    and never clears it. Only the next isochronous submission of that
 *    block does (10a0f8b0).
 *  - usbd_free_xfer gives the block back to the controller's pool as it is
 *    (10a0828c -> 10a0d358: a tail queue, first block at softc + 1744, a
 *    block's next at + 72), and usbd_alloc_xfer takes the pool's first
 *    block as it is (10a083c4 -> 10a0f84c).
 * So a block that an isochronous abort marked and that was then freed (a
 * USB sound adapter pulled out while it plays, or stopped by an abort)
 * goes to whoever allocates next: the OS's own control requests, a hub's
 * status pipe, a controller's interrupt pipe, a drive's bulk transfer. It
 * works there, and goes round the pool, until the transfer in it is
 * aborted: a device pulled out (the driver's detach aborts its pipes; the
 * OS aborts a hub's), or a request ended at its 5 s limit. That abort
 * sleeps for good. If it is in the OS's exploring task, no device is
 * offered again and the calculator's USB is dead until its restart; if it
 * is in the task that ends requests at their limit, a request that fails
 * from then on never returns.
 *
 * What this header gives (to every driver that goes through the shim):
 *  - usbshim_xfer_known(): the OS's code at the places above is the code
 *    these offsets were read from (compared word for word). Everything
 *    else here does nothing unless it is.
 *  - usbshim_xfer_unmark(xfer): takes a stale mark off a block the caller
 *    owns and is not aborting (before it aborts its own pipe).
 *  - usbshim_xfer_pool_clean(dev): takes the mark off every block in the
 *    pool of dev's host controller (free blocks: none of them is being
 *    aborted). From the USB task (a match or an attach): the first thing a
 *    driver does with a new device, so that what the OS and the drivers
 *    allocate for it is clean. CPU interrupts are masked for the walk (a
 *    few dozen blocks: microseconds).
 * The driver that makes the marks (nspire-usbaudio's ua_os.c) takes them
 * off its own aborted transfers before it frees them, by what the byte
 * reads before and after its abort, on any OS; this header's cleaning is
 * for marks left before that fix (an older build, another program) in the
 * same OS uptime.
 *
 * Tests: with USBSHIM_TEST the fake OS provides the four hooks below. */
#ifndef USBSHIM_XFER_H
#define USBSHIM_XFER_H

#include <stdint.h>

#define USBSHIM_XFER_MARK      84u     /* the flags' byte in a transfer block */
#define USBSHIM_XFER_MARK_BITS 3u
#define USBSHIM_XFER_BYTES     136u    /* a block's size */
#define USBSHIM_XFER_NEXT      72u     /* its link in the pool */
#define USBSHIM_HC_POOL        1744u   /* the pool's first block, in the controller's softc */
#define USBSHIM_XFER_POOL_MAX  256u    /* blocks walked at most */

#ifdef USBSHIM_TEST

/* The fake OS's: whether it is "the known OS"; a block's mark byte (NULL:
 * not a block of its); the pool of the controller dev is on. Weak: a fake
 * OS without them is an OS that is not known (nothing is done). */
int usbshim_test_xfer_known(void) __attribute__((weak));
uint8_t *usbshim_test_xfer_mark(void *xfer) __attribute__((weak));
void *usbshim_test_xfer_pool_first(void *dev) __attribute__((weak));
void *usbshim_test_xfer_pool_next(void *xfer) __attribute__((weak));

static inline int usbshim_xfer_known(void)
{
    return usbshim_test_xfer_known && usbshim_test_xfer_mark && usbshim_test_xfer_pool_first &&
           usbshim_test_xfer_pool_next && usbshim_test_xfer_known();
}

static inline int usbshim_xfer_unmark(void *xfer)
{
    uint8_t *b = xfer && usbshim_xfer_known() ? usbshim_test_xfer_mark(xfer) : 0;
    if (!b || !(*b & USBSHIM_XFER_MARK_BITS))
        return 0;
    *b &= (uint8_t) ~USBSHIM_XFER_MARK_BITS;
    return 1;
}

static inline int usbshim_xfer_pool_clean(void *dev)
{
    void *x;
    unsigned n = 0;
    int cleaned = 0;
    if (!dev || !usbshim_xfer_known())
        return -1;
    for (x = usbshim_test_xfer_pool_first(dev); x && n < USBSHIM_XFER_POOL_MAX; x = usbshim_test_xfer_pool_next(x), n++)
        cleaned += usbshim_xfer_unmark(x);
    return cleaned;
}

#else

/* RAM of a CX / CX II (0x10000000 + 64 MB), word aligned, len bytes of it. */
static inline int usbshim_xfer_ram(const volatile void *p, unsigned len)
{
    uintptr_t a = (uintptr_t) p;
    return a >= 0x10000000u && a < 0x14000000u && len <= 0x14000000u - a && !(a & 3u);
}

static inline int usbshim_xfer_known(void)
{
    /* The instructions the offsets come from, as the capture has them. */
    static const struct { uint32_t at, w[5]; } sig[] = {
        /* the isochronous abort: ldrb r0,[r0,#84]; ...; orr r3,r0,#1; ...; strb r3,[r4,#84] */
        { 0x10a0f578u, { 0xe5d00054u, 0xe5962004u, 0xe3803001u, 0xe5925000u, 0xe5c43054u } },
        /* the abort's "already being aborted": ldrb r3,[r4,#84]; tst r3,#1; beq; orr ip,r3,#2; ... */
        { 0x10a10908u, { 0xe5d43054u, 0xe3130001u, 0x0a000011u, 0xe383c002u, 0xe31c0001u } },
        /* the pool's take: ldr r3,[r0,#1744]; cmp r3,#0; mov r1,r3; beq; ldr ip,[r3,#72] */
        { 0x10a0f84cu, { 0xe59036d0u, 0xe3530000u, 0xe1a01003u, 0x0a00000bu, 0xe593c048u } },
        /* the pool's give: mov r3,#0; str r3,[r1,#72]; ldr ip,[r0,#1748]; add r3,r1,#72; str ip,[r1,#76] */
        { 0x10a0d358u, { 0xe3a03000u, 0xe5813048u, 0xe590c6d4u, 0xe2813048u, 0xe581c04cu } },
    };
    static int8_t known = -1;
    unsigned i, k;
    if (known >= 0)
        return known;
    known = 1;
    for (i = 0; i < sizeof sig / sizeof sig[0]; i++)
        for (k = 0; k < 5; k++)
            if (((const volatile uint32_t *) (uintptr_t) sig[i].at)[k] != sig[i].w[k])
                known = 0;
    return known;
}

static inline int usbshim_xfer_unmark(void *xfer)
{
    volatile uint8_t *b = (volatile uint8_t *) xfer + USBSHIM_XFER_MARK;
    if (!usbshim_xfer_known() || !usbshim_xfer_ram(xfer, USBSHIM_XFER_BYTES) || !(*b & USBSHIM_XFER_MARK_BITS))
        return 0;
    *b = (uint8_t) (*b & ~USBSHIM_XFER_MARK_BITS);
    return 1;
}

static inline int usbshim_xfer_pool_clean(void *dev)
{
    const volatile uint32_t *d = (const volatile uint32_t *) dev;
    const volatile uint32_t *sc;
    volatile uint8_t *x;
    uint32_t cpsr, off;
    unsigned n = 0;
    int cleaned = 0;

    if (!usbshim_xfer_known() || !usbshim_xfer_ram(dev, 4))
        return -1;
    sc = (const volatile uint32_t *) (uintptr_t) d[0];     /* the device's bus: the controller's softc */
    if (!usbshim_xfer_ram(sc, USBSHIM_HC_POOL + 8))
        return -1;
    __asm__ volatile("mrs %0, cpsr\n\torr %1, %0, #0x80\n\tmsr cpsr_c, %1" : "=&r"(cpsr), "=&r"(off) : : "memory");
    for (x = (volatile uint8_t *) (uintptr_t) sc[USBSHIM_HC_POOL / 4];
         x && n < USBSHIM_XFER_POOL_MAX && usbshim_xfer_ram(x, USBSHIM_XFER_BYTES);
         x = (volatile uint8_t *) (uintptr_t) *(volatile uint32_t *) (x + USBSHIM_XFER_NEXT), n++)
        if (x[USBSHIM_XFER_MARK] & USBSHIM_XFER_MARK_BITS) {
            x[USBSHIM_XFER_MARK] = (uint8_t) (x[USBSHIM_XFER_MARK] & ~USBSHIM_XFER_MARK_BITS);
            cleaned++;
        }
    __asm__ volatile("msr cpsr_c, %0" : : "r"(cpsr) : "memory");
    return cleaned;
}

#endif
#endif
