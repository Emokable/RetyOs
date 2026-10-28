// Isolated-only HWC 1.0 implementation for headless SurfaceFlinger testing.
// It composites through EGL and never opens or writes a physical display.
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <EGL/egl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

namespace {
struct Composer {
  hwc_composer_device_1_t device;
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  const hwc_procs_t* callbacks;
  bool enabled;
  bool stopping;
};
void* vsyncLoop(void* opaque) {
  Composer* c = static_cast<Composer*>(opaque);
  pthread_mutex_lock(&c->mutex);
  while (!c->stopping) {
    while (!c->stopping && (!c->enabled || !c->callbacks))
      pthread_cond_wait(&c->condition, &c->mutex);
    if (c->stopping) break;
    timespec deadline; clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 33333333;
    if (deadline.tv_nsec >= 1000000000) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000; }
    int result = pthread_cond_timedwait(&c->condition, &c->mutex, &deadline);
    const hwc_procs_t* cb = c->enabled && !c->stopping ? c->callbacks : nullptr;
    if (result == ETIMEDOUT && cb && cb->vsync) {
      timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
      pthread_mutex_unlock(&c->mutex);
      cb->vsync(cb, HWC_DISPLAY_PRIMARY, int64_t(now.tv_sec) * 1000000000LL + now.tv_nsec);
      pthread_mutex_lock(&c->mutex);
    }
  }
  pthread_mutex_unlock(&c->mutex);
  return nullptr;
}
void registerProcs(hwc_composer_device_1_t* device, const hwc_procs_t* callbacks) {
  Composer* c = reinterpret_cast<Composer*>(device);
  pthread_mutex_lock(&c->mutex); c->callbacks = callbacks;
  pthread_cond_signal(&c->condition); pthread_mutex_unlock(&c->mutex);
}
int closeDevice(hw_device_t* common) {
  Composer* c = reinterpret_cast<Composer*>(common);
  pthread_mutex_lock(&c->mutex); c->stopping = true;
  pthread_cond_signal(&c->condition); pthread_mutex_unlock(&c->mutex);
  pthread_join(c->thread, nullptr);
  pthread_cond_destroy(&c->condition); pthread_mutex_destroy(&c->mutex);
  free(c);
  return 0;
}

int prepare(hwc_composer_device_1_t*, size_t count,
            hwc_display_contents_1_t** displays) {
  if (!displays || count == 0 || !displays[0]) return -EINVAL;
  hwc_display_contents_1_t* display = displays[0];
  if (display->flags & HWC_GEOMETRY_CHANGED) {
    for (size_t i = 0; i < display->numHwLayers; ++i)
      display->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
  }
  return 0;
}

int set(hwc_composer_device_1_t*, size_t count,
        hwc_display_contents_1_t** displays) {
  if (!displays || count == 0 || !displays[0]) return -EINVAL;
  hwc_display_contents_1_t* display = displays[0];
  if (display->dpy == EGL_NO_DISPLAY || display->sur == EGL_NO_SURFACE)
    return -EINVAL;
  return eglSwapBuffers(display->dpy, display->sur) ? 0 : HWC_EGL_ERROR;
}

int blank(hwc_composer_device_1_t*, int display, int blanked) {
  if (display != HWC_DISPLAY_PRIMARY || (blanked != 0 && blanked != 1))
    return -EINVAL;
  return 0;
}

int eventControl(hwc_composer_device_1_t* device, int display, int event, int enabled) {
  if (display != HWC_DISPLAY_PRIMARY || event != HWC_EVENT_VSYNC ||
      (enabled != 0 && enabled != 1)) return -EINVAL;
  Composer* c = reinterpret_cast<Composer*>(device);
  pthread_mutex_lock(&c->mutex); c->enabled = enabled != 0;
  pthread_cond_signal(&c->condition); pthread_mutex_unlock(&c->mutex);
  return 0;
}

int openDevice(const hw_module_t* module, const char* id, hw_device_t** out) {
  if (!module || !id || !out || strcmp(id, HWC_HARDWARE_COMPOSER)) return -EINVAL;
  auto* state = static_cast<Composer*>(calloc(1, sizeof(Composer)));
  if (!state) return -ENOMEM;
  pthread_mutex_init(&state->mutex, nullptr); pthread_cond_init(&state->condition, nullptr);
  if (pthread_create(&state->thread, nullptr, vsyncLoop, state)) {
    pthread_cond_destroy(&state->condition); pthread_mutex_destroy(&state->mutex); free(state); return -ENOMEM;
  }
  auto* device = &state->device;
  device->common.tag = HARDWARE_DEVICE_TAG;
  device->common.version = HWC_DEVICE_API_VERSION_1_0;
  device->common.module = const_cast<hw_module_t*>(module);
  device->common.close = closeDevice;
  device->prepare = prepare;
  device->set = set;
  device->eventControl = eventControl;
  device->blank = blank;
  device->registerProcs = registerProcs;
  *out = &device->common;
  return 0;
}

hw_module_methods_t gMethods = { openDevice };
}

extern "C" hwc_module_t HAL_MODULE_INFO_SYM = {};

__attribute__((constructor)) static void initModuleInfo() {
  HAL_MODULE_INFO_SYM.common.tag = HARDWARE_MODULE_TAG;
  HAL_MODULE_INFO_SYM.common.module_api_version = HARDWARE_MODULE_API_VERSION(1, 0);
  HAL_MODULE_INFO_SYM.common.hal_api_version = HARDWARE_HAL_API_VERSION;
  HAL_MODULE_INFO_SYM.common.id = HWC_HARDWARE_MODULE_ID;
  HAL_MODULE_INFO_SYM.common.name = "Isolated headless mock HWC 1.0";
  HAL_MODULE_INFO_SYM.common.author = "Local Android 7 port experiment";
  HAL_MODULE_INFO_SYM.common.methods = &gMethods;
}
