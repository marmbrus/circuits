import { Card, CardContent, CardHeader, Chip, CircularProgress, Dialog, DialogContent, DialogTitle, Divider, IconButton, Link, Stack, Tooltip, Typography } from '@mui/material'
import ContentCopyIcon from '@mui/icons-material/ContentCopy'
import { useEffect, useState } from 'react'
import type { SensorState } from '../types'
import { useSensors } from '../mqttStore'
import SensorConfigView from './SensorConfig'

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

	const [nowMs, setNowMs] = useState<number>(Date.now())
	useEffect(() => {
		const t = setInterval(() => setNowMs(Date.now()), 1000)
		return () => clearInterval(t)
	}, [])

	const now = nowMs
	const lastTs = sensor.deviceStatusTs ?? 0
	const hasStatus = lastTs > 0
	const ageMs = Math.max(0, now - lastTs)
	const fresh = hasStatus && ageMs <= 10000
	const stale = hasStatus && ageMs > 11000
	const statusColor: 'success' | 'warning' | 'default' = !hasStatus ? 'warning' : (fresh ? 'success' : (stale ? 'warning' : 'default'))
	const statusLabel = !hasStatus ? 'offline' : (fresh ? 'status: ok' : (stale ? `offline: ${humanizeDuration(ageMs)}` : 'status: waiting'))

	function humanizeDuration(ms: number): string {
		const s = Math.floor(ms / 1000)
		if (s < 60) return `${s}s`
		const m = Math.floor(s / 60)
		if (m < 60) return `${m}m`
		const h = Math.floor(m / 60)
		return `${h}h ${m % 60}m`
	}

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
				title={id}
				subheader={
					<Stack direction="row" spacing={1} alignItems="center">
						<Tooltip title={sensor.mac} placement="top">
							<Typography variant="body2">{macShort}</Typography>
						</Tooltip>
						{ip && (
							<Link variant="body2" href={`https://${ip}`} target="_blank" rel="noreferrer">{ip}</Link>
						)}
						{sensor.pendingConfig && (
							<Stack direction="row" spacing={1} alignItems="center">
								<CircularProgress size={14} />
								<Typography variant="caption" color="text.secondary">applying…</Typography>
							</Stack>
						)}
					</Stack>
				}
				action={
					<Tooltip title={copied ? 'Copied' : 'Copy MAC'}>
						<IconButton size="small" onClick={handleCopy} aria-label="copy mac">
							<ContentCopyIcon fontSize="small" />
						</IconButton>
					</Tooltip>
				}
			/>
			<CardContent>
				<Stack spacing={1}>
					<Stack direction="row" spacing={1} alignItems="center">
						<Chip
							label={statusLabel}
							color={statusColor}
							size="small"
							onClick={() => setStatusOpen(true)}
						/>
					</Stack>
					{sensor.otaStatus && (
						<Stack direction="row" spacing={1} alignItems="center">
							<Chip
								label={`ota: ${String((sensor.otaStatus as any).status || 'unknown')}`}
								size="small"
								variant="outlined"
								onClick={() => setOtaOpen(true)}
							/>
						</Stack>
					)}
					{cfg && <SensorConfigView mac={sensor.mac} config={cfg} publishConfig={publishConfig} />}
					<Divider />
					<Typography variant="subtitle2">Latest metrics</Typography>
					<Stack direction="row" spacing={1} flexWrap="wrap" useFlexGap>
						{Object.entries(sensor.metrics).map(([key, m]) => (
							<Chip key={key} label={`${m.tags.type ?? m.tags.name ?? 'metric'}:${key.split('|')[0]}=${m.value.toFixed(3)}`} size="small" />
						))}
					</Stack>
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
					<pre style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>{JSON.stringify(sensor.otaStatus ?? {}, null, 2)}</pre>
				</DialogContent>
			</Dialog>
		</Card>
	)
}


