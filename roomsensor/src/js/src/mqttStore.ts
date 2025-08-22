import mqtt from 'mqtt'
import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
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

const ensureSensor = (map: SensorsMap, mac: string): SensorState => {
  let s = map.get(mac)
  if (!s) {
    s = { mac, metrics: {} }
    map.set(mac, s)
  }
  return s
}

export function useSensors(): UseSensorsResult {
  const [status, setStatus] = useState<UseSensorsResult['connectionStatus']>('idle')
  const [lastError, setLastError] = useState<string | undefined>(undefined)
  const [version, setVersion] = useState(0)
  const sensorsRef = useRef<SensorsMap>(new Map())
  const clientRef = useRef<mqtt.MqttClient | null>(null)

  useEffect(() => {
    let connected = false
    let attemptIndex = 0
    let timeout: number | undefined

    const connectNext = () => {
      if (attemptIndex >= WS_ENDPOINTS.length) {
        setStatus('error')
        setLastError('All websocket endpoints failed')
        return
      }
      const url = WS_ENDPOINTS[attemptIndex++]
      setStatus('connecting')
      const c = mqtt.connect(url, {
        clientId: `roomsensor-ui-${Math.random().toString(16).slice(2)}`,
        clean: true,
        reconnectPeriod: 0,
        protocolVersion: 4,
      })
      clientRef.current = c

      c.on('connect', () => {
        connected = true
        setStatus('connected')
        c.subscribe('sensor/#', (err) => {
          if (err) setLastError(err.message)
        })
      })

      c.on('message', (topic, payload) => {
        try {
          handleMessage(topic.toString(), payload)
        } catch (e) {
          setLastError((e as Error).message)
        }
      })

      c.on('reconnect', () => setStatus('reconnecting'))
      c.on('close', () => setStatus('closed'))
      c.on('error', (err) => setLastError(err.message))

      // probe timeout
      // @ts-expect-error setTimeout typing in DOM lib returns number
      timeout = setTimeout(() => {
        if (!connected) {
          try { c.end(true) } catch { /* noop */ }
          connectNext()
        }
      }, 2000)
      c.on('connect', () => { if (timeout) clearTimeout(timeout) })
      c.on('close', () => { if (connected && timeout) clearTimeout(timeout) })
    }

    const handleMessage = (topic: string, payload: Uint8Array | string) => {
      const map = sensorsRef.current

      // topic formats we care about
      // sensor/<mac>/config/current
      // sensor/<mac>/metrics/<metric>
      // sensor/<mac>/ip
      // sensor/<mac>/device/status
      // sensor/<mac>/device/ota
      const parts = topic.split('/')
      if (parts.length < 3 || parts[0] !== 'sensor') return
      const mac = parts[1].toLowerCase()
      const category = parts[2]

      if (category === 'config' && parts[3] === 'current') {
        try {
          const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
          const cfg = JSON.parse(text)
          const s = ensureSensor(map, mac)
          s.config = cfg
          s.pendingConfig = false
          setVersion((v) => v + 1)
        } catch (e) {
          setLastError(`config parse error: ${(e as Error).message}`)
        }
        return
      }

      if (category === 'metrics' && parts.length >= 4) {
        try {
          const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
          const m = JSON.parse(text) as MetricPayload
          const s = ensureSensor(map, mac)
          const key = m.metric + '|' + JSON.stringify(m.tags || {})
          s.metrics[key] = { value: m.value, ts: Date.now(), tags: m.tags }
          setVersion((v) => v + 1)
        } catch (e) {
          setLastError(`metric parse error: ${(e as Error).message}`)
        }
        return
      }

      if (category === 'ip' && parts.length === 3) {
        try {
          const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
          const s = ensureSensor(map, mac)
          s.ip = text
          setVersion((v) => v + 1)
        } catch (e) {
          setLastError(`ip parse error: ${(e as Error).message}`)
        }
        return
      }

      if (category === 'device' && parts[3] === 'status') {
        try {
          const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
          const obj = JSON.parse(text) as Record<string, unknown>
          const s = ensureSensor(map, mac)
          s.deviceStatus = obj
          s.deviceStatusTs = Date.now()
          setVersion((v) => v + 1)
        } catch (e) {
          setLastError(`status parse error: ${(e as Error).message}`)
        }
        return
      }

      if (category === 'device' && parts[3] === 'ota') {
        try {
          const text = typeof payload === 'string' ? payload : new TextDecoder().decode(payload)
          const obj = JSON.parse(text) as Record<string, unknown>
          const s = ensureSensor(map, mac)
          s.otaStatus = obj
          s.otaStatusTs = Date.now()
          setVersion((v) => v + 1)
        } catch (e) {
          setLastError(`ota parse error: ${(e as Error).message}`)
        }
        return
      }
    }

    connectNext()

    return () => {
      if (clientRef.current) {
        try { clientRef.current.end(true) } catch { /* noop */ }
      }
      if (timeout) clearTimeout(timeout)
    }
  }, [])

  const sensors = useMemo(() => sensorsRef.current, [version])
  const publishConfig = useCallback((mac: string, moduleName: string, configName: string, value: string | number | boolean) => {
    const c = clientRef.current
    if (!c) return
    const topic = `sensor/${mac.toLowerCase()}/config/${moduleName}/${configName}`
    const payload = String(value)
    try {
      c.publish(topic, payload)
      // mark pending; clear when config/current arrives
      const s = ensureSensor(sensorsRef.current, mac)
      s.pendingConfig = true
      setVersion((v) => v + 1)
    } catch (e) {
      setLastError((e as Error).message)
    }
  }, [])

  return { sensors, connectionStatus: status, lastError, publishConfig }
}


