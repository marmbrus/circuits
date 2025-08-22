import { Card, CardContent, CardHeader, Chip, CircularProgress, Dialog, DialogContent, DialogTitle, Divider, IconButton, Link, Stack, Tooltip, Typography } from '@mui/material'
import ContentCopyIcon from '@mui/icons-material/ContentCopy'
import { useEffect, useState } from 'react'
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
				title={
					<Stack direction="row" spacing={1} alignItems="baseline">
						<Typography variant="h6">{id}</Typography>
						<Tooltip title={sensor.mac} placement="top">
							<Typography variant="caption" color="text.secondary">{macShort}</Typography>
						</Tooltip>
						{ip && (
							<Link variant="caption" href={`https://${ip}`} target="_blank" rel="noreferrer">{ip}</Link>
						)}
					</Stack>
				}
				subheader={
					<Stack direction="row" spacing={1} alignItems="center" sx={{ mt: 0.5 }}>
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
				<Stack spacing={1} sx={{ mt: 0 }}>
					<Stack direction="row" spacing={1} alignItems="center" useFlexGap flexWrap="wrap">
						<Chip
							label={statusLabel}
							color={statusColor}
							size="small"
							onClick={(e) => { (e.currentTarget as HTMLElement).blur(); setStatusOpen(true) }}
						/>
						{sensor.otaStatus && (
							<Chip
								label={`ota: ${String((sensor.otaStatus as any).status || 'unknown')}`}
								size="small"
								variant="outlined"
								onClick={(e) => { (e.currentTarget as HTMLElement).blur(); setOtaOpen(true) }}
							/>
						)}
					</Stack>
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
					{sensor.otaStatusTs ? (
						<p style={{ marginTop: 0 }}>Last updated: <FriendlyDuration fromMs={sensor.otaStatusTs} /></p>
					) : null}
					<pre style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>{JSON.stringify(sensor.otaStatus ?? {}, null, 2)}</pre>
				</DialogContent>
			</Dialog>
		</Card>
	)
}


