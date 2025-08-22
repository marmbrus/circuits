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
}

export type WifiConfig = {
  ssid: string
  password: string
  mqtt_broker: string
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
}

export type MetricEntry = {
  value: number
  ts: number
  tags: MetricTags
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
}

export type SensorsMap = Map<string, SensorState>


