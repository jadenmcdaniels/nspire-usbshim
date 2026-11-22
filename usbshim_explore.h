/* TI's "a hub has news" flag (2026-11-17). Read from the CX II's OS 6.4.0.74
 * code (the 512 KB capture of 109c0000-10a40000);
 * the addresses below are that OS's.
 *
 * A hub's status pipe completes when a device is plugged into or pulled out
 * of one of its ports. Its callback (10a03de0) calls 10a053d0, which sets
 * bit 1 of the byte at 588 of the controller's bus structure (the device's
 * first word) and wakes the stack's explore task; that task (10a05404)
 * clears the bit just before it explores (10a0543c) and does the rest in
 * its own time: the port's reset, the address, the descriptors, the
 * drivers' matches. Bit 0 of the same byte is "the explore task is there"
 * (10a05420); 10a053d0 sets bit 1 only with bit 0 set.
 *
 * The callback runs where the controller's interrupt is taken (in any
 * moment the CPU's interrupts are on); the task only when the program
 * sleeps into the OS. So a program that gives the OS little task time
 * (sound streaming by interrupt windows) can see the bit set and know that
 * a hub's port changed and the OS's USB task wants to run: the time to give
 * it more for a second or two. Reads only; nothing here writes.
 *
 * usbshim_explore_bus(dev): the bus structure of the controller dev is on,
 * to keep (a device's own structure is freed at its detach; the bus's is
 * the controller's). NULL: not the OS whose code is known, or dev is not a
 * device structure in RAM.
 * usbshim_explore_pending(bus): 1 a hub's news waits for the explore task,
 * 0 not, -1 not known (no bus, another OS, the byte not as the code leaves
 * it).
 *
 * Tests: with USBSHIM_TEST the fake OS provides the two hooks below. */
#ifndef USBSHIM_EXPLORE_H
#define USBSHIM_EXPLORE_H

#include <stdint.h>

#define USBSHIM_BUS_EXPLORE 588u   /* the byte in the bus structure: bit 0 the task is there, bit 1 news waits */

#ifdef USBSHIM_TEST

/* The fake OS's: whether it is "the known OS", and whether a hub's news
 * waits for its explore task (the "bus" is whatever the fake likes: the dev
 * itself). Weak: a fake OS without them is one that is not known. */
int usbshim_test_explore_known(void) __attribute__((weak));
int usbshim_test_explore_pending(const void *bus) __attribute__((weak));

static inline int usbshim_explore_known(void)
{
    return usbshim_test_explore_known && usbshim_test_explore_pending && usbshim_test_explore_known();
}

static inline const void *usbshim_explore_bus(const void *dev)
{
    return dev && usbshim_explore_known() ? dev : 0;
}

static inline int usbshim_explore_pending(const void *bus)
{
    return bus && usbshim_explore_known() ? usbshim_test_explore_pending(bus) != 0 : -1;
}

#else

/* RAM of a CX / CX II (0x10000000 + 64 MB), word aligned, len bytes of it. */
static inline int usbshim_explore_ram(const volatile void *p, unsigned len)
{
    uintptr_t a = (uintptr_t) p;
    return a >= 0x10000000u && a < 0x14000000u && len <= 0x14000000u - a && !(a & 3u);
}

static inline int usbshim_explore_known(void)
{
    /* The instructions the offset and the bits come from, as the capture has them. */
    static const struct { uint32_t at, w; } sig[] = {
        { 0x10a053d0u, 0xe5903000u },   /* ldr r3, [r0]: the device's bus */
        { 0x10a053d4u, 0xe5d3224cu },   /* ldrb r2, [r3, #588] */
        { 0x10a053d8u, 0xe3120001u },   /* tst r2, #1 */
        { 0x10a053dcu, 0x012fff1eu },   /* bxeq lr */
        { 0x10a053e0u, 0xe3821002u },   /* orr r1, r2, #2 */
        { 0x10a053e4u, 0xe5c3124cu },   /* strb r1, [r3, #588] */
        { 0x10a0541cu, 0xe5d0124cu },   /* the explore task: ldrb r1, [r0, #588] */
        { 0x10a05420u, 0xe3812001u },   /* orr r2, r1, #1 */
        { 0x10a05424u, 0xe5c0224cu },   /* strb r2, [r0, #588] */
        { 0x10a05430u, 0xe3120002u },   /* tst r2, #2 */
        { 0x10a0543cu, 0xe3c20002u },   /* bic r0, r2, #2 */
        { 0x10a05440u, 0xe5c3024cu },   /* strb r0, [r3, #588] */
    };
    static int8_t known = -1;
    unsigned i;
    if (known >= 0)
        return known;
    known = 1;
    for (i = 0; i < sizeof sig / sizeof sig[0]; i++)
        if (*(const volatile uint32_t *) (uintptr_t) sig[i].at != sig[i].w)
            known = 0;
    return known;
}

static inline const void *usbshim_explore_bus(const void *dev)
{
    const void *bus;
    if (!usbshim_explore_known() || !usbshim_explore_ram(dev, 4))
        return 0;
    bus = (const void *) (uintptr_t) *(const volatile uint32_t *) dev;
    return usbshim_explore_ram(bus, USBSHIM_BUS_EXPLORE + 4) ? bus : 0;
}

static inline int usbshim_explore_pending(const void *bus)
{
    uint8_t b;
    if (!usbshim_explore_known() || !usbshim_explore_ram(bus, USBSHIM_BUS_EXPLORE + 4))
        return -1;
    b = ((const volatile uint8_t *) bus)[USBSHIM_BUS_EXPLORE];
    if (!(b & 1u))
        return -1;      /* no explore task there: not the byte the code leaves */
    return (b & 2u) != 0;
}

#endif
#endif
