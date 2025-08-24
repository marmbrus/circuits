export type LedConfig = {
  dataGPIO: number
  enabledGPIO?: number
  chip: string
  num_columns: number
  num_rows: number
  pattern?: string
  brightness?: number
  R?: number
  B?: number
  speed?: number
  duty?: number
}

export type WifiConfig = {
  ssid: string
  password: string
  mqtt_broker: string
  loglevel?: number
}

export type TagsConfig = {
  area: string
  room: string
  id: string
}

export type MotionConfig = {
  gpio: number
}

export type SensorConfig = {
  wifi: WifiConfig
  tags: TagsConfig
  [key: `led${number}`]: LedConfig | unknown
  motion?: MotionConfig
}

export type MetricTags = {
  area?: string
  room?: string
  id?: string
  mac?: string
  sensor?: string
  type?: string
  name?: string
  addr?: string
  channel?: string
}

export type MetricPayload = {
  metric: string
  value: number
  tags: MetricTags
  ts: string
  [k: string]: unknown
}

export type MetricEntry = {
  raw: MetricPayload
}

export type SensorState = {
  mac: string
  config?: SensorConfig
  metrics: Record<string, MetricEntry>
  ip?: string
  pendingConfig?: boolean
  deviceStatus?: Record<string, unknown>
  deviceStatusTs?: number
  otaStatus?: Record<string, unknown>
  otaStatusTs?: number
  deviceBoot?: Record<string, unknown>
  deviceBootTs?: number
  i2c?: {
    ts?: string
    devices: Array<{
      addr: string
      driver?: string
      index?: number
      module?: string
    }>
  }
  logs?: Array<{ ts: number; level: string; message: string }>
}

export type SensorsMap = Map<string, SensorState>


