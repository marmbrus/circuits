import { AppBar, Box, Button, Chip, Container, LinearProgress, Stack, Toolbar, Typography } from '@mui/material'
import './App.css'
import { useSensors } from './mqttStore'
import SensorGrid from './components/SensorGrid'
import VersionsChip from './components/VersionsChip'
import RestartAltIcon from '@mui/icons-material/RestartAlt'

function App() {
  const { sensors, connectionStatus, lastError, restartAllSensors } = useSensors()

  return (
    <Box sx={{ bgcolor: 'background.default', color: 'text.primary', minHeight: '100vh' }}>
      <AppBar position="sticky" color="default" elevation={1}>
        <Toolbar>
          <Typography variant="h6" sx={{ flexGrow: 1 }}>
            Sensors
            {import.meta.env.DEV && (
              <Box component="span" sx={{ ml: 1, color: 'warning.main', fontSize: '0.85rem', fontWeight: 600 }}>
                🚧 development
              </Box>
            )}
          </Typography>
          <Stack direction="row" spacing={1} alignItems="center">
            <Chip
              label={connectionStatus}
              size="small"
              color={
                connectionStatus === 'connected' ? 'success'
                : (connectionStatus === 'connecting' || connectionStatus === 'reconnecting') ? 'warning'
                : (connectionStatus === 'error') ? 'error'
                : 'default'
              }
              variant={connectionStatus === 'connected' ? 'filled' : 'outlined'}
            />
            <VersionsChip sensors={sensors} />
            <Button
              variant="outlined"
              size="small"
              startIcon={<RestartAltIcon />}
              onClick={() => {
                if (window.confirm('Are you sure you want to restart all sensors?')) {
                  restartAllSensors()
                }
              }}
              disabled={sensors.size === 0}
            >
              Restart All
            </Button>
          </Stack>
        </Toolbar>
        {(connectionStatus === 'connecting' || connectionStatus === 'reconnecting') && <LinearProgress />}
      </AppBar>
      <Container sx={{ py: 2 }}>
        {lastError && (
          <Typography variant="body2" color="error" sx={{ mb: 2 }}>{lastError}</Typography>
        )}
        <SensorGrid sensors={sensors} />
      </Container>
    </Box>
  )
}

export default App
