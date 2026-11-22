/* nspire-usbshim: register a USB device driver with the TI-Nspire OS's own
 * USB stack from a program that exits (one that is not resident).
 *
 * The OS has no call to unregister a driver. After a program exits, the OS
 * would still call that program's freed match/attach/detach code the next
 * time a device is plugged in. usbshim_register() therefore registers small
 * trampolines kept in a block that is never freed, and usbshim_retire()
 * points them at stubs that turn every device down.
 *
 * The same goes for transfer-completion callbacks (usbd_open_pipe_intr,
 * usbd_setup_xfer): if the OS keeps a pipe alive after the program closed it,
 * it would call the program's freed callback. usbshim_callback() gives a
 * trampoline in the same block to hand the OS instead, and usbshim_retire()
 * points it at a stub that just returns.
 *
 * Costs, on purpose: each run leaks one block (132 bytes) and leaves one dead
 * entry in the OS's driver list until the calculator reboots. Reusing an
 * earlier run's block is not safe: SDRAM can keep the old block through a
 * reset while the OS's list is rebuilt without it, so USB would silently stop.
 *
 * Rules for drivers built on this:
 *  - Close your pipes and stop touching the device before usbshim_retire().
 *    Completions the OS still delivers after it (a pipe it kept alive, a
 *    CANCELLED from a late abort) land in a stub and are lost.
 *  - Hand the OS only usbshim_callback()'s pointers, never your own
 *    completion functions; if it returns NULL, don't open the pipe.
 *  - A device that was plugged in before usbshim_register(), or that is still
 *    attached to an earlier run's retired driver, is not offered again: ask
 *    the user to unplug and replug it.
 *  - The OS's USB task has to run for match/attach/detach and transfers:
 *    don't keep IRQs masked while waiting on the device.
 *  - match/attach/detach run in the OS's USB task, not in your main loop.
 */
#ifndef NSPIRE_USBSHIM_H
#define NSPIRE_USBSHIM_H

#ifdef USBSHIM_TEST
typedef void *device_t;
#else
#include <os.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*usbshim_fn)(device_t);
typedef struct usbshim usbshim_t;

/* A transfer-completion callback as usbd_open_pipe_intr and usbd_setup_xfer
 * take it: (xfer, priv, status); usbd_status travels as a word (AAPCS). */
typedef void (*usbshim_cb)(void *xfer, void *priv, unsigned status);

/* Completion trampolines one usbshim_register() block holds. A trampoline
 * serves every pipe that shares the function (priv tells them apart), so a
 * driver needs one per distinct function: nspire-usbpad uses 2 (interrupt
 * IN, interrupt OUT). Each slot is 8 bytes of the leak; past 4,
 * usbshim_callback() returns NULL. */
#define USBSHIM_CALLBACKS 4

/* Registers match/attach/detach (same meaning as for any NetBSD-style USB
 * driver, see HIDn's ums.c) under name (up to 15 characters are kept).
 * softc_size: bytes the OS allocates per attached device, reachable with
 * device_get_softc(). Returns NULL when out of memory. */
usbshim_t *usbshim_register(const char *name, usbshim_fn match, usbshim_fn attach,
                            usbshim_fn detach, unsigned softc_size);

/* A pointer the OS may call in place of fn, as long as it likes: until
 * usbshim_retire() it jumps to fn with the arguments untouched, after it to
 * a stub that returns at once. It is ARM code (word aligned, bit 0 clear):
 * the OS enters it in ARM state, as it does match/attach/detach; fn itself
 * may be ARM or Thumb. Asking again for the same fn gives the same pointer.
 * NULL (hand the OS nothing: don't open the pipe) when shim is NULL or
 * retired, fn is NULL, or the block's USBSHIM_CALLBACKS slots are taken. */
usbshim_cb usbshim_callback(usbshim_t *shim, usbshim_cb fn);

/* What usb_register_driver returned when shim was registered, INT_MIN for a
 * NULL shim. Only recorded: its meaning on TI's OS is unknown, so the shim
 * does not act on it (the handle is good whatever it says). */
int usbshim_os_result(const usbshim_t *shim);

/* From now on the driver matches nothing, attach/detach do nothing and every
 * usbshim_callback() pointer just returns. Call it once before the program
 * exits, after closing your pipes (their abort/close may still call back);
 * the handle stays valid (it is leaked). */
void usbshim_retire(usbshim_t *shim);

#ifdef __cplusplus
}
#endif

#endif
