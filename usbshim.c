/* nspire-usbshim: see usbshim.h for why this exists and how to use it. */
#include "usbshim.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef USBSHIM_TEST
int usb_register_driver(int type, usbshim_fn methods[], const char *name, int flags,
                        unsigned softc_size);
#define UMATCH_NONE 0
#define ENXIO 6
#else
#include <usbdi.h>
#endif

#define SHIM_MAGIC   0x55534831u          /* "USH1" */
#define LDR_PC_LIT   0xE51FF004u          /* ldr pc, [pc, #-4]: jump to the next word */
#define BX_LR        0xE12FFF1Eu
#define MOV_R0(v)    (0xE3A00000u | (v))  /* mov r0, #v (v < 256) */

/* Every word is code or data the OS may read after the program is gone.
 * Code words are written once, in usbshim_register(), before the one
 * I-cache invalidate; later writes (a callback's target, retiring) touch
 * only the literal words, which the trampolines load through the data side. */
struct usbshim {
	uint32_t tramp[3][2];   /* code: ldr pc, [pc, #-4] ; .word target */
	uint32_t stub[3][2];    /* code: mov r0, #result ; bx lr */
	uint32_t cb_tramp[USBSHIM_CALLBACKS][2]; /* code: ldr pc, [pc, #-4] ; .word fn,
	                                            or cb_stub while unused / retired */
	uint32_t cb_stub;       /* code: bx lr */
	usbshim_fn methods[4];  /* what the OS gets: the trampolines, then NULL */
	uint32_t magic;
	uint32_t ncb;           /* cb_tramp slots handed out */
	uint32_t retired;
	int os_result;          /* usb_register_driver's return, kept, not acted on */
	char name[16];          /* the OS may keep this pointer too */
};

_Static_assert(sizeof(struct usbshim) == 132, "usbshim.h states the leak per run");

/* Results once retired: match refuses, attach fails, detach succeeds. */
static const uint32_t retired_result[3] = { UMATCH_NONE, ENXIO, 0 };

/* Make freshly written code visible to instruction fetch (ARM926EJ-S:
 * 32-byte cache lines; the CP15 ops need a privileged mode, which Ndless
 * programs run in). Under qemu-arm the emulator tracks code writes itself. */
static void sync_code(const void *start, size_t len)
{
#ifdef USBSHIM_TEST
	(void)start; (void)len;
#else
	uintptr_t a = (uintptr_t)start & ~(uintptr_t)31, end = (uintptr_t)start + len;
	for (; a < end; a += 32)
		__asm__ volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(a) : "memory"); /* clean D line */
	__asm__ volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0) : "memory");    /* drain write buffer */
	__asm__ volatile("mcr p15, 0, %0, c7, c5, 0" :: "r"(0) : "memory");     /* invalidate I-cache */
#endif
}

usbshim_t *usbshim_register(const char *name, usbshim_fn match, usbshim_fn attach,
                            usbshim_fn detach, unsigned softc_size)
{
	if (!match || !attach || !detach)
		return NULL;
	struct usbshim *s = malloc(sizeof *s);  /* never freed, see usbshim.h */
	if (!s)
		return NULL;
	usbshim_fn real[3] = { match, attach, detach };
	for (int i = 0; i < 3; i++) {
		s->tramp[i][0] = LDR_PC_LIT;
		s->tramp[i][1] = (uint32_t)(uintptr_t)real[i];
		s->stub[i][0] = MOV_R0(retired_result[i]);
		s->stub[i][1] = BX_LR;
		s->methods[i] = (usbshim_fn)(uintptr_t)s->tramp[i];
	}
	s->cb_stub = BX_LR;
	for (int i = 0; i < USBSHIM_CALLBACKS; i++) {
		s->cb_tramp[i][0] = LDR_PC_LIT;
		s->cb_tramp[i][1] = (uint32_t)(uintptr_t)&s->cb_stub;
	}
	s->methods[3] = NULL;
	s->magic = SHIM_MAGIC;
	s->ncb = 0;
	s->retired = 0;
	strncpy(s->name, name ? name : "usbshim", sizeof s->name - 1);
	s->name[sizeof s->name - 1] = '\0';
	sync_code(s, sizeof *s);
	s->os_result = usb_register_driver(2, s->methods, s->name, 0, softc_size);
	return s;
}

usbshim_cb usbshim_callback(usbshim_t *s, usbshim_cb fn)
{
	uintptr_t f = (uintptr_t)fn, b = (uintptr_t)s;
	if (!s || s->magic != SHIM_MAGIC || s->retired || !fn)
		return NULL;
	if (f >= b && f < b + sizeof *s)        /* a trampoline of ours: it would loop */
		return NULL;
	for (uint32_t i = 0; i < s->ncb; i++)
		if (s->cb_tramp[i][1] == (uint32_t)f)
			return (usbshim_cb)(uintptr_t)s->cb_tramp[i];
	if (s->ncb >= USBSHIM_CALLBACKS)
		return NULL;                        /* full: the caller opens no pipe */
	uint32_t *t = s->cb_tramp[s->ncb];
	t[1] = (uint32_t)f;                     /* data only, as in usbshim_retire() */
	sync_code(t, 8);
	s->ncb++;
	return (usbshim_cb)(uintptr_t)t;
}

int usbshim_os_result(const usbshim_t *s)
{
	if (!s || s->magic != SHIM_MAGIC)
		return INT_MIN;
	return s->os_result;
}

void usbshim_retire(usbshim_t *s)
{
	if (!s || s->magic != SHIM_MAGIC)
		return;
	/* Plain data writes: the trampolines read their target through the data
	 * side, so no instruction-cache work is needed; the clean just pushes the
	 * words out of the write buffer before the program's memory is reused. */
	s->retired = 1;                         /* no new callback trampolines */
	for (int i = 0; i < 3; i++)
		s->tramp[i][1] = (uint32_t)(uintptr_t)s->stub[i];
	for (int i = 0; i < USBSHIM_CALLBACKS; i++)
		s->cb_tramp[i][1] = (uint32_t)(uintptr_t)&s->cb_stub;
	sync_code(s, sizeof *s);
}
