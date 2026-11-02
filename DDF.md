# DDF - DAT definition file

```c
RECCNT = OFF;
{
	UINT id;
	UINT cnt;
	ASCF name;
	UINT values[10];
	MTX meshes[cnt];
		ENBBY = [(id:2,2),(cnt,3)];
}
```

Comments: `/* */`, `//`, `#`. `YES`, `NO`, `OFF` are 1, 0, -1.

## Settings

| Setting | Default | |
| --- | --- | --- |
| `FS` | `"\t"` | column separator in output |
| `HEADER` | `YES` | column names in the first line |
| `RECCNT` | `OFF` | record count, see below |
| `MAGIC` | `0` | zero dwords before the trailer |
| `TRAILER` | `YES` | SafePackage marker at the end |
| `MTXCNT_OUT`, `MATCNT_OUT` | `YES` | output `MTX*`, `MAT*` counters |
| `ORD_IGNORE` | `NO` | ignore `ORD` |

`RECCNT`:

- `OFF` - UINT count before the records
- `<number>` - fixed count, not stored
- `CNTR` - compact index count
- `EOF` - no count, records up to the trailer
- `<field>` - UINT count of records with nonzero `<field>`, records up to the trailer

A file can have several `{ }` blocks, each with its own `RECCNT`. Only the last one can be `EOF` or `<field>`. In text, each following block starts with a `\block` line.

## Types

| Type | |
| --- | --- |
| `UINT`, `INT`, `UWORD`, `WORD`, `UCHAR`, `CHAR` | 32, 16, 8-bit integers |
| `HEX`, `CHEX` | 32, 8-bit hex |
| `FLOAT` | `NaN(0x7FC00001)` for NaN |
| `CNTR` | compact index |
| `UNICODE` | INT byte length, UTF-16LE |
| `ASCF` | compact index length, 8-bit or UTF-16LE (negative length) |
| `FILLER x{n}` | n equal bytes |

```c
MTX  { INT cntm; UNICODE m[cntm]; INT cntt; UNICODE t[cntt]; }
MTX2 { INT cntm; { UNICODE mU; UCHAR mB; UINT mD; } [cntm]; INT cntt; UNICODE t[cntt]; }
MTX3 { INT cntm; { UNICODE mU; UCHAR mB[2]; } [cntm]; INT cntt; UNICODE t[cntt]; UNICODE tE; }
MAT  { INT cnt; { INT id; INT cnt; } [cnt]; }
MAT2 { INT cnt; INT extra; { INT id; INT cnt; } [cnt]; }
```

`x[10]` - fixed array, `x[cnt]` - array sized by an earlier integer field.

## Properties

- `ENBBY = [(f,v),(g:mask,v)]` - field exists if all conditions match, several `ENBBY` - if any matches
- `ORD = n` - output column order, `OFF` hides the column
- `SKIPIF = [(i, v), (i .. j, lo .. hi)]` - empty output cells for values in range, spaces around `..` are required
- `SOFT`, `SOFTM`, `SOFTT` - column count for dynamic arrays and `MTX*`, `MAT*` parts, required by assembly, see `disassemble -e`

`ORD`, `SKIPIF`, `MTXCNT_OUT = NO`, `MATCNT_OUT = NO` and `-o f` are lossy.

## Text

UTF-8, one line per record. `\\`, `\t`, `\0`, `\r`, `\n` are escaped. `ASCF` strings start with `a,` (8-bit) or `u,` (UTF-16).
