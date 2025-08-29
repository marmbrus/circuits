import { Accordion, AccordionDetails, AccordionSummary, Box, Chip, Stack, Typography } from '@mui/material'
import ExpandMoreIcon from '@mui/icons-material/ExpandMore'
import ErrorIcon from '@mui/icons-material/Error'
import WarningIcon from '@mui/icons-material/Warning'
import type { SensorsMap, SensorState } from '../types'
import Sensor from './Sensor'
import { useRef, useState } from 'react'

type Props = {
  sensors: SensorsMap
}

export default function SensorGrid({ sensors }: Props) {
  const sensorRefs = useRef<Map<string, HTMLElement>>(new Map())
  const [expandedAreas, setExpandedAreas] = useState<Set<string>>(new Set())
  const [expandedRooms, setExpandedRooms] = useState<Set<string>>(new Set())
  const [expandedSensors, setExpandedSensors] = useState<Set<string>>(new Set())
  const [expandedSensorLogs, setExpandedSensorLogs] = useState<Set<string>>(new Set())
  
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

  const countLogsByLevel = (sensors: SensorState[], level: string): number => {
    return sensors.reduce((count, sensor) => {
      const logs = sensor.logs || []
      return count + logs.filter(log => log.level === level).length
    }, 0)
  }

  const findFirstSensorWithLogs = (sensors: SensorState[], level: string): SensorState | null => {
    return sensors.find(sensor => {
      const logs = sensor.logs || []
      return logs.some(log => log.level === level)
    }) || null
  }

  const scrollToSensor = (sensorMac: string) => {
    // Find the sensor to get its area and room
    const sensor = items.find(s => s.mac === sensorMac)
    if (!sensor) return
    
    const area = sensor.config?.tags.area ?? 'unknown'
    const room = sensor.config?.tags.room ?? 'unknown'
    const roomKey = `${area}::${room}`
    
    // Expand the necessary accordions, sensor, and logs
    setExpandedAreas(prev => new Set(prev).add(area))
    setExpandedRooms(prev => new Set(prev).add(roomKey))
    setExpandedSensors(prev => new Set(prev).add(sensorMac))
    setExpandedSensorLogs(prev => new Set(prev).add(sensorMac))
    
    // Wait a bit for the accordions to expand, then scroll
    setTimeout(() => {
      const element = sensorRefs.current.get(sensorMac)
      if (element) {
        element.scrollIntoView({ behavior: 'smooth', block: 'center' })
        // Add a brief highlight effect
        element.style.transition = 'background-color 0.3s'
        element.style.backgroundColor = 'rgba(255, 193, 7, 0.1)'
        setTimeout(() => {
          element.style.backgroundColor = ''
        }, 1000)
      }
    }, 150) // Small delay to allow accordion expansion
  }

  return (
    <Stack spacing={2}>
      {Array.from(grouped.entries()).map(([area, rooms]) => (
        <Accordion 
          key={area} 
          expanded={expandedAreas.has(area)}
          onChange={(_, isExpanded) => {
            setExpandedAreas(prev => {
              const next = new Set(prev)
              if (isExpanded) {
                next.add(area)
              } else {
                next.delete(area)
              }
              return next
            })
          }}
          disableGutters
        >
          <AccordionSummary expandIcon={<ExpandMoreIcon />}>
            <Stack direction="row" spacing={1} alignItems="center">
              <Typography variant="h6">{area}</Typography>
              {(() => {
                const lists = Array.from(rooms.values())
                const allSensors = lists.flat()
                const total = allSensors.length
                const online = allSensors.filter(isOnline).length
                const offline = total - online
                const errorCount = countLogsByLevel(allSensors, 'error')
                const warningCount = countLogsByLevel(allSensors, 'warn')
                
                const handleErrorClick = () => {
                  const sensor = findFirstSensorWithLogs(allSensors, 'error')
                  if (sensor) scrollToSensor(sensor.mac)
                }
                
                const handleWarningClick = () => {
                  const sensor = findFirstSensorWithLogs(allSensors, 'warn')
                  if (sensor) scrollToSensor(sensor.mac)
                }
                
                return (
                  <Stack direction="row" spacing={1}>
                    {online > 0 && <Chip size="small" color="success" label={`${online}`} />}
                    {offline > 0 && <Chip size="small" color="warning" label={`${offline}`} />}
                    {errorCount > 0 && (
                      <Chip 
                        size="small" 
                        color="error" 
                        icon={<ErrorIcon />}
                        label={`${errorCount}`}
                        onClick={(e) => {
                          e.stopPropagation()
                          handleErrorClick()
                        }}
                        clickable
                        sx={{ cursor: 'pointer' }}
                      />
                    )}
                    {warningCount > 0 && (
                      <Chip 
                        size="small" 
                        color="warning" 
                        icon={<WarningIcon />}
                        label={`${warningCount}`}
                        onClick={(e) => {
                          e.stopPropagation()
                          handleWarningClick()
                        }}
                        clickable
                        sx={{ cursor: 'pointer' }}
                      />
                    )}
                  </Stack>
                )
              })()}
            </Stack>
          </AccordionSummary>
          <AccordionDetails>
            <Stack spacing={1}>
              {Array.from(rooms.entries()).map(([room, list]) => (
                <Accordion 
                  key={room} 
                  expanded={expandedRooms.has(`${area}::${room}`)}
                  onChange={(_, isExpanded) => {
                    const roomKey = `${area}::${room}`
                    setExpandedRooms(prev => {
                      const next = new Set(prev)
                      if (isExpanded) {
                        next.add(roomKey)
                      } else {
                        next.delete(roomKey)
                      }
                      return next
                    })
                  }}
                  disableGutters 
                  sx={{ boxShadow: 'none', border: 1, borderColor: 'divider' }}
                >
                  <AccordionSummary expandIcon={<ExpandMoreIcon />}>
                    <Stack direction="row" spacing={1} alignItems="center">
                      <Typography variant="subtitle1">{room}</Typography>
                      {(() => {
                        const online = list.filter(isOnline).length
                        const offline = list.length - online
                        const errorCount = countLogsByLevel(list, 'error')
                        const warningCount = countLogsByLevel(list, 'warn')
                        
                        const handleErrorClick = () => {
                          const sensor = findFirstSensorWithLogs(list, 'error')
                          if (sensor) scrollToSensor(sensor.mac)
                        }
                        
                        const handleWarningClick = () => {
                          const sensor = findFirstSensorWithLogs(list, 'warn')
                          if (sensor) scrollToSensor(sensor.mac)
                        }
                        
                        return (
                          <Stack direction="row" spacing={1}>
                            {online > 0 && <Chip size="small" color="success" label={`${online}`} />}
                            {offline > 0 && <Chip size="small" color="warning" label={`${offline}`} />}
                            {errorCount > 0 && (
                              <Chip 
                                size="small" 
                                color="error" 
                                icon={<ErrorIcon />}
                                label={`${errorCount}`}
                                onClick={(e) => {
                                  e.stopPropagation()
                                  handleErrorClick()
                                }}
                                clickable
                                sx={{ cursor: 'pointer' }}
                              />
                            )}
                            {warningCount > 0 && (
                              <Chip 
                                size="small" 
                                color="warning" 
                                icon={<WarningIcon />}
                                label={`${warningCount}`}
                                onClick={(e) => {
                                  e.stopPropagation()
                                  handleWarningClick()
                                }}
                                clickable
                                sx={{ cursor: 'pointer' }}
                              />
                            )}
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
                        <Box 
                          key={s.mac} 
                          sx={{ justifySelf: 'center', width: '100%' }}
                          ref={(el: HTMLElement | null) => {
                            if (el) {
                              sensorRefs.current.set(s.mac, el)
                            } else {
                              sensorRefs.current.delete(s.mac)
                            }
                          }}
                        >
                          <Sensor 
                            sensor={s} 
                            forceExpanded={expandedSensors.has(s.mac) ? true : undefined}
                            onExpandedChange={(expanded) => {
                              setExpandedSensors(prev => {
                                const next = new Set(prev)
                                if (expanded) {
                                  next.add(s.mac)
                                } else {
                                  next.delete(s.mac)
                                }
                                return next
                              })
                            }}
                            forceLogsExpanded={expandedSensorLogs.has(s.mac) ? true : undefined}
                            onLogsExpandedChange={(expanded) => {
                              setExpandedSensorLogs(prev => {
                                const next = new Set(prev)
                                if (expanded) {
                                  next.add(s.mac)
                                } else {
                                  next.delete(s.mac)
                                }
                                return next
                              })
                            }}
                          />
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


