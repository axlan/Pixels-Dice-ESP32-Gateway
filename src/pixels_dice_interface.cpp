#include "pixels_dice_interface.h"

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <esp32-hal-log.h>
#include <freertos/semphr.h>  // Include the semaphore definitions.

#include <unordered_map>

namespace pixels {
namespace {

struct DieState {
  DescriptionData description;
  BLEAdvertisedDevice device;
  BLEClient *client = nullptr;
};

static constexpr const char *serviceUUID =
    "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr const char *notifUUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr const char *writeUUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

static bool auto_connect_ = false;
static RollUpdates roll_updates_;
static BatteryUpdates battery_updates_;

static std::unordered_map<PixelsDieID, DieState> die_map_;

StaticSemaphore_t event_mutex_buffer_;
SemaphoreHandle_t event_mutex_handle_ =
    xSemaphoreCreateMutexStatic(&event_mutex_buffer_);

static inline bool DieFound(PixelsDieID id) {
  return die_map_.find(id) != die_map_.end();
}

class PixelAdvertiseCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
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
      DieState &die = die_map_[pixelId];
      die.description.name = advertisedDevice.getName();
      die.device = advertisedDevice;
      die.client = BLEDevice::createClient();

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

      // if (auto_connect_) {
      //   ConnectDie(pixelId);
      // }
    }
  }
};

void PixelNotifyCallback(PixelsDieID id,
                         BLERemoteCharacteristic *pBLERemoteCharacteristic,
                         uint8_t *pData, size_t length, bool isNotify) {
  /*
  Serial.print("Notify callback for characteristic ");
  Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
  Serial.print(" of data length ");
  Serial.println(length);
  Serial.print("Data type: ");
  Serial.println(pData[0]);
  */

  if (pData[0] == 3) {
    log_i("Roll state: %u, face: %u", pData[1], pData[2]);
    if (xSemaphoreTake(event_mutex_handle_, portMAX_DELAY)) {
      roll_updates_.emplace_back(id, RollEvent{RollState(pData[1]), pData[2]});
      xSemaphoreGive( event_mutex_handle_ );
    }
  } else if (pData[0] == 34) {
    // Serial.print("Battery level: "); Serial.print(pData[1]); Serial.print("
    // state: ");  Serial.println(pData[2]);
  }
}

}  // namespace

void ScanForDice(uint32_t duration, bool auto_connect) {
  static BLEScan *ble_scanner = nullptr;
  if (ble_scanner == nullptr) {
    log_i("Initialize BLE scan");
    BLEDevice::init("");
    ble_scanner = BLEDevice::getScan();  // create new scan
    ble_scanner->setAdvertisedDeviceCallbacks(new PixelAdvertiseCallbacks());
    ble_scanner->setActiveScan(
        true);  // active scan uses more power, but get results faster
    ble_scanner->setInterval(100);
    ble_scanner->setWindow(99);  // less or equal setInterval value
  }
  log_i("Start BLE scan");
  auto_connect_ = auto_connect;
  ble_scanner->start(duration);
  log_i("End BLE scan");
}

/**
 * Get the list of dice that have been found, but aren't currently connected.
 *
 * @return The list of unconnected dice.
 */
void ListDice(DieConnectionState die_to_list,
              std::vector<PixelsDieID> &out_list) {
  out_list.clear();
  for (const auto &pair : die_map_) {
    if (die_to_list == DieConnectionState::ANY ||
        (die_to_list == DieConnectionState::CONNECTED &&
         pair.second.client->isConnected()) ||
        (die_to_list == DieConnectionState::DISCONNECTED &&
         !pair.second.client->isConnected())) {
      out_list.push_back(pair.first);
    }
  }
}

bool ConnectDie(PixelsDieID id) {
  if (!DieFound(id)) {
    log_e("Connecting to unknown die");
    return false;
  }

  DieState &die = die_map_[id];
  if (die.client->isConnected()) {
    log_d("Device already connected");
    return true;
  }

  log_i("Connecting");
  die.client->connect(&die.device);
  if (!die.client->isConnected()) {
    log_e("Failed to connect to server");
    return false;
  }

  BLERemoteService *pRemoteService = die.client->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    log_e("Failed to find our service UUID");
    DisconnectDie(id);
    return false;
  }

  BLERemoteCharacteristic *pRemoteCharacteristic =
      pRemoteService->getCharacteristic(notifUUID);
  if (pRemoteCharacteristic == nullptr) {
    log_e("Failed to find our characteristic UUID");
    DisconnectDie(id);
    return false;
  }

  if (pRemoteCharacteristic->canNotify()) {
    auto callback = std::bind(&PixelNotifyCallback, id, std::placeholders::_1,
                              std::placeholders::_2, std::placeholders::_3,
                              std::placeholders::_4);
    pRemoteCharacteristic->registerForNotify(callback);
  } else {
    log_e("Failed to register for notifications");
    DisconnectDie(id);
    return false;
  }

  log_i("Connected to %s (%u) <<<", die.description.name.c_str(), id);
  return true;
}

void DisconnectDie(PixelsDieID id) {
  if (!DieFound(id)) {
    log_e("Disconnecting from unknown die");
  }

  DieState &die = die_map_[id];
  if (die.client->isConnected()) {
    log_i("Disconnect from %s (%u)", die.description.name.c_str(), id);
    die.client->disconnect();
  }
}

// /**
//  * Send a blink command to a die.
//  *
//  * See:
//  * https://github.com/GameWithPixels/.github/blob/main/doc/CommunicationsProtocol.md#blink
//  *
//  * @param id The die to send the message to.
//  * @param blink The message contents.
//  *
//  * @return `true` if the command sent successfully.
//  */
// bool SendBlink(PixelsDieID id, const BlinkData& blink);

// /**
//  * Get the fixed description of a die from when it was found in the BLE scan.
//  *
//  * @param id The die to disconnect from.
//  *
//  * @return The description of the die.
//  */
// DescriptionData GetDieDescription(PixelsDieID id);

// /**
//  * Get any new rolls that occurred since this function was last called.
//  */
// RollUpdates GetRollUpdates();

// /**
//  * Get any Battery changes since this was last called.
//  */
// BatteryUpdates GetBatteryUpdates();
}  // namespace pixels
