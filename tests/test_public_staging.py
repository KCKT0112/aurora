"""Public checkouts must build test images without personal music or credentials."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import wave

ROOT = Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import stage_system
from wav_fixture import tone


class PublicStagingTests(unittest.TestCase):
    def stage(self, testing, local_media=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'config').mkdir()
            config = json.loads((ROOT / 'config/system.json').read_text())
            (root / 'config/system.json').write_text(json.dumps(config))
            music = root / 'rootfs/home/user/Music'
            music.mkdir(parents=True)
            (root / 'rootfs/system/share').mkdir(parents=True)
            source = tone(seconds=.02)
            if local_media:
                (music / 'personal.wav').write_bytes(source)
            out = root / 'out'
            (out / 'user').mkdir(parents=True)
            names = [p['name'] for p in config['programs']]
            if testing:
                names += ['security-test', 'service-test']
            for name in names:
                (out / 'user' / (name + '.elf')).write_bytes(b'ELF fixture')
            with patch.object(stage_system, 'ROOT', root), patch.object(stage_system, 'database', return_value=b'test verifier'):
                stage_system.stage(out, testing)
            staged = out / 'rootfs/home/user/Music'
            if local_media:
                self.assertEqual((staged / 'personal.wav').read_bytes(), source)
                self.assertFalse((staged / 'aurora-test.wav').exists())
            elif testing:
                with wave.open(str(staged / 'aurora-test.wav'), 'rb') as wav:
                    self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()), (2, 3, 48000, 288000))
            else:
                self.assertEqual(list(staged.iterdir()), [])
            self.assertFalse((out / 'rootfs/system/boot/accounts.bin').exists())

    def test_public_test_image_generates_media(self):
        self.stage(True)

    def test_local_media_preserved(self):
        self.stage(True, local_media=True)

    def test_release_does_not_generate_personal_media(self):
        self.stage(False)
