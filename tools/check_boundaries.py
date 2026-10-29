#!/usr/bin/env python3
"""Reject dependencies that would silently collapse kernel/user separation."""
from pathlib import Path
import re
import sys
root = Path(__file__).resolve().parent.parent
errors = []
for area in ('apps', 'servers', 'libs/user', 'libs/core', 'abi'):
    for path in (root / area).rglob('*'):
        if path.suffix not in ('.c', '.h', '.S'):
            continue
        for number, line in enumerate(path.read_text().splitlines(), 1):
            if re.search(r'#\s*include\s*[<"](?:kernel|arch)/', line):
                errors.append(f'{path.relative_to(root)}:{number}: user/shared code imports a privileged header')
for path in (root / 'kernel').rglob('*.c'):
    if re.search(r'_binary_\w+_elf_(?:start|end)', path.read_text()):
        errors.append(f'{path.relative_to(root)}: user ELF embedded in kernel')
if errors:
    print('\n'.join(errors), file=sys.stderr)
    raise SystemExit(1)
print('Architecture boundaries: passed')
