#!/usr/bin/env python3
"""Run the firmware's 100-cycle test or analyse a captured serial log."""
import argparse, pathlib, re, statistics, time
p=argparse.ArgumentParser()
p.add_argument('--port', help='Device must already be booted at its launcher')
p.add_argument('--log', type=pathlib.Path, required=True)
p.add_argument('--analyse', action='store_true')
p.add_argument('--tolerance', type=int, default=1024, help='Allowed drop in median free bytes after warm-up')
a=p.parse_args()
if not a.analyse:
    if not a.port: p.error('--port is required unless --analyse')
    import serial
    # Opening the serial port must not intentionally reset the device.
    connection=serial.Serial(port=None,baudrate=115200,timeout=1)
    connection.dtr=False; connection.rts=False; connection.port=a.port; connection.open()
    connection.reset_input_buffer()
    connection.write(b'\nnative test 100\n'); connection.flush()
    deadline=time.monotonic()+360
    with a.log.open('w') as output:
        while time.monotonic()<deadline:
            line=connection.readline().decode('utf-8',errors='replace')
            output.write(line); output.flush()
            if '[NATIVE]' in line: print(line,end='',flush=True)
            if 'CYCLES COMPLETE' in line or '[NATIVE] ERROR' in line or 'Rebooting' in line: break
    connection.close()
text=a.log.read_text()
if '[NATIVE] ERROR' in text: raise SystemExit('FAIL: loader reported an error')
# Boot output may still be buffered when the port opens. Only a reset after
# the first cycle starts counts as a reset during this test.
start=text.find('[NATIVE] cycle=')
run_text=text[start:] if start>=0 else text
if re.search(r'Guru Meditation|Brownout detector|rst:0x|Rebooting|assert failed',run_text):
    raise SystemExit('FAIL: crash/reset indication in capture')
rows=[]
for line in text.splitlines():
    if 'phase=launcher-restored' in line:
        rows.append({k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)})
if len(rows)!=100 or 'CYCLES COMPLETE' not in text:
    raise SystemExit(f'INCOMPLETE: {len(rows)} completed cycles; expected 100')
if len({r['cycle'] for r in rows})!=100: raise SystemExit('FAIL: duplicate cycle numbers')
for event in ['reset button received','close button received']:
    if text.count('[NATIVE app] '+event)!=100:
        raise SystemExit('FAIL: expected 100 app acknowledgements for '+event)
for row in rows:
    if row['callbacks']<7: raise SystemExit('FAIL: timer/button callbacks did not run')
rows=rows[5:]
failed=False
for key in ['internal','largest','exec','exec_largest','psram','psram_largest','lvgl_free','lvgl_largest']:
    if not all(key in r for r in rows): continue
    first=statistics.median(r[key] for r in rows[:10]); last=statistics.median(r[key] for r in rows[-10:])
    # Regression slope also catches a gradual decline that happens to be masked
    # by different first/last window noise. This remains a heuristic, not proof.
    mean_x=(len(rows)-1)/2; mean_y=statistics.mean(r[key] for r in rows)
    slope=sum((i-mean_x)*(r[key]-mean_y) for i,r in enumerate(rows))/sum((i-mean_x)**2 for i in range(len(rows)))
    print(f'{key}: first median={first}, last median={last}, drop={first-last}, slope={slope:.2f} bytes/cycle')
    if first-last>a.tolerance or -slope*(len(rows)-1)>a.tolerance: failed=True
if failed: raise SystemExit('REVIEW REQUIRED: continuing memory decline exceeds tolerance')
print('PASS: 100 captured cycles, no detected resets, no decline above tolerance after five warm-up cycles.')
print('Still verify touch interaction, visual correctness and a built-in app manually.')
