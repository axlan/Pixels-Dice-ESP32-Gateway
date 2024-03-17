#pragma once

// Based on https://gist.github.com/JpEncausse/cb1dbcca156784ac1e0804243da8e481

#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#include <vector>

// ------------------------------------------
//  DICE EFFECTS
//  https://github.com/GameWithPixels/.github/blob/main/doc/CommunicationsProtocol.md#iamadie
// ------------------------------------------

struct Blink
{
    uint8_t type = 29;
    uint8_t count = 2;
    uint16_t duration = 2000;
    uint32_t color = 0xff0000;
    uint32_t mask = 0xffffffff;
    uint8_t fade = 0;
    uint8_t loop = 0;
};

// ------------------------------------------
//  DICE
//  https://github.com/GameWithPixels/.github/blob/main/doc/CommunicationsProtocol.md#iamadie
//  Max connections : https://github.com/espressif/arduino-esp32/issues/8823#issuecomment-1789924653
// ------------------------------------------

class PixelDice
{

public:
    uint8_t ledCount = 0;
    uint8_t designColor = 0;
    uint8_t rollState = 0; // 0:Unknown - 1:OnFace - 2:Handling - 3:Rolling - 4:Crooked
    uint8_t currentFace = 0;
    uint8_t batteryLevel = 0;
    uint8_t batteryCharge = 0;
    long timestamp = 0;
    uint32_t pixelId = 0;
    uint32_t firmwareDate = 0;
    std::string name;
    std::string addr;
    BLEAdvertisedDevice device;
    BLEClient *client;

    /**
     * @brief Print the dice data.
     */
    void print();

    /**
     * @brief Connect to the dice.
     */
    bool connect();

    /**
     * @brief Connect to the dice.
     */
    bool disconnect();

    /**
     * @brief Blink the dice.
     */
    bool blinkDice();

private:
    /**
     * @brief Notify the dice data.
     */
    void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify);
};

class PixelDiceManager
{

private:
    PixelDiceManager() {}                                           // Constructeur privé
    PixelDiceManager(const PixelDiceManager &) = delete;            // Supprime le constructeur de copie
    PixelDiceManager &operator=(const PixelDiceManager &) = delete; // Supprime l'opérateur d'affectation

    std::vector<PixelDice> diceList;
    int scanTime = 10;
    /**
     * @brief Move the dice to the top of the list.
     */
    void unshiftDice(PixelDice *dice);

public:
    BLEScan *pBLEScan;

    /**
     * @brief Find an existing dice for the given pixelIdD or create a new one.
     */
    PixelDice *findDice(uint32_t pixelId);

    /**
     * @brief Return the timestamp of the dice at the given index or the latest timestamp.
     */
    time_t getTimestamp(uint8_t index = 0);

    /**
     * @brief Return the dice list.
     */
    std::vector<PixelDice> getDiceList()
    {
        return diceList;
    }

    // ------------------------------------------
    //  SINGLETON
    // ------------------------------------------

    static PixelDiceManager &getInstance();

    // ------------------------------------------
    //  DEBUG
    // ------------------------------------------

    void debugDiceList();
    void debugDice(PixelDice *dice);

    // ------------------------------------------
    //  DICE ADVERTISEMENT
    // ------------------------------------------

    /**
     * @brief Extract and Update the dice data from the advertised device.
     */
    void extractPixelsData(BLEAdvertisedDevice advertisedDevice);

    // ------------------------------------------
    //  DICE WORKFLOW
    // ------------------------------------------

    void setup();
    void loop();
};
