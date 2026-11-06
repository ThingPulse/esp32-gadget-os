#!/usr/bin/env python3
"""Local usage bridge. Stores only allowance summaries, never provider credentials."""
import argparse
import hmac
import json
import math
import os
from pathlib import Path
import queue
import secrets
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def number(value, low, high):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and low <= value <= high


def window(raw, used_key, reset_key, minutes):
    if not isinstance(raw, dict):
        return None
    used, reset = raw.get(used_key), raw.get(reset_key)
    if not number(used, 0, 100) or not number(reset, 0, 4102444800) or not number(minutes, 1, 525600):
        return None
    return {'used': used, 'reset': int(reset), 'minutes': int(minutes)}


def codex_summary(result, now):
    if not isinstance(result, dict):
        raise ValueError('Invalid Codex response')
    buckets = result.get('rateLimitsByLimitId') or {}
    if not isinstance(buckets, dict):
        raise ValueError('Invalid Codex buckets')
    bucket = buckets.get('codex') if buckets else result.get('rateLimits')
    if not isinstance(bucket, dict):
        raise ValueError('Codex quota bucket unavailable')
    return {'observed': now, 'windows': [window(bucket.get(k), 'usedPercent', 'resetsAt',
            bucket[k].get('windowDurationMins') if isinstance(bucket.get(k), dict) else None) for k in ('primary', 'secondary')]}


def claude_summary(payload, now):
    if not isinstance(payload, dict):
        raise ValueError('Invalid status-line payload')
    limits = payload.get('rate_limits') or {}
    if not isinstance(limits, dict):
        raise ValueError('Invalid status-line limits')
    return {'observed': now, 'windows': [window(limits.get(k), 'used_percentage', 'resets_at', m)
            for k, m in [('five_hour', 300), ('seven_day', 10080)]]}


def atomic_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    temporary = path.with_name(path.name + '.' + secrets.token_hex(6) + '.tmp')
    try:
        with os.fdopen(os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as f:
            json.dump(data, f, allow_nan=False)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def read_json(path):
    try:
        if path.stat().st_size > 16384:
            return {}
        data = json.loads(path.read_text())
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def snapshot(state, providers, now=None):
    now = int(time.time()) if now is None else now
    result = {'schema': 1}
    for provider in ('codex', 'claude'):
        result[provider + '_enabled'] = int(provider in providers)
        data = read_json(state / (provider + '.json')) if provider in providers else {}
        observed = data.get('observed')
        result[provider + '_age'] = min(604800, int(now-observed)) if number(observed, 0, now) else -1
        windows = data.get('windows')
        for index, name in enumerate(('primary', 'secondary')):
            prefix = provider + '_' + name
            w = windows[index] if isinstance(windows, list) and index < len(windows) else None
            valid = window(w, 'used', 'reset', (w or {}).get('minutes')) if isinstance(w, dict) else None
            # Expired samples do not imply that the allowance is now full.
            if valid and valid['reset'] > now and result[provider + '_age'] >= 0:
                result[prefix + '_used'] = valid['used']
                result[prefix + '_minutes'] = valid['minutes']
                result[prefix + '_reset'] = min(31536000, valid['reset']-now)
            else:
                result[prefix + '_used'] = -1
    return result


def collect_codex(executable='codex', timeout=20):
    """Read-only RPC; does not start an agent turn or inspect credential files."""
    proc = subprocess.Popen([executable, 'app-server'], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    messages = queue.Queue(maxsize=256)
    def reader():
        try:
            for line in proc.stdout:
                if len(line) <= 1048576:
                    try:
                        messages.put_nowait(json.loads(line))
                    except (ValueError, queue.Full):
                        pass
        finally:
            try:
                messages.put_nowait(None)
            except queue.Full:
                pass
    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    deadline = time.monotonic()+timeout
    def send(data):
        proc.stdin.write(json.dumps(data)+'\n')
        proc.stdin.flush()
    def response(identifier):
        while True:
            remaining = deadline-time.monotonic()
            if remaining <= 0:
                raise TimeoutError('Codex timeout')
            message = messages.get(timeout=remaining)
            if message is None:
                raise RuntimeError('Codex closed')
            if not isinstance(message, dict):
                continue
            if message.get('id') == identifier and 'method' not in message:
                if 'error' in message:
                    raise RuntimeError('Codex usage unavailable; check CLI login/version')
                return message['result']
            # Refuse unsolicited server requests; this bridge cannot execute tools.
            if 'method' in message and 'id' in message:
                send({'id': message['id'], 'error': {'code': -32601, 'message': 'Unsupported'}})
    try:
        send({'id': 1, 'method': 'initialize', 'params': {'clientInfo': {'name': 'gadget_usage', 'version': '0.1.0'}}})
        response(1)
        send({'method': 'initialized', 'params': {}})
        send({'id': 2, 'method': 'account/rateLimits/read'})
        return codex_summary(response(2), int(time.time()))
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        thread.join(timeout=1)
        proc.stdin.close()
        proc.stdout.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state', type=Path, default=Path('.pio/agent-usage'))
    sub = parser.add_subparsers(dest='command', required=True)
    export = sub.add_parser('snapshot', help='Export a numeric snapshot for the simulator')
    export.add_argument('--output', type=Path, required=True)
    export.add_argument('--providers', nargs='+', choices=['codex', 'claude'], default=['codex', 'claude'])
    sub.add_parser('claude-statusline', help='Read Claude status-line JSON from stdin; save only quota fields')
    collect = sub.add_parser('collect-codex')
    collect.add_argument('--codex', default='codex')
    serve = sub.add_parser('serve')
    serve.add_argument('--port', type=int, default=8787)
    serve.add_argument('--providers', nargs='+', choices=['codex', 'claude'], default=['codex', 'claude'])
    serve.add_argument('--poll-codex', action='store_true')
    serve.add_argument('--codex', default='codex')
    args = parser.parse_args()
    if args.command == 'snapshot':
        atomic_json(args.output, snapshot(args.state, args.providers))
        return
    if args.command == 'claude-statusline':
        payload = sys.stdin.read(1048577)
        if len(payload) > 1048576:
            raise SystemExit('Status-line payload too large')
        summary = claude_summary(json.loads(payload), int(time.time()))
        # Status-line events with no quota data must not erase the last sample.
        if any(summary['windows']):
            atomic_json(args.state/'claude.json', summary)
        print('Gadget usage bridge')
        return
    if args.command == 'collect-codex':
        atomic_json(args.state/'codex.json', collect_codex(args.codex))
        print('Codex allowance summary saved')
        return
    args.state.mkdir(parents=True, exist_ok=True, mode=0o700)
    token_file = args.state/'access-token.json'
    token = read_json(token_file).get('token')
    if not isinstance(token, str) or len(token) < 32:
        token = secrets.token_urlsafe(32)
        atomic_json(token_file, {'token': token})
    stop = threading.Event()
    def poll():
        while not stop.is_set():
            try:
                atomic_json(args.state/'codex.json', collect_codex(args.codex))
            except (OSError, ValueError, RuntimeError, TimeoutError, queue.Empty):
                print('Codex refresh failed; retaining timestamped sample', file=sys.stderr)
            stop.wait(60)
    if args.poll_codex and 'codex' in args.providers:
        threading.Thread(target=poll, daemon=True).start()
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if not hmac.compare_digest(self.path, '/usage/'+token) or self.headers.get('Origin'):
                self.send_error(404)
                return
            body = json.dumps(snapshot(args.state, args.providers), separators=(',', ':')).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        def log_message(self, *_):
            pass  # Never log the capability URL.
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    print(f'Usage bridge listening on loopback port {args.port}. Token stored in {token_file}.')
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        server.server_close()

if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError, queue.Empty):
        print('Usage bridge failed. Check CLI login, version, input format and file access.', file=sys.stderr)
        sys.exit(1)
