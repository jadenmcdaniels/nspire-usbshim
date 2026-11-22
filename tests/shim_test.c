/* Runs the real trampolines and stubs on an ARMv5 CPU under qemu-arm:
 * tests/run.sh builds this with the calculator's compiler and flags. */
#include "../usbshim.h"

#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static usbshim_fn *got_methods[4];
static const char *got_name[4];
static unsigned got_size[4];
static int registrations;
static int register_result;

/* tests/run.sh links with --wrap=malloc: counts the shim's allocations. */
void *__real_malloc(size_t n);
static int mallocs;
void *__wrap_malloc(size_t n)
{
	mallocs++;
	return __real_malloc(n);
}

int usb_register_driver(int type, usbshim_fn methods[], const char *name, int flags,
                        unsigned softc_size)
{
	(void)type; (void)flags;
	got_methods[registrations] = methods;
	got_name[registrations] = name;
	got_size[registrations] = softc_size;
	registrations++;
	return register_result;
}

static int calls_match, calls_attach, calls_detach;
static device_t last_dev;
static int my_match(device_t d) { calls_match++; last_dev = d; return 3; }
static int my_attach(device_t d) { calls_attach++; last_dev = d; return 0; }
static int my_detach(device_t d) { calls_detach++; last_dev = d; return 0; }
static int other_match(device_t d) { (void)d; return 5; }

/* Completion callbacks: what reached them. */
static int cb_calls[6];
static void *cb_xfer, *cb_priv;
static unsigned cb_status;
#define CB(k) static void cb##k(void *x, void *p, unsigned s) \
	{ cb_calls[k]++; cb_xfer = x; cb_priv = p; cb_status = s; }
CB(0) CB(1) CB(2) CB(3) CB(4)
/* A Thumb callback: the ARM trampoline's ldr pc must switch state (ARMv5T). */
__attribute__((target("thumb"), noinline)) static void cb_thumb(void *x, void *p, unsigned s)
{
	cb_calls[5]++; cb_xfer = x; cb_priv = p; cb_status = s;
}

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* qemu-arm maps the heap without execute permission (on the calculator all
 * SDRAM is executable): make the start of the heap RWX with Linux's mprotect
 * syscall before the shim writes code there. */
static void heap_executable(void)
{
	char *p = malloc(64 * 1024);
	uintptr_t start = (uintptr_t)p & ~(uintptr_t)4095;
	register uintptr_t r0 __asm__("r0") = start;
	register uintptr_t r1 __asm__("r1") = 64 * 1024 + 4096;
	register uintptr_t r2 __asm__("r2") = 7;             /* PROT_READ|WRITE|EXEC */
	register uintptr_t r7 __asm__("r7") = 125;           /* __NR_mprotect */
	__asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
	if (r0 != 0)
		printf("mprotect failed: %d\n", (int)r0);
	free(p);                                             /* small blocks reuse it */
}

int main(void)
{
	heap_executable();
	char name[32] = "usbms-with-a-long-name";
	device_t dev = (device_t)0x1234;

	usbshim_t *a = usbshim_register(name, my_match, my_attach, my_detach, 40);
	CHECK(a != NULL);
	CHECK(registrations == 1);
	CHECK(got_size[0] == 40);
	strcpy(name, "changed");                       /* the OS may keep the pointer */
	CHECK(strcmp(got_name[0], "usbms-with-a-lo") == 0);

	usbshim_fn *m = got_methods[0];
	CHECK(m[3] == NULL);
	CHECK(m[0](dev) == 3 && calls_match == 1 && last_dev == dev);
	CHECK(m[1](dev) == 0 && calls_attach == 1);
	CHECK(m[2](dev) == 0 && calls_detach == 1);

	/* a second driver registered while the first is live */
	usbshim_t *b = usbshim_register("pad", other_match, my_attach, my_detach, 8);
	CHECK(b != NULL && registrations == 2);
	CHECK(got_methods[1][0](dev) == 5);

	usbshim_retire(a);
	CHECK(m[0](dev) == 0);                         /* UMATCH_NONE */
	CHECK(m[1](dev) == 6);                         /* ENXIO */
	CHECK(m[2](dev) == 0);
	CHECK(calls_match == 1 && calls_attach == 1 && calls_detach == 1);
	CHECK(got_methods[1][0](dev) == 5);            /* the other driver is untouched */

	usbshim_retire(a);                             /* twice is harmless */
	CHECK(m[0](dev) == 0);
	usbshim_retire(NULL);
	int before = mallocs;
	CHECK(usbshim_register("x", NULL, my_attach, my_detach, 0) == NULL);
	CHECK(usbshim_register("x", my_match, NULL, my_detach, 0) == NULL);
	CHECK(usbshim_register("x", my_match, my_attach, NULL, 0) == NULL);
	CHECK(mallocs == before);                      /* refused before allocating */
	CHECK(registrations == 2);

	/* usb_register_driver's result is kept, whatever it is */
	CHECK(usbshim_os_result(a) == 0);
	register_result = -7;
	usbshim_t *c = usbshim_register("res", my_match, my_attach, my_detach, 8);
	CHECK(c != NULL && usbshim_os_result(c) == -7);
	CHECK(mallocs == before + 1);
	register_result = 0;
	CHECK(usbshim_os_result(NULL) == INT_MIN);
	CHECK(usbshim_callback(NULL, cb0) == NULL);

	/* completion trampolines: arguments pass through untouched */
	void *xf = (void *)0xCAFE0, *pv = (void *)0xBEEF0;
	usbshim_cb t0 = usbshim_callback(b, cb0);
	CHECK(t0 != NULL && t0 != cb0);
	CHECK(((uintptr_t)t0 & 3) == 0);               /* ARM entry, word aligned */
	CHECK((uintptr_t)t0 >= (uintptr_t)b && (uintptr_t)t0 < (uintptr_t)b + 132);
	t0(xf, pv, 13);
	CHECK(cb_calls[0] == 1 && cb_xfer == xf && cb_priv == pv && cb_status == 13);
	CHECK(usbshim_callback(b, cb0) == t0);         /* same fn, same slot */
	usbshim_cb t1 = usbshim_callback(b, cb1);
	CHECK(t1 != NULL && t1 != t0);
	t1(xf, pv, 0xFFFFFFFFu);
	CHECK(cb_calls[1] == 1 && cb_status == 0xFFFFFFFFu && cb_calls[0] == 1);
	usbshim_cb tt = usbshim_callback(b, cb_thumb);
	CHECK(tt != NULL && ((uintptr_t)tt & 1) == 0);
	CHECK(((uintptr_t)cb_thumb & 1) == 1);         /* really Thumb */
	tt(pv, xf, 6);
	CHECK(cb_calls[5] == 1 && cb_xfer == pv && cb_priv == xf && cb_status == 6);
	CHECK(usbshim_callback(b, NULL) == NULL);
	CHECK(usbshim_callback(b, t0) == NULL);        /* a trampoline: it would loop */
	CHECK(usbshim_callback(b, cb2) != NULL);       /* the fourth: block full now */
	CHECK(usbshim_callback(b, cb3) == NULL);       /* fifth: fails safe */
	CHECK(usbshim_callback(b, cb1) == t1);          /* known ones still come back */
	CHECK(USBSHIM_CALLBACKS == 4);

	/* another block's slots are its own; a retired block's are stubs */
	usbshim_cb ta = usbshim_callback(a, cb4);
	CHECK(ta == NULL);                             /* a is retired: no new trampoline */
	usbshim_cb tc = usbshim_callback(c, cb4);
	CHECK(tc != NULL && tc != t0);
	usbshim_retire(b);
	CHECK(got_methods[1][0](dev) == 0);            /* b's match retired too */
	t0(xf, pv, 1);
	t1(xf, pv, 1);
	tt(xf, pv, 1);
	CHECK(cb_calls[0] == 1 && cb_calls[1] == 1 && cb_calls[5] == 1);
	CHECK(usbshim_callback(b, cb0) == NULL);       /* retired: nothing new, not even known */
	tc(xf, pv, 2);                                 /* c untouched */
	CHECK(cb_calls[4] == 1 && cb_status == 2);
	usbshim_retire(c);
	tc(xf, pv, 3);
	CHECK(cb_calls[4] == 1);
	usbshim_retire(b);
	t0(xf, pv, 1);                                 /* still a stub */
	CHECK(cb_calls[0] == 1);

	printf(failures ? "shim_test: %d FAILED\n" : "shim_test: all passed\n", failures);
	return failures != 0;
}
