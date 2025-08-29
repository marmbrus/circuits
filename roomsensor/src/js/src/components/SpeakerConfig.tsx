import { Stack, Chip, Typography, Button, TextField } from '@mui/material'
import { useState } from 'react'

export type OnEdit = (moduleName: string, configName: string, value: unknown) => void

type Props = {
  config: any
  onEdit: (moduleName: string, configName: string, value: unknown) => void
  publish: (moduleName: string, key: string, value: string | number | boolean) => void
}

export default function SpeakerConfig({ config, onEdit, publish }: Props) {
  const sp = (config as any).speaker || {}

  const clear = (name: string) => publish('speaker', name, '')

  return (
    <Stack spacing={1}>
      <Typography variant="body2">Pins</Typography>
      <Stack direction="row" spacing={1}>
        <Chip label={`SDIN: ${sp.sdin ?? '-'}`} size="small" onClick={() => onEdit('speaker', 'sdin', sp.sdin ?? '')} />
        <Chip label={`SCLK: ${sp.sclk ?? '-'}`} size="small" onClick={() => onEdit('speaker', 'sclk', sp.sclk ?? '')} />
        <Chip label={`LRCLK: ${sp.lrclk ?? '-'}`} size="small" onClick={() => onEdit('speaker', 'lrclk', sp.lrclk ?? '')} />
      </Stack>

      <Typography variant="body2" sx={{ mt: 1 }}>Playback</Typography>
      <Stack direction="row" spacing={1} alignItems="center" flexWrap="wrap">
        <Chip label={`Sine: ${sp.sine ?? '-'}`} size="small" onClick={() => onEdit('speaker', 'sine', sp.sine ?? '')} onDelete={sp.sine !== undefined ? () => clear('sine') : undefined} />
        <Chip label={`Volume: ${sp.volume ?? '-'}`} size="small" onClick={() => onEdit('speaker', 'volume', sp.volume ?? '')} onDelete={sp.volume !== undefined ? () => clear('volume') : undefined} />
        <Chip label={`URL: ${sp.url ?? '-'}`} size="small" onClick={() => onEdit('speaker', 'url', sp.url ?? '')} onDelete={sp.url ? () => clear('url') : undefined} />
      </Stack>
    </Stack>
  )
}


