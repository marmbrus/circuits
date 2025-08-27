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

  const isOnline = (s: any) => {
    const ts = s.deviceStatusTs || 0
    if (!ts) return false
    const age = Date.now() - ts
    return age <= 10000
  }

  return (
    <Stack spacing={2}>
      {Array.from(grouped.entries()).map(([area, rooms]) => (
        <Accordion key={area} defaultExpanded={false} disableGutters>
          <AccordionSummary expandIcon={<ExpandMoreIcon />}>
            <Stack direction="row" spacing={1} alignItems="center">
              <Typography variant="h6">{area}</Typography>
              {(() => {
                const lists = Array.from(rooms.values())
                const total = lists.reduce((n, arr) => n + arr.length, 0)
                const online = lists.reduce((n, arr) => n + arr.filter(isOnline).length, 0)
                const offline = total - online
                return (
                  <Stack direction="row" spacing={1}>
                    {online > 0 && <Chip size="small" color="success" label={`${online}`} />}
                    {offline > 0 && <Chip size="small" color="warning" label={`${offline}`} />}
                  </Stack>
                )
              })()}
            </Stack>
          </AccordionSummary>
          <AccordionDetails>
            <Stack spacing={1}>
              {Array.from(rooms.entries()).map(([room, list]) => (
                <Accordion key={room} defaultExpanded={false} disableGutters sx={{ boxShadow: 'none', border: 1, borderColor: 'divider' }}>
                  <AccordionSummary expandIcon={<ExpandMoreIcon />}>
                    <Stack direction="row" spacing={1} alignItems="center">
                      <Typography variant="subtitle1">{room}</Typography>
                      {(() => {
                        const online = list.filter(isOnline).length
                        const offline = list.length - online
                        return (
                          <Stack direction="row" spacing={1}>
                            {online > 0 && <Chip size="small" color="success" label={`${online}`} />}
                            {offline > 0 && <Chip size="small" color="warning" label={`${offline}`} />}
                          </Stack>
                        )
                      })()}
                    </Stack>
                  </AccordionSummary>
                  <AccordionDetails>
                    <Box
                      sx={{
                        display: 'grid',
                        gridTemplateColumns: {
                          xs: 'repeat(auto-fill, minmax(280px, 1fr))',
                          sm: 'repeat(auto-fill, minmax(300px, 1fr))',
                          md: 'repeat(auto-fill, minmax(340px, 1fr))',
                        },
                        gap: 2,
                        alignItems: 'start',
                      }}
                    >
                      {list.map((s) => (
                        <Box key={s.mac} sx={{ justifySelf: 'center', width: '100%' }}>
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


