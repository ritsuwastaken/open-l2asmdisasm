# l2asmdisasm CLI

### [Download](https://github.com/ritsuwastaken/open-l2asmdisasm/releases/latest)

## Usage

```sh
# Convert decrypted Hellbound itemname-e.dat to JSON
$ ./l2asmdisasm disassemble -d definitions -v CT1.5-L2NA-P831 ItemName-e.dat ItemName-e.json
# Convert JSON back to .dat
$ ./l2asmdisasm assemble -d definitions -v CT1.5-L2NA-P831 ItemName-e.json ItemName-e.rebuilt.dat
```

#### Options

- -h/--help - prints help message
- -d _string_ - JSON definition file or definitions directory; required for `assemble` and `disassemble`
- -v _string_ - client version; required when a definition has multiple layouts
- -n _string_ - DAT basename for discovery with a definitions directory; defaults to the input filename
- -g _string_ - decrypted `L2GameDataName.dat` for resolving name references; Helios (EP3) and later
- -G _string_ - path to save updated `L2GameDataName.dat`; assemble only, defaults to `-g`

## Credits

- **skeeto** - [optparse](https://github.com/skeeto/optparse)
