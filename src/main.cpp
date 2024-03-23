#include <Arduino.h>

#include "pixels_dice_interface.h"

using PixelsDieID = pixels::PixelsDieID;
using DieConnectionState = pixels::DieConnectionState;
using RollUpdates = pixels::RollUpdates;

void setup() {
  Serial.begin(115200);
  pixels::ScanForDice(2, 5);
}

std::vector<PixelsDieID> dice_list;
RollUpdates roll_updates;
pixels::BlinkData blink{1, 1000, 0xFF0000,0xFFFFFFFF, 0xFF, 0};

void loop() {
  delay(5000);
  pixels::ListDice(dice_list);
  pixels::GetDieRollUpdates(roll_updates);
  Serial.println("###########################");
  for (auto id : dice_list) {
    auto description = pixels::GetDieDescription(id);
    Serial.printf(">>>> %s (0x%08X)\n", description.name.c_str(), id);
    for (const auto& roll : roll_updates) {
      if (roll.first == id) {
        Serial.printf("  state: %s value: %u\n",
                      pixels::ToString(roll.second.state),
                      roll.second.current_face + 1);
      }
    }
    Serial.println("<<<<");
    pixels::SendDieBlink(id, blink);
  }
}
