import sys
from pathlib import Path
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
import display_mode
class DisplayTests(unittest.TestCase):
    def test_supported_modes(self):
        for mode in ['1024x768','1280x800','1440x900','1920x1080','2560x1440','2560x1600','2880x1800','3840x2160']:
            self.assertEqual(display_mode.validate_mode(mode),tuple(map(int,mode.split('x'))))
    def test_reject_invalid_and_overflow(self):
        for mode in ['0x0','999999x999999','1023x768','2560x1399','1440x900;echo x','3840x2161']:
            with self.assertRaises(ValueError):display_mode.validate_mode(mode)
    def test_auto_uses_host_pixels_and_explicit_fallback(self):
        with patch('display_mode.host_mode',return_value=(2880,1800)):
            self.assertEqual(display_mode.choose()[0],(2880,1800))
        with patch('display_mode.host_mode',return_value=None):
            self.assertIn('fallback',display_mode.choose()[1])
