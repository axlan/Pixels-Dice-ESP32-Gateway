/**
 * An interface for the Pixels Dice.
 *
 * This interface is intended to abstract away any asynchronous events to allow
 * the dice to be handled by a simple event loop. The functions handle
 * interacting with the BLE services and allow the data to be handled by
 * polling.
 *
 * This has the following trade offs:
 * 1. Events are queued and don't trigger immediate callbacks.
 * 2. This requires more data copying and allocations then might be needed
 *    otherwise.
 * 3. This wastes a lot of power/CPU compared to an interface that exposes more
 *    of the BLE search details.
 *
 * The goal here is simplicity, hiding as much of the implementation details as
 * possible.
 *
 * I'm also not particularly knowledgeable on the details of the BLE stack, so
 * some things are probably being done wrong/inefficiently.
 *
 * At a high level here's how this works:
 *
 * # Finding / Connecting to Dice
 * This interface doesn't do anything until ScanForDice is started. This
 * functions starts a FreeRTOS task in the background that manages searching for
 * new dice and connecting to them. My understanding is that the connect,
 * functionality blocks while a search is running, so the dice that are found
 * are only connected to when the search period is complete. These scans can be
 * stopped to save power or if you don't want to find new dice.
 *
 * By default each dice found is connected to, but by setting auto_connect to
 * false, connecting to the die can be handled manually with the ConnectDie
 * function.
 *
 * Once a die is found, it remains forever in the dice list and will be accessed
 * by it's PixelsDieID. It's description can be accessed through
 * GetDieDescription even if it's disconnected.
 *
 * # Handling Events
 * Events are read by polling the GetDieRollUpdates and GetDieBatteryUpdates
 * functions.
 *
 * These return all the events sent out by the connected die since the last
 * call.
 *
 * These queues continue to grow if not read, so they should be polled
 * regularly. There is a cutoff of 1000 values currently.
 *
 * TODO:
 * 1. Implement any useful missing features of the interface.
 * 2. Improve battery reporting. It seems to jump around, and probably doesn't
 *    need to be event driven.
 * 3. Add timestamps to roll events.
 * 4. Make Arduino/PlatformIO library.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pixels {
using PixelsDieID = uint32_t;

struct BlinkData {
  uint8_t count = 0;      // Number of blinks
  uint16_t duration = 0;  // Animation duration in milliseconds
  uint32_t color =
      0x000000;  // Color in 32 bits ARGB format (alpha value is ignored)
  uint32_t mask = 0x000000;  // Select which faces to light up
  uint8_t fade = 0;  // Amount of in and out fading (0: sharp transition 255:
                     // maximum fading.)
  uint8_t loop = 0;  // Whether to indefinitely loop the animation
  BlinkData(uint8_t count, uint16_t duration, uint32_t color, uint32_t mask,
            uint8_t fade, uint8_t loop)
      : count(count),
        duration(duration),
        color(color),
        mask(mask),
        fade(fade),
        loop(loop) {}
};

enum class RollState {
  UNKNOWN = 0,
  ON_FACE = 1,
  HANDLING = 2,
  ROLLING = 3,
  CROOKED = 4,
};

inline const char* ToString(RollState state) {
  switch (state) {
    case RollState::UNKNOWN:
      return "UNKNOWN";
    case RollState::ON_FACE:
      return "ON_FACE";
    case RollState::HANDLING:
      return "HANDLING";
    case RollState::ROLLING:
      return "ROLLING";
    case RollState::CROOKED:
      return "CROOKED";
  }
  return "INVALID";
}

enum class DieConnectionState {
  CONNECTED,
  DISCONNECTED,
  ANY,
};

struct RollEvent {
  RollState state = RollState::UNKNOWN;  // Current rolling state
  uint8_t current_face = 0;              // Current face up (face index)
  RollEvent(RollState state, uint8_t current_face)
      : state(state), current_face(current_face) {}
};

struct BatteryEvent {
  uint8_t battery_level = 0;  // Battery level in percentage
  bool is_charging = false;
  BatteryEvent(uint8_t battery_level, bool is_charging)
      : battery_level(battery_level), is_charging(is_charging) {}
};

struct DescriptionData {
  uint8_t led_count = 0;     // Number of LEDs
  uint8_t design_color = 0;  // Physical look and color of the die
  std::string name;  // Die name (as set by the user, up to 13 characters)
};

using RollUpdates = std::vector<std::pair<PixelsDieID, RollEvent>>;
using BatteryUpdates = std::vector<std::pair<PixelsDieID, BatteryEvent>>;

/**
 * Run a bluetooth scan looking for new dice.
 *
 * @param duration The duration in seconds to run the scan.
 * @param time_between_scans The time to wait in seconds between scans.
 * @param auto_connect Whether to automatically connect to any die found.
 */
void ScanForDice(uint32_t scan_time, uint32_t time_between_scans,
                 bool auto_connect = true);

void StopScanning();

/**
 * Get the list of dice that have been found based on their connection status.
 *
 * @param out_list The list to update with the dice. Any previous contents are
 * cleared.
 * @param die_to_list Select whether to return connected, disconnected, or all
 * dice.
 */
void ListDice(std::vector<PixelsDieID>& out_list,
              DieConnectionState die_to_list = DieConnectionState::CONNECTED);

/**
 * Start trying to establish a connection to a die.
 *
 * @param id The die to connect to.
 */
void ConnectDie(PixelsDieID id);

/**
 * End the connection to a die.
 *
 * @param id The die to disconnect from.
 */
void DisconnectDie(PixelsDieID id);

/**
 * Send a blink command to a connected die.
 *
 * See:
 * https://github.com/GameWithPixels/.github/blob/main/doc/CommunicationsProtocol.md#blink
 *
 * @param id The die to send the message to.
 * @param blink The message contents.
 *
 * @return `true` if the command sent successfully.
 */
bool SendDieBlink(PixelsDieID id, const BlinkData& blink);

/**
 * Get the fixed description of a die from when it was found in the BLE scan.
 *
 * This works even if the die is disconnected.
 *
 * @param id The die to disconnect from.
 *
 * @return The description of the die.
 */
DescriptionData GetDieDescription(PixelsDieID id);

/**
 * Get any new rolls from connected die that occurred since this function was
 * last called.
 */
void GetDieRollUpdates(RollUpdates& out_events);

/**
 * Get any Battery changes from connected since this was last called.
 */
void GetDieBatteryUpdates(BatteryUpdates& out_events);
}  // namespace pixels
