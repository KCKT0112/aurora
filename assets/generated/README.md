# Generated font asset

`ui_font.bin` is generated from InterVariable.ttf by `tools/fontgen.py` and is
provided so ordinary builds do not require Pillow/fontTools. To regenerate:

```sh
python3 -m venv tools/.venv
tools/.venv/bin/pip install pillow fonttools
make fonts
```

The old checked-in initramfs is retired. `make fs` stages the current rootfs and
ELFs and generates `out/<target>/initramfs.bin` on every packaging invocation.
