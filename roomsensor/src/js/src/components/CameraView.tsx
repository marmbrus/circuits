import { useMemo, useState } from 'react'

type Props = {
  frame: Int16Array
}

export default function CameraView({ frame }: Props) {
  const [useFixed, setUseFixed] = useState<boolean>(true)
  const [minFInput, setMinFInput] = useState<number>(75)
  const [maxFInput, setMaxFInput] = useState<number>(85)

  const { dataUrl, minF, maxF } = useMemo(() => {
    const cols = 8, rows = 8
    // Determine scale bounds (°F) based on mode
    const fixedMinF = Math.min(minFInput, maxFInput)
    const fixedMaxF = Math.max(minFInput, maxFInput)
    // If auto, derive from data
    let autoMinC = Infinity
    let autoMaxC = -Infinity
    if (!useFixed) {
      for (let i = 0; i < frame.length; i++) {
        const c = frame[i] * 0.25
        if (c < autoMinC) autoMinC = c
        if (c > autoMaxC) autoMaxC = c
      }
    }

    const minF = useFixed ? fixedMinF : (autoMinC * 9/5 + 32)
    const maxF = useFixed ? fixedMaxF : (autoMaxC * 9/5 + 32)
    const min = (minF - 32) * 5/9
    const max = (maxF - 32) * 5/9
    const range = Math.max(1e-3, max - min)
    const scale = 255 / range
    const canvas = document.createElement('canvas')
    canvas.width = cols
    canvas.height = rows
    const ctx = canvas.getContext('2d')!
    const img = ctx.createImageData(cols, rows)
    for (let y = 0; y < rows; y++) {
      for (let x = 0; x < cols; x++) {
        // Rotate 180°: map to source index flipped in both axes
        const srcY = rows - 1 - y
        const srcX = cols - 1 - x
        const idx = srcY * cols + srcX
        const c = frame[idx] * 0.25
        // Simple grayscale palette with fixed [min,max] in °C
        const v = Math.max(0, Math.min(255, Math.round((c - min) * scale)))
        const p = (y * cols + x) * 4
        img.data[p + 0] = v
        img.data[p + 1] = v
        img.data[p + 2] = v
        img.data[p + 3] = 255
      }
    }
    ctx.putImageData(img, 0, 0)
    // Upscale for visibility
    const out = document.createElement('canvas')
    const scaleFactor = 16
    out.width = cols * scaleFactor
    out.height = rows * scaleFactor
    const octx = out.getContext('2d')!
    octx.imageSmoothingEnabled = false
    octx.drawImage(canvas, 0, 0, out.width, out.height)
    return { dataUrl: out.toDataURL(), minF, maxF }
  }, [frame, useFixed, minFInput, maxFInput])

  return (
    <div style={{ display: 'grid', gap: 8 }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <label style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
          <input type="checkbox" checked={useFixed} onChange={(e) => setUseFixed(e.target.checked)} />
          <span>Fixed scale</span>
        </label>
        {useFixed && (
          <>
            <label style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }}>
              <span>Min (°F)</span>
              <input type="number" value={minFInput} onChange={(e) => setMinFInput(Number(e.target.value))} style={{ width: 72 }} />
            </label>
            <label style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }}>
              <span>Max (°F)</span>
              <input type="number" value={maxFInput} onChange={(e) => setMaxFInput(Number(e.target.value))} style={{ width: 72 }} />
            </label>
          </>
        )}
      </div>
      <img src={dataUrl} alt="IR frame" style={{ width: '100%', maxWidth: 256, imageRendering: 'pixelated' }} />
      <div style={{ display: 'grid', gridTemplateColumns: 'auto 1fr auto', alignItems: 'center', gap: 8, maxWidth: 256 }}>
        <span style={{ fontVariantNumeric: 'tabular-nums' }}>{minF.toFixed(1)}°F</span>
        <div style={{ height: 8, background: 'linear-gradient(90deg, #000 0%, #fff 100%)', borderRadius: 4 }} />
        <span style={{ textAlign: 'right', fontVariantNumeric: 'tabular-nums' }}>{maxF.toFixed(1)}°F</span>
      </div>
    </div>
  )
}


