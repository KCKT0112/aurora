# Aurora vector icons

Original 24x24 outline SVGs using currentColor, round joins/caps and a consistent
1.65-unit stroke. No external icon font or raster assets are required.

`tools/vector_icons.py` compiles the audited M/L/C/Z and circle subset into
1/256-unit segments. `libs/ui/vector.c` draws at the requested size using 4x4
coverage samples, caching alpha masks for repeated sizes. The authoritative
artwork stays vector and can be edited in an SVG editor. Arbitrary SVG files,
filters, embedded scripts and external references are not supported.
