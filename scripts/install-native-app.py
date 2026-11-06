#!/usr/bin/env python3
"""Install a .gapp using USB pairing and the device's existing Wi-Fi installer."""
import argparse
import http.client
import ipaddress
import json
from pathlib import Path
import re
import struct
import sys
import time
from urllib.parse import urlsplit


def metadata(raw):
    if len(raw) < 52 or len(raw) > 70908 or raw[:8] not in (b'GOSAPP1\0', b'GOSAPP2\0'):
        raise ValueError('Not a supported .gapp package')
    offset = 124 if raw[:8] == b'GOSAPP2\0' else 52
    manifest_size, icon_size, elf_size = struct.unpack_from('<III', raw, 8)
    if not 0 < manifest_size <= 2048 or icon_size != 3200 or elf_size > 65536 or offset+manifest_size+icon_size+elf_size != len(raw):
        raise ValueError('Invalid package lengths')
    result = json.loads(raw[offset:offset+manifest_size])
    if not isinstance(result, dict) or not re.fullmatch('[a-z][a-z0-9-]{0,31}', result.get('id', '')):
        raise ValueError('Invalid package manifest')
    return result


def pairing(url):
    parsed = urlsplit(url)
    address = ipaddress.ip_address(parsed.hostname or '')
    if (parsed.scheme != 'http' or address.version != 4 or parsed.port != 8081 or
            parsed.username or parsed.password or parsed.path != '/' or parsed.query or
            not re.fullmatch('[0-9a-f]{32}', parsed.fragment)):
        raise ValueError('Unexpected device pairing URL')
    return str(address), parsed.fragment


def request(address, key, route, data=None):
    # Direct connection: no proxy, redirects or token in logs/command arguments.
    connection = http.client.HTTPConnection(address, 8081, timeout=30)
    try:
        connection.request('POST' if data is not None else 'GET', route, body=data,
                           headers={'X-App-Key': key, 'Content-Type': 'application/octet-stream'})
        response = connection.getresponse()
        body = response.read(16385)
        if len(body) > 16384:
            raise ValueError('Device response too large')
        try:
            result = json.loads(body)
        except ValueError:
            raise ValueError(f'Device returned HTTP {response.status} without valid JSON') from None
        if response.status != 200:
            message = result.get('error', 'Request failed') if isinstance(result, dict) else 'Request failed'
            raise ValueError(f'Device returned HTTP {response.status}: {message}')
        if not isinstance(result, dict):
            raise ValueError('Invalid device response')
        return result
    finally:
        connection.close()


def install(url, raw):
    app = metadata(raw)
    address, key = pairing(url)
    state = request(address, key, '/api/apps')
    if app.get('chip') != state.get('chip'):
        raise ValueError(f"Wrong package chip: device is {state.get('chip')}, package is {app.get('chip')}")
    if app.get('api_major') != state.get('apiMajor') or not isinstance(app.get('api_minor'), int) or app['api_minor'] > state.get('apiMinor', -1):
        raise ValueError('Update the device firmware first: this app needs a newer SDK')
    if raw[:8] == b'GOSAPP1\0' and not state.get('developerMode'):
        raise ValueError('Unsigned package: enable Developer mode on the device installer screen, then retry')
    request(address, key, '/api/install', raw)
    state = request(address, key, '/api/apps')
    if not any(x.get('id') == app['id'] and x.get('version') == app.get('version') for x in state.get('apps', [])):
        raise ValueError('Upload completed but the installed version could not be verified')
    print(f"Installed {app['id']} {app.get('version')} on {address}.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='USB serial port; close other serial monitors first')
    parser.add_argument('--package', required=True, type=Path)
    parser.add_argument('--timeout', type=float, default=40, help='Pairing wait in seconds')
    args = parser.parse_args()
    if not 10 <= args.timeout <= 120:
        parser.error('--timeout must be between 10 and 120 seconds')
    if args.package.stat().st_size > 70908:
        raise ValueError("Package exceeds the installer size limit")
    raw = args.package.read_bytes()
    metadata(raw)  # Fail before interacting with the device.
    import serial
    port = serial.Serial(port=None, baudrate=115200, timeout=0.25)
    port.dtr = False
    port.rts = False
    port.port = args.port
    with port:
        print('Waiting for USB; leave the device at the launcher and connected to Wi-Fi.')
        # Some USB/UART bridges reset on opening even with DTR/RTS disabled.
        deadline = time.monotonic()+8
        while time.monotonic() < deadline:
            port.read(4096)
        port.write(b'native manager close\n')
        port.flush()
        time.sleep(0.5)
        port.reset_input_buffer()
        port.write(b'native manager\n')
        port.flush()
        deadline = time.monotonic()+args.timeout
        url = None
        while time.monotonic() < deadline:
            line = port.read_until(b'\n', 4096).decode('utf-8', 'replace')
            match = re.search(r'\[NATIVE MANAGER\] url=(http://[^\s]+)', line)
            if match:
                url = match.group(1)
                break
        if not url:
            raise ValueError('No pairing response. Use app-enabled firmware, connect Wi-Fi, return to the launcher, and close other serial monitors')
        install(url, raw)
        port.write(b'native manager close\n')
        port.flush()
    print('Open Installed apps on the device to launch it.')

if __name__ == '__main__':
    try:
        main()
    except ImportError:
        sys.exit('Pyserial is required. Use .venv-build/bin/python or install pyserial.')
    except (OSError, ValueError, http.client.HTTPException) as error:
        sys.exit(str(error))
