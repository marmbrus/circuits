import { Fragment } from 'react'
import { Chip, Stack, Typography, Accordion, AccordionSummary, AccordionDetails } from '@mui/material'
import ExpandMoreIcon from '@mui/icons-material/ExpandMore'
import type { SensorConfig } from '../types'
import { useState } from 'react'
import ConfigEditor, { type ConfigField, type ControlSpec } from './ConfigEditor'
import LEDConfigSection from './LEDConfigSection'
import A2DConfigSection from './A2DConfigSection'
import IOConfigSection from './IOConfigSection'


type Props = {
  mac: string
  config: SensorConfig
  publishConfig: (mac: string, moduleName: string, configName: string, value: string | number | boolean) => void
  presentA2DModules?: string[]
  presentIOModules?: string[]
}

export default function SensorConfigView({ mac, config, publishConfig, presentA2DModules = [], presentIOModules = [] }: Props) {
  const [editorOpen, setEditorOpen] = useState(false)
  const [field, setField] = useState<ConfigField | null>(null)
  const [control, setControl] = useState<ControlSpec | undefined>(undefined)

  const openEditor = (moduleName: string, configName: string, value: unknown, ctl?: ControlSpec) => {
    setField({ label: `${moduleName}.${configName}`, moduleName, configName, value })
    setControl(ctl)
    setEditorOpen(true)
  }

  const openTagsEditor = () => {
    setField({ label: 'tags', moduleName: 'tags', configName: 'area', value: config.tags.area })
    setControl(undefined)
    setEditorOpen(true)
  }

  const publish = (moduleName: string, key: string, value: string | number | boolean) => {
    publishConfig(mac, moduleName, key, value)
  }

  return (
    <Stack spacing={1} sx={{ width: '100%' }}>
      <Typography variant="subtitle2">Tags</Typography>
      <Stack direction="row" spacing={1}>
        <Chip label={`Area: ${config.tags.area}`} size="small" onClick={openTagsEditor} />
        <Chip label={`Room: ${config.tags.room}`} size="small" onClick={openTagsEditor} />
        <Chip label={`ID: ${config.tags.id}`} size="small" onClick={openTagsEditor} />
      </Stack>

      <Accordion disableGutters elevation={0} defaultExpanded={false} sx={{ border: '1px solid', borderColor: 'divider', '&:before': { display: 'none' } }}>
        <AccordionSummary expandIcon={<ExpandMoreIcon />} sx={{ minHeight: 36, '& .MuiAccordionSummary-content': { my: 0.5 } }}>
          <Typography variant="subtitle2">WiFi</Typography>
        </AccordionSummary>
        <AccordionDetails>
          <Stack direction="row" spacing={1}>
            <Chip label={`SSID: ${config.wifi.ssid}`} size="small" />
            <Chip label={`Broker: ${config.wifi.mqtt_broker}`} size="small" />
          </Stack>
        </AccordionDetails>
      </Accordion>

      {Object.keys(config).some((k) => k.startsWith('led')) && (
        <Fragment>
          <Accordion disableGutters elevation={0} defaultExpanded={false} sx={{ border: '1px solid', borderColor: 'divider', '&:before': { display: 'none' } }}>
            <AccordionSummary expandIcon={<ExpandMoreIcon />} sx={{ minHeight: 36, '& .MuiAccordionSummary-content': { my: 0.5 } }}>
              <Typography variant="subtitle2">LEDs</Typography>
            </AccordionSummary>
            <AccordionDetails>
              <LEDConfigSection config={config as any} onEdit={openEditor} publish={publish} />
            </AccordionDetails>
          </Accordion>
        </Fragment>
      )}

      {presentA2DModules.length > 0 ? (
        <Fragment>
          <Accordion disableGutters elevation={0} defaultExpanded={false} sx={{ border: '1px solid', borderColor: 'divider', '&:before': { display: 'none' } }}>
            <AccordionSummary expandIcon={<ExpandMoreIcon />} sx={{ minHeight: 36, '& .MuiAccordionSummary-content': { my: 0.5 } }}>
              <Typography variant="subtitle2">A2D Configuration</Typography>
            </AccordionSummary>
            <AccordionDetails>
              <A2DConfigSection modules={presentA2DModules} config={config as any} onEdit={openEditor} />
            </AccordionDetails>
          </Accordion>
        </Fragment>
      ) : null}

      {presentIOModules.length > 0 ? (
        <Fragment>
          <Accordion disableGutters elevation={0} defaultExpanded={false} sx={{ border: '1px solid', borderColor: 'divider', '&:before': { display: 'none' } }}>
            <AccordionSummary expandIcon={<ExpandMoreIcon />} sx={{ minHeight: 36, '& .MuiAccordionSummary-content': { my: 0.5 } }}>
              <Typography variant="subtitle2">IO Configuration</Typography>
            </AccordionSummary>
            <AccordionDetails>
              <IOConfigSection modules={presentIOModules} config={config as any} onEdit={openEditor} publish={publish} />
            </AccordionDetails>
          </Accordion>
        </Fragment>
      ) : null}

      <ConfigEditor
        open={editorOpen}
        mac={mac}
        field={field}
        fields={field?.moduleName === 'tags' ? [
          { label: 'Area', moduleName: 'tags', configName: 'area', value: config.tags.area },
          { label: 'Room', moduleName: 'tags', configName: 'room', value: config.tags.room },
          { label: 'ID', moduleName: 'tags', configName: 'id', value: config.tags.id },
        ] : undefined}
        title={field?.moduleName === 'tags' ? 'Edit tags' : undefined}
        onClose={() => setEditorOpen(false)}
        onSubmit={publishConfig}
        control={control}
      />
    </Stack>
  )
}


