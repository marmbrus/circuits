import { Card, CardContent, CardHeader, Chip, CircularProgress, Dialog, DialogContent, DialogTitle, IconButton, Link, Stack, Tooltip, Typography, Accordion, AccordionSummary, AccordionDetails, Box } from '@mui/material'
import ExpandMoreIcon from '@mui/icons-material/ExpandMore'
import ContentCopyIcon from '@mui/icons-material/ContentCopy'
import { useEffect, useMemo, useState } from 'react'
import type { SensorState } from '../types'
import { useSensors } from '../mqttStore'
import SensorConfigView from './SensorConfig'
import FriendlyDuration, { formatDuration } from './FriendlyDuration'


type Props = {
	sensor: SensorState
}

export default function Sensor({ sensor }: Props) {
	const cfg = sensor.config
	const { publishConfig } = useSensors()
	const id = cfg?.tags.id ?? sensor.mac
	const macShort = sensor.mac.slice(-4)
	const ip = sensor.ip
	const [copied, setCopied] = useState(false)
	const [statusOpen, setStatusOpen] = useState(false)
	const [otaOpen, setOtaOpen] = useState(false)
	const [i2cOpen, setI2cOpen] = useState<{ open: boolean; device?: any } >({ open: false })
	const [metricOpen, setMetricOpen] = useState<{ open: boolean; data?: any }>({ open: false })
	const [bootOpen, setBootOpen] = useState(false)

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
	return (
		<Card variant="outlined">
			<CardHeader
				sx={{ pb: 0.5 }}
				title={
					<Stack direction="row" spacing={1} alignItems="baseline" sx={{ minHeight: 28 }}>
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
						</Stack>
						{ip && (
							<Link variant="caption" href={`https://${ip}`} target="_blank" rel="noreferrer">{ip}</Link>
						)}
					</Stack>
				}
				subheader={null}
				action={
					<Box sx={{ width: 24, height: 24, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
						<CircularProgress size={16} sx={{ visibility: sensor.pendingConfig ? 'visible' : 'hidden' }} />
					</Box>
				}
			/>
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
							label={`ota: ${String((sensor.otaStatus as any)?.status || 'unknown')}`}
							size="small"
							variant="outlined"
							onClick={(e) => { (e.currentTarget as HTMLElement).blur(); setOtaOpen(true) }}
						/>
						{sensor.deviceBoot && (
							<Chip
								label={`boot: ${sensor.deviceBootTs ? formatDuration(now - (sensor.deviceBootTs || 0)) : 'unknown'}`}
								size="small"
								variant="outlined"
								onClick={(e) => { (e.currentTarget as HTMLElement).blur(); setBootOpen(true) }}
							/>
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
					{sensor.i2c && sensor.i2c.devices?.length ? (
						<Accordion disableGutters elevation={0} defaultExpanded={false} sx={{ border: '1px solid', borderColor: 'divider', '&:before': { display: 'none' } }}>
							<AccordionSummary expandIcon={<ExpandMoreIcon />} sx={{ minHeight: 36, '& .MuiAccordionSummary-content': { my: 0.5 } }}>
								<Stack direction="row" spacing={1} alignItems="center">
									<Typography variant="subtitle2">I2C devices</Typography>
									<Chip label={String(sensor.i2c.devices.length)} size="small" />
								</Stack>
							</AccordionSummary>
							<AccordionDetails>
								<Stack direction="row" spacing={1} flexWrap="wrap" useFlexGap>
									{sensor.i2c.devices.map((d, idx) => (
										<Chip key={`${d.addr}-${idx}`} size="small" label={`${d.addr}${d.driver ? ` ${d.driver}` : ''}`} onClick={() => setI2cOpen({ open: true, device: d })} />
									))}
								</Stack>
							</AccordionDetails>
						</Accordion>
					) : null}
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
									const labelName = m.tags.name ?? m.tags.type ?? 'metric'
									const data = { metric: metricName, value: m.value, ts: m.ts, tags: m.tags }
									return (
										<Chip
											key={key}
											label={`${labelName}:${metricName}=${m.value.toFixed(3)}`}
											size="small"
											onClick={() => setMetricOpen({ open: true, data })}
										/>
									)
								})}
							</Stack>
						</AccordionDetails>
					</Accordion>
				</Stack>
			</CardContent>
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
					<pre style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>{JSON.stringify(metricOpen.data ?? {}, null, 2)}</pre>
				</DialogContent>
			</Dialog>
			<Dialog open={i2cOpen.open} onClose={() => setI2cOpen({ open: false })} fullWidth maxWidth="sm">
				<DialogTitle>I2C device</DialogTitle>
				<DialogContent>
					<pre style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>{JSON.stringify(i2cOpen.device ?? {}, null, 2)}</pre>
				</DialogContent>
			</Dialog>
		</Card>
	)
}


