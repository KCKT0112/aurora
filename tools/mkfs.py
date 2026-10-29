#!/usr/bin/env python3
"""Pack an Aurora system tree into a bounded, strictly validated initramfs.

Files larger than 32 MiB, symlinks and images above 64 MiB fail explicitly.
Use `make fs` to stage executable modules alongside the source data first.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_ROOT = ROOT / "rootfs"
DEFAULT_OUTPUT = ROOT / "out" / "x86_64" / "initramfs.bin"

MAGIC = 0x31465341
VERSION = 2
MAX_NODES = 256
NAME_BYTES = 39
MAX_FILE_BYTES = 32 * 1024 * 1024
HEADER = struct.Struct("<8I")
NODE = struct.Struct("<40sIIIIIIII")
HEADER_SIZE = HEADER.size          # 32
NODE_SIZE = NODE.size              # 72
NODE_DIR = 1
NODE_FILE = 2


class PackError(RuntimeError):
    pass


def fnv1a(data: bytes) -> int:
    value = 0x811C9DC5
    for byte in data:
        value = ((value ^ byte) * 0x01000193) & 0xFFFFFFFF
    return value


def collect(root: pathlib.Path):
    if not root.is_dir():
        raise PackError(f"rootfs not found: {root}")
    nodes = []  # (name, parent_index, type, source_path)
    nodes.append(("/", 0, NODE_DIR, root))

    def walk(directory: pathlib.Path, parent_index: int):
        children = sorted(directory.iterdir(),
                          key=lambda path: (path.is_file(), path.name))
        for child in children:
            if child.is_symlink():
                raise PackError(f"symlinks are not supported: {child}")
            if child.name.startswith("."):
                continue
            encoded = child.name.encode("utf-8")
            if len(encoded) > NAME_BYTES:
                raise PackError(f"name too long: {child}")
            if child.is_dir():
                index = len(nodes)
                nodes.append((child.name, parent_index, NODE_DIR, child))
                walk(child, index)
            elif child.is_file():
                size = child.stat().st_size
                if size > MAX_FILE_BYTES:
                    raise PackError(f"file exceeds {MAX_FILE_BYTES} bytes: {child}")
                nodes.append((child.name, parent_index, NODE_FILE, child))
            else:
                raise PackError(f"unsupported file type: {child}")

    walk(root, 0)
    if len(nodes) > MAX_NODES:
        raise PackError(f"too many nodes: {len(nodes)} > {MAX_NODES}")
    return nodes


def pack(root: pathlib.Path) -> bytes:
    nodes = collect(root)
    data = bytearray()
    node_blob = bytearray()

    for name, parent, kind, path in nodes:
        offset = 0
        size = 0
        relative = '/' + path.relative_to(root).as_posix().strip('.')
        uid = gid = 0
        mode = 0o755 if kind == NODE_DIR else 0o644
        for home, owner in (('/root', 0), ('/home/user', 1000), ('/home/guest', 1001)):
            if relative == home or relative.startswith(home + '/'):
                uid = gid = owner
                mode = 0o700 if kind == NODE_DIR else 0o600
        if relative == '/tmp': mode = 0o1777
        if relative == '/system' or relative.startswith('/system/'):
            mode = 0o555 if kind == NODE_DIR else 0o444
        if kind == NODE_FILE:
            payload = path.read_bytes()
            offset = len(data)
            size = len(payload)
            data.extend(payload)
            data.extend(b"\0" * ((8 - len(data) % 8) % 8))
        node_blob.extend(NODE.pack(
            name.encode("utf-8").ljust(40, b"\0"),
            parent, kind, size, offset, mode, uid, gid, 0,
        ))

    data_offset = HEADER_SIZE + len(node_blob)
    total_size = data_offset + len(data)
    if total_size > 64 * 1024 * 1024:
        raise PackError("image exceeds 64 MiB")
    checksum = fnv1a(bytes(node_blob) + bytes(data))
    header = HEADER.pack(MAGIC, VERSION, len(nodes), 0, data_offset,
                         len(data), total_size, checksum)
    return header + bytes(node_blob) + bytes(data)


def validate(blob: bytes) -> tuple[int, int]:
    if len(blob) < HEADER_SIZE:
        raise PackError("image shorter than header")
    (magic, version, node_count, root_index, data_offset, data_size,
     total_size, checksum) = HEADER.unpack_from(blob, 0)
    if magic != MAGIC or version != VERSION:
        raise PackError("bad magic or version")
    if node_count == 0 or node_count > MAX_NODES or root_index != 0:
        raise PackError("bad node count or root")
    if total_size != len(blob):
        raise PackError(f"size mismatch: header {total_size}, file {len(blob)}")
    expected = HEADER_SIZE + node_count * NODE_SIZE
    if data_offset != expected or data_offset + data_size != total_size:
        raise PackError("bad data area bounds")
    nodes = [NODE.unpack_from(blob, HEADER_SIZE + index * NODE_SIZE)
             for index in range(node_count)]
    for index, node in enumerate(nodes):
        name, parent, kind, size, offset, mode, uid, gid, reserved = node
        decoded = name.split(b"\0", 1)[0]
        if name[-1] != 0 or mode > 0o1777 or reserved:
            raise PackError(f"node {index}: invalid metadata")
        if index == 0 and (decoded != b"/" or kind != NODE_DIR):
            raise PackError("invalid root node")
        if index and (parent >= index or nodes[parent][2] != NODE_DIR or
                      decoded in (b".", b"..") or b"/" in decoded or
                      any(c < 32 for c in decoded)):
            raise PackError(f"node {index}: invalid tree or name")
        if any(n[1] == parent and n[0] == name for n in nodes[1:index]):
            raise PackError(f"node {index}: duplicate name")
        if parent >= node_count or (index == 0 and parent != 0):
            raise PackError(f"node {index}: bad parent")
        if index != 0 and not name.split(b"\0", 1)[0]:
            raise PackError(f"node {index}: empty name")
        if kind not in (NODE_DIR, NODE_FILE):
            raise PackError(f"node {index}: bad type")
        if offset > data_size or size > data_size - offset:
            raise PackError(f"node {index}: bad data range")
        if kind == NODE_DIR and (size != 0 or offset != 0):
            raise PackError(f"node {index}: directory with data")
    actual = fnv1a(blob[HEADER_SIZE:])
    if actual != checksum:
        raise PackError("checksum mismatch")
    return node_count, data_size


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path, default=DEFAULT_ROOT)
    parser.add_argument("--output", type=pathlib.Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", type=pathlib.Path)
    args = parser.parse_args()

    try:
        if args.check is not None:
            blob = args.check.read_bytes()
            count, size = validate(blob)
            print(f"{args.check}: {count} nodes, {size} data bytes, valid")
            return 0
        blob = pack(args.root)
        count, size = validate(blob)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(blob)
        print(f"{args.output}: {count} nodes, {size} data bytes, "
              f"{len(blob)} bytes total")
        return 0
    except (PackError, OSError) as error:
        print(f"mkfs: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
