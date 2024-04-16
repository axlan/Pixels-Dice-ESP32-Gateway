#!/usr/bin/env python
import json
import time

# Dependency installed with `pip install paho-mqtt`.
# https://pypi.org/project/paho-mqtt/
import paho.mqtt.client as mqtt

MQTT_SERVER_ADDR = 'broker.hivemq.com'
# SET THE TOPIC TO USE!
MQTT_TOPIC = '/76B865E4/dice_rolls'

state = {}

csv_fd = open('log_file.csv', 'a')

# Define MQTT callbacks
def on_connect(client, userdata, connect_flags, reason_code, properties):
    print("Connected with result code "+str(reason_code))
    state['start_time'] = None
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    json_str = msg.payload.decode('ascii')
    msg_data = json.loads(json_str)
    # Convert the relative timestamps reported to the dice to an approximate absolute time.
    # The "last_time" check is to detect if the ESP32 was restarted or the counter rolled over.
    if state['start_time'] is None or msg_data['time'] < state['last_time']:
        state['start_time'] = time.time() - (msg_data['time'] / 1000.)
    state['last_time'] = msg_data['time']
    timestamp = state['start_time'] + (msg_data['time'] / 1000.)
    csv_fd.write(f"{timestamp:.3f}, {msg_data['name']}, {msg_data['state']}, {msg_data['val']}\n")
    csv_fd.flush()
    if msg_data['state'] == 1:
        print(f"{timestamp:.3f}: {msg_data['name']} rolled {msg_data['val']}")

# Create an MQTT client
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

# Set MQTT callbacks
client.on_connect = on_connect
client.on_message = on_message

# Connect to the MQTT broker
client.connect(MQTT_SERVER_ADDR, 1883, 60)

while client.loop(timeout=1.0) == mqtt.MQTT_ERR_SUCCESS:
    time.sleep(.1)

print('Connection Failure')
