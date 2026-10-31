"""Parser fixtures are synthetic; they are not hardware performance results."""
import importlib.util
import sys
sys.dont_write_bytecode = True
from pathlib import Path
import unittest

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('summary', root / 'scripts/summarize-display-benchmark.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SummaryTests(unittest.TestCase):
    def test_comparison_and_incomplete_capture(self):
        log = ('12:00:00 BENCH,baseline-8-psram,full-redraw,0,90,10000,9000,12000,14000,6000,60,153600\n'
               'BENCH,dma-32,full-redraw,0,90,5000,4000,7000,8000,2000,15,153600\n')
        report = module.summarize(log)
        self.assertIn('-50.0%', report)
        self.assertIn('5.000 | 7.000 | 2.000 | 15.0', report)
        self.assertIn('INCOMPLETE RUN', report)

    def test_repeated_captures_are_not_silently_combined(self):
        line = 'BENCH,dma-32,full-redraw,0,90,5000,4000,7000,8000,2000,15,153600\n'
        with self.assertRaises(ValueError):
            module.summarize(line * 2)

    def test_no_records(self):
        with self.assertRaises(ValueError):
            module.summarize('booting')

    def test_complete_capture(self):
        lines = ['BENCH_DONE']
        for profile in ['baseline-8-psram', 'internal-8', 'internal-32', 'dma-32', 'dma-64']:
            for scene in ['raw-full', 'full-redraw', 'partial-update']:
                for repeat in range(3):
                    lines.append(f'BENCH,{profile},{scene},{repeat},90,5000,4000,7000,8000,2000,15,153600')
        self.assertNotIn('INCOMPLETE RUN', module.summarize('\n'.join(lines)))


if __name__ == '__main__':
    unittest.main()
