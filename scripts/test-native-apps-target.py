#!/usr/bin/env python3
"""Check app selection and PlatformIO upload orchestration without a device."""
import importlib.util
import os
from pathlib import Path
import runpy
import sys
import types
import unittest
from unittest.mock import patch, Mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('build_apps', ROOT / 'scripts/build-native-apps.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class TargetTest(unittest.TestCase):
    def test_selection_and_release_defaults(self):
        self.assertEqual(builder.select_apps(), builder.select_apps(['*']))
        self.assertEqual([a['id'] for a in builder.select_apps(['stocks'])], ['stocks'])
        with self.assertRaisesRegex(ValueError, 'Unknown app IDs'):
            builder.select_apps(['stockz'])
        with patch.object(builder.subprocess, 'run') as run:
            builder.build_apps(Path('/compiler'), Path('/output'), ['esp32', 'esp32s3'], builder.select_apps(['stocks']), Path('/key'))
            self.assertEqual(run.call_count, 2)
            for call, chip in zip(run.call_args_list, ['esp32', 'esp32s3']):
                command = call.args[0]
                self.assertEqual(command[command.index('--chip')+1], chip)
                self.assertEqual(command[command.index('--signing-key')+1], '/key')

    def test_upload_depends_on_build_and_only_installs_selected_packages(self):
        aliases = {}
        env = Mock()
        env.subst.side_effect = lambda value: {'$PROJECT_DIR': str(ROOT), '$BUILD_DIR': '/build/color-pro-apps', '$UPLOAD_PORT': 'test-usb-port'}[value]
        env.GetProjectOption.return_value = '*'
        env.BoardConfig.return_value = {'build.mcu': 'esp32s3'}
        env.PioPlatform.return_value.get_package_dir.return_value = '/compiler'
        env.VerboseAction.side_effect = lambda action, title: action
        env.Alias.side_effect = lambda name, dependencies, action: aliases.setdefault(name, (dependencies, action)) and name
        script = types.ModuleType('SCons.Script')
        script.Default = Mock()
        script.DefaultEnvironment = lambda: env
        with patch.dict(sys.modules, {'SCons': types.ModuleType('SCons'), 'SCons.Script': script}), patch.dict(os.environ, {'GADGET_APPS': 'stocks,system-monitor'}):
            state = runpy.run_path(str(ROOT / 'scripts/native-apps-target.py'))
        self.assertEqual(aliases['upload'][0], 'buildprog')
        script.Default.assert_called_once_with('buildprog')
        with patch.object(state['builder'], 'build_apps') as build:
            aliases['buildprog'][1](None, None, env)
            self.assertEqual(build.call_args.args[2], ['esp32s3'])
            self.assertEqual([a['id'] for a in build.call_args.args[3]], ['system-monitor', 'stocks'])
        with patch('subprocess.run') as run:
            run.return_value.returncode = 1
            self.assertEqual(aliases['upload'][1](None, None, env), 1)
            env.AutodetectUploadPort.assert_called_once()
            command = run.call_args.args[0]
            self.assertEqual(Path(command[1]).name, 'install-native-app.py')
            self.assertEqual(command[2:5], ['--port', 'test-usb-port', '--package'])
            self.assertEqual(command[5:], ['/build/color-pro-apps/apps/esp32s3/system-monitor.gapp', '/build/color-pro-apps/apps/esp32s3/stocks.gapp'])


if __name__ == '__main__':
    unittest.main()
