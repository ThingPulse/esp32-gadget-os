#!/usr/bin/env python3
"""CLI upload contract tests; no device access."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('installer',Path(__file__).with_name('install-native-app.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class InstallerTest(unittest.TestCase):
    def setUp(self):
        self.raw=Path('.pio/native-app-packages/esp32s3/agent-usage.gapp').read_bytes()
        self.url='http://192.168.1.25:8081/#'+'a'*32
        self.state={'chip':'esp32s3','apiMajor':1,'apiMinor':3,'developerMode':True,'apps':[]}
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
if __name__=='__main__':unittest.main()
