// Headless-only substitute for the AOSP GPU shell service. The Vulkan JSON
// helper is not part of the captured source set and is unrelated to display
// composition; SurfaceFlinger still publishes the normal Binder service name.
#include "GpuService.h"
#include <binder/Parcel.h>

namespace android {

class BpGpuService : public BpInterface<IGpuService> {
public:
    explicit BpGpuService(const sp<IBinder>& impl) : BpInterface<IGpuService>(impl) {}
};

IMPLEMENT_META_INTERFACE(GpuService, "android.ui.IGpuService");

status_t BnGpuService::onTransact(uint32_t code, const Parcel& data,
        Parcel* reply, uint32_t flags) {
    return BBinder::onTransact(code, data, reply, flags);
}

const char* const GpuService::SERVICE_NAME = "gpu";
GpuService::GpuService() {}
status_t GpuService::shellCommand(int, int, int, Vector<String16>&) {
    return NO_ERROR;
}

} // namespace android
