/* TI's USB status ABI, captured read-only from CX II OS 6.4.0.74 on
 * 2026-11-14. Ndless usbdi.h carries NetBSD's different enumeration: in
 * particular, TI status 2 means queued, not a refusal. Never resubmit a
 * transfer with that status before its completion callback.
 *
 * usbd_errstr (syscall 254) has a bounded table for 0..20. Call the check
 * below from MAIN before registering callbacks on another OS version;
 * refuse an unknown ABI instead of applying these meanings blindly.
 * Resolved OS calls must be called directly from callbacks, not through
 * Ndless's shared syscall gateway. usbd_get_xfer_status's status output
 * is uint8_t *, and setup_xfer's buffer is {data pointer, 0, 0}.
 */
#ifndef USBSHIM_USB_H
#define USBSHIM_USB_H

enum usbshim_usb_status {
    USBSHIM_USB_OK = 0,
    USBSHIM_USB_NOT_STARTED = 1,
    USBSHIM_USB_IN_PROGRESS = 2,
    USBSHIM_USB_NOT_PERMITTED = 3,
    USBSHIM_USB_NO_ENTRY = 4,
    USBSHIM_USB_IO_ERROR = 5,
    USBSHIM_USB_NOT_CONFIGURED = 6,
    USBSHIM_USB_NO_MEMORY = 7,
    USBSHIM_USB_BUSY = 8,
    USBSHIM_USB_NO_DEVICE = 9,
    USBSHIM_USB_INVALID_ARGUMENT = 10,
    USBSHIM_USB_NOT_SUPPORTED = 11,
    USBSHIM_USB_TIMEOUT = 12,
    USBSHIM_USB_SUSPENDED = 13,
    USBSHIM_USB_ERROR = 14,
    USBSHIM_USB_TEST_FAILED = 15,
    USBSHIM_USB_INCORRECT_STATE = 16,
    USBSHIM_USB_STALLED = 17,
    USBSHIM_USB_INVALID_PARAMETER = 18,
    USBSHIM_USB_ABORTED = 19,
    USBSHIM_USB_SHORT_TRANSFER = 20,
    USBSHIM_USB_STATUS_COUNT = 21
};

#define USBSHIM_USB_ERRSTR_SYSCALL 254

/* Ignore punctuation/case so harmless formatting changes do not disable
 * a supported OS. Bound the read even if an OS returns a malformed name. */
static inline int usbshim_usb_status_word(const char *s, const char *want)
{
    unsigned i;
    if (!s)
        return 0;
    for (i = 0; i < 80; i++) {
        unsigned c = (unsigned char)s[i];
        if (!c)
            return !*want;
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            if (!*want || c != (unsigned char)*want++)
                return 0;
        }
    }
    return 0;
}

static inline int usbshim_usb_status_abi(const char *(*errstr)(unsigned))
{
    static const char *const names[USBSHIM_USB_STATUS_COUNT] = {
        "NORMALCOMPLETION", "OPERATIONNOTSTARTED", "OPERATIONINPROGRESS",
        "OPERATIONNOTPERMITTED", "NOSUCHENTRY", "INPUTOUTPUTERROR",
        "DEVICENOTCONFIGURED", "FAILEDALLOCATINGMEMORY", "RESOURCEISBUSY",
        "NOSUCHDEVICE", "INVALIDARGUMENT", "OPERATIONNOTSUPPORTED",
        "OPERATIONTIMEDOUT", "DEVICEISSUSPENDED", "GENERALPURPOSEERROR",
        "LOGICALTESTFAILURE", "INCORRECTSTATE", "PIPEISSTALLED",
        "INVALIDPARAMETER", "OPERATIONABORTED", "SHORTTRANSFER"
    };
    unsigned i;
    if (!errstr)
        return 0;
    for (i = 0; i < USBSHIM_USB_STATUS_COUNT; i++)
        if (!usbshim_usb_status_word(errstr(i), names[i]))
            return 0;
    return 1;
}
#endif
