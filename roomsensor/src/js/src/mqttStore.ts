import mqtt, { type MqttClient } from 'mqtt'
import { useEffect, useMemo, useState } from 'react'
import type { MetricPayload, SensorsMap, SensorState } from './types'

export type UseSensorsResult = {
  sensors: SensorsMap
  connectionStatus: 'idle' | 'connecting' | 'connected' | 'reconnecting' | 'closed' | 'error'
  lastError?: string
  publishConfig: (mac: string, moduleName: string, configName: string, value: string | number | boolean) => void
}

const WS_ENDPOINTS = [
  'ws://gaia.home:9001/',
]

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

  public sensors: SensorsMap = new Map()
  public status: UseSensorsResult['connectionStatus'] = 'idle'
  public lastError: string | undefined
  public version = 0

  private listeners = new Set<() => void>()
  private readonly debugLevel = getDebugLevel()

  private dlog = (...args: unknown[]) => {
    if (this.debugLevel > 0) {
      // eslint-disable-next-line no-console
      console.debug('[MQTT]', ...args)
    }
  }

  subscribe(listener: () => void): () => void {
    this.listeners.add(listener)
    // Ensure connection is started
    this.connectIfNeeded()
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
    this.connecting = true
    this.attemptIndex = 0
    this.setStatus('connecting')
    this.tryConnectNext()
  }

  private tryConnectNext() {
    if (this.attemptIndex >= WS_ENDPOINTS.length) {
      this.connecting = false
      this.setStatus('error')
      this.setError('All websocket endpoints failed')
      this.dlog('All websocket endpoints failed')
      return
    }
    const url = WS_ENDPOINTS[this.attemptIndex++]
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

    c.on('message', (topic, payload) => {
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
        this.handleMessage(topic.toString(), payload)
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

  private handleMessage(topic: string, payload: Uint8Array | string) {
    const parts = topic.split('/')
    if (parts.length < 3 || parts[0] !== 'sensor') return
    const mac = parts[1].toLowerCase()
    const category = parts[2]

    if (category === 'config' && parts[3] === 'current') {
      try {
        const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
        const cfg = JSON.parse(text)
        const s = this.ensureSensor(mac)
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
        s.metrics[key] = { value: m.value, ts: Date.now(), tags: m.tags }
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
        const obj = JSON.parse(text) as Record<string, unknown>
        const s = this.ensureSensor(mac)
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
        const obj = JSON.parse(text) as Record<string, unknown>
        const s = this.ensureSensor(mac)
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
        const obj = JSON.parse(text) as { ts?: string; sensors?: Array<{ addr: string; driver?: string; index?: number; module?: string }> }
        const s = this.ensureSensor(mac)
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
        const obj = JSON.parse(text) as Record<string, unknown>
        const s = this.ensureSensor(mac)
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
  }
}


