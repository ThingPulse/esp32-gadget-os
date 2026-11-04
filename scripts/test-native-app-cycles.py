#!/usr/bin/env python3
"""Test log acceptance using explicitly synthetic data, never hardware results."""
import pathlib, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
def fixture(leak=False, lvgl_leak=False):
    lines=[]
    for i in range(1,101):
        value=100000-(i*100 if leak else 0)
        fields=' '.join(f'{k}={value}' for k in ['internal','largest','exec','exec_largest','psram','psram_largest'])
        fields += f' lvgl_free={64000-i*100 if lvgl_leak else 64000} lvgl_largest={64000-i*100 if lvgl_leak else 64000}'
        lines.extend(['[NATIVE app] reset button received','[NATIVE app] close button received'])
        lines.append(f'[NATIVE] cycle={i} phase=launcher-restored {fields} callbacks=8')
    return '\n'.join(lines)+'\n[NATIVE] CYCLES COMPLETE\n'
with tempfile.TemporaryDirectory() as directory:
    log=pathlib.Path(directory)/'synthetic.log'
    for text,success in [(fixture(),True),('rst:0x15 boot\n'+fixture(),True),(fixture(True),False),(fixture(lvgl_leak=True),False),(fixture().replace('CYCLES COMPLETE',''),False),(fixture()+'rst:0x1\n',False),(fixture().replace('callbacks=8','callbacks=0'),False),(fixture().replace('reset button received','missing reset'),False),(fixture()+'[NATIVE] ERROR failed\n',False)]:
        log.write_text(text)
        r=subprocess.run(['python3',str(ROOT/'experiments/native-app/cycles.py'),'--analyse','--log',str(log)],capture_output=True,text=True)
        assert (r.returncode==0)==success,(r.stdout,r.stderr)
print('PASS: synthetic cycle analyser fixtures (stable/leaking/incomplete/reset/no-callback/error)')
