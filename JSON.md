# JSON - DAT definitions

[TypeScript types](schema/types.ts) · [JSON Schema](schema/definition.schema.json)

`definitions/<dat-basename>.json` contains layouts and their supported clients:

```json
[
  {
    "clients": ["C3-L2NA-P530", "C4-L2NA-P656"],
    "blocks": [{ "fields": [
      { "name": "id", "type": "u32" },
      { "name": "description", "type": "string" },
      { "name": "meshCount", "type": "i32" },
      { "name": "meshes", "type": "utf16", "count": "meshCount" }
    ] }]
  }
]
```

`resolve_definition(directory, filename)` finds the file. `parse_schema(json, client)` selects a layout; `parse_schemas(json)` reads all layouts. Export with `export_schema()` / `export_schemas()`.
DAT names ignore case; clients match exactly. A single layout may also be an object. Multiple layouts require a client.

## Types

Fixed-width numbers are little-endian.

| Type | JSON value | Storage |
| ---- | ---------- | ------- |
| `u8`, `u16`, `u32`, `i8`, `i16`, `i32` | integer | unsigned / signed integer |
| `hex8`, `hex32` | integer | same as `u8`, `u32`; hexadecimal in TSV |
| `compact` | integer | signed Unreal compact index |
| `i64` | decimal string | signed 64-bit integer |
| `f32`, `f64` | number | float / double; non-finite values use `{ "bits": "0x7FC00001" }` |
| `string` | `{ "encoding": "byte", "value": "text" }` | Unreal `FString`; compact length, byte text or UTF-16LE |
| `utf16` | string | signed 32-bit byte length, UTF-16LE; used by `L2FNameSerialize` |
| `padding` | byte value | repeated byte; requires `"size": n` |
| `group` | object | nested `"fields": [...]` |

For `string`, `encoding` is `byte` or `utf16`. Positive lengths count bytes; negative lengths count UTF-16 code units. Empty UTF-16 strings have no distinct encoding.

## Fields

- `count`: fixed array length or preceding integer field. Array-only counters are inferred; others stay explicit.
- `enabledBy`: `[[{"field":"flags","mask":2,"value":2}]]` tests `(field & mask) == value`. AND within clauses, OR between them. Default mask: `-1`.
- Groups may reference preceding fields in their own or enclosing scope.
- Repeated names use `name`, `name#2`, etc.; append `#` for collisions. References use the first field.

Meshes, textures, and materials use ordinary groups and arrays:

```json
{
  "name": "materials",
  "type": "group",
  "fields": [
    { "name": "count", "type": "i32" },
    { "name": "entries", "type": "group", "count": "count", "fields": [
      { "name": "id", "type": "i32" },
      { "name": "count", "type": "i32" }
    ] }
  ]
}
```

## Blocks

Each block has `fields` and optional `recordCount`:

| `recordCount` | Meaning |
| ------------- | ------- |
| `"u32"` (default) | stored unsigned 32-bit count |
| `"compact"` | stored compact index |
| Nonnegative integer | fixed count, not stored |
| `"eof"` | records up to the trailer |
| `{ "nonzero": "field" }` | stored count of records with nonzero `field`; read up to the trailer |

Only the last block may run to the end. Layout settings: `trailer` (SafePackage marker, default `true`), `magic` (zero dwords before the trailer, default `0`), `version` (currently `1`).

## Data

UTF-8 JSON, in definition order. Input keys may be reordered; omit disabled fields.

```json
[
  { "id": 1, "description": { "encoding": "byte", "value": "Example\u0000" }, "meshes": ["Item.Mesh"] }
]
```

Several blocks: `{ "blocks": [[records...], [records...]] }`.
Strings preserve terminators as `\u0000`; comma-separated IDs remain text.
`TextOptions::charset` selects the byte-string charset. Other text options apply only to TSV.
