/* Diagnostic : descripteur de rapport de l'interface TFT (usage page 0xFF68). */
#include <hidapi/hidapi.h>
#include <stdio.h>

int main(void) {
    hid_init();
    struct hid_device_info *list = hid_enumerate(0x0C45, 0x8009), *it;
    for (it = list; it; it = it->next) {
        if (it->usage_page != 0xFF68) continue;
        hid_device *d = hid_open_path(it->path);
        if (!d) { printf("ouverture impossible: %ls\n", hid_error(NULL)); continue; }
        unsigned char desc[512] = { 0 };
        int n = hid_get_report_descriptor(d, desc, sizeof desc);
        printf("iface %d, descripteur (%d o):\n ", it->interface_number, n);
        for (int i = 0; i < n; i++) printf("%02x ", desc[i]);
        printf("\n");
        hid_close(d);
    }
    hid_free_enumeration(list);
    hid_exit();
    return 0;
}
