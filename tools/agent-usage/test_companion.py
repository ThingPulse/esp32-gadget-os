import json
from pathlib import Path
import tempfile
import unittest
from companion import atomic_json, snapshot, codex_summary, claude_summary, collect_codex

class CompanionTest(unittest.TestCase):
    def test_normalize_missing_expired_and_disabled(self):
        with tempfile.TemporaryDirectory() as tmp:
            state=Path(tmp)
            atomic_json(state/'codex.json',codex_summary({'rateLimits': {'primary': {'usedPercent':28,'windowDurationMins':300,'resetsAt':2000}}},1000))
            data=snapshot(state,['codex'],1100)
            self.assertEqual(data['codex_primary_used'],28)
            self.assertEqual(data['codex_primary_reset'],900)
            self.assertEqual(data['codex_age'],100)
            self.assertEqual(data['codex_secondary_used'],-1)
            self.assertEqual(data['claude_enabled'],0)
            self.assertLess(len(json.dumps(data)),2048)
            self.assertEqual(snapshot(state,['codex'],2000)['codex_primary_used'],-1)
            self.assertEqual(snapshot(state,['codex'],900)['codex_primary_used'],-1)
    def test_no_credentials_or_context_as_allowance(self):
        summary=claude_summary({'access_token':'secret','transcript_path':'private','context_window':{'remaining_percentage':80},'rate_limits':{'five_hour':{'used_percentage':42,'resets_at':2000}}},1000)
        self.assertNotIn('secret',json.dumps(summary))
        self.assertEqual(summary['windows'][0]['used'],42)
        self.assertIsNone(summary['windows'][1])
        self.assertEqual(claude_summary({'context_window':{'remaining_percentage':80}},1000)['windows'],[None,None])
    def test_invalid_percentages(self):
        for value in [True, -1,101,float('nan'),'42']:
            self.assertIsNone(claude_summary({'rate_limits':{'five_hour':{'used_percentage':value,'resets_at':2000}}},1000)['windows'][0])
    def test_codex_protocol(self):
        with tempfile.TemporaryDirectory() as tmp:
            executable=Path(tmp)/'fake-codex'
            executable.write_text('''#!/usr/bin/env python3
import json,sys
for line in sys.stdin:
 m=json.loads(line)
 if m['method']=='initialize': print(json.dumps({'id':m['id'],'result':{}}),flush=True)
 elif m['method']=='initialized': pass
 elif m['method']=='account/rateLimits/read': print(json.dumps({'id':m['id'],'result':{'rateLimits':{'primary':{'usedPercent':30,'windowDurationMins':300,'resetsAt':2000000000}}}}),flush=True)
 else: raise RuntimeError('Unexpected operation')
''')
            executable.chmod(0o700)
            self.assertEqual(collect_codex(str(executable))['windows'][0]['used'],30)

if __name__=='__main__': unittest.main()
