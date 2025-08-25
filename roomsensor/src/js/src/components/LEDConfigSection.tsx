import { Box, Chip, Stack, Typography, Slider } from '@mui/material'
import { Box as MuiBox } from '@mui/material'
import { useState, useEffect, useCallback, useRef } from 'react'
import type { ControlSpec } from './ConfigEditor'

type Props = {
  config: Record<string, any>
  onEdit: (moduleName: string, key: string, value: unknown, control?: ControlSpec) => void
  publish?: (moduleName: string, key: string, value: string | number | boolean) => void
}

// Custom hook for managing slider state with debounced updates
function useSliderValue(
  initialValue: number,
  onUpdate: (value: number) => void,
  debounceMs: number = 300
) {
  const [localValue, setLocalValue] = useState(initialValue)
  const timeoutRef = useRef<NodeJS.Timeout | null>(null)

  // Update local value when prop changes
  useEffect(() => {
    setLocalValue(initialValue)
  }, [initialValue])

  const debouncedUpdate = useCallback((value: number) => {
    if (timeoutRef.current) {
      clearTimeout(timeoutRef.current)
    }
    timeoutRef.current = setTimeout(() => {
      onUpdate(value)
    }, debounceMs)
  }, [onUpdate, debounceMs])

  const handleChange = useCallback((value: number) => {
    setLocalValue(value)
    debouncedUpdate(value)
  }, [debouncedUpdate])

  const handleChangeCommitted = useCallback((value: number) => {
    if (timeoutRef.current) {
      clearTimeout(timeoutRef.current)
    }
    onUpdate(value)
  }, [onUpdate])

  // Cleanup timeout on unmount
  useEffect(() => {
    return () => {
      if (timeoutRef.current) {
        clearTimeout(timeoutRef.current)
      }
    }
  }, [])

  return {
    value: localValue,
    onChange: handleChange,
    onChangeCommitted: handleChangeCommitted
  }
}

// Component for individual LED configuration with smooth sliders
function LEDCard({ ledKey, led, onEdit, publish }: {
  ledKey: string
  led: any
  onEdit: (moduleName: string, key: string, value: unknown, control?: ControlSpec) => void
  publish?: (moduleName: string, key: string, value: string | number | boolean) => void
}) {
  // Create slider handlers for each property
  const brightnessSlider = useSliderValue(
    ('brightness' in led && led.brightness !== undefined) ? led.brightness : 100,
    (value) => publish ? publish(ledKey, 'brightness', value) : onEdit(ledKey, 'brightness', value)
  )
  
  const speedSlider = useSliderValue(
    ('speed' in led && led.speed !== undefined) ? led.speed : 50,
    (value) => publish ? publish(ledKey, 'speed', value) : onEdit(ledKey, 'speed', value)
  )
  
  const redSlider = useSliderValue(
    ('R' in led && led.R !== undefined) ? led.R : 0,
    (value) => publish ? publish(ledKey, 'R', value) : onEdit(ledKey, 'R', value)
  )
  
  const greenSlider = useSliderValue(
    ('G' in led && led.G !== undefined) ? led.G : 0,
    (value) => publish ? publish(ledKey, 'G', value) : onEdit(ledKey, 'G', value)
  )
  
  const blueSlider = useSliderValue(
    ('B' in led && led.B !== undefined) ? led.B : 0,
    (value) => publish ? publish(ledKey, 'B', value) : onEdit(ledKey, 'B', value)
  )
  
  const whiteSlider = useSliderValue(
    ('W' in led && led.W !== undefined) ? led.W : 0,
    (value) => publish ? publish(ledKey, 'W', value) : onEdit(ledKey, 'W', value)
  )

  return (
    <Box sx={{ border: '1px solid', borderColor: 'divider', p: 0.75, borderRadius: 1 }}>
      <Typography variant="body2" fontWeight={600} sx={{ mb: 0.25 }}>{ledKey}</Typography>
      <Stack direction="row" spacing={1} flexWrap="wrap" useFlexGap>
        {'chip' in led && (
          <Chip
            label={`Chip: ${led.chip}`}
            size="small"
            onClick={() => onEdit(ledKey, 'chip', led.chip, { type: 'select', options: ['WS2812', 'SK6812', 'WS2814', 'FLIPDOT'], label: 'Chip' })}
          />
        )}
        <Chip
          label={`Pattern: ${'pattern' in led ? led.pattern : 'OFF'}`}
          size="small"
          onClick={() => onEdit(ledKey, 'pattern', ('pattern' in led ? led.pattern : 'OFF'), { type: 'select', options: ['OFF','SOLID','FADE','STATUS','RAINBOW','CHASE','LIFE'], label: 'Pattern' })}
        />
        {'dataGPIO' in led && <Chip label={`Data: ${led.dataGPIO}`} size="small" onClick={() => onEdit(ledKey, 'dataGPIO', led.dataGPIO, { type: 'number', label: 'Data GPIO' })} />}                
        {'enabledGPIO' in led && <Chip label={`Enable: ${led.enabledGPIO}`} size="small" onClick={() => onEdit(ledKey, 'enabledGPIO', led.enabledGPIO, { type: 'number', label: 'Enable GPIO' })} />}
        {'num_columns' in led && <Chip label={`Cols: ${led.num_columns}`} size="small" onClick={() => onEdit(ledKey, 'num_columns', led.num_columns, { type: 'number', label: 'Columns' })} />}
        {'num_rows' in led && <Chip label={`Rows: ${led.num_rows}`} size="small" onClick={() => onEdit(ledKey, 'num_rows', led.num_rows, { type: 'number', label: 'Rows' })} />}
      </Stack>
      <Stack spacing={0.75} sx={{ mt: 0.5 }}>
        <Box>
          <Typography variant="caption">Brightness ({brightnessSlider.value})</Typography>
          <Slider
            size="small"
            min={0}
            max={100}
            step={1}
            value={brightnessSlider.value}
            onChange={(_, v) => brightnessSlider.onChange(Math.round(v as number))}
            onChangeCommitted={(_, v) => brightnessSlider.onChangeCommitted(Math.round(v as number))}
          />
        </Box>
        <Box>
          <Typography variant="caption">Speed ({speedSlider.value})</Typography>
          <Slider
            size="small"
            min={0}
            max={100}
            step={1}
            value={speedSlider.value}
            onChange={(_, v) => speedSlider.onChange(Math.round(v as number))}
            onChangeCommitted={(_, v) => speedSlider.onChangeCommitted(Math.round(v as number))}
          />
        </Box>
        <Box>
          <Typography variant="caption">Red ({redSlider.value})</Typography>
          <Slider
            size="small"
            min={0}
            max={255}
            step={1}
            value={redSlider.value}
            onChange={(_, v) => redSlider.onChange(Math.round(v as number))}
            onChangeCommitted={(_, v) => redSlider.onChangeCommitted(Math.round(v as number))}
          />
        </Box>
        <Box>
          <Typography variant="caption">Green ({greenSlider.value})</Typography>
          <Slider
            size="small"
            min={0}
            max={255}
            step={1}
            value={greenSlider.value}
            onChange={(_, v) => greenSlider.onChange(Math.round(v as number))}
            onChangeCommitted={(_, v) => greenSlider.onChangeCommitted(Math.round(v as number))}
          />
        </Box>
        <Box>
          <Typography variant="caption">Blue ({blueSlider.value})</Typography>
          <Slider
            size="small"
            min={0}
            max={255}
            step={1}
            value={blueSlider.value}
            onChange={(_, v) => blueSlider.onChange(Math.round(v as number))}
            onChangeCommitted={(_, v) => blueSlider.onChangeCommitted(Math.round(v as number))}
          />
        </Box>
        {(led.chip === 'SK6812' || led.chip === 'WS2814') && (
          <Box>
            <Typography variant="caption">White ({whiteSlider.value})</Typography>
            <Slider
              size="small"
              min={0}
              max={255}
              step={1}
              value={whiteSlider.value}
              onChange={(_, v) => whiteSlider.onChange(Math.round(v as number))}
              onChangeCommitted={(_, v) => whiteSlider.onChangeCommitted(Math.round(v as number))}
            />
          </Box>
        )}
      </Stack>
    </Box>
  )
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
            <LEDCard ledKey={k} led={led} onEdit={onEdit} publish={publish} />
          </MuiBox>
        )
      })}
    </MuiBox>
  )
}


