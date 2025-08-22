import { Fragment } from 'react'
import { Box, Chip, Divider, Stack, Typography } from '@mui/material'
import { Box as MuiBox } from '@mui/material'
import type { SensorConfig } from '../types'
import { useState } from 'react'
import ConfigEditor, { type ConfigField } from './ConfigEditor'

type Props = {
  mac: string
  config: SensorConfig
  publishConfig: (mac: string, moduleName: string, configName: string, value: string | number | boolean) => void
}

export default function SensorConfigView({ mac, config, publishConfig }: Props) {
  const [editorOpen, setEditorOpen] = useState(false)
  const [field, setField] = useState<ConfigField | null>(null)

  const openEditor = (moduleName: string, configName: string, value: unknown) => {
    setField({ label: `${moduleName}.${configName}`, moduleName, configName, value })
    setEditorOpen(true)
  }
  const ledKeys = Object.keys(config).filter((k) => k.startsWith('led'))
  return (
    <Stack spacing={1} sx={{ width: '100%' }}>
      <Typography variant="subtitle2">WiFi</Typography>
      <Stack direction="row" spacing={1}>
        <Chip label={`SSID: ${config.wifi.ssid}`} size="small" />
        <Chip label={`Broker: ${config.wifi.mqtt_broker}`} size="small" />
      </Stack>

      <Typography variant="subtitle2">Tags</Typography>
      <Stack direction="row" spacing={1}>
        <Chip label={`Area: ${config.tags.area}`} size="small" onClick={() => openEditor('tags', '*', config.tags)} />
        <Chip label={`Room: ${config.tags.room}`} size="small" onClick={() => openEditor('tags', '*', config.tags)} />
        <Chip label={`ID: ${config.tags.id}`} size="small" onClick={() => openEditor('tags', '*', config.tags)} />
      </Stack>

      {ledKeys.length > 0 && (
        <Fragment>
          <Divider />
          <Typography variant="subtitle2">LEDs</Typography>
          <MuiBox sx={{ display: 'grid', gridTemplateColumns: { xs: '1fr', sm: '1fr 1fr', md: '1fr 1fr 1fr' }, gap: 1 }}>
            {ledKeys.map((k) => {
              const led = (config as Record<string, any>)[k] as any
              return (
                <MuiBox key={k}>
                  <Box sx={{ border: '1px solid', borderColor: 'divider', p: 1, borderRadius: 1 }}>
                    <Typography variant="body2" fontWeight={600}>{k}</Typography>
                    <Stack direction="row" spacing={1} flexWrap="wrap" useFlexGap>
                      {'chip' in led && <Chip label={`Chip: ${led.chip}`} size="small" onClick={() => openEditor(k, 'chip', led.chip)} />}
                      <Chip label={`Pattern: ${'pattern' in led ? led.pattern : 'OFF'}`} size="small" onClick={() => openEditor(k, 'pattern', ('pattern' in led ? led.pattern : 'OFF'))} />
                      {'dataGPIO' in led && <Chip label={`Data: ${led.dataGPIO}`} size="small" onClick={() => openEditor(k, 'dataGPIO', led.dataGPIO)} />}
                      {'enabledGPIO' in led && <Chip label={`Enable: ${led.enabledGPIO}`} size="small" onClick={() => openEditor(k, 'enabledGPIO', led.enabledGPIO)} />}
                      {'num_columns' in led && <Chip label={`Cols: ${led.num_columns}`} size="small" onClick={() => openEditor(k, 'num_columns', led.num_columns)} />}
                      {'num_rows' in led && <Chip label={`Rows: ${led.num_rows}`} size="small" onClick={() => openEditor(k, 'num_rows', led.num_rows)} />}
                      <Chip label={`Brightness: ${('brightness' in led && led.brightness !== undefined) ? led.brightness : '(unset)'}`} size="small" onClick={() => openEditor(k, 'brightness', ('brightness' in led && led.brightness !== undefined) ? led.brightness : 0)} />
                      <Chip label={`Speed: ${('speed' in led && led.speed !== undefined) ? led.speed : '(unset)'}`} size="small" onClick={() => openEditor(k, 'speed', ('speed' in led && led.speed !== undefined) ? led.speed : 0)} />
                    </Stack>
                  </Box>
                </MuiBox>
              )
            })}
          </MuiBox>
        </Fragment>
      )}
      <ConfigEditor
        open={editorOpen}
        mac={mac}
        field={field}
        onClose={() => setEditorOpen(false)}
        onSubmit={publishConfig}
      />
    </Stack>
  )
}


