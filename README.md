# Minimal BLE Sensors

Very minimal code to receive data from BLE sensors, specifically Estimote Telemetry packets.

## Requirements

Assumes Linux environment.

* Python
* C compiler
* libbluetooth (i.e. bluez) and development headers

## Build

```
sudo apt-get install libbluetooth-dev
cc -O2 -Wall recv-estimote.c -o recv-estimote -lbluetooth
```

You can give the binary appropriate capabilities so that running as root is not required.
```
sudo setcap 'cap_net_raw,cap_net_admin+eip' recv-estimote
```

## Usage

Binary only takes device name of host controller interface used.
```
./recv-estimote hci0
```

Outputs JSON descriptions of Estimote Telemetry packets received.
```javascript
{ "ts": 1501427152, "mac": "57800000b6eb", "id": "c3e8230000000e4e", "magnetometer": [0.000, 0.000, 0.000], "light_level": 0.00, "temperature": 23.25, "uptime": "645h", "battery_voltage": 3019, "battery_level": 99, "clock_error": false, "firmware_error": false }
```
