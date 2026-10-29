import importlib.util
from pathlib import Path
import unittest
root=Path(__file__).parents[1]
spec=importlib.util.spec_from_file_location('icons',root/'tools/vector_icons.py')
icons=importlib.util.module_from_spec(spec);spec.loader.exec_module(icons)
class VectorTests(unittest.TestCase):
    def test_assets_compile_to_bounded_geometry(self):
        for name in icons.NAMES:
            with self.subTest(icon=name):
                segments=icons.segments(root/'assets/icons'/(name+'.svg'))
                self.assertGreater(len(segments),0)
                for segment in segments:
                    self.assertTrue(all(0<=value<=24*256 for value in segment))
        data=icons.generate(root/'assets/icons')
        self.assertEqual(data,icons.generate(root/'assets/icons'))
