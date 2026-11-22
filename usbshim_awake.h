/* TI's USB stack puts what nobody uses to sleep, and the port does not come
 * back from it (2026-11-17). Read from the CX II's OS 6.4.0.74 code (the
 * 512 KB capture of 109c0000-10a40000); the
 * addresses below are that OS's.
 *
 * What the code does. It is Jungo's stack with selective suspend:
 *  - A hub's explore (10a040cc) ends with: if it was asked to, and the hub
 *    is not suspended and has no active child (10a043a4: the two bytes at
 *    softc + 26), suspend it (10a043f0 -> 10a04024). For a hub on a cable
 *    that closes the hub's status pipe (usbd_abort_pipe, usbd_close_pipe)
 *    and asks its parent to suspend its port; for the root hub it suspends
 *    the controller (10a05534 -> the controller's method -> 10a0ed4c).
 *  - A driver may ask the same for its own device (ioctl 1 to its parent;
 *    TI's own HID driver does once nobody has it open, 10a14964), and a
 *    device with several interfaces passes it on when its last active one
 *    asked (10a03398).
 *  - The controller's suspend (10a0ed4c) stops the schedules and the
 *    controller (USBCMD run 0) and suspends the port (10a0e7a0): PORTSC bit
 *    7, and bit 8 of the register at 0x40, the transceiver's own suspend.
 *    That bit is cleared in one place only: the port's resume (10a0eafc).
 * So: a hub with nothing on it, a hub whose last used device was pulled, a
 * device no driver uses - the bus is suspended a second or two later
 * (USBCMD 00080000, PORTSC 0000088d: three hardware logs). A hub asleep has
 * no status pipe: what is plugged into it then is never seen. And when the
 * suspended device is pulled out, nothing resumes the port: the
 * transceiver stays asleep, the OS resets the next device on it, and that
 * device never answers ("connected, not enabled", or enabled and nothing
 * offered) until the OTG adapter itself comes out (the controller is set
 * up anew then).
 *
 * What this header does, on that OS only (the code compared word for word),
 * for as long as the program runs (usbshim_awake_set(1) ... (0)): three
 * single-word changes of the OS's code.
 *  - 10a043ac `bne 10a04674` -> `b 10a04674`: a hub's explore never finds
 *    the hub idle. No hub (the root's included) is suspended for having
 *    nothing active on it; a hub on a cable keeps its status pipe.
 *  - 10a055b4 `push {r4, lr}` -> `b 10a12084` (mov r0, #10; bx lr): the
 *    stack's "suspend the bus" for its callers outside (10a02bdc, 10a02fe8)
 *    refuses at once, as it does itself with no root hub (10a055ec), before
 *    it has suspended the tree's drivers (10a055d4: that closes a hub's
 *    status pipe too).
 *  - 10a0ed4c `push {r3-r7, lr}` -> `b 10a12084`: the controller's own
 *    suspend refuses (its caller, 10a05534, passes the error on). The
 *    controller is never stopped whoever asks.
 * A device may still be suspended alone on its port by the driver that has
 * it (TI's HID driver); what wakes the transceiver after that is the
 * program's (gpSP: nspire_usb_phy_wake).
 * Each change is one aligned word (no instruction is ever half written);
 * CPU interrupts are masked for it, the data cache line is written out and
 * the instruction cache emptied. usbshim_awake_set(0) puts the OS's words
 * back; a program that died without it leaves the OS awake until its
 * restart, and the next usbshim_awake_set() finds the changed words and
 * knows them (state()).
 *
 * Tests: with USBSHIM_TEST the fake OS provides the three hooks below. */
#ifndef USBSHIM_AWAKE_H
#define USBSHIM_AWAKE_H

#include <stdint.h>

#define USBSHIM_AWAKE_HUB 1u   /* a hub's explore never suspends it */
#define USBSHIM_AWAKE_BUS 2u   /* the stack's bus suspend refuses */
#define USBSHIM_AWAKE_HC  4u   /* the controller's suspend refuses */
#define USBSHIM_AWAKE_ALL 7u
#define USBSHIM_AWAKE_N   3u

#ifdef USBSHIM_TEST

/* The fake OS's: whether it is "the known OS"; which of the changes are
 * in; put them in or take them out. Weak: a fake OS without them is an OS
 * that is not known (nothing is done). */
int usbshim_test_awake_known(void) __attribute__((weak));
unsigned usbshim_test_awake_get(void) __attribute__((weak));
void usbshim_test_awake_put(unsigned mask) __attribute__((weak));

static inline int usbshim_awake_known(void)
{
    return usbshim_test_awake_known && usbshim_test_awake_get && usbshim_test_awake_put && usbshim_test_awake_known();
}

static inline int usbshim_awake_state(void)
{
    return usbshim_awake_known() ? (int) usbshim_test_awake_get() : -1;
}

static inline int usbshim_awake_set(int on)
{
    if (!usbshim_awake_known())
        return -1;
    usbshim_test_awake_put(on ? USBSHIM_AWAKE_ALL : 0u);
    return (int) usbshim_test_awake_get();
}

#else

struct usbshim_awake_word { uint32_t at, was, put; };

/* The three words changed. */
static const struct usbshim_awake_word usbshim_awake_site[USBSHIM_AWAKE_N] = {
    { 0x10a043acu, 0x1a0000b0u, 0xea0000b0u },   /* bne 10a04674 -> b 10a04674 */
    { 0x10a055b4u, 0xe92d4010u, 0xea0032b2u },   /* push {r4, lr} -> b 10a12084 */
    { 0x10a0ed4cu, 0xe92d40f8u, 0xea000cccu },   /* push {r3-r7, lr} -> b 10a12084 */
};

/* The code around them, as the capture has it (was == put: never written). */
static const struct usbshim_awake_word usbshim_awake_ctx[] = {
    { 0x10a043a4u, 0xe1d7e1bau, 0 },   /* ldrh lr, [r7, #26]: suspended, active children */
    { 0x10a043a8u, 0xe35e0000u, 0 },   /* cmp lr, #0 */
    { 0x10a043e8u, 0xe1a00007u, 0 },   /* mov r0, r7 */
    { 0x10a043ecu, 0xe3a01001u, 0 },   /* mov r1, #1 */
    { 0x10a043f0u, 0xebffff0bu, 0 },   /* bl 10a04024: the hub's suspend */
    { 0x10a04024u, 0xe92d4070u, 0 },   /* ...which begins push {r4-r6, lr} */
    { 0x10a04028u, 0xe5d0601au, 0 },   /* ldrb r6, [r0, #26] */
    { 0x10a055b8u, 0xe5903010u, 0 },   /* the bus suspend for outside: ldr r3, [r0, #16] */
    { 0x10a055d4u, 0xebe9c307u, 0 },   /* bl 104761f8: the tree's drivers suspended */
    { 0x10a055e8u, 0xeaffffd1u, 0 },   /* b 10a05534 */
    { 0x10a055ecu, 0xe3a0000au, 0 },   /* its own refusal: mov r0, #10 */
    { 0x10a05534u, 0xe92d4038u, 0 },   /* the stack's bus suspend: push {r3-r5, lr} */
    { 0x10a0ed50u, 0xe59056e0u, 0 },   /* the controller's suspend: ldr r5, [r0, #1760] */
    { 0x10a0ed54u, 0xe1a04000u, 0 },   /* mov r4, r0 */
    { 0x10a0ed58u, 0xe3550000u, 0 },   /* cmp r5, #0 */
    { 0x10a12084u, 0xe3a0000au, 0 },   /* the branch's target: mov r0, #10 */
    { 0x10a12088u, 0xe12fff1eu, 0 },   /* bx lr */
};

static inline uint32_t usbshim_awake_rd(uint32_t at)
{
    return *(const volatile uint32_t *) (uintptr_t) at;
}

/* 1: this OS's code is the code read (each changed word holds the OS's
 * word or ours). */
static inline int usbshim_awake_known(void)
{
    static int8_t known = -1;
    unsigned i;
    if (known >= 0)
        return known;
    known = 1;
    for (i = 0; i < sizeof usbshim_awake_ctx / sizeof usbshim_awake_ctx[0]; i++)
        if (usbshim_awake_rd(usbshim_awake_ctx[i].at) != usbshim_awake_ctx[i].was)
            known = 0;
    for (i = 0; i < USBSHIM_AWAKE_N; i++) {
        uint32_t v = usbshim_awake_rd(usbshim_awake_site[i].at);
        if (v != usbshim_awake_site[i].was && v != usbshim_awake_site[i].put)
            known = 0;
    }
    return known;
}

/* Which changes are in (USBSHIM_AWAKE_*), -1: not this OS. */
static inline int usbshim_awake_state(void)
{
    int m = 0;
    unsigned i;
    if (!usbshim_awake_known())
        return -1;
    for (i = 0; i < USBSHIM_AWAKE_N; i++)
        if (usbshim_awake_rd(usbshim_awake_site[i].at) == usbshim_awake_site[i].put)
            m |= 1 << i;
    return m;
}

/* on: the changes in; 0: the OS's words back. Returns the state after
 * (USBSHIM_AWAKE_ALL or 0 when it worked), -1: not this OS, nothing done. */
static inline int usbshim_awake_set(int on)
{
    uint32_t cpsr, off;
    unsigned i;
    int wrote = 0;

    if (!usbshim_awake_known())
        return -1;
    __asm__ volatile("mrs %0, cpsr\n\torr %1, %0, #0x80\n\tmsr cpsr_c, %1" : "=&r"(cpsr), "=&r"(off) : : "memory");
    for (i = 0; i < USBSHIM_AWAKE_N; i++) {
        volatile uint32_t *p = (volatile uint32_t *) (uintptr_t) usbshim_awake_site[i].at;
        uint32_t want = on ? usbshim_awake_site[i].put : usbshim_awake_site[i].was;
        if (*p != want) {
            *p = want;
            /* the word out of the data cache into memory */
            __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" : : "r"(usbshim_awake_site[i].at & ~31u) : "memory");
            wrote = 1;
        }
    }
    if (wrote) {
        __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" : : "r"(0) : "memory");   /* drain the write buffer */
        __asm__ volatile("mcr p15, 0, %0, c7, c5, 0" : : "r"(0) : "memory");    /* empty the instruction cache */
    }
    __asm__ volatile("msr cpsr_c, %0" : : "r"(cpsr) : "memory");
    return usbshim_awake_state();
}

#endif
#endif
