import { Box, Chip, Stack, Typography } from '@mui/material'
import { Box as MuiBox } from '@mui/material'
import type { ControlSpec } from './ConfigEditor'

type Props = {
  config: Record<string, any>
  onEdit: (moduleName: string, key: string, value: unknown, control?: ControlSpec) => void
}

export default function LEDConfigSection({ config, onEdit }: Props) {
  const ledKeys = Object.keys(config).filter((k) => k.startsWith('led'))
  if (ledKeys.length === 0) return null
  return (
    <MuiBox sx={{ display: 'grid', gridTemplateColumns: { xs: '1fr', sm: '1fr 1fr', md: '1fr 1fr 1fr' }, gap: 1 }}>
      {ledKeys.map((k) => {
        const led = (config as Record<string, any>)[k] as any
        return (
          <MuiBox key={k}>
            <Box sx={{ border: '1px solid', borderColor: 'divider', p: 1, borderRadius: 1 }}>
              <Typography variant="body2" fontWeight={600}>{k}</Typography>
              <Stack direction="row" spacing={1} flexWrap="wrap" useFlexGap>
                {'chip' in led && (
                  <Chip
                    label={`Chip: ${led.chip}`}
                    size="small"
                    onClick={() => onEdit(k, 'chip', led.chip, { type: 'select', options: ['WS2812', 'SK6812'], label: 'Chip' })}
                  />
                )}
                <Chip
                  label={`Pattern: ${'pattern' in led ? led.pattern : 'OFF'}`}
                  size="small"
                  onClick={() => onEdit(k, 'pattern', ('pattern' in led ? led.pattern : 'OFF'), { type: 'select', options: ['OFF','SOLID','FADE','STATUS','RAINBOW','CHASE','LIFE'], label: 'Pattern' })}
                />
                {'dataGPIO' in led && <Chip label={`Data: ${led.dataGPIO}`} size="small" onClick={() => onEdit(k, 'dataGPIO', led.dataGPIO, { type: 'number', label: 'Data GPIO' })} />}                
                {'enabledGPIO' in led && <Chip label={`Enable: ${led.enabledGPIO}`} size="small" onClick={() => onEdit(k, 'enabledGPIO', led.enabledGPIO, { type: 'number', label: 'Enable GPIO' })} />}
                {'num_columns' in led && <Chip label={`Cols: ${led.num_columns}`} size="small" onClick={() => onEdit(k, 'num_columns', led.num_columns, { type: 'number', label: 'Columns' })} />}
                {'num_rows' in led && <Chip label={`Rows: ${led.num_rows}`} size="small" onClick={() => onEdit(k, 'num_rows', led.num_rows, { type: 'number', label: 'Rows' })} />}
                <Chip
                  label={`Brightness: ${('brightness' in led && led.brightness !== undefined) ? led.brightness : '(unset)'}`}
                  size="small"
                  onClick={() => onEdit(k, 'brightness', ('brightness' in led && led.brightness !== undefined) ? led.brightness : 0, { type: 'slider', min: 0, max: 255, step: 1, round: true, commitOnRelease: true, label: 'Brightness' })}
                />
                <Chip
                  label={`Speed: ${('speed' in led && led.speed !== undefined) ? led.speed : '(unset)'}`}
                  size="small"
                  onClick={() => onEdit(k, 'speed', ('speed' in led && led.speed !== undefined) ? led.speed : 50, { type: 'slider', min: 0, max: 100, step: 1, round: true, commitOnRelease: true, label: 'Speed' })}
                />
              </Stack>
            </Box>
          </MuiBox>
        )
      })}
    </MuiBox>
  )
}


