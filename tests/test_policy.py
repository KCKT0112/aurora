import importlib.util
import json
from pathlib import Path
import unittest

root = Path(__file__).parents[1]
spec = importlib.util.spec_from_file_location('policy', root / 'tools/policy.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)

class PolicyTests(unittest.TestCase):
    def config(self):
        return json.loads((root / 'config/system.json').read_text())

    def test_authority_is_generated_from_one_source(self):
        output = policy.generate(self.config())
        self.assertIn('[IMAGE_INIT]', output)
        self.assertIn('CAP_SPAWN', output)
        self.assertEqual(output.count('CAP_SPAWN'), 1)

    def test_no_app_self_escalation(self):
        spec = self.config()
        spec['programs'][-1]['grants'].append('spawn')
        with self.assertRaises(ValueError):
            policy.generate(spec)

    def test_unknown_grant_and_path_escape(self):
        spec = self.config()
        spec['programs'][0]['grants'].append('arbitrary_device')
        with self.assertRaises(ValueError):
            policy.generate(spec)
        spec = self.config()
        spec['programs'][0]['path'] = 'system/../../outside'
        with self.assertRaises(ValueError):
            policy.generate(spec)

    def test_duplicate_image(self):
        spec = self.config()
        spec['programs'].append(spec['programs'][0])
        with self.assertRaises(ValueError):
            policy.generate(spec)
