import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('mkfs', Path(__file__).parents[1] / 'tools/mkfs.py')
mkfs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mkfs)

class FilesystemTests(unittest.TestCase):
    def test_roundtrip_and_determinism(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / 'empty').mkdir()
            (root / 'hello').write_text('hello')
            first = mkfs.pack(root)
            self.assertEqual(first, mkfs.pack(root))
            self.assertEqual(mkfs.validate(first)[0], 3)
            with self.assertRaises(mkfs.PackError):
                mkfs.validate(first[:-1])
            corrupt = bytearray(first)
            corrupt[-1] ^= 1
            with self.assertRaises(mkfs.PackError):
                mkfs.validate(corrupt)

    def test_symlink_escape(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / 'escape').symlink_to('/etc')
            with self.assertRaises(mkfs.PackError):
                mkfs.pack(root)

    def test_oversize_is_error_not_silent_skip(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            with (root / 'large').open('wb') as file:
                file.truncate(mkfs.MAX_FILE_BYTES + 1)
            with self.assertRaises(mkfs.PackError):
                mkfs.pack(root)

    def test_invalid_tree_with_correct_checksum(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / 'file').write_text('data')
            data = bytearray(mkfs.pack(root))
            node = list(mkfs.NODE.unpack_from(data, mkfs.HEADER_SIZE + mkfs.NODE_SIZE))
            node[1] = 1
            mkfs.NODE.pack_into(data, mkfs.HEADER_SIZE + mkfs.NODE_SIZE, *node)
            header = list(mkfs.HEADER.unpack_from(data))
            header[-1] = mkfs.fnv1a(data[mkfs.HEADER_SIZE:])
            mkfs.HEADER.pack_into(data, 0, *header)
            with self.assertRaises(mkfs.PackError):
                mkfs.validate(data)

if __name__ == '__main__':
    unittest.main()
