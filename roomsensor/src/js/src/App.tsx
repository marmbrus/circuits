import { AppBar, Box, Container, LinearProgress, Toolbar, Typography } from '@mui/material'
import './App.css'
import { useSensors } from './mqttStore'
import SensorGrid from './components/SensorGrid'

function App() {
  const { sensors, connectionStatus, lastError } = useSensors()

  return (
    <Box sx={{ bgcolor: 'background.default', color: 'text.primary', minHeight: '100vh' }}>
      <AppBar position="sticky" color="default" elevation={1}>
        <Toolbar>
          <Typography variant="h6" sx={{ flexGrow: 1 }}>Room Sensor Dashboard</Typography>
          <Typography variant="body2" color="text.secondary">
            {connectionStatus}
          </Typography>
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
