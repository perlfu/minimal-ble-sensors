# Minimal BLE Sensors

Very minimal code to receive data from BLE sensors, specifically Estimote Telemetry packets.
Also contains support for RuuviTag.

## Requirements

Assumes Linux environment.

* Python
* C compiler
* libbluetooth (i.e. bluez) and development headers

## Build

```
sudo apt-get install libbluetooth-dev
cc -O2 -Wall recv-sensors.c -o recv-estimote -lbluetooth
```

You can give the binary appropriate capabilities so that running as root is not required.
```
sudo setcap 'cap_net_raw,cap_net_admin+eip' recv-sensors
```

## Usage

Executable only takes a single parameter, device name of host controller interface used.
```
./recv-sensors hci0
```

Outputs JSON descriptions of Bluetooth LE sensor data received.

Estimote Telemetry packets:
```javascript
{ "ts": 1501427152, "mac": "57800000b6eb", "id": "c3e8230000000e4e", "magnetometer": [0.000, 0.000, 0.000], "light_level": 0.00, "temperature": 23.25, "uptime": "645h", "battery_voltage": 3019, "battery_level": 99, "clock_error": false, "firmware_error": false }
```

RuuviTag packets:
```javascript
{ "ts": 1645250302, "mac": "000000b6eb00", "id": "00ebb6000000", "temperature": 18.98, "humidity": 30.49, "pressure": 102173, "accelerometer": [-0.108, 0.996, -0.008], "battery_voltage": 2.978, "tx_power": 4, "movement_counter": 246, "sequence": 21965 }
```

SwitchBot Motion Detector packets:
```javascript
{ "ts": 1645250305, "mac": "000000b6eb00", "id": "00ebb6000000", "motion": false, "motion_count": 140, "seconds_since": 5392 }
```

The script `recorder.py` aggregates and records sensor data to files, and/or copies real-time data to an MQTT topic.
This is done on a 60 second interval by default.
Edit the script and the constants to values appropriate for your setup.
```python
RECV_BIN = './recv-estimote'
HCI_DEV = 'hci0'
DATA_DIR = 'data'

MQTT_HOST = "localhost"
MQTT_PORT = 1883
MQTT_TOPIC = "ble-sensors/raw"
```
