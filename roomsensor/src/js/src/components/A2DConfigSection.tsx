import { Accordion, AccordionDetails, AccordionSummary, Box, Chip, Stack, Typography } from '@mui/material'
import ExpandMoreIcon from '@mui/icons-material/ExpandMore'
import { Box as MuiBox } from '@mui/material'
import type { ControlSpec } from './ConfigEditor'

type Props = {
  modules: string[]
  config: Record<string, any>
  onEdit: (moduleName: string, key: string, value: unknown, control?: ControlSpec) => void
}

export default function A2DConfigSection({ modules, config, onEdit }: Props) {
  if (modules.length === 0) return null
  const gains = ['FULL','FSR_6V144','FSR_4V096','FSR_2V048','FSR_1V024','FSR_0V512','FSR_0V256']
  const sensors = ['', 'BTS7002', 'RSUV']
  return (
    <MuiBox sx={{ display: 'grid', gridTemplateColumns: { xs: '1fr', sm: '1fr 1fr' }, gap: 1 }}>
      {modules.map((mod) => {
        const a2d = (config as Record<string, any>)[mod] || {}
        const channels = [1,2,3,4]
        return (
          <MuiBox key={mod}>
            <Box sx={{ border: '1px solid', borderColor: 'divider', p: 1, borderRadius: 1 }}>
              <Typography variant="body2" fontWeight={600}>{mod}</Typography>
              {channels.map((ch) => {
                const prefix = `ch${ch}`
                const chObj = a2d[prefix] || {}
                const enabled = chObj.enabled
                const gain = chObj.gain
                const sensor = chObj.sensor
                const name = chObj.name
                return (
                  <Accordion key={prefix} disableGutters elevation={0} sx={{ border: '1px solid', borderColor: 'divider', mt: 0.5, '&:before': { display: 'none' } }}>
                    <AccordionSummary expandIcon={<ExpandMoreIcon />} sx={{ minHeight: 32, '& .MuiAccordionSummary-content': { my: 0.25 } }}>
                      <Stack direction="row" spacing={0.75} flexWrap="wrap" useFlexGap alignItems="center">
                        <Typography variant="caption" color="text.secondary">ch{ch}</Typography>
                        <Chip label={`name: ${name ?? '(unset)'}`} size="small" onClick={(e) => { (e.currentTarget as HTMLDivElement).blur?.(); onEdit(mod, `${prefix}.name`, name ?? '', { type: 'text', label: `${prefix}.name` }) }} />
                        <Chip label={`sensor: ${sensor ?? '(none)'}`} size="small" onClick={(e) => { (e.currentTarget as HTMLDivElement).blur?.(); onEdit(mod, `${prefix}.sensor`, sensor ?? '', { type: 'select', options: sensors, label: `${prefix}.sensor` }) }} />
                      </Stack>
                    </AccordionSummary>
                    <AccordionDetails>
                      <Stack direction="row" spacing={0.75} flexWrap="wrap" useFlexGap>
                        <Chip label={`enabled: ${enabled === undefined ? '(unset)' : String(enabled)}`} size="small" onClick={() => onEdit(mod, `${prefix}.enabled`, String(enabled ?? 'false'), { type: 'boolean', label: `${prefix}.enabled` })} />
                        <Chip label={`gain: ${gain ?? '(unset)'}`} size="small" onClick={() => onEdit(mod, `${prefix}.gain`, gain ?? '', { type: 'select', options: gains, label: `${prefix}.gain` })} />
                      </Stack>
                    </AccordionDetails>
                  </Accordion>
                )
              })}
            </Box>
          </MuiBox>
        )
      })}
    </MuiBox>
  )
}


