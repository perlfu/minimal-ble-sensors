/* cc -O2 -Wall recv-sensors.c -o recv-sensors -lbluetooth */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>


static int decode_motion_ts(uint8_t byte, char *unit) {
    int number = byte & 0x3f;
    int _unit = byte >> 6;
    if (_unit == 3 && number >= 32) {
        // weeks
        number -= 32;
        *unit = 'w';
    } else if (_unit == 3) {
        // days
        *unit = 'd';
    } else if (_unit == 2) {
        // hours
        *unit = 'h';
    } else if (_unit == 1) {
        // minutes
        *unit = 'm';
    } else {
        // seconds
        *unit = 's';
    }
    return number;
}

static void process_estimote(time_t ts, bdaddr_t addr, uint8_t flags, uint8_t *data, int len) {
    uint8_t header = data[0];
    uint8_t protocol = header >> 4;
    uint8_t frame_type = header & 0xf;

    // only interested in telemetry packets of 20 bytes
    if (len == 20) {
        uint8_t sub_frame_type = data[9];
        char name[32], mac[16];

        snprintf(mac, sizeof(mac), "%02x%02x%02x%02x%02x%02x",
            addr.b[0], addr.b[1], addr.b[2], addr.b[3], addr.b[4], addr.b[5]);

        snprintf(name, sizeof(name), "%02x%02x%02x%02x%02x%02x%02x%02x",
            data[1], data[2], data[3], data[4],
            data[5], data[6], data[7], data[8]);

        if (frame_type == 0x2 && sub_frame_type == 0x0) {
            // telemetry 0
            float accel_x = (((float)((int8_t)data[10])) * 2.0) / 127.0;
            float accel_y = (((float)((int8_t)data[11])) * 2.0) / 127.0;
            float accel_z = (((float)((int8_t)data[12])) * 2.0) / 127.0;
            float pressure = 0.0;

            char motion_p_unit;
            int motion_p = decode_motion_ts(data[13], &motion_p_unit);
            char motion_c_unit;
            int motion_c = decode_motion_ts(data[14], &motion_c_unit);
            int in_motion = (data[15] & 0x3) == 1;

            int gpio0 = (data[15] >> 4) & 0x1;
            int gpio1 = (data[15] >> 5) & 0x1;
            int gpio2 = (data[15] >> 6) & 0x1;
            int gpio3 = (data[15] >> 7) & 0x1;

            int fw_error = 0;
            int clk_error = 0;

            if (protocol == 2) {
                fw_error = (data[15] >> 2) & 0x1;
                clk_error = (data[15] >> 3) & 0x1;
            } else if (protocol == 1) {
                fw_error = (data[16] >> 2) & 0x1;
                clk_error = (data[16] >> 3) & 0x1;
            }

            if (protocol == 2) {
                uint32_t buf;
                memcpy(&buf, data + 16, sizeof(buf));
                if (buf != 0xffffffff)
                    pressure = ((float)btohl(buf)) / 256.0;
            }

            fprintf(stdout, "{ "
                    "\"ts\": %llu, "
                    "\"mac\": \"%s\", "
                    "\"id\": \"%s\", "
                    "\"accelerometer\": [%.3f, %.3f, %.3f], "
                    "\"in_motion\": %s, "
                    "\"motion_duration\": { \"previous\": \"%d%c\", \"current\": \"%d%c\" }, "
                    "\"gpio\": [%d,%d,%d,%d], "
                    "\"clock_error\": %s, "
                    "\"firmware_error\": %s, "
                    "\"pressure\": %.2f "
                "}\n",
                (long long unsigned) ts,
                mac, name,
                accel_x, accel_y, accel_z,
                in_motion ? "true" : "false",
                motion_p, motion_p_unit,
                motion_c, motion_c_unit,
                gpio0, gpio1, gpio2, gpio3,
                clk_error ? "true" : "false",
                fw_error ? "true" : "false",
                pressure);

        } else if (frame_type == 0x2 && sub_frame_type == 0x1) {
            // telemetry 1
            float mag_x = (((float)((int8_t)data[10])) * 2.0) / 128.0;
            float mag_y = (((float)((int8_t)data[11])) * 2.0) / 128.0;
            float mag_z = (((float)((int8_t)data[12])) * 2.0) / 128.0;

            int light_u = data[13] >> 4;
            int light_l = data[13] & 0xf;
            float lux = data[13] != 0xff ? ((float)(1 << light_u)) * (float)(light_l) * 0.72f : 0.0;

            int temp = (data[15] & 0xc0) >> 6 | (data[16] << 2) | ((data[17] & 0x3) << 10);
            int temp_s = temp > 2047 ? temp - 4096 : temp;
            float celsius = temp_s / 16.0;

            int battery_v = (data[18] << 6) | (data[17] >> 2);
            int battery_l = -1;

            int fw_error = 0;
            int clk_error = 0;

            int _uptime_unit = (data[15] >> 4) & 0x3;
            char uptime_unit = ' ';
            int uptime_val = ((data[15] & 0xf) << 8) | data[14];

            // uptime
            switch (_uptime_unit) {
                case 0: uptime_unit = 's'; break;
                case 1: uptime_unit = 'm'; break;
                case 2: uptime_unit = 'h'; break;
                case 3: uptime_unit = 'd'; break;
            }

            // battery voltage
            if (battery_v == 0x3fff)
                battery_v = -1;

            // errors & battery level
            if (protocol == 0) {
                fw_error = (data[15] >> 0) & 0x1;
                clk_error = (data[15] >> 1) & 0x1;
            } else if (protocol >= 1) {
                if (data[19] != 0xff) {
                    battery_l = data[19];
                }
            }

            // cancel invalid magnometer readings
            if (data[10] == 0xff && data[11] == 0xff && data[12] == 0xff) {
                mag_x = mag_y = mag_z = 0.0;
            }

            fprintf(stdout, "{ "
                    "\"ts\": %llu, "
                    "\"mac\": \"%s\", "
                    "\"id\": \"%s\", "
                    "\"magnetometer\": [%.3f, %.3f, %.3f], "
                    "\"light_level\": %.2f, "
                    "\"temperature\": %.2f, "
                    "\"uptime\": \"%d%c\", "
                    "\"battery_voltage\": %d, "
                    "\"battery_level\": %d, "
                    "\"clock_error\": %s, "
                    "\"firmware_error\": %s "
                "}\n",
                (long long unsigned)ts,
                mac, name,
                mag_x, mag_y, mag_z,
                lux,
                celsius,
                uptime_val, uptime_unit,
                battery_v, battery_l,
                clk_error ? "true" : "false",
                fw_error ? "true" : "false");
        }
    }
}

static void process_ruuvitag(time_t ts, bdaddr_t addr, uint8_t flags, uint8_t *data, int len) {
    uint8_t header = data[0];

    // only interested in full format 5 (RAWv2) packets
    if (header == 0x05 && len == 24) {
        char name[16], mac[16];
        int16_t temp_raw = (data[1] << 8) | data[2];
        float temperature = ((float)temp_raw) * 0.005f;
        uint16_t humidity_raw = (data[3] << 8) | data[4];
        float humidity = ((float)humidity_raw) * 0.0025f;
        uint16_t pressure_raw = (data[5] << 8) | data[6];
        unsigned int pressure = 50000 + pressure_raw;
        int16_t accel_x_raw = (data[7] << 8) | data[8];
        float accel_x = ((float)accel_x_raw) / 1000.0f;
        int16_t accel_y_raw = (data[9] << 8) | data[10];
        float accel_y = ((float)accel_y_raw) / 1000.0f;
        int16_t accel_z_raw = (data[11] << 8) | data[12];
        float accel_z = ((float)accel_z_raw) / 1000.0f;
        uint16_t voltage_raw = (data[13] << 3) | (data[14] >> 5);
        float voltage = 1.6f + (((float)voltage_raw) / 1000.0);
        int tx_power = (-40) + ((int)(data[14] & 0x1f)) * 2;
        uint8_t movement_counter = data[15];
        uint16_t sequence = (data[16] << 8) | data[17];

        snprintf(mac, sizeof(mac), "%02x%02x%02x%02x%02x%02x",
            addr.b[0], addr.b[1], addr.b[2], addr.b[3], addr.b[4], addr.b[5]);

        snprintf(name, sizeof(name), "%02x%02x%02x%02x%02x%02x",
            data[18], data[19], data[20], data[21], data[22], data[23]);

        fprintf(stdout, "{ "
                "\"ts\": %llu, "
                "\"mac\": \"%s\", "
                "\"id\": \"%s\", "
                "\"temperature\": %.2f, "
                "\"humidity\": %.2f, "
                "\"pressure\": %u, "
                "\"accelerometer\": [%.3f, %.3f, %.3f], "
                "\"battery_voltage\": %.4f, "
                "\"tx_power\": %d, "
                "\"movement_counter\": %u, "
                "\"sequence\": %u "
            "}\n",
            (long long unsigned)ts,
            mac, name,
            temperature,
            humidity,
            pressure,
            accel_x, accel_y, accel_z,
            voltage,
            tx_power,
            movement_counter,
            sequence);
    }
}

static void decode_gap(time_t ts, bdaddr_t addr, uint8_t *data, const int len) {
    uint16_t uuid = 0, s_uuid = 0;
    uint8_t *s_data = NULL;
    uint8_t flags = 0;
    int s_data_len = 0;

    int ptr = 0;
    while (ptr < (len - 2)) {
        int ulen = data[ptr + 0];
        int dtype = data[ptr + 1];

        if ((ptr + ulen) > len)
            ulen = len - ptr;

        ptr += 2; // move past ulen and type
        ulen -= 1; // remove type from data

        switch (dtype) {
            case 0x01: // flags
                if (ulen >= 1)
                    flags = data[ptr];
                break;
            case 0x02: case 0x03: // 16-bit uuids
                // ignoring that there may be multiple
                if (ulen >= 2) {
                    uint16_t buf;
                    memcpy(&buf, data + ptr, sizeof(buf));
                    uuid = btohs(buf);
                }
                break;
            case 0x16: // service data
            case 0xff: // manufacturer specific data
                if (ulen >= 2) {
                    uint16_t buf;
                    memcpy(&buf, data + ptr, sizeof(buf));
                    s_uuid = btohs(buf);
                }
                s_data = data + ptr + 2;
                s_data_len = ulen - 2;
                break;
            default:
                // other
                break;
        }
        ptr += ulen;
    }

    #if 0
    fprintf(stderr, "uuid = %04x, flags = %02x, service uuid = %04x, service data = %d bytes\n",
                        uuid, flags, s_uuid, s_data_len);
    #endif
    if (uuid == 0xfe9a && s_uuid == 0xfe9a && s_data_len > 0) {
        process_estimote(ts, addr, flags, s_data, s_data_len);
    } else if (uuid == 0x0000 && s_uuid == 0x0499 && s_data_len > 0) {
        process_ruuvitag(ts, addr, flags, s_data, s_data_len);
    }
}

static void check_exit(int val, const char *msg) {
    if (val < 0) {
        fprintf(stderr, "error: %s (%d), errno = %s (%d)\n", msg, val, strerror(errno), errno);
        exit(1);
    }
}

volatile int do_shutdown = 0;

static void shutdown_signal(int sig) {
    do_shutdown = 1;
}

int main(int argc, char *argv[]) {
    struct hci_filter new_options, old_options;
    socklen_t slen = sizeof(old_options);

    int last_ts = 0;
    int cmd_timeout = 1000; // for commands sent to HCI
    uint8_t scan_type = 0x00; // passive type
    uint16_t interval = htobs(0x0010);
    uint16_t window = htobs(0x0010);
    uint8_t own_type = 0x00;
    uint8_t filter_policy = 0x00;
    char *dev_name = "hci0";
    int result;
    int attempt;
    int dev_id;
    int dev;

    signal(SIGINT, shutdown_signal);

    if (argc > 1)
        dev_name = argv[1];

    fprintf(stderr, "using device %s\n", dev_name);

    // get device number
    dev_id  = hci_devid(dev_name);
    check_exit(dev_id, "unable to find device");

    // open device handle
    dev     = hci_open_dev(dev_id);
    check_exit(dev, "unable open device");

    // try to set BLE parameters
    for (attempt = 1; attempt >= 0; attempt--) {
        result  = hci_le_set_scan_parameters(dev,
            scan_type, interval, window,
            own_type, filter_policy, cmd_timeout);

        // try to disable old scan; if parameter set failed
        if (result < 0) {
            result = hci_le_set_scan_enable(dev, 0x00, 1, cmd_timeout);
            check_exit(result, "unable to terminate old scan");
        }
    }
    check_exit(result, "unable to set scan parameters");

    // enable BLE scan
    result = hci_le_set_scan_enable(dev, 0x01, 1, cmd_timeout);
    check_exit(result, "unable to start scan");

    // store socket options
    result = getsockopt(dev, SOL_HCI, HCI_FILTER, &old_options, &slen);
    check_exit(result, "unable to get socket options");

    // configure socket to get relevant events
    hci_filter_clear(&new_options);
    hci_filter_set_ptype(HCI_EVENT_PKT, &new_options);
    hci_filter_set_event(EVT_LE_META_EVENT, &new_options);
    result = setsockopt(dev, SOL_HCI, HCI_FILTER, &new_options, sizeof(new_options));
    check_exit(result, "unable to set socket options");

    // main loop
    while (!do_shutdown) {
        unsigned char buffer[HCI_MAX_EVENT_SIZE];
        struct timeval wait;
        fd_set read_set;
        int len;

        // wait 1 second for a packet
        memset(&wait, 0, sizeof(wait));
        wait.tv_sec = 1;

        FD_ZERO(&read_set);
        FD_SET(dev, &read_set);

        result = select(FD_SETSIZE, &read_set, NULL, NULL, &wait);
        if (result < 0) {
            fprintf(stderr, "error: select returned %d\n", result);
            break;
        }

        // if there was a packet then read and process it
        if (result) {
            int ts = time(NULL);

            // read event packet
            len = read(dev, buffer, sizeof(buffer));
            if (len > HCI_EVENT_HDR_SIZE) {
                //hci_event_hdr *hdr = (hci_event_hdr *)(buffer + 1);
                uint8_t *ptr = buffer + HCI_EVENT_HDR_SIZE + 1;
                evt_le_meta_event *meta = (evt_le_meta_event *) ptr;

                len -= (1 + HCI_EVENT_HDR_SIZE);

                // hand off suitable packets for decoding
                if (meta->subevent == EVT_LE_ADVERTISING_REPORT) {
                    le_advertising_info *info = (le_advertising_info *) (meta->data + 1);
                    decode_gap(ts, info->bdaddr, info->data, len);
                }
            }

            // store the current timestamp
            last_ts = ts;
            // flush any output
            fflush(stdout);
        } else {
            // no packets for 60 seconds? then something is probably broken
            int since_last_packet = time(NULL) - last_ts;
            if (since_last_packet > 60) {
                fprintf(stderr, "error: packets seem to have stopped...\n");
                break;
            }
        }
    }

    // restore old socket options
    setsockopt(dev, SOL_HCI, HCI_FILTER, &old_options, sizeof(old_options));
    //check_exit(result, "unable to restore socket options");

    // disable scanning
    result = hci_le_set_scan_enable(dev, 0x00, 1, cmd_timeout);
    check_exit(result, "unable to stop scan");

    return 0;
}
