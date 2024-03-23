#include <Arduino.h>

#include "pixels_dice_interface.h"

using PixelsDieID = pixels::PixelsDieID;
using DieConnectionState = pixels::DieConnectionState;

void setup() {
  Serial.begin(115200);
}

std::vector<PixelsDieID> dice_list;

void loop() {
  pixels::ScanForDice(10);
  pixels::ListDice(DieConnectionState::DISCONNECTED, dice_list);
  for (auto& id : dice_list) {
    pixels::ConnectDie(id);
  }
}
