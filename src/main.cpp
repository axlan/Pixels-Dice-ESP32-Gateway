#include <Arduino.h>

#include "pixels_dice_interface.h"

void setup() {
  Serial.begin(115200);
  pixels::ScanForDice(2, 5);
}

std::vector<pixels::PixelsDieID> dice_list;
pixels::RollUpdates roll_updates;
pixels::BatteryUpdates battery_updates;

pixels::BlinkData blink{1, 1000, 0xFF0000, 0xFFFFFFFF, 0xFF, 0};

void loop() {
  delay(5000);
  pixels::ListDice(dice_list);
  pixels::GetDieRollUpdates(roll_updates);
  pixels::GetDieBatteryUpdates(battery_updates);
  Serial.println("###########################");
  for (auto id : dice_list) {
    auto description = pixels::GetDieDescription(id);
    Serial.printf(">>>> %s (0x%08X)\n", description.name.c_str(), id);
    for (const auto& roll : roll_updates) {
      if (roll.first == id) {
        Serial.printf("   Roll state: %s value: %u\n",
                      pixels::ToString(roll.second.state),
                      roll.second.current_face + 1);
      }
    }
    for (const auto& battery : battery_updates) {
      if (battery.first == id) {
        Serial.printf("   Battery level: %u%% is_charging: %u \n",
                      battery.second.battery_level, battery.second.is_charging);
      }
    }
    Serial.println("<<<<");
    pixels::SendDieBlink(id, blink);
  }
}
