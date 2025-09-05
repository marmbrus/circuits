import { useEffect, useState } from 'react'

export function formatDuration(ms: number): string {
  const totalSeconds = Math.max(0, Math.floor(ms / 1000))
  if (totalSeconds < 60) return `${totalSeconds}s`
  const totalMinutes = Math.floor(totalSeconds / 60)
  if (totalMinutes < 60) return `${totalMinutes}m`
  const totalHours = Math.floor(totalMinutes / 60)
  if (totalHours < 24) return `${totalHours}h ${totalMinutes % 60}m`
  const days = Math.floor(totalHours / 24)
  return `${days}d ${totalHours % 24}h`
}

type Props = {
  fromMs: number
  prefix?: string
  emptyLabel?: string
}

export default function FriendlyDuration({ fromMs, prefix, emptyLabel }: Props) {
  const [, setTick] = useState(0)
  useEffect(() => {
    const t = setInterval(() => setTick((v) => (v + 1) % 1_000_000), 1000)
    return () => clearInterval(t)
  }, [])

  if (!fromMs || fromMs <= 0) return <span>{emptyLabel ?? ''}</span>
  const delta = Date.now() - fromMs
  const text = `${prefix ?? ''}${prefix ? ' ' : ''}${formatDuration(delta)}`
  return <span>{text}</span>
}


