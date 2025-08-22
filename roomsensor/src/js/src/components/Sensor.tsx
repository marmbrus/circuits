import { Card, CardContent, CardHeader, Chip, CircularProgress, Divider, IconButton, Link, Stack, Tooltip, Typography } from '@mui/material'
import ContentCopyIcon from '@mui/icons-material/ContentCopy'
import { useState } from 'react'
import type { SensorState } from '../types'
import { useSensors } from '../mqttStore'
import SensorConfigView from './SensorConfig'

type Props = {
  sensor: SensorState
}

export default function Sensor({ sensor }: Props) {
  const cfg = sensor.config
  const { publishConfig } = useSensors()
  const id = cfg?.tags.id ?? sensor.mac
  const macShort = sensor.mac.slice(-4)
  const ip = sensor.ip
  const [copied, setCopied] = useState(false)

  const handleCopy = async () => {
    try {
      await navigator.clipboard.writeText(sensor.mac)
      setCopied(true)
      setTimeout(() => setCopied(false), 1200)
    } catch {
      // ignore
    }
  }
  return (
    <Card variant="outlined">
      <CardHeader
        title={id}
        subheader={
          <Stack direction="row" spacing={1} alignItems="center">
            <Tooltip title={sensor.mac} placement="top">
              <Typography variant="body2">{macShort}</Typography>
            </Tooltip>
            {ip && (
              <Link variant="body2" href={`https://${ip}`} target="_blank" rel="noreferrer">{ip}</Link>
            )}
            {sensor.pendingConfig && (
              <Stack direction="row" spacing={1} alignItems="center">
                <CircularProgress size={14} />
                <Typography variant="caption" color="text.secondary">applying…</Typography>
              </Stack>
            )}
          </Stack>
        }
        action={
          <Tooltip title={copied ? 'Copied' : 'Copy MAC'}>
            <IconButton size="small" onClick={handleCopy} aria-label="copy mac">
              <ContentCopyIcon fontSize="small" />
            </IconButton>
          </Tooltip>
        }
      />
      <CardContent>
        <Stack spacing={1}>
          {cfg && <SensorConfigView mac={sensor.mac} config={cfg} publishConfig={publishConfig} />}
          <Divider />
          <Typography variant="subtitle2">Latest metrics</Typography>
          <Stack direction="row" spacing={1} flexWrap="wrap" useFlexGap>
            {Object.entries(sensor.metrics).map(([key, m]) => (
              <Chip key={key} label={`${m.tags.type ?? m.tags.name ?? 'metric'}:${key.split('|')[0]}=${m.value.toFixed(3)}`} size="small" />
            ))}
          </Stack>
        </Stack>
      </CardContent>
    </Card>
  )
}


