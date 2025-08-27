import mqtt, { type MqttClient } from 'mqtt'
import { useEffect, useMemo, useState } from 'react'
import type { MetricPayload, SensorsMap, SensorState } from './types'

export type UseSensorsResult = {
  sensors: SensorsMap
  connectionStatus: 'idle' | 'connecting' | 'connected' | 'reconnecting' | 'closed' | 'error'
  lastError?: string
  publishConfig: (mac: string, moduleName: string, configName: string, value: string | number | boolean) => void
  deleteRetainedForSensor: (mac: string) => void
}

// Development-only fallback endpoints; in production we fetch from the device
const DEV_WS_ENDPOINTS = [
  'ws://gaia.home:9001/',
]

function deriveWsUrlFromBrokerUri(uri: string | undefined | null): string | null {
  if (!uri) return null
  try {
    const u = new URL(uri)
    // If already ws/wss, return as-is
    if (u.protocol === 'ws:' || u.protocol === 'wss:') {
      // Ensure trailing slash for mqtt.js
      if (!u.pathname || u.pathname === '/') return `${u.origin}/`
      return u.toString()
    }
    // Map common MQTT URI schemes to WebSocket
    const isSecure = (u.protocol === 'mqtts:' || u.protocol === 'ssl:' || u.protocol === 'tls:' || u.protocol === 'mqtt+ssl:')
    const protocol = isSecure ? 'wss:' : 'ws:'
    const host = u.hostname
    // Heuristic: 1883 -> 9001; otherwise keep provided port or default to 9001
    let port = u.port
    if (!port || port === '1883') port = '9001'
    return `${protocol}//${host}:${port}/`
  } catch {
    return null
  }
}

function getWsOverrideFromQuery(): string | null {
  try {
    const url = new URL(window.location.href)
    const v = url.searchParams.get('mqtt_ws')
    return v ? (deriveWsUrlFromBrokerUri(v) || v) : null
  } catch {
    return null
  }
}

async function fetchJson<T = unknown>(url: string, timeoutMs = 2000): Promise<T> {
  const ctrl = new AbortController()
  const to = setTimeout(() => ctrl.abort(), timeoutMs)
  try {
    const r = await fetch(url, { signal: ctrl.signal })
    if (!r.ok) throw new Error(`http ${r.status}`)
    const ct = r.headers.get('content-type') || ''
    if (!ct.includes('application/json')) throw new Error('unexpected content-type')
    return await r.json()
  } finally {
    clearTimeout(to)
  }
}

// Debug level: 0=off, 1=truncated payloads, 2=full payloads
function getDebugLevel(): number {
  let level = 0
  try {
    const url = new URL(window.location.href)
    const qp = url.searchParams.get('debug_mqtt')
    if (qp) level = Number(qp) || 0
  } catch { /* noop */ }
  if (level === 0) {
    try { level = Number(localStorage.getItem('mqttDebug') || '0') || 0 } catch { /* noop */ }
  }
  return level
}

class MqttManager {
  private client: MqttClient | null = null
  private connecting = false
  private attemptIndex = 0
  private probeTimeout: number | undefined
  private endpoints: string[] = []
  private initStarted = false

  public sensors: SensorsMap = new Map()
  public status: UseSensorsResult['connectionStatus'] = 'idle'
  public lastError: string | undefined
  public version = 0

  // Track retained topics received per sensor MAC to enable deletion
  private retainedTopicsByMac = new Map<string, Set<string>>()

  private listeners = new Set<() => void>()
  private readonly debugLevel = getDebugLevel()

  private dlog = (...args: unknown[]) => {
    if (this.debugLevel > 0) {
      // eslint-disable-next-line no-console
      console.debug('[MQTT]', ...args)
    }
  }

  private isSensorRetainedDataEmpty(s: SensorState): boolean {
    // Ignore metrics and logs; only consider retained-derived fields
    return !s.config && !s.ip && !s.deviceStatus && !s.otaStatus && !s.deviceBoot && !s.i2c
  }

  private deleteSensorIfEmpty(mac: string) {
    const s = this.sensors.get(mac)
    if (!s) return
    if (this.isSensorRetainedDataEmpty(s)) {
      this.sensors.delete(mac)
      this.retainedTopicsByMac.delete(mac)
      this.notify()
      this.dlog('Deleted sensor due to no retained data', mac)
    } else {
      this.notify()
    }
  }

  subscribe(listener: () => void): () => void {
    this.listeners.add(listener)
    // Initialize endpoints and then ensure connection is started
    this.initializeEndpointsIfNeeded()
    // Notify initial state
    listener()
    return () => this.listeners.delete(listener)
  }

  private notify() {
    this.version++
    for (const l of this.listeners) l()
  }

  private setStatus(next: UseSensorsResult['connectionStatus']) {
    if (this.status !== next) {
      this.status = next
      this.notify()
    }
  }

  private setError(err?: string) {
    this.lastError = err
    this.notify()
  }

  private ensureSensor(mac: string): SensorState {
    let s = this.sensors.get(mac)
    if (!s) {
      s = { mac, metrics: {} }
      this.sensors.set(mac, s)
    }
    return s
  }

  private connectIfNeeded() {
    if (this.client || this.connecting) return
    if (this.endpoints.length === 0) return
    this.connecting = true
    this.attemptIndex = 0
    this.setStatus('connecting')
    this.tryConnectNext()
  }

  private tryConnectNext() {
    if (this.attemptIndex >= this.endpoints.length) {
      this.connecting = false
      this.setStatus('error')
      this.setError('All websocket endpoints failed')
      this.dlog('All websocket endpoints failed')
      return
    }
    const url = this.endpoints[this.attemptIndex++]
    this.dlog('Connecting to', url)

    const c = mqtt.connect(url, {
      clientId: `roomsensor-ui-${MqttManager.randomId}`,
      clean: true,
      reconnectPeriod: 0,
      protocolVersion: 4,
    })
    this.client = c

    c.on('connect', (packet) => {
      this.connecting = false
      this.setStatus('connected')
      this.dlog('Connected', packet)
      c.subscribe('sensor/#', (err) => {
        if (err) {
          this.setError(err.message)
          this.dlog('Subscribe error', err)
        } else {
          this.dlog('Subscribed to sensor/#')
        }
      })
    })

    c.on('message', (topic, payload, packet) => {
      if (this.debugLevel > 0) {
        try {
          const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
          const t = String(topic)
          const isMetric = t.includes('/metrics/')
          if (!isMetric) {
            if (this.debugLevel >= 2) {
              this.dlog('Message', topic, text)
            } else {
              const preview = text.length > 256 ? text.slice(0, 256) + '…' : text
              this.dlog('Message', topic, preview)
            }
          }
        } catch {
          this.dlog('Message (binary)', topic, (payload as any)?.length ?? 0, 'bytes')
        }
      }
      try {
        this.handleMessage(topic.toString(), payload, Boolean((packet as any)?.retain))
      } catch (e) {
        this.setError(`message handler error: ${(e as Error).message}`)
        this.dlog('Message handler error', (e as Error).message)
      }
    })

    c.on('reconnect', () => { this.setStatus('reconnecting'); this.dlog('Reconnect...') })
    c.on('close', () => { this.setStatus('closed'); this.dlog('Closed') })
    c.on('error', (err) => { this.setError(err.message); this.dlog('Error', err.message) })

    // Low-level packet tracing
    c.on('packetsend', (pkt) => this.dlog('Packet send', (pkt as any)?.cmd))
    c.on('packetreceive', (pkt) => { if ((pkt as any)?.cmd !== 'publish') this.dlog('Packet recv', (pkt as any)?.cmd) })

    // Probe timeout to advance endpoints if initial connect fails silently
    // @ts-expect-error DOM lib typing
    this.probeTimeout = setTimeout(() => {
      if (this.status !== 'connected' && this.client === c) {
        this.dlog('Connect timeout; ending and trying next')
        try { c.end(true) } catch { /* noop */ }
        this.client = null
        this.setStatus('connecting')
        this.tryConnectNext()
      }
    }, 2000)

    c.on('connect', () => { if (this.probeTimeout) clearTimeout(this.probeTimeout) })
    c.on('close', () => { if (this.status === 'connected' && this.probeTimeout) clearTimeout(this.probeTimeout) })
  }

  private initializeEndpointsIfNeeded() {
    if (this.initStarted) return
    this.initStarted = true
    const qpOverride = getWsOverrideFromQuery()
    // Statically gate dev path so the production fetch code is not even present in dev builds
    /* eslint-disable no-constant-condition */
    if (import.meta.env.DEV) {
      const eps: string[] = []
      if (qpOverride) eps.push(qpOverride)
      else eps.push(...DEV_WS_ENDPOINTS)
      this.endpoints = eps
      this.dlog('DEV mode endpoints:', this.endpoints)
      this.connectIfNeeded()
      return
    }
    // Production: fetch from device HTTP server
    this.setStatus('connecting')
    ;(async () => {
      try {
        // If override provided, use it directly in prod as well
        if (qpOverride) {
          const ws = qpOverride
          this.endpoints = [ws]
          this.dlog('Using override endpoint:', this.endpoints)
          this.connectIfNeeded()
          return
        }

        // Fetch broker config
        const obj = await fetchJson<any>('/mqttconfig', 2000)
        const broker = String(obj?.mqtt_broker || '')
        const ws = deriveWsUrlFromBrokerUri(broker)
        if (!ws) throw new Error('invalid mqtt_broker in mqttconfig')
        this.endpoints = [ws]
        this.dlog('Fetched endpoints:', this.endpoints)
        this.connectIfNeeded()
      } catch (e: any) {
        this.setError(`mqttconfig fetch failed: ${e?.message || e}`)
      }
    })()
  }

  private handleMessage(topic: string, payload: Uint8Array | string, isRetained?: boolean) {
    const parts = topic.split('/')
    if (parts.length < 3 || parts[0] !== 'sensor') return
    const mac = parts[1].toLowerCase()
    const category = parts[2]

    // Record retained topics so they can be deleted later
    if (isRetained) {
      let set = this.retainedTopicsByMac.get(mac)
      if (!set) {
        set = new Set<string>()
        this.retainedTopicsByMac.set(mac, set)
      }
      set.add(topic)
    }
    // Logs: sensor/$mac/logs/$level -> plain text with ANSI escapes
    if (category === 'logs' && parts.length >= 4) {
      try {
        const level = parts[3]
        const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
        const s = this.ensureSensor(mac)
        if (!s.logs) s.logs = []
        const ts = Date.now()
        s.logs.push({ ts, level, message: text })
        // Enforce FIFO cap
        const MAX_LOGS = 500
        if (s.logs.length > MAX_LOGS) s.logs.splice(0, s.logs.length - MAX_LOGS)
        this.notify()
        return
      } catch (e) {
        this.setError(`log parse error: ${(e as Error).message}`)
      }
    }

    if (category === 'config' && parts[3] === 'current') {
      try {
        const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
        const s = this.ensureSensor(mac)
        if (text.trim().length === 0) {
          s.config = undefined
          s.pendingConfig = false
          this.dlog('Cleared config/current for', mac)
          this.deleteSensorIfEmpty(mac)
          return
        }
        const cfg = JSON.parse(text)
        s.config = cfg
        s.pendingConfig = false
        this.notify()
        this.dlog('Updated config/current for', mac)
      } catch (e) {
        this.setError(`config parse error: ${(e as Error).message}`)
        this.dlog('Config parse error', (e as Error).message)
      }
      return
    }

    if (category === 'metrics' && parts.length >= 4) {
      try {
        const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
        const m = JSON.parse(text) as MetricPayload
        const s = this.ensureSensor(mac)
        const key = m.metric + '|' + JSON.stringify(m.tags || {})
        s.metrics[key] = { raw: m }
        this.notify()
      } catch (e) {
        this.setError(`metric parse error: ${(e as Error).message}`)
      }
      return
    }

    if (category === 'ip' && parts.length === 3) {
      try {
        const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
        const s = this.ensureSensor(mac)
        if (text.trim().length === 0) {
          s.ip = undefined
          this.dlog('Cleared ip for', mac)
          this.deleteSensorIfEmpty(mac)
          return
        }
        s.ip = text
        this.notify()
        this.dlog('Updated ip for', mac, text)
      } catch (e) {
        this.setError(`ip parse error: ${(e as Error).message}`)
        this.dlog('IP parse error', (e as Error).message)
      }
      return
    }

    if (category === 'device' && parts[3] === 'status') {
      try {
        const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
        const s = this.ensureSensor(mac)
        if (text.trim().length === 0) {
          s.deviceStatus = undefined
          s.deviceStatusTs = undefined
          this.dlog('Cleared device status for', mac)
          this.deleteSensorIfEmpty(mac)
          return
        }
        const obj = JSON.parse(text) as Record<string, unknown>
        s.deviceStatus = obj
        s.deviceStatusTs = Date.now()
        this.notify()
        this.dlog('Device status updated for', mac)
      } catch (e) {
        this.setError(`status parse error: ${(e as Error).message}`)
        this.dlog('Status parse error', (e as Error).message)
      }
      return
    }

    if (category === 'device' && parts[3] === 'ota') {
      try {
        const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
        const s = this.ensureSensor(mac)
        if (text.trim().length === 0) {
          s.otaStatus = undefined
          s.otaStatusTs = undefined
          this.dlog('Cleared OTA status for', mac)
          this.deleteSensorIfEmpty(mac)
          return
        }
        const obj = JSON.parse(text) as Record<string, unknown>
        s.otaStatus = obj
        s.otaStatusTs = Date.now()
        this.notify()
        this.dlog('OTA status updated for', mac)
      } catch (e) {
        this.setError(`ota parse error: ${(e as Error).message}`)
        this.dlog('OTA parse error', (e as Error).message)
      }
      return
    }

    if (category === 'device' && parts[3] === 'i2c') {
      try {
        const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
        const s = this.ensureSensor(mac)
        if (text.trim().length === 0) {
          s.i2c = undefined
          this.dlog('Cleared I2C inventory for', mac)
          this.deleteSensorIfEmpty(mac)
          return
        }
        const obj = JSON.parse(text) as { ts?: string; sensors?: Array<{ addr: string; driver?: string; index?: number; module?: string }> }
        s.i2c = { ts: obj.ts, devices: obj.sensors || [] }
        this.notify()
        this.dlog('I2C inventory updated for', mac)
      } catch (e) {
        this.setError(`i2c parse error: ${(e as Error).message})`)
        this.dlog('I2C parse error', (e as Error).message)
      }
      return
    }

    if (category === 'device' && parts[3] === 'boot') {
      try {
        const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
        const s = this.ensureSensor(mac)
        if (text.trim().length === 0) {
          s.deviceBoot = undefined
          s.deviceBootTs = undefined
          this.dlog('Cleared boot info for', mac)
          this.deleteSensorIfEmpty(mac)
          return
        }
        const obj = JSON.parse(text) as Record<string, unknown>
        s.deviceBoot = obj
        s.deviceBootTs = Date.now()
        this.notify()
        this.dlog('Boot info updated for', mac)
      } catch (e) {
        this.setError(`boot parse error: ${(e as Error).message}`)
        this.dlog('Boot parse error', (e as Error).message)
      }
      return
    }
  }

  publishConfig = (mac: string, moduleName: string, configName: string, value: string | number | boolean) => {
    if (!this.client) return
    const topic = `sensor/${mac.toLowerCase()}/config/${moduleName}/${configName}`
    const payload = String(value)
    try {
      this.client.publish(topic, payload)
      this.dlog('Publish', topic, payload)
      const s = this.ensureSensor(mac)
      s.pendingConfig = true
      this.notify()
    } catch (e) {
      this.setError((e as Error).message)
      this.dlog('Publish error', (e as Error).message)
    }
  }

  deleteRetainedForSensor = (mac: string) => {
    if (!this.client) return
    const key = mac.toLowerCase()
    const topics = this.retainedTopicsByMac.get(key)
    if (!topics || topics.size === 0) return
    for (const t of topics) {
      try {
        this.client.publish(t, '', { retain: true, qos: 0 })
        this.dlog('Delete retained', t)
      } catch (e) {
        this.setError((e as Error).message)
        this.dlog('Delete retained error', (e as Error).message)
      }
    }
    this.retainedTopicsByMac.delete(key)
  }

  private static randomId = Math.random().toString(16).slice(2)
}

const manager = new MqttManager()

export function useSensors(): UseSensorsResult {
  const [version, setVersion] = useState(0)
  const [status, setStatus] = useState<UseSensorsResult['connectionStatus']>(manager.status)
  const [lastError, setLastError] = useState<string | undefined>(manager.lastError)

  useEffect(() => {
    const unsubscribe = manager.subscribe(() => {
      setVersion(manager.version)
      setStatus(manager.status)
      setLastError(manager.lastError)
    })
    return unsubscribe
  }, [])

  const sensors = useMemo(() => manager.sensors, [version])
  return {
    sensors,
    connectionStatus: status,
    lastError,
    publishConfig: manager.publishConfig,
    deleteRetainedForSensor: manager.deleteRetainedForSensor,
  }
}


