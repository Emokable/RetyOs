// Export process-local properties for the isolated headless renderer only.
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/system_properties.h>

int property_get(const char* key, char* value, const char* default_value) {
    const char* selected = NULL;
    if (!strcmp(key, "ro.kernel.qemu")) selected = access("/system/lib/egl/libEGL_swiftshader.so", R_OK) == 0 ? "0" : "1";
    else if (!strcmp(key, "qemu.gles") || !strcmp(key, "ro.kernel.qemu.gles")) selected = "0";
    if (!selected) {
        const int length = __system_property_get(key, value);
        if (length > 0) return length;
        selected = default_value ? default_value : "";
    }
    const size_t length = strlen(selected);
    memcpy(value, selected, length + 1);
    return (int)length;
}

int property_set(const char* key, const char* value) {
    (void)key;
    (void)value;
    return 0;
}
