import { Card, CardContent, CardHeader, Chip, CircularProgress, Dialog, DialogContent, DialogTitle, IconButton, Link, Stack, Tooltip, Typography, Accordion, AccordionSummary, AccordionDetails, Box, Collapse, FormControl, InputLabel, MenuItem, Select, Button } from '@mui/material'
import DialogActions from '@mui/material/DialogActions'
import TextField from '@mui/material/TextField'
import AnsiText from './AnsiText'
import ExpandMoreIcon from '@mui/icons-material/ExpandMore'
import ContentCopyIcon from '@mui/icons-material/ContentCopy'
import DeleteOutlineIcon from '@mui/icons-material/DeleteOutline'
import RestartAltIcon from '@mui/icons-material/RestartAlt'
import FileCopyIcon from '@mui/icons-material/FileCopy'
import ClearIcon from '@mui/icons-material/Clear'
import { useEffect, useMemo, useState } from 'react'
import type { SensorState } from '../types'
import { useSensors } from '../mqttStore'
import SensorConfigView from './SensorConfig'
import I2CConfigSection from './I2CConfig'
import FriendlyDuration, { formatDuration } from './FriendlyDuration'


type Props = {
	sensor: SensorState
	forceExpanded?: boolean
	onExpandedChange?: (expanded: boolean) => void
	forceLogsExpanded?: boolean
	onLogsExpandedChange?: (expanded: boolean) => void
}

export default function Sensor({ sensor, forceExpanded, onExpandedChange, forceLogsExpanded, onLogsExpandedChange }: Props) {
	const cfg = sensor.config
	const { sensors: allSensors, publishConfig, deleteRetainedForSensor, clearSensorLogs, restartSensor, resetConfig } = useSensors()
	const id = cfg?.tags.id ?? sensor.mac
	const macShort = sensor.mac.slice(-4)
	const ip = sensor.ip
	const [copied, setCopied] = useState(false)
	const [statusOpen, setStatusOpen] = useState(false)
	const [otaOpen, setOtaOpen] = useState(false)
	const [i2cOpen, setI2cOpen] = useState<{ open: boolean; device?: any } >({ open: false })
	const [metricOpen, setMetricOpen] = useState<{ open: boolean; data?: any }>({ open: false })
	const [bootOpen, setBootOpen] = useState(false)
	const [internalExpanded, setInternalExpanded] = useState(false)
	const expanded = forceExpanded ?? internalExpanded
  const [internalLogsExpanded, setInternalLogsExpanded] = useState(false)
	const logsExpanded = forceLogsExpanded ?? internalLogsExpanded

	// Clone dialog state
	const [cloneOpen, setCloneOpen] = useState(false)
	const [cloneSourceMac, setCloneSourceMac] = useState<string>('')
	const [draftJson, setDraftJson] = useState<string>('')
	const [draftError, setDraftError] = useState<string>('')

	const [nowMs, setNowMs] = useState<number>(Date.now())
	useEffect(() => {
		const t = setInterval(() => setNowMs(Date.now()), 1000)
		return () => clearInterval(t)
	}, [])

	const presentA2D = useMemo(() => (sensor.i2c?.devices || []).filter((x) => (x.module || '').startsWith('a2d')).map((x) => String(x.module)), [sensor.i2c])
	const presentIO = useMemo(() => {
		const ios: string[] = []
		for (const d of sensor.i2c?.devices || []) {
			if (!d.addr) continue
			const addrStr = String(d.addr)
			const hex = addrStr.startsWith('0x') ? parseInt(addrStr, 16) : Number(addrStr)
			if (!Number.isFinite(hex)) continue
			if (hex >= 0x20 && hex <= 0x27) {
				const index = hex - 0x20 + 1
				ios.push(`io${index}`)
			}
		}
		return Array.from(new Set(ios))
	}, [sensor.i2c])

	const now = nowMs
	const lastTs = sensor.deviceStatusTs ?? 0
	const hasStatus = lastTs > 0
	const ageMs = Math.max(0, now - lastTs)
	const fresh = hasStatus && ageMs <= 10000
	const stale = hasStatus && ageMs > 11000
	const statusColor: 'success' | 'warning' | 'default' = !hasStatus ? 'warning' : (fresh ? 'success' : (stale ? 'warning' : 'default'))
	const statusLabel = !hasStatus ? 'offline' : (fresh ? 'status: ok' : (stale ? `offline: ${formatDuration(ageMs)}` : 'status: waiting'))

	const handleCopy = async () => {
		try {
			await navigator.clipboard.writeText(sensor.mac)
			setCopied(true)
			setTimeout(() => setCopied(false), 1200)
		} catch {
			// ignore
		}
	}

	const candidates = useMemo(() => {
		const arr = Array.from(allSensors.values()).filter(s => s.mac !== sensor.mac && s.config)
		// Prefer sensors that have a known area (not 'unknown') first
		return arr.sort((a, b) => {
			const aUnknown = String(a.config?.tags?.area || 'unknown') === 'unknown'
			const bUnknown = String(b.config?.tags?.area || 'unknown') === 'unknown'
			return Number(aUnknown) - Number(bUnknown)
		})
	}, [allSensors, sensor.mac])

	function deepMergePreferB(a: any, b: any): any {
		if (Array.isArray(a) && Array.isArray(b)) {
			return b.length > 0 ? b : a
		}
		if (a && typeof a === 'object' && b && typeof b === 'object') {
			const out: any = { ...a }
			for (const k of Object.keys(b)) {
				out[k] = deepMergePreferB(a?.[k], b[k])
			}
			return out
		}
		return b !== undefined ? b : a
	}

	const openCloneDialog = () => {
		setCloneOpen(true)
		setCloneSourceMac('')
		setDraftJson('')
		setDraftError('')
	}

	const onSelectCloneSource = (mac: string) => {
		setCloneSourceMac(mac)
		try {
			const src = Array.from(allSensors.values()).find(s => s.mac === mac)
			const targetCfg = sensor.config || {}
			const srcCfg = (src?.config || {}) as any
			// Merge where unknown/target takes precedence
			const merged = deepMergePreferB(srcCfg, targetCfg)
			setDraftJson(JSON.stringify(merged, null, 2))
			setDraftError('')
		} catch (e: any) {
			setDraftError(String(e?.message || e))
		}
	}

	const onConfirmClone = () => {
		try {
			const parsed = JSON.parse(draftJson)
			resetConfig(sensor.mac, parsed)
			setCloneOpen(false)
		} catch (e: any) {
			setDraftError(`Invalid JSON: ${String(e?.message || e)}`)
		}
	}
	return (
		<Card variant="outlined" sx={{ maxWidth: { xs: 360, sm: 420, md: 480 }, mx: 'auto', overflow: 'visible' }}>
			<CardHeader
				sx={{ py: 0.5, pr: 2, overflow: 'visible', '& .MuiCardHeader-content': { overflow: 'hidden', minWidth: 0 }, '& .MuiCardHeader-action': { alignSelf: 'center', mt: 0, mr: 0, flexShrink: 0 } }}
				title={
					<Stack direction="row" spacing={1} alignItems="baseline" sx={{ minHeight: 36 }}>
						<Typography variant="h6">{id}</Typography>
						<Stack direction="row" alignItems="center" spacing={0.5}>
							<Tooltip title={sensor.mac} placement="top">
								<Typography variant="caption" color="text.secondary">{macShort}</Typography>
							</Tooltip>
							<Tooltip title={copied ? 'Copied' : 'Copy MAC'}>
								<IconButton size="small" onClick={handleCopy} aria-label="copy mac">
									<ContentCopyIcon fontSize="inherit" />
								</IconButton>
							</Tooltip>
							<Tooltip title="Delete retained topics">
								<IconButton size="small" onClick={() => deleteRetainedForSensor(sensor.mac)} aria-label="delete retained">
									<DeleteOutlineIcon fontSize="inherit" />
								</IconButton>
							</Tooltip>
							<Tooltip title="Restart device">
								<IconButton size="small" onClick={() => restartSensor(sensor.mac)} aria-label="restart">
									<RestartAltIcon fontSize="inherit" />
								</IconButton>
							</Tooltip>
						</Stack>
						{ip && (
							<Link variant="caption" href={`https://${ip}`} target="_blank" rel="noreferrer">{ip}</Link>
						)}
						{!expanded && (
							<Chip
								label={statusLabel}
								color={statusColor}
								size="small"
								onClick={(e) => { (e.currentTarget as HTMLElement).blur(); setStatusOpen(true) }}
							/>
						)}
					</Stack>
				}
				subheader={null}
				action={
					<Stack direction="row" spacing={0.5} alignItems="center">
						<Box sx={{ width: 24, height: 24, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
							<CircularProgress size={16} sx={{ visibility: sensor.pendingConfig ? 'visible' : 'hidden' }} />
						</Box>
						{String(cfg?.tags?.area || 'unknown') === 'unknown' && (
							<Tooltip title="Clone config from another sensor">
								<IconButton size="small" onClick={openCloneDialog} aria-label="clone">
									<FileCopyIcon fontSize="inherit" />
								</IconButton>
							</Tooltip>
						)}
						<IconButton
							size="small"
							onClick={() => {
								const newExpanded = !expanded
								if (forceExpanded === undefined) {
									setInternalExpanded(newExpanded)
								}
								onExpandedChange?.(newExpanded)
							}}
							aria-label={expanded ? 'collapse' : 'expand'}
							disableRipple
							disableFocusRipple
							sx={{ transform: expanded ? 'rotate(180deg)' : 'rotate(0deg)', transition: (theme) => (theme as any).transitions?.create?.('transform') || 'transform 150ms ease', '&:focus-visible': { outline: 'none' } }}
						>
							<ExpandMoreIcon fontSize="inherit" />
						</IconButton>
					</Stack>
				}
			/>
			<Collapse in={expanded} timeout="auto" unmountOnExit>
			<CardContent sx={{ pt: 0.5 }}>
				<Stack spacing={1} sx={{ mt: 0 }}>
					<Stack direction="row" spacing={1} alignItems="center" useFlexGap flexWrap="wrap">
						<Chip
							label={statusLabel}
							color={statusColor}
							size="small"
							onClick={(e) => { (e.currentTarget as HTMLElement).blur(); setStatusOpen(true) }}
						/>
						<Chip
							label={(() => {
								const o = (sensor.otaStatus as any) || {}
								const statusStr = String(o.status || 'unknown')
								const norm = statusStr.toLowerCase().replace(/[^a-z]/g, '')
								if (norm === 'uptodate') {
									const ver = String(o.firmware_local_version || o.web_local_version || '')
									if (ver) return `ota: ${ver.slice(0, 7)}`
								}
								return `ota: ${statusStr}`
							})()}
							size="small"
							variant="outlined"
							onClick={(e) => { (e.currentTarget as HTMLElement).blur(); setOtaOpen(true) }}
						/>
						{sensor.deviceBoot && (
							(() => {
								const bootTsIso = (sensor.deviceBoot as any)?.boot_ts as string | undefined
								let label = 'unknown'
								if (bootTsIso) {
									const t = Date.parse(bootTsIso)
									if (!Number.isNaN(t)) {
										label = formatDuration(now - t)
									}
								} else if (sensor.deviceBootTs) {
									label = `> ${formatDuration(now - sensor.deviceBootTs)}`
								}
								return (
									<Chip
										label={`boot: ${label}`}
										size="small"
										variant="outlined"
										onClick={(e) => { (e.currentTarget as HTMLElement).blur(); setBootOpen(true) }}
									/>
								)
							})()
						)}
					</Stack>
					{cfg && (
						<SensorConfigView
								mac={sensor.mac}
								config={cfg}
								publishConfig={publishConfig}
								presentA2DModules={presentA2D}
								presentIOModules={presentIO}
							/>
						)}
                    <I2CConfigSection sensor={sensor} publishConfig={publishConfig} />
					<Accordion disableGutters elevation={0} defaultExpanded={false} sx={{ border: '1px solid', borderColor: 'divider', '&:before': { display: 'none' } }}>
						<AccordionSummary expandIcon={<ExpandMoreIcon />} sx={{ minHeight: 36, '& .MuiAccordionSummary-content': { my: 0.5 } }}>
							<Stack direction="row" spacing={1} alignItems="center">
								<Typography variant="subtitle2">Metrics</Typography>
								<Chip label={String(Object.keys(sensor.metrics).length)} size="small" />
							</Stack>
						</AccordionSummary>
						<AccordionDetails>
							<Stack direction="row" spacing={1} flexWrap="wrap" useFlexGap>
								{Object.entries(sensor.metrics).map(([key, m]) => {
									const metricName = key.split('|')[0]
									const raw = (m as any).raw
									const labelName = raw?.tags?.name ?? raw?.tags?.type ?? 'metric'
									let color: 'default' | 'primary' | 'secondary' | 'success' | 'error' | 'warning' | 'info' = 'default'
									try {
										const t = Date.parse(String(raw?.ts))
										if (Number.isFinite(t)) {
											const fresh = (Date.now() - t) <= 10000
											if (fresh) color = 'warning'
										}
									} catch { /* ignore */ }
									return (
										<Chip
											key={key}
											label={`${labelName}:${metricName}=${raw?.value?.toFixed ? raw.value.toFixed(3) : String(raw?.value)}`}
											size="small"
											color={color}
											onClick={() => setMetricOpen({ open: true, key: key } as any)}
										/>
									)
								})}
							</Stack>
						</AccordionDetails>
					</Accordion>
					<Accordion disableGutters elevation={0} expanded={logsExpanded} onChange={(_, isExpanded) => {
						const newLogsExpanded = isExpanded
						if (forceLogsExpanded === undefined) {
							setInternalLogsExpanded(newLogsExpanded)
						}
						onLogsExpandedChange?.(newLogsExpanded)
					}} sx={{ border: '1px solid', borderColor: 'divider', '&:before': { display: 'none' } }}>
						<AccordionSummary expandIcon={<ExpandMoreIcon />} sx={{ minHeight: 36, '& .MuiAccordionSummary-content': { my: 0.5 } }}>
							<Stack direction="row" spacing={1} alignItems="center">
								<Typography variant="subtitle2">Logs</Typography>
								{(() => {
									const order = ['error','warn','info','debug','verbose']
									const counts: Record<string, number> = {}
									for (const e of (sensor.logs || [])) {
										const lvl = String(e.level || '').toLowerCase()
										counts[lvl] = (counts[lvl] || 0) + 1
									}
									const colorFor = (lvl: string): 'default' | 'primary' | 'secondary' | 'success' | 'error' | 'warning' | 'info' => {
										switch (lvl) {
											case 'error': return 'error'
											case 'warn': return 'warning'
											case 'info': return 'info'
											default: return 'default'
										}
									}
									return order.filter(l => (counts[l] || 0) > 0).map(l => (
										<Chip key={l} label={String(counts[l])} size="small" color={colorFor(l)} />
									))
								})()}
							</Stack>
						</AccordionSummary>
						<AccordionDetails>
							<Stack direction="row" spacing={1} alignItems="center" sx={{ mb: 1 }}>
								{(() => {
									const current = Number(sensor.config?.wifi?.loglevel ?? 2)
									const levels = [
										{ label: 'none', value: 0 },
										{ label: 'error', value: 1 },
										{ label: 'warn', value: 2 },
										{ label: 'info', value: 3 },
										{ label: 'debug', value: 4 },
										{ label: 'verbose', value: 5 },
									]
									return (
										<FormControl size="small" sx={{ minWidth: 140 }}>
											<InputLabel id={`loglevel-${sensor.mac}`}>Level</InputLabel>
											<Select
												labelId={`loglevel-${sensor.mac}`}
												label="Level"
												value={current}
												onChange={(e) => publishConfig(sensor.mac, 'wifi', 'loglevel', Number(e.target.value))}
											>
												{levels.map((l) => (
													<MenuItem key={l.value} value={l.value}>{l.label}</MenuItem>
												))}
											</Select>
										</FormControl>
									)
								})()}
								<Button
									size="small"
									variant="outlined"
									color="warning"
									startIcon={<ClearIcon />}
									onClick={() => clearSensorLogs(sensor.mac)}
									disabled={!sensor.logs || sensor.logs.length === 0}
									sx={{ minWidth: 'auto' }}
								>
									Clear
								</Button>
							</Stack>
							<Box sx={{ maxHeight: 240, overflow: 'auto', bgcolor: 'background.paper', borderRadius: 1, p: 1, fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace', fontSize: 12 }}>
								{(sensor.logs || []).slice(-500).map((entry, idx) => (
									<div key={idx} style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>
										<AnsiText text={entry.message} />
									</div>
								))}
							</Box>
						</AccordionDetails>
					</Accordion>
				</Stack>
			</CardContent>
			</Collapse>
			<Dialog open={statusOpen} onClose={() => setStatusOpen(false)} fullWidth maxWidth="sm">
				<DialogTitle>Device status</DialogTitle>
				<DialogContent>
					<pre style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>{JSON.stringify(sensor.deviceStatus ?? {}, null, 2)}</pre>
				</DialogContent>
			</Dialog>
			<Dialog open={otaOpen} onClose={() => setOtaOpen(false)} fullWidth maxWidth="sm">
				<DialogTitle>OTA status</DialogTitle>
				<DialogContent>
					{sensor.otaStatusTs ? (
						<p style={{ marginTop: 0 }}>Last updated: <FriendlyDuration fromMs={sensor.otaStatusTs} /></p>
					) : null}
					{(() => {
						const o = (sensor.otaStatus as any) || {}
						const fw = `fw: local ${String(o.firmware_local_version || 'n/a')} vs remote ${String(o.firmware_remote_version || 'n/a')}`
						const web = `web: local ${String(o.web_local_version || 'n/a')} vs remote ${String(o.web_remote_version || 'n/a')}${o.error ? ` — ${String(o.error)}` : ''}`
						return (
							<div style={{ fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace', fontSize: 12, marginBottom: 8 }}>
								<div>{fw}</div>
								<div>{web}</div>
							</div>
						)
					})()}
					<pre style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>{JSON.stringify(sensor.otaStatus ?? {}, null, 2)}</pre>
				</DialogContent>
			</Dialog>
			<Dialog open={bootOpen} onClose={() => setBootOpen(false)} fullWidth maxWidth="sm">
				<DialogTitle>Boot info</DialogTitle>
				<DialogContent>
					<pre style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>{JSON.stringify(sensor.deviceBoot ?? {}, null, 2)}</pre>
				</DialogContent>
			</Dialog>
			<Dialog open={metricOpen.open} onClose={() => setMetricOpen({ open: false })} fullWidth maxWidth="sm">
				<DialogTitle>Metric</DialogTitle>
				<DialogContent>
					<pre style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>{JSON.stringify((metricOpen as any).key ? sensor.metrics[(metricOpen as any).key!]?.raw ?? {} : {}, null, 2)}</pre>
				</DialogContent>
			</Dialog>
			<Dialog open={i2cOpen.open} onClose={() => setI2cOpen({ open: false })} fullWidth maxWidth="sm">
				<DialogTitle>I2C device</DialogTitle>
				<DialogContent>
					<pre style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>{JSON.stringify(i2cOpen.device ?? {}, null, 2)}</pre>
				</DialogContent>
			</Dialog>

			<Dialog open={cloneOpen} onClose={() => setCloneOpen(false)} fullWidth maxWidth="md">
				<DialogTitle>Clone configuration</DialogTitle>
				<DialogContent>
					<Stack spacing={1} sx={{ mt: 1 }}>
						<FormControl size="small" sx={{ minWidth: 240 }}>
							<InputLabel id={`clone-source-${sensor.mac}`}>Clone from</InputLabel>
							<Select
								labelId={`clone-source-${sensor.mac}`}
								label="Clone from"
								value={cloneSourceMac}
								onChange={(e) => onSelectCloneSource(String(e.target.value))}
							>
								{candidates.map(s => {
									const label = String(s.config?.tags?.id || s.mac)
									const area = String(s.config?.tags?.area || 'unknown')
									const room = String(s.config?.tags?.room || 'unknown')
									return (
										<MenuItem key={s.mac} value={s.mac}>{label} — {area}/{room}</MenuItem>
									)
								})}
							</Select>
						</FormControl>
						<TextField
							label="Merged configuration (editable JSON)"
							value={draftJson}
							onChange={(e) => setDraftJson(e.target.value)}
							multiline
							minRows={12}
							maxRows={24}
							fullWidth
							placeholder="Select a source sensor to generate a merged configuration"
							error={Boolean(draftError)}
							helperText={draftError || 'Original/unknown sensor values take precedence'}
						/>
					</Stack>
				</DialogContent>
				<DialogActions>
					<Button onClick={() => setCloneOpen(false)}>Cancel</Button>
					<Button variant="contained" disabled={!cloneSourceMac || !draftJson} onClick={onConfirmClone}>Apply</Button>
				</DialogActions>
			</Dialog>
		</Card>
	)
}



