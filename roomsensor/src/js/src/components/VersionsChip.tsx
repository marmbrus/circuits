import { Chip, Dialog, DialogContent, DialogTitle, Stack, Typography } from '@mui/material'
import { useState } from 'react'
import type { SensorsMap, SensorState } from '../types'

type Props = {
  sensors: SensorsMap
}

function labelForSensor(s: SensorState): string {
  const t = s.config?.tags
  const area = t?.area || 'unknown'
  const room = t?.room || 'unknown'
  const id = t?.id || s.mac.slice(-4)
  return `${area}-${room}-${id}`
}

export default function VersionsChip({ sensors }: Props) {
  const [open, setOpen] = useState(false)

  // Compute on every render so it reacts to sensor content changes;
  // cost is small for expected sensor counts.
  const fwSet = new Set<string>()
  const webSet = new Set<string>()
  const fwMap = new Map<string, string[]>()
  const webMap = new Map<string, string[]>()
  for (const [, s] of sensors) {
    const o = (s.otaStatus || {}) as Record<string, unknown>
    const fw = String(o.firmware_local_version || '')
    const web = String(o.web_local_version || '')
    if (fw) {
      fwSet.add(fw)
      if (!fwMap.has(fw)) fwMap.set(fw, [])
      fwMap.get(fw)!.push(labelForSensor(s))
    }
    if (web) {
      webSet.add(web)
      if (!webMap.has(web)) webMap.set(web, [])
      webMap.get(web)!.push(labelForSensor(s))
    }
  }
  for (const list of fwMap.values()) list.sort((a, b) => a.localeCompare(b))
  for (const list of webMap.values()) list.sort((a, b) => a.localeCompare(b))
  const fwVersions = Array.from(fwSet).sort()
  const webVersions = Array.from(webSet).sort()
  const unionCount = new Set<string>([...fwSet, ...webSet]).size

  return (
    <>
      <Chip
        label={`${unionCount} versions`}
        size="small"
        variant="outlined"
        onClick={() => setOpen(true)}
      />
      <Dialog open={open} onClose={() => setOpen(false)} fullWidth maxWidth="sm">
        <DialogTitle>Deployed versions</DialogTitle>
        <DialogContent>
          <Typography variant="body2" sx={{ mb: 2 }}>
            Firmware: {fwVersions.length} • Web: {webVersions.length}
          </Typography>
          <Stack spacing={2} sx={{ mb: 1 }}>
            <div>
              <Typography variant="subtitle2" sx={{ mb: 0.5 }}>Firmware</Typography>
              <Stack spacing={1}>
                {fwVersions.length === 0 ? (
                  <Typography variant="body2" color="text.secondary">No data</Typography>
                ) : fwVersions.map((v) => (
                  <div key={`fw-${v}`}>
                    <Typography variant="body2" sx={{ fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace' }}>{v}</Typography>
                    <Typography variant="caption" color="text.secondary">{(fwMap.get(v) || []).join(', ')}</Typography>
                  </div>
                ))}
              </Stack>
            </div>
            <div>
              <Typography variant="subtitle2" sx={{ mb: 0.5 }}>Web</Typography>
              <Stack spacing={1}>
                {webVersions.length === 0 ? (
                  <Typography variant="body2" color="text.secondary">No data</Typography>
                ) : webVersions.map((v) => (
                  <div key={`web-${v}`}>
                    <Typography variant="body2" sx={{ fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace' }}>{v}</Typography>
                    <Typography variant="caption" color="text.secondary">{(webMap.get(v) || []).join(', ')}</Typography>
                  </div>
                ))}
              </Stack>
            </div>
          </Stack>
        </DialogContent>
      </Dialog>
    </>
  )
}


