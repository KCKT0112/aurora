import unittest,ast
from pathlib import Path
class RunnerPolicyTests(unittest.TestCase):
    def test_no_automatic_geometry_observer(self):
        root=Path(__file__).parents[1]
        source=(root/'tools/run_qemu.py').read_text()
        tree=ast.parse(source)
        imports=[n.module for n in ast.walk(tree) if isinstance(n,ast.ImportFrom)]
        self.assertNotIn('host_resize',imports)
        self.assertNotIn('ResizeMonitor',source)
        self.assertNotIn('window_mode',source)
        self.assertIn('Resolution locked for this boot',source)
