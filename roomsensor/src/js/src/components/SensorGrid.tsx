import { Accordion, AccordionDetails, AccordionSummary, Box, Chip, Stack, Typography } from '@mui/material'
import ExpandMoreIcon from '@mui/icons-material/ExpandMore'
import type { SensorsMap } from '../types'
import Sensor from './Sensor'

type Props = {
  sensors: SensorsMap
}

export default function SensorGrid({ sensors }: Props) {
  const items = Array.from(sensors.values()).filter((s) => !!s.config && !!s.config.tags)
  const grouped = new Map<string, Map<string, typeof items>>()
  for (const s of items) {
    const area = s.config?.tags.area ?? 'unknown'
    const room = s.config?.tags.room ?? 'unknown'
    if (!grouped.has(area)) grouped.set(area, new Map())
    const byRoom = grouped.get(area)!
    if (!byRoom.has(room)) byRoom.set(room, [])
    byRoom.get(room)!.push(s)
  }

  return (
    <Stack spacing={2}>
      {Array.from(grouped.entries()).map(([area, rooms]) => (
        <Accordion key={area} defaultExpanded={false} disableGutters>
          <AccordionSummary expandIcon={<ExpandMoreIcon />}>
            <Stack direction="row" spacing={1} alignItems="center">
              <Typography variant="h6">{area}</Typography>
              <Chip size="small" label={`${Array.from(rooms.values()).reduce((n, arr) => n + arr.length, 0)} sensors`} />
            </Stack>
          </AccordionSummary>
          <AccordionDetails>
            <Stack spacing={1}>
              {Array.from(rooms.entries()).map(([room, list]) => (
                <Accordion key={room} defaultExpanded={false} disableGutters sx={{ boxShadow: 'none', border: 1, borderColor: 'divider' }}>
                  <AccordionSummary expandIcon={<ExpandMoreIcon />}>
                    <Stack direction="row" spacing={1} alignItems="center">
                      <Typography variant="subtitle1">{room}</Typography>
                      <Chip size="small" label={`${list.length} sensors`} />
                    </Stack>
                  </AccordionSummary>
                  <AccordionDetails>
                    <Box
                      sx={{
                        display: 'grid',
                        gridTemplateColumns: { xs: '1fr', sm: '1fr 1fr', md: '1fr 1fr 1fr' },
                        gap: 2,
                      }}
                    >
                      {list.map((s) => (
                        <Box key={s.mac}>
                          <Sensor sensor={s} />
                        </Box>
                      ))}
                    </Box>
                  </AccordionDetails>
                </Accordion>
              ))}
            </Stack>
          </AccordionDetails>
        </Accordion>
      ))}
    </Stack>
  )
}


