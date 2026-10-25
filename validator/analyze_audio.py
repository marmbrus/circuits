import sys, numpy as np
from scipy.io import wavfile
from scipy.signal import welch, find_peaks
sr, x = wavfile.read(sys.argv[1])
x = x.astype(np.float64) / 32768
dbfs = lambda v: 20*np.log10(max(v, 1e-12))
print(f"{len(x)/sr:.1f}s @ {sr} Hz  RMS {dbfs(np.sqrt(np.mean(x**2))):.1f} dBFS  peak {dbfs(np.abs(x).max()):.1f} dBFS  DC {x.mean():+.4f}")
# Loudness over time in 0.5s windows
win = sr // 2
print("RMS per 0.5s:", " ".join(f"{dbfs(np.sqrt(np.mean(x[i:i+win]**2))):.0f}" for i in range(0, len(x)-win+1, win)))
# Spectrum
f, p = welch(x, sr, nperseg=8192)
pdb = 10*np.log10(p + 1e-20)
bands = [(20,250,"low"),(250,2000,"mid"),(2000,8000,"high"),(8000,20000,"v.high")]
print("band energy dB:", "  ".join(f"{n} {10*np.log10(p[(f>=a)&(f<b)].sum()+1e-20):.0f}" for a,b,n in bands))
pk, _ = find_peaks(pdb, prominence=10)
top = sorted(pk, key=lambda i: pdb[i], reverse=True)[:8]
print("prominent tones (Hz, dB):", ", ".join(f"{f[i]:.0f} ({pdb[i]:.0f})" for i in sorted(top)) or "none")
