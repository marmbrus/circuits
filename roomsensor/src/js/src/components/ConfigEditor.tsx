import { useMemo, useState, useEffect } from 'react'
import { Box, Button, Dialog, DialogActions, DialogContent, DialogTitle, Stack, TextField, Typography, Slider, Switch, FormControlLabel, Select, MenuItem } from '@mui/material'

export type ConfigField = {
	label: string
	moduleName: string
	configName: string
	value: unknown
}

type SelectControl = { type: 'select'; options: string[]; label?: string }
type SliderControl = { type: 'slider'; min: number; max: number; step?: number; round?: boolean; label?: string; commitOnRelease?: boolean }
type BoolControl = { type: 'boolean'; label?: string }
type NumberControl = { type: 'number'; label?: string }
type TextControl = { type: 'text'; label?: string }
export type ControlSpec = SelectControl | SliderControl | BoolControl | NumberControl | TextControl

type Props = {
	open: boolean
	mac: string
	field: ConfigField | null
	onClose: () => void
	onSubmit: (mac: string, moduleName: string, configName: string, value: string | number | boolean) => void
	control?: ControlSpec
	fields?: ConfigField[]
	title?: string
}

export default function ConfigEditor({ open, mac, field, onClose, onSubmit, control, fields, title }: Props) {
	const [raw, setRaw] = useState<string>('')
	const [num, setNum] = useState<number>(0)
	const [bool, setBool] = useState<boolean>(false)
	const [multiValues, setMultiValues] = useState<string[]>([])

	const kind: 'string' | 'number' | 'boolean' = useMemo(() => {
		if (!field) return 'string'
		const { value } = field
		if (typeof value === 'boolean') return 'boolean'
		if (typeof value === 'number') return 'number'
		return 'string'
	}, [field])

	// Initialize single-field values only when opening or when the field identity changes
	const singleFieldIdentity = field ? `${field.moduleName}.${field.configName}` : ''
	useEffect(() => {
		if (!open || !field) return
		const v = field.value
		setRaw(String(v ?? ''))
		setNum(typeof v === 'number' ? v : Number(v ?? 0))
		setBool(typeof v === 'boolean' ? v : String(v).toLowerCase() === 'true')
	}, [open, singleFieldIdentity])

	// Initialize multi-field values only when opening or when the set of fields changes
	const multiFieldsIdentity = (fields && fields.length > 0) ? fields.map((f) => `${f.moduleName}.${f.configName}`).join('|') : ''
	useEffect(() => {
		if (!open) return
		if (fields && fields.length > 0) {
			setMultiValues(fields.map((f) => String(f.value ?? '')))
		} else {
			setMultiValues([])
		}
	}, [open, multiFieldsIdentity])

	const handleSubmit = () => {
		if (!field) return
		let out: string | number | boolean
		if (control) {
			switch (control.type) {
				case 'boolean': out = bool; break
				case 'number': out = num; break
				case 'text': out = raw; break
				case 'slider': out = control.round ? Math.round(num) : num; break
				case 'select': out = raw; break
				default: out = raw
			}
			onSubmit(mac, field.moduleName, field.configName, out)
			onClose()
			return
		}
		switch (kind) {
			case 'boolean': out = bool; break
			case 'number': out = num; break
			default: out = raw
		}
		onSubmit(mac, field.moduleName, field.configName, out)
		onClose()
	}

	const handleSubmitMulti = () => {
		if (!fields || fields.length === 0) return
		fields.forEach((f, idx) => {
			const v = multiValues[idx]
			onSubmit(mac, f.moduleName, f.configName, v)
		})
		onClose()
	}

	return (
		<Dialog open={open} onClose={onClose} fullWidth maxWidth="sm">
			<DialogTitle>{title || 'Edit configuration'}</DialogTitle>
			<DialogContent>
				{fields && fields.length > 0 ? (
					<Stack spacing={2} sx={{ mt: 1 }}>
						{fields.map((f, idx) => (
							<TextField
								key={`${f.moduleName}.${f.configName}`}
								label={f.label || `${f.moduleName}.${f.configName}`}
								value={multiValues[idx] ?? ''}
								onChange={(e) => {
									const next = [...multiValues]
									next[idx] = e.target.value
									setMultiValues(next)
								}}
								fullWidth
							/>
						))}
					</Stack>
				) : field ? (
					<Stack spacing={2} sx={{ mt: 1 }}>
						<Typography variant="body2" color="text.secondary">{field.moduleName} / {field.configName}</Typography>
						{/* Control override */}
						{control && control.type === 'slider' && (
							<Box>
								<Typography gutterBottom>{control.label ?? 'Value'}</Typography>
								<Slider
									value={num}
									onChange={(_, v) => setNum(v as number)}
									onChangeCommitted={(_, v) => {
										if (control.commitOnRelease && field) {
											onSubmit(mac, field.moduleName, field.configName, control.round ? Math.round(v as number) : (v as number))
										}
									}}
									min={control.min}
									max={control.max}
									step={control.step ?? 1}
									valueLabelDisplay="auto"
								/>
							</Box>
						)}
						{control && control.type === 'select' && (
							<Box>
								<Typography gutterBottom>{control.label ?? 'Value'}</Typography>
								<Select size="small" value={raw} onChange={(e) => setRaw(String(e.target.value))} fullWidth>
									{control.options.map((opt) => (
										<MenuItem key={opt} value={opt}>{opt || '(none)'}</MenuItem>
									))}
								</Select>
							</Box>
						)}
						{control && control.type === 'boolean' && (
							<FormControlLabel control={<Switch checked={bool} onChange={(e) => setBool(e.target.checked)} />} label={control.label ?? field.label} />
						)}
						{control && control.type === 'number' && (
							<TextField type="number" label={control.label ?? field.label} value={num} onChange={(e) => setNum(Number(e.target.value))} fullWidth />
						)}
						{control && control.type === 'text' && (
							<TextField label={control.label ?? field.label} value={raw} onChange={(e) => setRaw(e.target.value)} fullWidth />
						)}
						{/* Fallbacks */}
						{!control && kind === 'number' && (
							<TextField type="number" label={field.label} value={num} onChange={(e) => setNum(Number(e.target.value))} fullWidth />
						)}
						{!control && kind === 'boolean' && (
							<FormControlLabel control={<Switch checked={bool} onChange={(e) => setBool(e.target.checked)} />} label={field.label} />
						)}
						{!control && kind === 'string' && (
							<TextField label={field.label} value={raw} onChange={(e) => setRaw(e.target.value)} fullWidth />
						)}
					</Stack>
				) : (
					<Typography>No field selected</Typography>
				)}
			</DialogContent>
			<DialogActions>
				<Button onClick={onClose}>Cancel</Button>
				{fields && fields.length > 0 ? (
					<Button onClick={handleSubmitMulti} variant="contained">Save</Button>
				) : (
					<Button onClick={handleSubmit} variant="contained">Save</Button>
				)}
			</DialogActions>
		</Dialog>
	)
}
