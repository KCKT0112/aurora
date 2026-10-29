import io,unittest,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from qemu_output import relay
class OutputTests(unittest.TestCase):
    def test_only_recording_warning_is_summarized(self):
        text="qemu: audio: Can not open `ac97.pi' (no host audio driver)\nqemu: audio: Can not open `ac97.mc' (no host audio driver)\nqemu: audio: Can not open `ac97.po' (no host audio driver)\nother fatal error\n"
        log,console=io.StringIO(),io.StringIO();relay(io.StringIO(text),log,console)
        self.assertEqual(log.getvalue(),text)
        self.assertEqual(console.getvalue().count('recording is unavailable'),1)
        self.assertIn('ac97.po',console.getvalue());self.assertIn('other fatal error',console.getvalue())
