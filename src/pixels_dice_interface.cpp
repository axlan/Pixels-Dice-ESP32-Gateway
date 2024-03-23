#include "pixels_dice_interface.h"

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <esp32-hal-log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <unordered_map>

namespace pixels {

namespace {

struct DieState {
  DescriptionData description;
  BLEAdvertisedDevice device;
  BLEClient *client = nullptr;
  bool try_to_connect_ = false;
};

class PixelAdvertiseCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override;
};

#pragma pack(push, 1)
struct PackedBlinkData {
  uint8_t type = 29;
  uint8_t count = 0;
  uint16_t duration = 0;
  uint32_t color = 0;
  uint32_t mask = 0;
  uint8_t fade = 0;
  uint8_t loop = 0;
  PackedBlinkData(const BlinkData &data)
      : count(data.count),
        duration(data.duration),
        color(data.color),
        mask(data.mask),
        fade(data.fade),
        loop(data.loop) {}
};
#pragma pack(pop)

}  // namespace

static constexpr const char *serviceUUID =
    "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr const char *notifUUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr const char *writeUUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

static constexpr size_t MAX_EVENT_QUEUE_SIZE = 1000;

static std::atomic<uint32_t> scan_duration_{0};
static std::atomic<uint32_t> time_between_scans_{0};
static std::atomic<bool> run_scans_{false};
static std::atomic<bool> auto_connect_{false};

static RollUpdates roll_updates_;
static BatteryUpdates battery_updates_;

static std::unordered_map<PixelsDieID, DieState> die_map_;

static StaticSemaphore_t event_mutex_buffer_;
static SemaphoreHandle_t event_mutex_handle_ =
    xSemaphoreCreateMutexStatic(&event_mutex_buffer_);

static StaticSemaphore_t connect_mutex_buffer_;
static SemaphoreHandle_t connect_mutex_handle_ =
    xSemaphoreCreateMutexStatic(&connect_mutex_buffer_);

static inline bool DieFound(PixelsDieID id) {
  return die_map_.find(id) != die_map_.end();
}

void PixelAdvertiseCallbacks::onResult(BLEAdvertisedDevice advertisedDevice) {
  if (!advertisedDevice.haveServiceUUID()) {
    return;
  }

  for (size_t i = 0; i < advertisedDevice.getServiceUUIDCount(); i++) {
    BLEUUID service = advertisedDevice.getServiceUUID(i);
    if (service.toString() != serviceUUID) {
      continue;
    }
    if (!advertisedDevice.haveServiceData()) {
      log_e("No Service Data");
      continue;
    }

    // See:
    // https://github.com/GameWithPixels/.github/blob/main/doc/CommunicationsProtocol.md#service-data
    std::__cxx11::string rawData = advertisedDevice.getServiceData();
    PixelsDieID pixelId = *(uint32_t *)&rawData[0];

    // The Die was previously found.
    if (DieFound(pixelId)) {
      log_d("Existing die %u found", pixelId);
      continue;
    }
    if (xSemaphoreTake(connect_mutex_handle_, portMAX_DELAY)) {
      DieState &die = die_map_[pixelId];
      die.description.name = advertisedDevice.getName();
      die.device = advertisedDevice;
      die.client = BLEDevice::createClient();
      die.try_to_connect_ = auto_connect_;

      // This default constructs an instance of DieState in the map.
      log_i(">>> CREATE NEW DICE %s (%u) <<<", die.description.name.c_str(),
            pixelId);

      // See:
      // https://github.com/GameWithPixels/.github/blob/main/doc/CommunicationsProtocol.md#manufacturer-data
      if (advertisedDevice.haveManufacturerData()) {
        uint8_t *pointer =
            (uint8_t *)advertisedDevice.getManufacturerData().data();
        die.description.led_count = pointer[2];
        die.description.design_color = pointer[3];

        // For simplicity don't generate events for die that may not be
        // connected. battery_updates_.emplace_back(
        //     pixelId,
        //     BatteryEvent{uint8_t(pointer[6] & 0x7f), (pointer[6] & 0x80) !=
        //     0});
        // roll_updates_.emplace_back(
        //     pixelId, RollEvent{RollState(pointer[4]), pointer[5]});
      }
      xSemaphoreGive(connect_mutex_handle_);
    }

    // if (auto_connect_) {
    //   ConnectDie(pixelId);
    // }
  }
}

static void PixelNotifyCallback(
    PixelsDieID id, BLERemoteCharacteristic *pBLERemoteCharacteristic,
    uint8_t *pData, size_t length, bool isNotify) {

  if (pData[0] == 3) {
    log_i("Roll state: %u, face: %u", pData[1], pData[2]);
    if (xSemaphoreTake(event_mutex_handle_, portMAX_DELAY)) {
      if (roll_updates_.size() >= MAX_EVENT_QUEUE_SIZE) {
        roll_updates_.erase(roll_updates_.begin());
      }
      roll_updates_.emplace_back(id, RollEvent{RollState(pData[1]), pData[2]});
      xSemaphoreGive(event_mutex_handle_);
    }
  } else if (pData[0] == 34) {
    log_i("Battery level: %u%%, is_charging: %u", pData[1], pData[2]);
    if (xSemaphoreTake(event_mutex_handle_, portMAX_DELAY)) {
      if (battery_updates_.size() >= MAX_EVENT_QUEUE_SIZE) {
        battery_updates_.erase(battery_updates_.begin());
      }
      battery_updates_.emplace_back(id, BatteryEvent{pData[1], pData[2]});
      xSemaphoreGive(event_mutex_handle_);
    }
  }
}

void UpdateDieConnections() {
  for (auto &pair : die_map_) {
    DieState &die = pair.second;
    PixelsDieID id = pair.first;

    auto TryDisconnect = [&die, id]() {
      if (die.client->isConnected()) {
        log_i("Disconnect from %s (%u)", die.description.name.c_str(), id);
        die.client->disconnect();
      }
    };

    if (!die.try_to_connect_) {
      TryDisconnect();
    } else if (!die.client->isConnected()) {
      log_i("Connecting");
      die.client->connect(&die.device);
      if (!die.client->isConnected()) {
        log_e("Failed to connect to server");
        return;
      }

      BLERemoteService *pRemoteService = die.client->getService(serviceUUID);
      if (pRemoteService == nullptr) {
        log_e("Failed to find our service UUID");
        DisconnectDie(id);
        return;
      }

      BLERemoteCharacteristic *pRemoteCharacteristic =
          pRemoteService->getCharacteristic(notifUUID);
      if (pRemoteCharacteristic == nullptr) {
        log_e("Failed to find our characteristic UUID");
        DisconnectDie(id);
        return;
      }

      if (pRemoteCharacteristic->canNotify()) {
        auto callback = std::bind(&PixelNotifyCallback, id,
                                  std::placeholders::_1, std::placeholders::_2,
                                  std::placeholders::_3, std::placeholders::_4);
        pRemoteCharacteristic->registerForNotify(callback);
      } else {
        log_e("Failed to register for notifications");
        DisconnectDie(id);
        return;
      }

      log_i("Connected to %s (%u) <<<", die.description.name.c_str(), id);
      return;
    }
  }
}

static void ScanTaskLoop(void *parameter) {
  log_i("Initialize BLE scan");
  BLEDevice::init("");
  BLEScan *ble_scanner = BLEDevice::getScan();  // create new scan
  ble_scanner->setAdvertisedDeviceCallbacks(new PixelAdvertiseCallbacks());
  ble_scanner->setActiveScan(
      true);  // active scan uses more power, but get results faster
  ble_scanner->setInterval(100);
  ble_scanner->setWindow(99);  // less or equal setInterval value

  while (true) {
    if (run_scans_) {
      log_i("Start BLE scan");
      ble_scanner->start(scan_duration_);
      log_i("End BLE scan");
    }
    UpdateDieConnections();
    vTaskDelay(pdMS_TO_TICKS(time_between_scans_ * 1000));
  }
}

void ScanForDice(uint32_t duration, uint32_t delay_between_scans,
                 bool auto_connect) {
  static bool initialized = false;

  scan_duration_ = duration;
  time_between_scans_ = delay_between_scans;
  run_scans_ = true;
  auto_connect_ = auto_connect;

  if (!initialized) {
    xTaskCreate(ScanTaskLoop, "ScanTask", 10000, NULL, 1, NULL);
    initialized = true;
  }
}

void StopScanning() { run_scans_ = false; }

/**
 * Get the list of dice that have been found, but aren't currently connected.
 *
 * @return The list of unconnected dice.
 */
void ListDice(std::vector<PixelsDieID> &out_list,
              DieConnectionState die_to_list) {
  out_list.clear();
  if (xSemaphoreTake(connect_mutex_handle_, portMAX_DELAY)) {
    for (const auto &pair : die_map_) {
      if (die_to_list == DieConnectionState::ANY ||
          (die_to_list == DieConnectionState::CONNECTED &&
           pair.second.client->isConnected()) ||
          (die_to_list == DieConnectionState::DISCONNECTED &&
           !pair.second.client->isConnected())) {
        out_list.push_back(pair.first);
      }
    }
    xSemaphoreGive(connect_mutex_handle_);
  }
}

void ConnectDie(PixelsDieID id) {
  if (!DieFound(id)) {
    log_e("Connecting to unknown die");
  } else {
    die_map_[id].try_to_connect_ = true;
  }
}

void DisconnectDie(PixelsDieID id) {
  if (!DieFound(id)) {
    log_e("Disconnecting from unknown die");
  } else {
    die_map_[id].try_to_connect_ = false;
  }
}

bool SendDieBlink(PixelsDieID id, const BlinkData &blink) {
  if (!DieFound(id)) {
    log_e("Blinking unknown die");
    return false;
  }
  auto &die = die_map_[id];
  if (!die.client->isConnected()) {
    log_e("Blinking disconnected die");
    return false;
  }

  BLERemoteService *pRemoteService = die.client->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    log_e("Failed to find our service UUID");
    DisconnectDie(id);
    return false;
  }

  BLERemoteCharacteristic *pRemoteCharacteristic =
      pRemoteService->getCharacteristic(writeUUID);
  if (pRemoteCharacteristic == nullptr) {
    log_e("Failed to find our characteristic UUID");
    DisconnectDie(id);
    return false;
  }

  if (!pRemoteCharacteristic->canWrite()) {
    log_e("Failed to write");
    DisconnectDie(id);
    return false;
  }

  log_e("Sending blink");

  PackedBlinkData packed_blink(blink);
  uint8_t *dataPtr = (uint8_t *)&packed_blink;
  // Serial.println("Write pRemoteCharacteristic");
  pRemoteCharacteristic->writeValue(dataPtr, sizeof(PackedBlinkData), true);
  return true;
}

DescriptionData GetDieDescription(PixelsDieID id) {
  if (!DieFound(id)) {
    log_e("Connecting to unknown die");
    return DescriptionData();
  } else {
    return die_map_[id].description;
  }
}

void GetDieRollUpdates(RollUpdates &out_events) {
  out_events.clear();
  if (xSemaphoreTake(event_mutex_handle_, portMAX_DELAY)) {
    std::swap(out_events, roll_updates_);
    xSemaphoreGive(event_mutex_handle_);
  }
}

void GetDieBatteryUpdates(BatteryUpdates &out_events) {
  out_events.clear();
  if (xSemaphoreTake(event_mutex_handle_, portMAX_DELAY)) {
    std::swap(out_events, battery_updates_);
    xSemaphoreGive(event_mutex_handle_);
  }
}

}  // namespace pixels
