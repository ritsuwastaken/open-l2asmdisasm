/** JSON binary definitions. */
export type PrimitiveType =
  | 'u32' | 'hex32' | 'i32' | 'u16' | 'i16' | 'u8' | 'hex8' | 'i8'
  | 'f32' | 'f64' | 'i64' | 'unicode' | 'ascf' | 'compact';
export interface Condition {
  field: string;
  value: number;
  mask?: number;
}
interface BaseField {
  name: string;
  /** Fixed length or preceding integer field; omitted for scalars. */
  count?: number | string;
  /** OR of AND clauses: (field & mask) == value. */
  enabledBy?: Condition[][];
}
export type Field =
  | (BaseField & { type: PrimitiveType | 'mtx' | 'mtx2' | 'mtx3' | 'mat' | 'mat2' })
  | (BaseField & { type: 'group'; fields: Field[] })
  | (Omit<BaseField, 'count'> & { type: 'filler'; size: number });
export interface Block {
  fields: Field[];
  /** Default: u32. `nonzero` counts records with a nonzero field. */
  recordCount?: 'u32' | 'compact' | 'eof' | number | { nonzero: string };
}
export interface Schema {
  /** Supported client versions. */
  clients?: string[];
  $schema?: string;
  version?: 1;
  blocks: Block[];
  trailer?: boolean;
  /** Number of zero dwords before the trailer. */
  magic?: number;
}
export interface Ascf { encoding: 'byte' | 'utf16'; value: string }
export interface FloatBits { bits: string }
export type RecordValue = number | string | Ascf | FloatBits | DatRecord | RecordValue[];
export interface DatRecord { [field: string]: RecordValue }
export type DatDocument = DatRecord[] | { blocks: DatRecord[][] };
