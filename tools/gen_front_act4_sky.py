"""The main menu's Act 4 sky: the River of Flame's own sky as the game's level shows it in VR ([sky] texture_act4 /
cap_act4 - smoky dark clouds lit red-orange from below), turned into one 360-degree picture for the menu (owner
2026-11-15: "niebo ma być jak ... wyciągnięte z levelu rzeki"). Equirectangular, 2:1, its bottom row at -10 degrees and
its top at 90 ([frontend] pano_low_4 / pano_high_4), sampled as D2R_Sky.fxh's PaintedSky does: the band once round
(its bottom 8 degrees under the horizon, its top 65 up), handing over to the cap (the clouds straight overhead,
laid flat) between 40 and 62 degrees; NIGHT mixes in the night pictures (0 day .. 1 night), DARK darkens it all.

  python gen_front_act4_sky.py <Textures\\D2R_Sky_ours folder> <out.png> [night 0..1] [dark 0..1] [width]
"""
import sys
from pathlib import Path
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates

src = Path(sys.argv[1])
NIGHT = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
DARK = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
W = int(sys.argv[5]) if len(sys.argv) > 5 else 4096
H = W // 2
LOW, HIGH = -10.0, 90.0


def load(name, size):
    return np.asarray(Image.open(src / name).convert('RGB').resize(size, Image.LANCZOS), np.float32) / 255.0


band = load('D2R_Sky_act4.png', (3072, 1024)) * (1 - NIGHT) + load('D2R_SkyNight_act4.png', (3072, 1024)) * NIGHT
cap = load('D2R_SkyCap_act4.png', (1024, 1024)) * (1 - NIGHT) + load('D2R_SkyCapNight_act4.png', (1024, 1024)) * NIGHT

el = HIGH - (np.arange(H)[:, None] + 0.5) / H * (HIGH - LOW) * np.ones((1, W))
az = ((np.arange(W)[None, :] + 0.5) / W - 0.5) * 2 * np.pi * np.ones((H, 1))   # atan2(x, z): -pi..pi
e = np.radians(el)
dx, dy, dz = np.cos(e) * np.sin(az), np.sin(e), np.cos(e) * np.cos(az)


def sample(img, u, v, wrap_v):
    h, w = img.shape[:2]
    x = (u * w - 0.5) % w
    y = v * h - 0.5
    y = y % h if wrap_v else np.clip(y, 0, h - 1)
    return np.stack([map_coordinates(img[..., c], [y, x], order=1, mode='grid-wrap') for c in range(3)], -1)


u = (az / np.pi) * 0.5
v = np.clip((65.0 - el) / 73.0, 0, 1)
c = sample(band, u, v, False)
wt = np.clip((el - 40.0) / 22.0, 0, 1)
wt = wt * wt * (3 - 2 * wt)
p = np.stack([dx, dz], -1) / np.maximum(dy, 0.2)[..., None] * 0.4
z = sample(cap, p[..., 0], p[..., 1], True)
img = np.clip((c * (1 - wt[..., None]) + z * wt[..., None]) * DARK, 0, 1)
Image.fromarray((img * 255 + 0.5).astype(np.uint8)).save(sys.argv[2], optimize=True)
print(sys.argv[2], W, H)
