// Explicit virtual battery for the isolated headless Android laboratory.
// Does not open Kindle battery, power, charging or suspend interfaces.
#include <batteryservice/BatteryService.h>
#include <batteryservice/IBatteryPropertiesRegistrar.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <stdio.h>

using namespace android;
class VirtualBattery final : public BnBatteryPropertiesRegistrar {
 public:
  void registerListener(const sp<IBatteryPropertiesListener>& listener) override {
    if (!listener.get()) return;
    BatteryProperties properties{};
    properties.chargerAcOnline = true;
    properties.batteryStatus = BATTERY_STATUS_CHARGING;
    properties.batteryHealth = BATTERY_HEALTH_GOOD;
    properties.batteryPresent = true;
    properties.batteryLevel = 50;
    properties.batteryVoltage = 4000;
    properties.batteryTemperature = 250;
    properties.batteryTechnology = String8("Virtual");
    listener->batteryPropertiesChanged(properties);
    fprintf(stderr, "HEADLESS_BATTERY_LISTENER level=50 virtual=1\n");
  }
  void unregisterListener(const sp<IBatteryPropertiesListener>&) override {}
  status_t getProperty(int id, BatteryProperty* value) override {
    if (!value || id != BATTERY_PROP_CAPACITY) return BAD_VALUE;
    value->valueInt64 = 50;
    return NO_ERROR;
  }
};
int main() {
  sp<VirtualBattery> battery = new VirtualBattery();
  status_t result = defaultServiceManager()->addService(String16("batteryproperties"), battery);
  if (result != NO_ERROR) return 2;
  fprintf(stderr, "HEADLESS_BATTERY_REGISTERED virtual=1 level=50\n");
  ProcessState::self()->startThreadPool();
  IPCThreadState::self()->joinThreadPool();
  return 0;
}
