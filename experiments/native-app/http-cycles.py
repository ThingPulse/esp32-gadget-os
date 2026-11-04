#!/usr/bin/env python3
"""Exercise real ISS HTTPS, button closure during a request, and reopening.

Requires the experimental host and both packages already installed. Logs only
native experiment records, not Wi-Fi credentials or general boot output.
"""
import argparse, pathlib, re, time
import serial

p=argparse.ArgumentParser()
p.add_argument('--port',required=True)
p.add_argument('--log',type=pathlib.Path,required=True)
p.add_argument('--cycles',type=int,default=3)
p.add_argument('--hold-until',type=pathlib.Path,help='Leave ISS open for manual checks until this file exists (max 10 minutes)')
a=p.parse_args()
s=serial.Serial(port=None,baudrate=115200,timeout=0.2)
s.dtr=False; s.rts=False; s.port=a.port; s.open()
lines=[]
with a.log.open('w') as log:
    def read():
        line=s.readline().decode('utf-8',errors='replace').strip()
        if '[NATIVE' in line:
            lines.append(line)
            if 'phase=activity' not in line: print(line,flush=True)
            log.write(line+'\n'); log.flush()
        if any(x in line for x in ['Guru Meditation','assert failed','CORRUPT HEAP','Brownout']):
            raise RuntimeError('Device failure: '+line)
    def drain(seconds):
        end=time.monotonic()+seconds
        while time.monotonic()<end: read()
    def send(command):
        mark=len(lines); s.write((command+'\n').encode()); s.flush(); return mark
    def wait(text,mark,seconds=45):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            for line in lines[mark:]:
                if text in line: return line
                if '[NATIVE] ERROR' in line: raise RuntimeError(line)
            read()
        raise RuntimeError('Timeout waiting for '+text)
    drain(10)
    for i in range(a.cycles):
        mark=send('native iss'); wait('ISS position updated',mark)
        drain(5.5)
        mark=send('native iss refresh'); request=wait('phase=request',mark)
        token=int(re.search(r'generation=(\d+)',request)[1])
        closed=send('native iss close'); wait('phase=launcher-restored',closed)
        reopened=send('native iss'); wait('phase=app-ui-created',reopened)
        completion=wait(f'generation={token} phase=complete',mark)
        assert 'delivered=0' in completion, 'Response arrived before close; repeat cancellation test'
        drain(5.5)
        mark=send('native iss refresh'); wait('ISS position updated',mark)
        mark=send('native iss close'); wait('phase=launcher-restored',mark)
        print(f'PASS HTTP cycle {i+1}: success, close pending, discard, reopen, refresh, close',flush=True)
        drain(5.5)
    if a.hold_until:
        mark=send('native iss'); wait('ISS position updated',mark)
        print('READY for manual launcher/icon/Refresh/Close/Calculator checks',flush=True)
        end=time.monotonic()+600
        while not a.hold_until.exists() and time.monotonic()<end:
            try: read()
            except serial.SerialException:
                print('Device disconnected during manual hold; automated checks already passed.',flush=True)
                break
s.close()
