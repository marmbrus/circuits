import { useMemo, useState } from 'react'
import { Box, Button, Dialog, DialogActions, DialogContent, DialogTitle, Stack, TextField, Typography, Slider, Switch, FormControlLabel, Select, MenuItem } from '@mui/material'

export type ConfigField = {
	label: string
	moduleName: string
	configName: string
	value: unknown
}

type Props = {
	open: boolean
	mac: string
	field: ConfigField | null
	onClose: () => void
	onSubmit: (mac: string, moduleName: string, configName: string, value: string | number | boolean) => void
}

const LED_PATTERNS = ['OFF','SOLID','FADE','STATUS','RAINBOW','CHASE','LIFE'] as const
const A2D_GAINS = ['FSR_6V144','FSR_4V096','FSR_2V048','FSR_1V024','FSR_0V512','FSR_0V256','FULL'] as const
const A2D_SENSORS = ['', 'BTS7002', 'RSUV'] as const

export default function ConfigEditor({ open, mac, field, onClose, onSubmit }: Props) {
	const [raw, setRaw] = useState<string>('')
	const [num, setNum] = useState<number>(0)
	const [bool, setBool] = useState<boolean>(false)
	const [pattern, setPattern] = useState<string>('OFF')
	const [a2dGain, setA2dGain] = useState<string>('FULL')
	const [a2dSensor, setA2dSensor] = useState<string>('')
  const [tagArea, setTagArea] = useState<string>('')
  const [tagRoom, setTagRoom] = useState<string>('')
  const [tagId, setTagId] = useState<string>('')

	const kind: 'string' | 'number' | 'boolean' | 'slider-brightness' | 'pattern' | 'a2d-gain' | 'a2d-sensor' | 'tags-all' = useMemo(() => {
		if (!field) return 'string'
		const { moduleName, configName, value } = field
		if (moduleName.startsWith('led') && configName === 'pattern') return 'pattern'
		if (moduleName === 'a2d') {
			if (configName.endsWith('.gain')) return 'a2d-gain'
			if (configName.endsWith('.sensor')) return 'a2d-sensor'
		}
    if (moduleName === 'tags' && configName === '*') return 'tags-all'
		if (typeof value === 'boolean') return 'boolean'
		if (typeof value === 'number') {
			if (moduleName.startsWith('led') && configName === 'brightness') return 'slider-brightness'
			return 'number'
		}
		return 'string'
	}, [field])

	// keep local state in sync when field changes
	useMemo(() => {
		if (!field) return
		const v = field.value
		setRaw(String(v ?? ''))
		setNum(typeof v === 'number' ? v : Number(v ?? 0))
		setBool(typeof v === 'boolean' ? v : String(v).toLowerCase() === 'true')
		setPattern(typeof v === 'string' && LED_PATTERNS.includes(v as any) ? String(v) : 'OFF')
		if (field.moduleName === 'a2d') {
			if (field.configName.endsWith('.gain')) setA2dGain(typeof v === 'string' && A2D_GAINS.includes(v as any) ? String(v) : 'FULL')
			if (field.configName.endsWith('.sensor')) setA2dSensor(typeof v === 'string' ? String(v) : '')
		}
    if (field.moduleName === 'tags' && field.configName === '*') {
      const t = (v as any) || {}
      setTagArea(String(t.area ?? ''))
      setTagRoom(String(t.room ?? ''))
      setTagId(String(t.id ?? ''))
    }
	}, [field])

	const handleSubmit = () => {
		if (!field) return
		let out: string | number | boolean
		switch (kind) {
			case 'boolean': out = bool; break
			case 'number': out = num; break
			case 'slider-brightness': out = Math.round(num); break
			case 'pattern': out = pattern; break
			case 'a2d-gain': out = a2dGain; break
			case 'a2d-sensor': out = a2dSensor; break
      case 'tags-all':
        // publish all fields
        onSubmit(mac, 'tags', 'area', tagArea)
        onSubmit(mac, 'tags', 'room', tagRoom)
        onSubmit(mac, 'tags', 'id', tagId)
        onClose()
        return
			default: out = raw
		}
		onSubmit(mac, field.moduleName, field.configName, out)
		onClose()
	}

	return (
		<Dialog open={open} onClose={onClose} fullWidth maxWidth="sm">
			<DialogTitle>Edit configuration</DialogTitle>
			<DialogContent>
				{field ? (
					<Stack spacing={2} sx={{ mt: 1 }}>
						<Typography variant="body2" color="text.secondary">{field.moduleName} / {field.configName}</Typography>
						{kind === 'slider-brightness' && (
							<Box>
								<Typography gutterBottom>Brightness</Typography>
								<Slider
									value={num}
									onChange={(_, v) => setNum(v as number)}
									onChangeCommitted={(_, v) => onSubmit(mac, field.moduleName, field.configName, Math.round(v as number))}
									min={0}
									max={255}
									step={1}
									valueLabelDisplay="auto"
								/>
							</Box>
						)}
						{kind === 'number' && (
							<TextField type="number" label={field.label} value={num} onChange={(e) => setNum(Number(e.target.value))} fullWidth />
						)}
						{kind === 'boolean' && (
							<FormControlLabel control={<Switch checked={bool} onChange={(e) => setBool(e.target.checked)} />} label={field.label} />
						)}
						{kind === 'pattern' && (
							<Box>
								<Typography gutterBottom>Pattern</Typography>
								<Select size="small" value={pattern} onChange={(e) => setPattern(String(e.target.value))} fullWidth>
									{LED_PATTERNS.map((p) => (
										<MenuItem key={p} value={p}>{p}</MenuItem>
									))}
								</Select>
							</Box>
						)}
						{kind === 'a2d-gain' && (
							<Box>
								<Typography gutterBottom>Gain</Typography>
								<Select size="small" value={a2dGain} onChange={(e) => setA2dGain(String(e.target.value))} fullWidth>
									{A2D_GAINS.map((g) => (
										<MenuItem key={g} value={g}>{g}</MenuItem>
									))}
								</Select>
							</Box>
						)}
						{kind === 'a2d-sensor' && (
							<Box>
								<Typography gutterBottom>Sensor</Typography>
								<Select size="small" value={a2dSensor} onChange={(e) => setA2dSensor(String(e.target.value))} fullWidth displayEmpty>
									{A2D_SENSORS.map((s) => (
										<MenuItem key={s || 'none'} value={s}>{s || '(none)'}</MenuItem>
									))}
								</Select>
							</Box>
						)}
						{kind === 'tags-all' && (
							<Stack spacing={2}>
								<TextField label="Area" value={tagArea} onChange={(e) => setTagArea(e.target.value)} fullWidth />
								<TextField label="Room" value={tagRoom} onChange={(e) => setTagRoom(e.target.value)} fullWidth />
								<TextField label="ID" value={tagId} onChange={(e) => setTagId(e.target.value)} fullWidth />
							</Stack>
						)}
						{kind === 'string' && (
							<TextField label={field.label} value={raw} onChange={(e) => setRaw(e.target.value)} fullWidth />
						)}
					</Stack>
				) : (
					<Typography>No field selected</Typography>
				)}
			</DialogContent>
			<DialogActions>
				<Button onClick={onClose}>Cancel</Button>
				<Button onClick={handleSubmit} variant="contained">Save</Button>
			</DialogActions>
		</Dialog>
	)
}
