import { Box, Chip, Stack, Typography, Slider } from '@mui/material'
import { Box as MuiBox } from '@mui/material'
import type { ControlSpec } from './ConfigEditor'

type Props = {
  config: Record<string, any>
  onEdit: (moduleName: string, key: string, value: unknown, control?: ControlSpec) => void
  publish?: (moduleName: string, key: string, value: string | number | boolean) => void
}

export default function LEDConfigSection({ config, onEdit, publish }: Props) {
  const ledKeys = Object.keys(config).filter((k) => k.startsWith('led'))
  if (ledKeys.length === 0) return null
  return (
    <MuiBox sx={{ display: 'grid', gridTemplateColumns: { xs: '1fr', sm: '1fr 1fr', md: '1fr 1fr 1fr' }, gap: 0.75 }}>
      {ledKeys.map((k) => {
        const led = (config as Record<string, any>)[k] as any
        return (
          <MuiBox key={k}>
            <Box sx={{ border: '1px solid', borderColor: 'divider', p: 0.75, borderRadius: 1 }}>
              <Typography variant="body2" fontWeight={600} sx={{ mb: 0.25 }}>{k}</Typography>
              <Stack direction="row" spacing={1} flexWrap="wrap" useFlexGap>
                {'chip' in led && (
                  <Chip
                    label={`Chip: ${led.chip}`}
                    size="small"
                    onClick={() => onEdit(k, 'chip', led.chip, { type: 'select', options: ['WS2812', 'SK6812', 'WS2814'], label: 'Chip' })}
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
              </Stack>
              <Stack spacing={0.75} sx={{ mt: 0.5 }}>
                <Box>
                  <Typography variant="caption">Brightness ({('brightness' in led && led.brightness !== undefined) ? led.brightness : 100})</Typography>
                  <Slider
                    size="small"
                    min={0}
                    max={100}
                    step={1}
                    value={('brightness' in led && led.brightness !== undefined) ? led.brightness : 100}
                    onChangeCommitted={(_, v) => publish ? publish(k, 'brightness', Math.round(v as number)) : onEdit(k, 'brightness', Math.round(v as number))}
                  />
                </Box>
                <Box>
                  <Typography variant="caption">Speed ({('speed' in led && led.speed !== undefined) ? led.speed : 50})</Typography>
                  <Slider
                    size="small"
                    min={0}
                    max={100}
                    step={1}
                    value={('speed' in led && led.speed !== undefined) ? led.speed : 50}
                    onChangeCommitted={(_, v) => publish ? publish(k, 'speed', Math.round(v as number)) : onEdit(k, 'speed', Math.round(v as number))}
                  />
                </Box>
                <Box>
                  <Typography variant="caption">Red ({('R' in led && led.R !== undefined) ? led.R : 0})</Typography>
                  <Slider
                    size="small"
                    min={0}
                    max={255}
                    step={1}
                    value={('R' in led && led.R !== undefined) ? led.R : 0}
                    onChangeCommitted={(_, v) => publish ? publish(k, 'R', Math.round(v as number)) : onEdit(k, 'R', Math.round(v as number))}
                  />
                </Box>
                <Box>
                  <Typography variant="caption">Green ({('G' in led && led.G !== undefined) ? led.G : 0})</Typography>
                  <Slider
                    size="small"
                    min={0}
                    max={255}
                    step={1}
                    value={('G' in led && led.G !== undefined) ? led.G : 0}
                    onChangeCommitted={(_, v) => publish ? publish(k, 'G', Math.round(v as number)) : onEdit(k, 'G', Math.round(v as number))}
                  />
                </Box>
                <Box>
                  <Typography variant="caption">Blue ({('B' in led && led.B !== undefined) ? led.B : 0})</Typography>
                  <Slider
                    size="small"
                    min={0}
                    max={255}
                    step={1}
                    value={('B' in led && led.B !== undefined) ? led.B : 0}
                    onChangeCommitted={(_, v) => publish ? publish(k, 'B', Math.round(v as number)) : onEdit(k, 'B', Math.round(v as number))}
                  />
                </Box>
                {(led.chip === 'SK6812' || led.chip === 'WS2814') && (
                  <Box>
                    <Typography variant="caption">White ({('W' in led && led.W !== undefined) ? led.W : 0})</Typography>
                    <Slider
                      size="small"
                      min={0}
                      max={255}
                      step={1}
                      value={('W' in led && led.W !== undefined) ? led.W : 0}
                      onChangeCommitted={(_, v) => publish ? publish(k, 'W', Math.round(v as number)) : onEdit(k, 'W', Math.round(v as number))}
                    />
                  </Box>
                )}
              </Stack>
            </Box>
          </MuiBox>
        )
      })}
    </MuiBox>
  )
}


