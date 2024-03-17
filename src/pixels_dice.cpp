#include "pixels_dice.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr const char *serviceUUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr const char *notifUUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr const char *writeUUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

class PixelAdvertiseCallbacks : public BLEAdvertisedDeviceCallbacks
{
    void onResult(BLEAdvertisedDevice advertisedDevice) override
    {
        PixelDiceManager::getInstance().extractPixelsData(advertisedDevice);
    }
};

// ------------------------------------------
//  PIXEL DICE
// ------------------------------------------

void PixelDice::print()
{
    Serial.print("PixelId : ");
    Serial.println(pixelId);
    Serial.print("Name: ");
    Serial.println(name.c_str());
    Serial.print("LEDs: ");
    Serial.println(ledCount);
    Serial.print("Design & Color: ");
    Serial.println(designColor);
    Serial.print("Roll State: ");
    Serial.println(rollState);
    Serial.print("Current Face: ");
    Serial.println(currentFace);
    Serial.print("Batterie Level: ");
    Serial.println(batteryLevel);
    Serial.print("isCharging : ");
    Serial.println(batteryCharge);
    Serial.print("Timestamp : ");
    Serial.println(timestamp);
    Serial.print("FirmwareDate : ");
    Serial.println(firmwareDate);
}

void PixelDice::notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
    /*
    Serial.print("Notify callback for characteristic ");
    Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
    Serial.print(" of data length ");
    Serial.println(length);
    Serial.print("Data type: ");
    Serial.println(pData[0]);
    */

    if (pData[0] == 3)
    {
        Serial.print("Roll state: ");
        Serial.print(pData[1]);
        Serial.print(" face: ");
        Serial.println(pData[2]);
        if (pData[1] == 1)
        { // On Face
            currentFace = pData[2];
            timestamp = millis();
        }
    }
    else if (pData[0] == 34)
    {
        // Serial.print("Battery level: "); Serial.print(pData[1]); Serial.print(" state: ");  Serial.println(pData[2]);
    }
}

bool PixelDice::blinkDice()
{
    bool isConnected = client->isConnected();
    if (!isConnected)
    {
        connect();
    }

    BLERemoteService *pRemoteService = client->getService(serviceUUID);
    if (pRemoteService == nullptr)
    {
        Serial.println("Failed to find our service UUID");
        disconnect();
        return false;
    }

    BLERemoteCharacteristic *pRemoteCharacteristic = pRemoteService->getCharacteristic(writeUUID);
    if (pRemoteCharacteristic == nullptr)
    {
        Serial.println("Failed to find our characteristic UUID");
        disconnect();
        return false;
    }

    if (!pRemoteCharacteristic->canWrite())
    {
        Serial.println("Failed to write");
        disconnect();
        return false;
    }

    Blink blink;
    uint8_t *dataPtr = (uint8_t *)&blink;
    // Serial.println("Write pRemoteCharacteristic");
    pRemoteCharacteristic->writeValue(dataPtr, sizeof(blink), true);
    delay(3000);

    if (!isConnected)
    {
        disconnect();
    }
    return true;
}

bool PixelDice::disconnect()
{
    if (!client->isConnected())
    {
        return true;
    }
    Serial.print("Disconnect from ");
    Serial.println(name.c_str());
    client->disconnect();
    return true;
}

bool PixelDice::connect()
{
    long t0 = millis();

    if (client->isConnected())
    {
        // Serial.println("Device already connected");
        return true;
    }

    Serial.println("Connecting");
    client->connect(&device);
    if (!client->isConnected())
    {
        Serial.println("Failed to connect to server");
        return false;
    }

    BLERemoteService *pRemoteService = client->getService(serviceUUID);
    if (pRemoteService == nullptr)
    {
        Serial.print("Failed to find our service UUID");
        disconnect();
        return false;
    }

    BLERemoteCharacteristic *pRemoteCharacteristic = pRemoteService->getCharacteristic(notifUUID);
    if (pRemoteCharacteristic == nullptr)
    {
        Serial.print("Failed to find our characteristic UUID");
        disconnect();
        return false;
    }

    if (pRemoteCharacteristic->canNotify())
    {
        auto callback = std::bind(&PixelDice::notifyCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
        pRemoteCharacteristic->registerForNotify(callback);
    }
    else
    {
        Serial.println("Failed to register for notifications");
        disconnect();
        return false;
    }
    Serial.print("Connected to ");
    Serial.print(name.c_str());
    Serial.print(" in ");
    Serial.print(millis() - t0);
    Serial.println("ms");
    return true;
}

// ------------------------------------------
//  PIXEL DICE MANAGER
// ------------------------------------------

PixelDice *PixelDiceManager::findDice(uint32_t pixelId)
{
    for (int i = 0; i < diceList.size(); i++)
    {
        if (diceList[i].pixelId == pixelId)
            return &diceList[i];
    }

    Serial.println(">>> CREATE NEW DICE <<<");
    PixelDice dice;
    dice.pixelId = pixelId;
    dice.client = BLEDevice::createClient();
    diceList.insert(diceList.begin(), dice);
    return &diceList.front();
}

void PixelDiceManager::unshiftDice(PixelDice *dice)
{
    for (int i = 0; i < diceList.size(); i++)
    {
        if (dice->pixelId != diceList[i].pixelId)
            continue;
        dice->timestamp = millis();
        PixelDice temp = *dice;
        diceList.erase(diceList.begin() + i);
        diceList.insert(diceList.begin(), temp);
    }
}

long PixelDiceManager::getTimestamp(uint8_t index)
{
    if (index < diceList.size())
    {
        PixelDice *dice = &diceList[index];
        return dice->timestamp;
    }
    return 0;
}

void PixelDiceManager::extractPixelsData(BLEAdvertisedDevice advertisedDevice)
{

    if (!advertisedDevice.haveServiceUUID())
    {
        return;
    }

    for (size_t i = 0; i < advertisedDevice.getServiceUUIDCount(); i++)
    {
        BLEUUID service = advertisedDevice.getServiceUUID(i);
        if (service.toString() != serviceUUID)
        {
            continue;
        }
        if (!advertisedDevice.haveServiceData())
        {
            Serial.print("No Service Data ");
            continue;
        }

        std::__cxx11::string rawData = advertisedDevice.getServiceData();
        uint32_t pixelId = *(uint32_t *)&rawData[0];
        std::string name = advertisedDevice.getName();
        std::string addr = advertisedDevice.getAddress().toString();

        PixelDice *dice = findDice(pixelId);
        delay(500); // seems dice override their position in the list
        // Serial.print("Find Dice: "); debugDice(dice);

        dice->pixelId = pixelId;
        dice->firmwareDate = *(uint32_t *)&rawData[4];
        dice->device = advertisedDevice;
        dice->name = name;
        dice->addr = addr;

        if (advertisedDevice.haveManufacturerData())
        {
            uint8_t *pointer = (uint8_t *)advertisedDevice.getManufacturerData().data();
            std::__cxx11::string rawData = advertisedDevice.getManufacturerData();
            dice->ledCount = pointer[2];
            dice->designColor = pointer[3];
            dice->batteryLevel = pointer[6] & 0x7f;
            dice->batteryCharge = (pointer[6] & 0x80) > 0;

            uint8_t rollState = pointer[4];
            if (rollState == 1)
            { // On Face
                dice->rollState = rollState;
                uint8_t currentFace = pointer[5];
                if (currentFace != dice->currentFace)
                {
                    dice->currentFace = currentFace;
                    // unshiftDice(dice);
                }
            }
        }
    }
}

void startScan(void *parameter)
{
    BLEScan *pScan = static_cast<BLEScan *>(parameter);
    while (true)
    {
        Serial.println(">>> SCAN START <<<");
        pScan->start(10);
        pScan->clearResults();
        Serial.println(">>> SCAN END <<<");
        PixelDiceManager::getInstance().debugDiceList();
        delay(2000);
    }
}

void PixelDiceManager::setup()
{
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan(); // create new scan
    pBLEScan->setAdvertisedDeviceCallbacks(new PixelAdvertiseCallbacks());
    pBLEScan->setActiveScan(true); // active scan uses more power, but get results faster
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99); // less or equal setInterval value
    xTaskCreate(startScan, "ScanTask", 10000, pBLEScan, 1, NULL);
}

void PixelDiceManager::debugDiceList()
{
    for (int i = 0; i < diceList.size(); i++)
    {
        PixelDice *dice = &diceList[i];
        debugDice(dice);
    }
}

void PixelDiceManager::debugDice(PixelDice *dice)
{
    Serial.print("PixelDice : ");
    Serial.print(dice->pixelId);
    Serial.print(" | ");
    Serial.print(dice->name.c_str());
    Serial.print(" | ");
    Serial.print(dice->addr.c_str());
    Serial.print(" | ");
    Serial.print(dice->currentFace + 1);
    Serial.print(" | ");
    Serial.print(dice->client->isConnected() ? "CONNECTED" : "DISCONNECTED");
    Serial.println();
}

void PixelDiceManager::loop()
{
    uint8_t t0 = getTimestamp();
    for (int i = 0; i < diceList.size(); i++)
    {
        PixelDice *dice = &diceList[i];
        dice->connect();
        // dice->disconnect();
        // if (dice->currentFace == 9){ dice->blinkDice(); }
    }
    uint8_t t1 = getTimestamp();
    if (t1 > t0)
    {
        Serial.println("SCAN UPDATE");
    }
    // Serial.print("LOOP");
    // delay(10000);
}

PixelDiceManager &PixelDiceManager::getInstance()
{
    static PixelDiceManager instance; // Crée une seule instance
    return instance;
}
