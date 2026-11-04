"""Real native +activate S&D timers; private headless Windows/WSL servers only."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fps', type=int, choices=(20, 100), required=True)
    parser.add_argument('--case', choices=('defuse', 'expiry'), required=True)
    args = parser.parse_args()
    windows = os.name == 'nt'
    projects = Path('D:/Development/Projects' if windows else '/mnt/d/Development/Projects')
    game = Path('C:/Games/CoD4x-100Hz-Validation' if windows else '/mnt/c/Games/CoD4x-100Hz-Validation')
    base = game / 'base' if windows else Path('/mnt/c/Games/CoD4-CODTV-Review/base')
    server = projects / ('cod4x-100hz-server/bin-win-final/cod4x100hz_final_r3.exe' if windows
                         else 'cod4x-100hz-server/bin-linux-final/cod4x18_dedrun')
    mod = projects / 'cod4-promod-projects/pmflex/build/tickrate/game/mods/pmflex'
    port = 28984 if windows else 28982
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(('127.0.0.1', port))
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    folder = game / 'logs' / f'bomb-{os.name}-{args.fps}hz-{args.case}-{stamp}'
    home = folder / 'runtime'
    (home / 'main').mkdir(parents=True)
    (home / 'main_shared/promod').mkdir(parents=True)
    shutil.copytree(mod, home / 'mods/pmflex')
    shutil.copy2(Path(__file__).with_name('fixtures') / 'tickrate_bomb.gsx',
                 home / 'main_shared/promod/tickrate_smoke.gsx')
    for name in ('xbase_00.iwd', 'jcod4x_00.iwd'):
        shutil.copy2(game / 'server-main' / name, home / 'main' / name)
    password = secrets.token_hex(16)
    config = '\n'.join((f'set rcon_password "{password}"', 'set promod_mode custom_public',
                        'set promod_economy 0', 'set cp_platform_enabled 0', 'set tickrate_test 1',
                        f'set tickrate_bomb_case {args.case}', 'set g_log games_mp.log',
                        'set g_logSync 2', 'set logfile 2', 'set sv_allowDownload 0',
                        'set sv_wwwDownload 0', 'set sv_minRate 100000', 'set sv_maxRate 100000'))
    (home / 'main/validation.cfg').write_text(config, encoding='ascii')
    settings = {'dedicated': 1, 'developer': 1, 'net_enabled': 1, 'net_ip': '127.0.0.1',
                'net_port': port, 'fs_basepath': base, 'fs_homepath': home, 'fs_game': 'mods/pmflex',
                'sv_fps': args.fps, 'sv_maxclients': 10, 'sv_noauth': 1, 'sv_authorizemode': -1,
                'sv_punkbuster': 0, 'sv_cheats': 0, 'sv_pure': 1, 'g_gametype': 'sd', 'logfile': 2}
    command = [str(server)]
    for key, value in settings.items(): command += ['+set', key, str(value)]
    command += ['+exec', 'validation.cfg', '+map', 'mp_crash']
    options = {}
    if windows:
        options['creationflags'] = subprocess.CREATE_NO_WINDOW
        options['startupinfo'] = subprocess.STARTUPINFO()
        options['startupinfo'].dwFlags |= subprocess.STARTF_USESHOWWINDOW
        options['startupinfo'].wShowWindow = 0
    result = {'fps': args.fps, 'case': args.case, 'platform': os.name, 'folder': str(folder),
              'server_sha256': hashlib.sha256(server.read_bytes()).hexdigest(), 'passed': False}

    def query(command):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.settimeout(.7)
            udp.sendto(b'\xff' * 4 + command.encode('ascii') + b'\n', ('127.0.0.1', port))
            return udp.recv(65535).decode('latin1')

    def read_logs():
        return '\n'.join(p.read_text(errors='replace') for p in home.rglob('*.log'))

    def hud(label, expected, tolerance=60):
        response = query(f'rcon {password} showconfigstring 11')
        match = re.search(r'CS is:\s*(-?\d+)', response)
        if not match: raise AssertionError(f'Native CS11 unavailable: {response}')
        actual = int(match[1])
        result.setdefault('hud', []).append({'phase': label, 'expected': expected, 'actual': actual})
        assert abs(actual - expected) <= tolerance, result['hud'][-1]

    with (folder / 'stdout.log').open('wb') as output:
        process = subprocess.Popen(command, cwd=home, stdin=subprocess.PIPE, stdout=output,
                                   stderr=subprocess.STDOUT, **options)
        result['pid'] = process.pid
        print(f'Private {os.name} {args.fps} Hz {args.case}: PID {process.pid}, UDP {port}', flush=True)
        try:
            deadline = time.monotonic() + 120
            seen = set()
            times = {}
            while time.monotonic() < deadline:
                text = read_logs()
                errors = re.findall(r'(?:script compile error|script runtime error|TICKRATE_TEST;FAILED)[^\r\n]*', text, re.I)
                if errors: raise AssertionError(errors[-5:])
                for line in re.findall(r'TICKRATE_BOMB;[^\r\n]*', text):
                    if line in seen: continue
                    seen.add(line)
                    parts = line.split(';')
                    label = parts[1]
                    times[label] = time.monotonic()
                    if label == 'ROUND_HUD': hud(label, int(parts[3]))
                    elif label == 'PLANTED': hud(label, int(parts[3])); assert parts[4] == '1'
                    elif label in ('DEFUSED', 'EXPLODED'):
                        hud(label, 0, 0)
                        assert parts[3] == '0'
                    print(line, flush=True)
                if 'TICKRATE_TEST;PASSED;' in text:
                    assert {'ROUND_HUD', 'PLANT_BEGIN', 'PLANTED'} <= seen_labels(seen)
                    finish = 'DEFUSED' if args.case == 'defuse' else 'EXPLODED'
                    assert finish in times
                    wall = {'plant': (times['PLANTED'] - times['PLANT_BEGIN']) * 1000}
                    assert abs(wall['plant'] - 5000) < 500, wall
                    if args.case == 'defuse':
                        wall['defuse'] = (times['DEFUSED'] - times['DEFUSE_BEGIN']) * 1000
                        assert abs(wall['defuse'] - 7000) < 500, wall
                    else:
                        wall['bomb'] = (times['EXPLODED'] - times['PLANTED']) * 1000
                        assert abs(wall['bomb'] - 45000) < 500, wall
                    result.update(passed=True, lines=sorted(seen), wall_ms=wall,
                                  status=query(f'rcon {password} tickrate_status'))
                    break
                if process.poll() is not None: raise RuntimeError(f'Server exited: {process.returncode}')
                time.sleep(.1)
            else: raise TimeoutError('120-second native interaction timeout')
        except Exception as error:
            result['failure'] = str(error).replace(password, '[redacted]')
        finally:
            if process.poll() is None:
                try:
                    query(f'rcon {password} quit')
                    process.communicate(b'quit\n', timeout=3)
                except (OSError, subprocess.TimeoutExpired):
                    process.terminate()
                    try: process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill(); process.wait(timeout=3)
            result.update(closed=process.poll() is not None, exit_code=process.returncode)
    (folder / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2), flush=True)
    return 0 if result['passed'] and result['closed'] else 1


def seen_labels(lines):
    return {line.split(';')[1] for line in lines}


if __name__ == '__main__':
    raise SystemExit(main())
