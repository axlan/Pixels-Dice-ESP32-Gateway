#include "pixels_dice.h"

static PixelDiceManager& dice_manager = PixelDiceManager::getInstance();

void setup() {
  Serial.begin(115200);
  dice_manager.setup();
}

void loop() {
  dice_manager.loop();
}
