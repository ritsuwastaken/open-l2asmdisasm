# JSON - DAT definitions

```json
[
  {
    "clients": ["C3-L2NA-P530", "C4-L2NA-P656"],
    "blocks": [
      {
        "fields": [
          { "name": "id", "type": "u32" },
          { "name": "description", "type": "string" },
          { "name": "meshCount", "type": "i32" },
          { "name": "meshes", "type": "utf16", "count": "meshCount" }
        ]
      }
    ]
  }
]
```

## Types

Fixed-width numbers are little-endian.

| Type                                   | JSON value     | Storage                                                          |
| -------------------------------------- | -------------- | ---------------------------------------------------------------- |
| `u8`, `u16`, `u32`, `i8`, `i16`, `i32` | integer        | unsigned / signed integer                                        |
| `compact`                              | integer        | signed Unreal compact index                                      |
| `i64`                                  | decimal string | signed 64-bit integer                                            |
| `f32`, `f64`                           | number         | float / double; non-finite values use `{ "bits": "0x7FC00001" }` |
| `string`                               | string         | Unreal `FString`; compact length, byte text or UTF-16LE          |
| `utf16`                                | string         | signed 32-bit byte length, UTF-16LE                              |
| `padding`                              | byte value     | repeated byte; requires `"size": n`                              |
| `group`                                | object         | nested `"fields": [...]`                                         |

`string` defaults to byte encoding and null-terminated text; empty text has zero length. `utf16` has no terminator. Fields can override `encoding` (`string` only) and `terminated`.

## Fields

- `count`: fixed array length or preceding integer field. Array-only counters are inferred; others stay explicit.
- `format: "id-list"`: comma-separated u32 IDs in a string field become arrays; empty text becomes `[]`. Nonstandard text stays a string.
- `format: "name-ref"`: `u32`/`i32` index into `L2GameDataName.dat`; with a name table it exports as `{ "index": 123, "value": "Package.Mesh" }`. On assembly, `index` is kept while `value` matches it; otherwise `value` is reused or appended.
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
    {
      "name": "entries",
      "type": "group",
      "count": "count",
      "fields": [
        { "name": "id", "type": "i32" },
        { "name": "count", "type": "i32" }
      ]
    }
  ]
}
```

## Blocks

Each block has `fields` and optional `recordCount`:

| `recordCount`            | Meaning                                                              |
| ------------------------ | -------------------------------------------------------------------- |
| `"u32"` (default)        | stored unsigned 32-bit count                                         |
| `"compact"`              | stored compact index                                                 |
| Nonnegative integer      | fixed count, not stored                                              |
| `"eof"`                  | records up to the trailer                                            |
| `{ "nonzero": "field" }` | stored count of records with nonzero `field`; read up to the trailer |

Only the last block may run to the end. Layout settings: `trailer` (SafePackage marker, default `true`), `version` (`1`).
Default settings are omitted from definitions and exports.

## Data

UTF-8 JSON, in definition order. Input keys may be reordered; omit disabled fields.

```json
[{ "id": 1, "description": "Example", "meshes": ["Item.Mesh"] }]
```

Several blocks: `{ "blocks": [[records...], [records...]] }`.
String overrides: `{ "value": "text", "encoding": "utf16", "terminated": false }`; only `value` is required. Terminators are restored on assembly; embedded nulls stay `\u0000`. Comma-separated IDs remain text.
Byte strings are ISO-8859-1; characters outside it need `"encoding": "utf16"`.
