import sys, cv2, numpy as np
img = cv2.imread(sys.argv[1])
h, w = img.shape[:2]
hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
print(f"size {w}x{h}, mean brightness {hsv[...,2].mean():.0f}/255, mean BGR {img.reshape(-1,3).mean(0).round(0)}")
# Bright, saturated blobs = candidate lit LEDs / indicator lights
mask = ((hsv[...,1] > 120) & (hsv[...,2] > 150)).astype(np.uint8)
mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, np.ones((3,3),np.uint8))
n, lab, stats, cent = cv2.connectedComponentsWithStats(mask)
names = [(10,"red"),(25,"orange"),(35,"yellow"),(85,"green"),(100,"cyan"),(130,"blue"),(160,"magenta"),(180,"red")]
blobs = []
for i in range(1, n):
    x,y,bw,bh,area = stats[i]
    if area < 20: continue
    px = hsv[lab == i]
    hue = int(np.median(px[:,0]))
    name = next(nm for lim,nm in names if hue <= lim)
    bgr = img[lab == i].mean(0).round(0).astype(int)
    blobs.append((area, f"  {name:8s} hue={hue:3d} area={area:5d}px center=({int(cent[i][0])},{int(cent[i][1])}) RGB={tuple(bgr[::-1])}"))
print(f"{len(blobs)} bright saturated blobs:")
for _, s in sorted(blobs, reverse=True)[:10]: print(s)
