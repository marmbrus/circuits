import { Box, Chip, Stack, Typography, Switch, FormControlLabel } from '@mui/material'
import { Box as MuiBox } from '@mui/material'
import type { ControlSpec } from './ConfigEditor'


type Props = {
  modules: string[]
  config: Record<string, any>
  onEdit: (moduleName: string, key: string, value: unknown, control?: ControlSpec) => void
  publish: (moduleName: string, key: string, value: string | number | boolean) => void
}

// IOConfig supports pin1config..pin8config (persisted enum: SWITCH|SENSOR) and pin1switch..pin8switch (non-persisted boolean)
export default function IOConfigSection({ modules, config, onEdit, publish }: Props) {
  if (modules.length === 0) return null
  const pinModes = ['', 'SWITCH', 'SWITCH_HIGH', 'SWITCH_LOW', 'SENSOR']
  const logicOptions = ['', 'NONE', 'LOCK_KEYPAD']
  return (
    <MuiBox sx={{ display: 'grid', gridTemplateColumns: '1fr', gap: 1, width: '100%' }}>
      {modules.map((mod) => {
        const io = (config as Record<string, any>)[mod] || {}
        const pins = Array.from({ length: 8 }, (_, i) => i + 1)
        return (
          <MuiBox key={mod} sx={{ width: '100%' }}>
            <Box sx={{ border: '1px solid', borderColor: 'divider', p: 1, borderRadius: 1, width: '100%' }}>
              <Typography variant="body2" fontWeight={600}>{mod}</Typography>
              <Stack direction="row" spacing={0.75} alignItems="center" sx={{ mt: 0.5 }}>
                <Chip
                  label={`logic: ${io['logic'] ?? '(unset)'}`}
                  size="small"
                  onClick={(e) => { (e.currentTarget as HTMLDivElement).blur?.(); onEdit(mod, 'logic', io['logic'] ?? '', { type: 'select', options: logicOptions, label: 'logic' }) }}
                />
              </Stack>
              <Stack spacing={0.5} sx={{ mt: 0.5 }}>
                {pins.map((idx) => {
                  const pinKey = `pin${idx}config`
                  const switchKey = `pin${idx}switch`
                  const nameKey = `pin${idx}name`
                  const mode = io[pinKey]
                  const sw = io[switchKey]
                  const pname = io[nameKey]
                  return (
                    <Stack key={pinKey} direction="row" spacing={0.75} alignItems="center" flexWrap="nowrap" useFlexGap sx={{ overflowX: 'auto', pr: 1 }}>
                      <Typography variant="caption" color="text.secondary" sx={{ whiteSpace: 'nowrap' }}>pin{idx}</Typography>
                      <Chip label={`name: ${pname ?? '(unset)'}`} size="small" onClick={(e) => { (e.currentTarget as HTMLDivElement).blur?.(); onEdit(mod, nameKey, pname ?? '', { type: 'text', label: nameKey }) }} />
                      <Chip label={`mode: ${mode ?? '(unset)'}`} size="small" onClick={(e) => { (e.currentTarget as HTMLDivElement).blur?.(); onEdit(mod, pinKey, mode ?? '', { type: 'select', options: pinModes, label: pinKey }) }} />
                      {mode === 'SWITCH' || mode === 'SWITCH_HIGH' || mode === 'SWITCH_LOW' ? (
                        <FormControlLabel
                          sx={{ ml: 1, whiteSpace: 'nowrap' }}
                          control={<Switch size="small" checked={Boolean(sw)} onChange={(e) => publish(mod, switchKey, e.target.checked)} />}
                          label={<Typography variant="caption" color="text.secondary">switch</Typography>}
                        />
                      ) : null}
                    </Stack>
                  )
                })}
              </Stack>
            </Box>
          </MuiBox>
        )
      })}
    </MuiBox>
  )
}
