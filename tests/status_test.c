#include <stdio.h>
#include <string.h>
#include "../usbshim_usb.h"

/* Literal hardware capture, kept independent of the enum/helper table. */
static const char *const captured[] = {
    "NORMAL COMPLETION", "OPERATION NOT STARTED", "OPERATION IN PROGRESS",
    "OPERATION NOT PERMITTED", "NO SUCH ENTRY", "INPUT/OUTPUT ERROR",
    "DEVICE NOT CONFIGURED", "FAILED ALLOCATING MEMORY", "RESOURCE IS BUSY",
    "NO SUCH DEVICE", "INVALID ARGUMENT", "OPERATION NOT SUPPORTED",
    "OPERATION TIMED OUT", "DEVICE IS SUSPENDED", "GENERAL-PURPOSE ERROR",
    "LOGICAL TEST FAILURE", "INCORRECT STATE", "PIPE IS STALLED",
    "INVALID PARAMETER", "OPERATION ABORTED", "SHORT TRANSFER"
};
static unsigned calls, highest;
static int mismatch = -1;
static const char *errstr(unsigned s)
{
    calls++;
    if (s > highest)
        highest = s;
    if ((int)s == mismatch)
        return "UNKNOWN";
    return s < 21 ? captured[s] : "OUT OF RANGE";
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "status ABI: line %d\n", __LINE__); return 1; } } while (0)
int main(void)
{
    char long_name[81];
    unsigned i;
    CHECK(USBSHIM_USB_IN_PROGRESS == 2);
    CHECK(USBSHIM_USB_IO_ERROR == 5);
    CHECK(USBSHIM_USB_TIMEOUT == 12);
    CHECK(USBSHIM_USB_ABORTED == 19);
    CHECK(USBSHIM_USB_SHORT_TRANSFER == 20);
    CHECK(usbshim_usb_status_abi(errstr));
    CHECK(calls == 21 && highest == 20);
    CHECK(!usbshim_usb_status_abi(NULL));
    for (i = 0; i < 21; i++) {
        mismatch = (int)i;
        CHECK(!usbshim_usb_status_abi(errstr));
    }
    CHECK(usbshim_usb_status_word("operation_in-progress", "OPERATIONINPROGRESS"));
    CHECK(!usbshim_usb_status_word("IN PROGRESS", "OPERATIONINPROGRESS"));
    CHECK(!usbshim_usb_status_word(NULL, "OPERATIONINPROGRESS"));
    memset(long_name, '-', sizeof long_name);
    long_name[80] = 0;
    CHECK(!usbshim_usb_status_word(long_name, ""));
    puts("TI USB status ABI: all passed");
    return 0;
}
