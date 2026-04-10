#pragma once

#include "ServiceRegistry.h"

#include <string>

struct BlueScanDevice {
  bool valid = false;
  std::string addr;
  std::string name;
  int rssi = 0;
  std::string decoded;
  unsigned long last_seen_ms = 0;
};

class BlueScanService : public SystemService {
public:
  static constexpr int MAX_DEVICES = 8;

  static BlueScanService &instance();

  const char *name() const override { return "BlueScan"; }
  bool begin() override;
  void loop() override;

  void start_scan();
  void stop_scan();
  bool scanning() const { return _scanning; }

  const BlueScanDevice *devices() const { return _devices; }
  BlueScanDevice *mutable_devices() { return _devices; }
  int device_count() const;

  bool decoder_available() const { return _decoder_available; }
  std::string status_text() const;

  int find_slot_for_addr(const std::string &addr);

private:
  void clear_devices();
  unsigned long now_ms() const;

#if !(defined(ARDUINO) && defined(ESP_PLATFORM))
  void simulate_devices();
  int _sim_tick = 0;
  unsigned long _last_sim_ms = 0;
#endif

  BlueScanDevice _devices[MAX_DEVICES]{};
  bool _scanning = false;
  bool _decoder_available = false;
};
