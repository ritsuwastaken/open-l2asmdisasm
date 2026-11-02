# l2asm/l2disasm-like CLI

### [Download](https://github.com/ritsuwastaken/open-l2asmdisasm/releases/latest)

## Usage

```sh
./l2asmdisasm --help
./l2asmdisasm cntr encode -64   # c001
./l2asmdisasm cntr decode c001  # -64

./l2asmdisasm disassemble -d ddf/CT1_5/itemname-e.ddf ItemName-e.dat ItemName-e.tsv
./l2asmdisasm assemble -d ddf/CT1_5/itemname-e.ddf ItemName-e.tsv ItemName-e.rebuilt.dat

./l2asmdisasm disassemble -d ddf/CT1_5/armorgrp.ddf -e Armorgrp.ddf Armorgrp.dat Armorgrp.tsv
./l2asmdisasm assemble -d Armorgrp.ddf Armorgrp.tsv Armorgrp.rebuilt.dat
```

## Options

|                |                                                                                                                                                                                                         |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `-d <ddf>`     | DAT definition file; required                                                                                                                                                                           |
| `-e <ddf>`     | disassemble only: export the DDF with updated SOFT\* widths                                                                                                                                             |
| `-q`           | no header line in this run, overriding `HEADER`; an exported DDF keeps `HEADER`                                                                                                                         |
| `-o g\|f\|e`   | disassemble only: FLOAT notation. `g` (default) is the shortest exact round trip; `f` is legacy `%.8f` and loses small values; `e` is legacy `%.8e` with a three-digit exponent and round-trips exactly |
| `-l`           | legacy byte-oriented text instead of UTF-8                                                                                                                                                              |
| `-f`           | disassemble: write every ASCF with the `a,` hint; assemble: encode every ASCF as 8-bit                                                                                                                  |
| `-a <charset>` | charset for 8-bit ASCF strings, default ISO-8859-1; ignored with `-l`                                                                                                                                   |
## Credits

- **skeeto** - [optparse](https://github.com/skeeto/optparse)
