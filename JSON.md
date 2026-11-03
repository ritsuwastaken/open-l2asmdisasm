# JSON - DAT definition

```json
{
  "clients": ["C3-L2NA-P530", "C4-L2NA-P656"],
  "blocks": [{
    "fields": [
      { "name": "id", "type": "u32" },
      { "name": "name", "type": "unicode" },
      { "name": "n", "type": "compact" },
      { "name": "values", "type": "u16", "count": "n" }
    ]
  }]
}
```

`parse_schema()` reads a definition; `export_schema()` writes it.
See [`schema/types.ts`](schema/types.ts) for TypeScript types and
[`schema/definition.schema.json`](schema/definition.schema.json) for JSON Schema.

## Settings

| Property | Default | |
| --- | --- | --- |
| `version` | `1` | definition format version |
| `clients` | omitted | supported client versions, used for discovery |
| `blocks` | required | ordered blocks, each with `fields` and optional `recordCount` |
| `trailer` | `true` | SafePackage marker at the end |
| `magic` | `0` | zero dwords before the trailer |

`recordCount`:

| Value | |
| --- | --- |
| `"u32"` (default) | stored UINT count |
| `"compact"` | stored compact-index count |
| Nonnegative integer | fixed count, not stored |
| `"eof"` | records up to the trailer |
| `{ "nonzero": "field" }` | stored UINT counts records with nonzero `field`; records up to the trailer |

Only the last block may run to the end.

## Types

All fixed-width numbers are little-endian.

| Type | JSON value | |
| --- | --- | --- |
| `u32`, `i32`, `u16`, `i16`, `u8`, `i8` | integer | unsigned / signed, 32 / 16 / 8 bits |
| `hex32`, `hex8` | integer | same as `u32`, `u8`; hex display in TSV |
| `compact` | integer | signed Unreal compact index |
| `i64` | decimal string | signed 64-bit integer |
| `f32`, `f64` | number | float / double; NaN and infinity use `{ "bits": "0x7FC00001" }` |
| `unicode` | string | INT byte length, UTF-16LE |
| `ascf` | `{ "encoding": "byte", "value": "text" }` | compact length; `encoding` is `byte` or `utf16` |
| `filler` | byte value | repeated byte; definition requires `"size": n` |
| `group` | object | definition requires nested `"fields": [...]` |

| Compound | Object fields |
| --- | --- |
| `mtx` | `meshCount`, `meshes` (strings), `textureCount`, `textures` (strings) |
| `mtx2` | same, meshes are `{ "name": "...", "byte": 0, "value": 0 }` |
| `mtx3` | same, meshes are `{ "name": "...", "bytes": [0, 0] }`; also `extraTexture` |
| `mat` | `count`, `entries` (objects with `id` and `count`) |
| `mat2` | same as `mat`, with `extra` before entries on disk |

## Fields

- `count`: fixed array length or name of a preceding integer field. Length one stays an array; dynamic arrays may be empty. Counts must match.
- `enabledBy`: `[[{"field":"flags","mask":2,"value":2}]]`. Terms within a clause are AND; clauses are OR. Comparison is `(field & mask) == value`; `mask` defaults to `-1`. Omit disabled fields from data.
- Groups may reference preceding fields in their own and enclosing scopes.
- Repeated names use `name`, `name#2`, `name#3`, etc. in data. Append `#` to avoid collisions with original names. References use the first original field.

## Data

UTF-8 JSON. Field order in the definition is byte order; object key order does not matter.

```json
[
  { "id": 1, "name": "Example item", "n": 3, "values": [10, 20, 30] },
  { "id": 2, "name": "", "n": 0, "values": [] }
]
```

Several blocks: `{ "blocks": [[records...], [records...]] }`.
Strings use JSON escapes, including `\u0000`. Empty UTF-16 ASCF is not representable.
`TextOptions::charset` selects the byte ASCF charset; other text options apply to TSV helpers.

## Definitions

`definitions/<dat-basename>/<first-version>.json`. Each layout lists its supported
`clients`. `resolve_definition(directory, filename, client_version)` finds the matching
file. DAT names are case-insensitive; client versions match exactly. Missing or
ambiguous matches are errors. Standalone definitions may omit `clients`.
