export type PrimitiveType =
  | 'u32' | 'i32' | 'u16' | 'i16' | 'u8' | 'i8'
  | 'f32' | 'f64' | 'i64' | 'utf16' | 'string' | 'compact';
export interface Condition {
  field: string;
  value: number;
  mask?: number;
}
interface BaseField {
  name: string;
  count?: number | string;
  enabledBy?: Condition[][];
}
export type Field =
  | (BaseField & { type: Exclude<PrimitiveType, 'string' | 'utf16'> })
  | (BaseField & { type: 'string'; encoding?: 'byte' | 'utf16'; terminated?: boolean; format?: 'id-list' })
  | (BaseField & { type: 'utf16'; terminated?: boolean; format?: 'id-list' })
  | (BaseField & { type: 'group'; fields: Field[] })
  | (Omit<BaseField, 'count'> & { type: 'padding'; size: number });
export interface Block {
  fields: Field[];
  recordCount?: 'u32' | 'compact' | 'eof' | number | { nonzero: string };
}
export interface Schema {
  clients?: string[];
  $schema?: string;
  version?: 1;
  blocks: Block[];
  trailer?: boolean;
  magic?: number;
}
export type Definition = Schema | (Schema & { clients: string[] })[];
export interface StringValue { value: string | number[]; encoding?: 'byte' | 'utf16'; terminated?: boolean }
export interface FloatBits { bits: string }
export type RecordValue = number | string | StringValue | FloatBits | DatRecord | RecordValue[];
export interface DatRecord { [field: string]: RecordValue }
export type DatDocument = DatRecord[] | { blocks: DatRecord[][] };
