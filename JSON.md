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

`parse_schema(json, client)` selects a layout. DAT names ignore case; clients match exactly.
A single layout may be an object. Multiple layouts require a client.

## Types

Fixed-width numbers are little-endian.

| Type | JSON value | Storage |
| ---- | ---------- | ------- |
| `u8`, `u16`, `u32`, `i8`, `i16`, `i32` | integer | unsigned / signed integer |
| `compact` | integer | signed Unreal compact index |
| `i64` | decimal string | signed 64-bit integer |
| `f32`, `f64` | number | float / double; non-finite values use `{ "bits": "0x7FC00001" }` |
| `string` | string | Unreal `FString`; compact length, byte text or UTF-16LE |
| `utf16` | string | signed 32-bit byte length, UTF-16LE; used by `L2FNameSerialize` |
| `padding` | byte value | repeated byte; requires `"size": n` |
| `group` | object | nested `"fields": [...]` |

`string` defaults to byte encoding and null-terminated text; empty text has zero length. `utf16` has no terminator. Fields can override `encoding` (`string` only) and `terminated`.

## Fields

- `count`: fixed array length or preceding integer field. Array-only counters are inferred; others stay explicit.
- `format: "id-list"`: comma-separated u32 IDs in a string field become arrays; empty text becomes `[]`. Nonstandard text stays a string.
- `enabledBy`: `[[{"field":"flags","mask":2,"value":2}]]` tests `(field & mask) == value`. AND within clauses, OR between them. Default mask: `-1`.
- Groups may reference preceding fields in their own or enclosing scope.
- Repeated names use `name`, `name#2`, etc.; append `#` for collisions. References use the first field.

Group example:

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

Only the last block may run to the end. Layout settings: `trailer` (SafePackage marker, default `true`), `magic` (zero dwords before the trailer, default `0`), `version` (`1`).

## Data

UTF-8 JSON, in definition order. Input keys may be reordered; omit disabled fields.

```json
[
  { "id": 1, "description": "Example", "meshes": ["Item.Mesh"] }
]
```

Several blocks: `{ "blocks": [[records...], [records...]] }`.
String overrides: `{ "value": "text", "encoding": "utf16", "terminated": false }`; only `value` is required. Terminators are restored on assembly; embedded nulls stay `\u0000`. Comma-separated IDs remain text.
`EncodingOptions::charset` selects the byte-string charset.
