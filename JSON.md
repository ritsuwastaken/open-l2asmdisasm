# JSON - DAT definition

```json
{
  "clients": ["C3-L2NA-P530", "C4-L2NA-P656"],
  "blocks": [
    {
      "fields": [
        { "name": "id", "type": "u32" },
        { "name": "name", "type": "unicode" },
        { "name": "n", "type": "compact" },
        { "name": "values", "type": "u16", "count": "n" }
      ]
    }
  ]
}
```

`parse_schema()` / `export_schema()`: one layout. `parse_schemas()` / `export_schemas()`: all layouts.
[TypeScript types](schema/types.ts), [JSON Schema](schema/definition.schema.json).

## Settings

| Property  | Default  |                                                               |
| --------- | -------- | ------------------------------------------------------------- |
| `version` | `1`      | definition format version                                     |
| `clients` | omitted  | supported client versions; required in a layout array         |
| `blocks`  | required | ordered blocks, each with `fields` and optional `recordCount` |
| `trailer` | `true`   | SafePackage marker at the end                                 |
| `magic`   | `0`      | zero dwords before the trailer                                |

`recordCount`:

| Value                    |                                                                            |
| ------------------------ | -------------------------------------------------------------------------- |
| `"u32"` (default)        | stored UINT count                                                          |
| `"compact"`              | stored compact-index count                                                 |
| Nonnegative integer      | fixed count, not stored                                                    |
| `"eof"`                  | records up to the trailer                                                  |
| `{ "nonzero": "field" }` | stored UINT counts records with nonzero `field`; records up to the trailer |

Only the last block may run to the end.

## Types

All fixed-width numbers are little-endian.

| Type                                   | JSON value                                |                                                                 |
| -------------------------------------- | ----------------------------------------- | --------------------------------------------------------------- |
| `u32`, `i32`, `u16`, `i16`, `u8`, `i8` | integer                                   | unsigned / signed, 32 / 16 / 8 bits                             |
| `hex32`, `hex8`                        | integer                                   | same as `u32`, `u8`; hex display in TSV                         |
| `compact`                              | integer                                   | signed Unreal compact index                                     |
| `i64`                                  | decimal string                            | signed 64-bit integer                                           |
| `f32`, `f64`                           | number                                    | shortest round-trip float / double; NaN and infinity use `{ "bits": "0x7FC00001" }` |
| `unicode`                              | string                                    | INT byte length, UTF-16LE                                       |
| `ascf`                                 | `{ "encoding": "byte", "value": "text" }` | compact length; `encoding` is `byte` or `utf16`                 |
| `filler`                               | byte value                                | repeated byte; definition requires `"size": n`                  |
| `group`                                | object                                    | definition requires nested `"fields": [...]`                    |

| Compound | Object fields                                                              |
| -------- | -------------------------------------------------------------------------- |
| `mtx`    | `meshes` (strings), `textures` (strings)                                    |
| `mtx2`   | same, meshes are `{ "name": "...", "byte": 0, "value": 0 }`                |
| `mtx3`   | same, meshes are `{ "name": "...", "bytes": [0, 0] }`; also `extraTexture` |
| `mat`    | `entries` (objects with `id` and `count`)                                  |
| `mat2`   | same as `mat`, with `extra` before entries on disk                         |

`count`, `meshCount`, `textureCount`: optional, inferred from arrays.

## Fields

- `count`: array length or preceding counter field. Array-only counters are inferred; others stay explicit.
- `enabledBy`: `[[{"field":"flags","mask":2,"value":2}]]` — `(field & mask) == value`. AND within clauses, OR between them. Default mask: `-1`. Omit disabled fields.
- Groups can reference earlier fields in their own or enclosing scope.
- Repeated names: `name`, `name#2`, etc.; append `#` for collisions. References use the first field.

## Data

UTF-8 JSON. Definition order is disk/output order; input keys may be reordered.

```json
[
  { "id": 1, "name": "Example item", "values": [10, 20, 30] },
  { "id": 2, "name": "", "values": [] }
]
```

Several blocks: `{ "blocks": [[records...], [records...]] }`.
Strings use JSON escapes, including `\u0000`. Empty UTF-16 ASCF is not representable.
`TextOptions::charset`: byte ASCF charset. Other text options are TSV-only.

## Definitions

`definitions/<dat-basename>.json`: an array of layouts.

```json
[
  {
    "clients": ["C3", "C4"],
    "blocks": [{ "fields": [{ "name": "id", "type": "u32" }] }]
  },
  {
    "clients": ["C5"],
    "blocks": [{ "fields": [{ "name": "id", "type": "u16" }] }]
  }
]
```

`resolve_definition(directory, filename)` finds the file; `parse_schema(json, client_version)` selects a layout.
DAT names ignore case; client versions match exactly. Multiple layouts require a version.
Missing or duplicate matches are errors. Standalone layout objects are also accepted.
