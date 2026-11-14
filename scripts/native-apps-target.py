"""PlatformIO app-only build/upload aliases; never flash firmware or LittleFS."""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
from SCons.Script import Default, DefaultEnvironment
env = DefaultEnvironment()

root = Path(env.subst('$PROJECT_DIR'))
spec = importlib.util.spec_from_file_location('native_app_builder', root / 'scripts/build-native-apps.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
selection = os.environ.get('GADGET_APPS', env.GetProjectOption('custom_apps', '*'))
apps = builder.select_apps([part for part in re.split(r'[,\s]+', selection.strip()) if part])
chip = env.BoardConfig().get('build.mcu')
if chip not in ('esp32', 'esp32s3'):
    raise ValueError('App deployment supports ESP32 and ESP32-S3 only')
package_dir = env.PioPlatform().get_package_dir('toolchain-xtensa-esp-elf')
if not package_dir:
    raise ValueError('PlatformIO Xtensa toolchain is unavailable')
toolchain = Path(package_dir) / 'bin'
output = Path(env.subst('$BUILD_DIR')) / 'apps'
signing_key = os.environ.get('GADGET_APP_SIGNING_KEY')


def build(source, target, env):
    print(f"Building {len(apps)} apps for {chip} ({'signed' if signing_key else 'unsigned developer packages'}).")
    try:
        builder.build_apps(toolchain, output, [chip], apps, signing_key)
    except subprocess.CalledProcessError as error:
        return error.returncode
    return 0


def upload(source, target, env):
    env.AutodetectUploadPort()
    port = env.subst('$UPLOAD_PORT')
    if not port or port == '$UPLOAD_PORT':
        raise ValueError('Specify the device USB port with --upload-port')
    command = [sys.executable, str(root / 'scripts/install-native-app.py'), '--port', port, '--package']
    command += [str(output / chip / (app['id'] + '.gapp')) for app in apps]
    return subprocess.run(command, cwd=root, check=False).returncode


build_target = env.Alias('buildprog', [], env.VerboseAction(build, 'Building installable apps'))
env.AlwaysBuild(build_target)
upload_target = env.Alias('upload', build_target, env.VerboseAction(upload, 'Installing apps using USB pairing and Wi-Fi'))
env.AlwaysBuild(upload_target)
Default(build_target)
