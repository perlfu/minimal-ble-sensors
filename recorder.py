#!/usr/bin/env python

import json
import threading
import os
import time
import subprocess
import sys


RECV_BIN = './recv-estimote'
HCI_DEV = 'hci1'
DATA_DIR = 'data'
POST_URL = None 


def decode_tm(tm):
    unit = tm[-1]
    val = int(tm[0:-1])
    if unit == 's':
        pass
    elif unit == 'm':
        val *= 60
    elif unit == 'h':
        val *= 60 * 60
    elif unit == 'd':
        val *= 60 * 60 * 24
    elif unit == 'w':
        val *= 60 * 60 * 24 * 7
    else:
        assert False, 'unknown unit ' + unit
    return val


def deduce_motion(vs):
    current = []
    previous = []
    for v in vs:
        current.append(decode_tm(v['current']))
        previous.append(decode_tm(v['previous']))

    minima = min(current)
    if minima != current[0]:
        motion = True
    else:
        motion = False

    return { 'motion': motion, 'last_change': min(current) }


def mean(vs):
    s = sum(vs)
    if len(vs) > 0:
        return float(s) / float(len(vs))
    else:
        return float('nan')


def output_worker(data):
    headers = { 'Content-Type': 'application/json' }
    try:
        import requests
        response = requests.post(POST_URL, headers=headers, data=data) 
    except Exception as e:
        print >>sys.stderr, 'post to {} failed: {}'.format(POST_URL, e)    


def output_data(data, ts):
    gm = time.gmtime(ts)
    day_file = '%04d%02d%02d.json' % (gm[0], gm[1], gm[2])
    json_data = json.dumps(data)
    
    if DATA_DIR:
        with open(os.path.join(DATA_DIR, day_file), 'a') as f:
            print >>f, json_data
    if POST_URL:
        th = threading.Thread(target=output_worker, args=(json_data, ts))
        th.start()


def main_loop(input_fh, interval=60):
    proc = {
        'temperature': mean, 
        'light_level': mean, 
        'pressure': mean, 
        'battery_level': min, 
        'battery_voltage': min,
        'in_motion': max,
        'motion_duration': deduce_motion,
        'uptime': lambda vs:min(map(decode_tm, vs))
    }

    samples = {}
    last_ts = 0
    for line in input_fh:
        ts = last_ts
        try:
            data = json.loads(line)
            
            ts = data['ts']
            if last_ts == 0:
                last_ts = ts 

            _id = data['id']
            if _id not in samples:
                samples[_id] = {}
            for key in data.keys():
                if key not in samples[_id]:
                    samples[_id][key] = []
                samples[_id][key].append(data[key])
        except Exception as e:
            print >>sys.stderr, e
        
        if (ts - last_ts) >= interval:
            output = []

            for _id in samples.keys():
                summary = { 'id': _id, 'ts': ts }
                for key in samples[_id].keys():
                    if key in proc:
                        summary[key] = proc[key](samples[_id][key])

                output.append(summary)

            samples = {}
            last_ts = ts
            output_data(output, ts)


def main(args):
    proc = subprocess.Popen([RECV_BIN, HCI_DEV], stdout=subprocess.PIPE)
    
    try:
        main_loop(proc.stdout)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            proc.terminate()
        except Exception as e:
            print >>sys.stderr, e
    
    proc.wait()


if __name__ == "__main__":
    main(sys.argv[1:])
    sys.exit(0)
