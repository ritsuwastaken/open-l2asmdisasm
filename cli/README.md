# l2asmdisasm CLI

### [Download](https://github.com/ritsuwastaken/open-l2asmdisasm/releases/latest)

```sh
./l2asmdisasm --help
./l2asmdisasm cntr encode -64   # c001
./l2asmdisasm cntr decode c001  # -64

./l2asmdisasm disassemble -d definitions -v CT1.5-L2NA-P831 ItemName-e.dat ItemName-e.json
./l2asmdisasm assemble -d definitions -v CT1.5-L2NA-P831 ItemName-e.json ItemName-e.rebuilt.dat
```

See [`JSON.md`](../JSON.md) for the format.

## Options

| Option | Meaning |
| --- | --- |
| `-d <path>` | JSON definition file or definitions directory; required |
| `-v <version>` | Client version; required when a file has multiple layouts |
| `-n <name>` | DAT basename; defaults to the input filename |
| `-a <charset>` | Byte-string charset, default ISO-8859-1 |

## Credits

- **skeeto** - [optparse](https://github.com/skeeto/optparse)
