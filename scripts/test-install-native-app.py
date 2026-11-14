#!/usr/bin/env python3
"""CLI upload contract tests; no device access."""
import importlib.util
from pathlib import Path
import unittest
import hashlib
import json
import struct
import tempfile
import types
from unittest.mock import MagicMock
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('installer',Path(__file__).with_name('install-native-app.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class InstallerTest(unittest.TestCase):
    def setUp(self):
        manifest=json.dumps({'id':'agent-usage','version':'0.7.0','chip':'esp32s3','api_major':1,'api_minor':7}).encode()
        payload=manifest+bytes(3200)+b'ELF fixture'
        header=b'GOSAPP1\0'+struct.pack('<III',len(manifest),3200,len(b'ELF fixture'))
        self.raw=header+hashlib.sha256(header+payload).digest()+payload
        self.url='http://192.168.1.25:8081/#'+'a'*32
        self.state={'chip':'esp32s3','apiMajor':1,'apiMinor':7,'developerMode':True,'apps':[]}
    def test_install_preserves_package_and_verifies(self):
        meta=m.metadata(self.raw)
        final=dict(self.state,apps=[{'id':meta['id'],'version':meta['version']}])
        with patch.object(m,'request',side_effect=[self.state,{'ok':True},final]) as request:
            m.install(self.url,self.raw)
            self.assertEqual(request.call_args_list[1].args[2:],('/api/install',self.raw))
            self.assertEqual(request.call_count,3)
    def test_preflight_blocks_wrong_chip_old_sdk_and_unsigned(self):
        for changes in [{'chip':'esp32'},{'apiMinor':2},{'developerMode':False}]:
            with patch.object(m,'request',return_value=dict(self.state,**changes)) as request:
                with self.assertRaises(ValueError):m.install(self.url,self.raw)
                self.assertEqual(request.call_count,1)
    def test_bad_url_and_truncated_file(self):
        for url in [self.url.replace('8081','80'),self.url.replace('http:','https:'),self.url.split('#')[0],self.url.replace('192.168.1.25','example.com')]:
            with self.assertRaises(ValueError):m.pairing(url)
        with self.assertRaises(ValueError):m.metadata(self.raw[:-1])
    def test_batch_preflights_every_package_before_install(self):
        with patch.object(m,'request',return_value=self.state) as request, patch.object(m,'install') as install:
            with self.assertRaises(ValueError):m.install_all(self.url,[self.raw,self.raw[:-1]])
            install.assert_not_called()
            self.assertEqual(request.call_count,1)
    def test_batch_order_and_stop_on_failure(self):
        with patch.object(m,'request',return_value=self.state), patch.object(m,'install') as install:
            m.install_all(self.url,[self.raw,self.raw])
            self.assertEqual(install.call_count,2)
        with patch.object(m,'request',return_value=self.state), patch.object(m,'install',side_effect=[None,ValueError('storage full')]) as install:
            with self.assertRaisesRegex(ValueError,'storage full'):m.install_all(self.url,[self.raw]*3)
            self.assertEqual(install.call_count,2)
    def test_batch_pairs_only_once(self):
        port=MagicMock();port.read_until.return_value=('[NATIVE MANAGER] url='+self.url+'\n').encode()
        serial=types.SimpleNamespace(Serial=MagicMock(return_value=port))
        with tempfile.TemporaryDirectory() as folder:
            files=[Path(folder)/name for name in ('one.gapp','two.gapp')]
            for file in files:file.write_bytes(self.raw)
            with patch.dict(m.sys.modules,{'serial':serial}), patch.object(m.sys,'argv',['install','--port','test-port','--package',*map(str,files)]), patch.object(m.time,'monotonic',side_effect=[0,9,10,11]), patch.object(m.time,'sleep'), patch.object(m,'install_all') as install:
                m.main()
                install.assert_called_once_with(self.url,[self.raw,self.raw])
                serial.Serial.assert_called_once()
                self.assertEqual([c.args[0] for c in port.write.call_args_list],[b'native manager close\n',b'native manager\n',b'native manager close\n'])
if __name__=='__main__':unittest.main()
