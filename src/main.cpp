#include <ArduinoOTA.h>

#include <WiFiManager.h>           // https://github.com/tzapu/WiFiManager
#include <MQTT.h>                  // https://github.com/256dpi/arduino-mqtt
#include <pixels_dice_interface.h> // https://github.com/axlan/arduino-pixels-dice

#include "WifiManagerParamHelper.h"

WiFiManager wm;
static constexpr const char *AP_NAME = "DiceGatewayAP";

// Entries for the "Setup" page on the web portal.
WifiManagerParamHelper wm_helper(wm);
constexpr const char *SETTING_DEVICE_NAME = "device_name";
constexpr const char *SETTING_MQTT_TOPIC = "mqtt_topic";
constexpr const char *SETTING_MQTT_SERVER = "mqtt_server";
constexpr const char *SETTING_MQTT_PORT = "mqtt_port";
constexpr const char *SETTING_MQTT_USERNAME = "mqtt_username";
constexpr const char *SETTING_MQTT_PASSWORD = "mqtt_password";
constexpr const char *SETTING_MAX_DICE = "max_dice";
// Generate the default topic based on the device EFuse MAC.
char default_topic[64];
std::array<ParamEntry, 7> PARAMS = {
    ParamEntry(SETTING_DEVICE_NAME, "dice_gateway"),
    ParamEntry(SETTING_MQTT_TOPIC, default_topic),
    ParamEntry(SETTING_MQTT_SERVER, "broker.hivemq.com"),
    ParamEntry(SETTING_MQTT_PORT, "1883"),
    ParamEntry(SETTING_MQTT_USERNAME, "public"),
    ParamEntry(SETTING_MQTT_PASSWORD, "public"),
    ParamEntry(SETTING_MAX_DICE, "8"),
};

static constexpr size_t BLE_SCAN_DURATION_SEC = 4;
static constexpr size_t BLE_TIME_BETWEEN_SCANS_SEC = 5;

MQTTClient client;
WiFiClient net;
long long next_reconnect = 0;

// The vectors to hold results queried from the library
// Since vectors allocate data, it's more efficient to keep reusing objects
// instead of declaring them on the stack
std::vector<pixels::PixelsDieID> dice_list;
pixels::RollUpdates roll_updates;
pixels::BatteryUpdates battery_updates;
bool scanning = true;

bool ReconnectMQTT()
{
  const char *device_name = wm_helper.GetSettingValue(SETTING_DEVICE_NAME);
  const char *mqtt_server = wm_helper.GetSettingValue(SETTING_MQTT_SERVER);
  int mqtt_port = 0;
  WifiManagerParamHelper::str2int(wm_helper.GetSettingValue(SETTING_MQTT_PORT), &mqtt_port);
  const char *mqtt_username = wm_helper.GetSettingValue(SETTING_MQTT_USERNAME);
  const char *mqtt_password = wm_helper.GetSettingValue(SETTING_MQTT_PASSWORD);

  if (next_reconnect > millis() || !WiFi.isConnected() ||
      strlen(device_name) == 0 ||
      strlen(mqtt_server) == 0)
  {
    return false;
  }

  if (mqtt_port == 0)
  {
    Serial.printf("Invalid port specified \"%s\".\n", wm_helper.GetSettingValue(SETTING_MQTT_PORT));
  }

  client.setHost(mqtt_server, mqtt_port);
  Serial.print("Attempting MQTT connection...");
  // Attempt to connect
  if (client.connect(device_name, mqtt_username, mqtt_password))
  {
    Serial.println("connected");
    return true;
  }
  else
  {
    Serial.print("failed, rc=");
    Serial.print(client.returnCode());
    Serial.println(" try again in 5 seconds");
    // Wait 5 seconds before retrying
    next_reconnect = millis() + 5000;
  }

  return false;
}

void setup()
{
  Serial.begin(115200);
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
  ArduinoOTA.begin();

  // Set the default topic based on the "random" device MAC.
  sprintf(default_topic, "/%X/dice_rolls", ESP.getEfuseMac());
  wm_helper.Init(0xBEEF, PARAMS.data(), PARAMS.size());
  // Configure the WiFiManager
  wm.setConfigPortalBlocking(false);
  wm.setParamsPage(true);
  wm.setTitle("Pixels Dice Gateway");

  // Automatically connect using saved credentials if they exist
  // If connection fails it starts an access point with the specified name
  if (wm.autoConnect(AP_NAME))
  {
    Serial.println("Auto connect succeeded!");
  }
  else
  {
    Serial.println("Config portal running");
  }

  // Start a background task scanning for dice.
  // On completion the discovered dice are connected to.
  pixels::ScanForDice(BLE_SCAN_DURATION_SEC, BLE_TIME_BETWEEN_SCANS_SEC);

  client.begin(net);
}

void loop()
{
  // Update dice_list with the connected dice
  pixels::ListDice(dice_list);
  // Get all the roll/battery updates since the last loop
  pixels::GetDieRollUpdates(roll_updates);
  pixels::GetDieBatteryUpdates(battery_updates);

  for (const auto &roll : roll_updates)
  {
    Serial.printf("Roll %s: %s value: %u\n",
                  pixels::GetDieDescription(roll.first).name.c_str(),
                  pixels::ToString(roll.second.state),
                  roll.second.current_face + 1);
  }

  // Only perform discovery scans if max number of dice hasn't been reached.
  int max_dice = 0;
  WifiManagerParamHelper::str2int(wm_helper.GetSettingValue(SETTING_MAX_DICE), &max_dice);
  if (max_dice != 0 && dice_list.size() >= max_dice)
  {
    if (scanning)
    {
      pixels::StopScanning();
      scanning = false;
      Serial.println("Max dice reached. Stopping discovery.");
    }
  }
  else
  {
    if (!scanning)
    {
      pixels::ScanForDice(BLE_SCAN_DURATION_SEC, BLE_TIME_BETWEEN_SCANS_SEC);
      scanning = true;
      Serial.println("Restarting discovery.");
    }
  }

  // Manage configuration web portal.
  // Normally, it shuts down once the device is connected, but we want it to always be running.
  if (!wm.getWebPortalActive() && WiFi.isConnected())
  {
    Serial.println("Forcing web portal to start.");
    wm.startWebPortal();
  }
  wm.process();

  // Manage MQTT
  if (!client.connected())
  {
    ReconnectMQTT();
  }
  else
  {
    const char *mqtt_topic = wm_helper.GetSettingValue(SETTING_MQTT_TOPIC);
    bool new_events = false;
    for (const auto &roll : roll_updates)
    {
      new_events = true;
      char json_buffer[128];
      sprintf(json_buffer,
              "{\"name\":\"%s\",\"state\":%d,\"val\":%d,\"time\":%d}",
              pixels::GetDieDescription(roll.first).name.c_str(),
              int(roll.second.state),
              roll.second.current_face + 1,
              roll.second.timestamp);
      client.publish(mqtt_topic, json_buffer);
    }
    if (new_events)
    {
      Serial.printf("Publishing to %s\n", mqtt_topic);
    }
  }
  client.loop();

  ArduinoOTA.handle();
}
