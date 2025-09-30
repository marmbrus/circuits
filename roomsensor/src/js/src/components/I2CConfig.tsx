import { Accordion, AccordionDetails, AccordionSummary, Box, Button, Chip, FormControl, IconButton, InputLabel, MenuItem, Select, Stack, TextField, Tooltip, Typography } from '@mui/material'
import ExpandMoreIcon from '@mui/icons-material/ExpandMore'
import ClearIcon from '@mui/icons-material/Clear'
import EditIcon from '@mui/icons-material/Edit'
import { useMemo, useState } from 'react'
import type { SensorState } from '../types'

type Props = {
	sensor: SensorState
	publishConfig: (mac: string, moduleName: string, configName: string, value: string | number | boolean) => void
}

const AVAILABLE_DRIVERS = [
	// Keep in sync with firmware driver names used in tags and logs
	'mcp23008',
	'sen55',
	'scd4x',
	'bme280',
	'ads1115',
	'lis2dh',
	'opt3001',
	'none', // special: disables this address
]

function normalizeHex(input: string): string | null {
	try {
		let s = String(input || '').trim()
		if (!s) return null
		if (s.startsWith('0x') || s.startsWith('0X')) s = s.slice(2)
		s = s.toLowerCase()
		if (!/^[0-9a-f]{1,2}$/.test(s)) return null
		if (s.length === 1) s = `0${s}`
		const v = parseInt(s, 16)
		if (v < 0x08 || v > 0x77) return null
		return s
	} catch { return null }
}

export default function I2CConfigSection({ sensor, publishConfig }: Props) {
    const overrides = useMemo(() => Object.entries((sensor.config as any)?.i2c || {} as Record<string, string>), [sensor.config])
    const devices = sensor.i2c?.devices || []
    const [addrInput, setAddrInput] = useState('')
    const [driverInput, setDriverInput] = useState<string>('mcp23008')
    const [editKey, setEditKey] = useState<string | null>(null)
    const [editDriver, setEditDriver] = useState<string>('mcp23008')
    const [selectedDevice, setSelectedDevice] = useState<any | null>(null)

    const handleAdd = () => {
        const norm = normalizeHex(addrInput)
        if (!norm) return
        publishConfig(sensor.mac, 'i2c', norm, driverInput)
        setAddrInput('')
    }

    const handleDelete = (key: string) => {
        publishConfig(sensor.mac, 'i2c', key, '')
    }

    const startEdit = (key: string, cur: string) => {
        setEditKey(key)
        setEditDriver(cur)
    }

    const applyEdit = () => {
        if (!editKey) return
        publishConfig(sensor.mac, 'i2c', editKey, editDriver)
        setEditKey(null)
    }

    return (
        <Accordion disableGutters elevation={0} defaultExpanded={false} sx={{ border: '1px solid', borderColor: 'divider', '&:before': { display: 'none' } }}>
            <AccordionSummary expandIcon={<ExpandMoreIcon />} sx={{ minHeight: 36, '& .MuiAccordionSummary-content': { my: 0.5 } }}>
                <Stack direction="row" spacing={1} alignItems="center">
                    <Typography variant="subtitle2">I2C</Typography>
                    <Chip label={String(devices.length)} size="small" />
                </Stack>
            </AccordionSummary>
            <AccordionDetails>
                <Stack spacing={1.5}>
                    <Stack spacing={0.75}>
                        <Typography variant="subtitle2">Devices</Typography>
                        <Stack direction="row" spacing={1} flexWrap="wrap" useFlexGap>
                            {devices.map((d: any, idx: number) => (
                                <Chip key={`${d.addr}-${idx}`} size="small" label={`${d.addr}${d.driver ? ` ${d.driver}` : ''}`} onClick={() => setSelectedDevice(d)} />
                            ))}
                        </Stack>
                        {selectedDevice && (
                            <Box sx={{ maxHeight: 200, overflow: 'auto', bgcolor: 'background.paper', borderRadius: 1, p: 1, fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace', fontSize: 12 }}>
                                <pre style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word', margin: 0 }}>{JSON.stringify(selectedDevice, null, 2)}</pre>
                            </Box>
                        )}
                    </Stack>

                    <Stack spacing={1}>
                        <Typography variant="subtitle2">Overrides</Typography>
                        {overrides.length === 0 ? (
                            <Typography variant="body2" color="text.secondary">No address overrides configured.</Typography>
                        ) : (
                            <Stack spacing={0.75}>
                                {overrides.map(([key, val]) => (
                                    <Stack key={key} direction="row" spacing={1} alignItems="center">
                                        <Chip label={`0x${key.toLowerCase()} → ${val}`} size="small" />
                                        <Tooltip title="Delete override">
                                            <IconButton size="small" onClick={() => handleDelete(key)} aria-label="delete i2c override">
                                                <ClearIcon fontSize="inherit" />
                                            </IconButton>
                                        </Tooltip>
                                        <Tooltip title="Modify override">
                                            <IconButton size="small" onClick={() => startEdit(key, String(val))} aria-label="edit i2c override">
                                                <EditIcon fontSize="inherit" />
                                            </IconButton>
                                        </Tooltip>
                                        {editKey === key && (
                                            <Stack direction="row" spacing={1} alignItems="center">
                                                <FormControl size="small" sx={{ minWidth: 160 }}>
                                                    <InputLabel id={`drv-${key}`}>Driver</InputLabel>
                                                    <Select labelId={`drv-${key}`} label="Driver" value={editDriver} onChange={(e) => setEditDriver(String(e.target.value))}>
                                                        {AVAILABLE_DRIVERS.map(d => <MenuItem key={d} value={d}>{d}</MenuItem>)}
                                                    </Select>
                                                </FormControl>
                                                <Button variant="contained" size="small" onClick={applyEdit}>Save</Button>
                                                <Button size="small" onClick={() => setEditKey(null)}>Cancel</Button>
                                            </Stack>
                                        )}
                                    </Stack>
                                ))}
                            </Stack>
                        )}

                        <Box sx={{ display: 'flex', flexDirection: 'row', gap: 8, alignItems: 'center', flexWrap: 'wrap' }}>
                            <Stack direction="row" spacing={1} alignItems="center">
                                <FormControl size="small" sx={{ minWidth: 140 }}>
                                    <TextField size="small" label="Address (hex)" placeholder="2a" value={addrInput} onChange={(e) => setAddrInput(e.target.value)} />
                                </FormControl>
                                <FormControl size="small" sx={{ minWidth: 160 }}>
                                    <InputLabel id="drv-new">Driver</InputLabel>
                                    <Select labelId="drv-new" label="Driver" value={driverInput} onChange={(e) => setDriverInput(String(e.target.value))}>
                                        {AVAILABLE_DRIVERS.map(d => <MenuItem key={d} value={d}>{d}</MenuItem>)}
                                    </Select>
                                </FormControl>
                                <Button variant="contained" size="small" onClick={handleAdd} disabled={!normalizeHex(addrInput)}>Add</Button>
                            </Stack>
                            <Typography variant="caption" color="text.secondary">Driver "none" disables this address. Delete to clear override.</Typography>
                        </Box>
                    </Stack>
                </Stack>
            </AccordionDetails>
        </Accordion>
    )
}


