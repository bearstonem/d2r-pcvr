"""The main menu's Act 2 sky: a burning yellow desert noon, one 360-degree picture (equirectangular, 2:1), its
bottom row at -10 degrees and its top at 90 ([frontend] pano_low_2 / pano_high_2). The sun ahead and a little to
the right of the act's camera (which looks down -x: u 0.75), low; dust haze in streaks. Our own, from noise.

  python gen_front_act2_sky.py <out.png> [width]
"""
import sys
import numpy as np
from PIL import Image

W = int(sys.argv[2]) if len(sys.argv) > 2 else 4096
H = W // 2
LOW, HIGH = -10.0, 90.0
SUN_U, SUN_EL = 0.75 - 18.0 / 360.0, 14.0
rng = np.random.default_rng(2002)


def noise(h, w, slope, stretch):
    """Periodic fractal noise (both ways), stretched along x by stretch."""
    f = np.fft.fft2(rng.standard_normal((h, w)))
    ky = np.fft.fftfreq(h)[:, None] * h
    kx = np.fft.fftfreq(w)[None, :] * w * stretch
    k = np.sqrt(kx * kx + ky * ky)
    k[0, 0] = 1.0
    n = np.real(np.fft.ifft2(f / k ** slope))
    return (n - n.mean()) / n.std()


el = HIGH - (np.arange(H)[:, None] + 0.5) / H * (HIGH - LOW)          # degrees, per row
az = (np.arange(W)[None, :] + 0.5) / W                                # 0..1 round
stops = np.array([-10, 0, 4, 12, 30, 55, 90], float)
cols = np.array([[214, 176, 112], [255, 244, 196], [255, 234, 160], [250, 212, 118],
                 [236, 178, 78], [206, 140, 50], [176, 112, 40]], float) / 255.0
base = np.stack([np.interp(el[:, 0], stops, cols[:, c]) for c in range(3)], -1)[:, None, :] * np.ones((1, W, 1))

# the sun: its angle from each direction
e, s_el = np.radians(el), np.radians(SUN_EL)
da = (az - SUN_U) * 2 * np.pi
cosang = np.sin(e) * np.sin(s_el) + np.cos(e) * np.cos(s_el) * np.cos(da)
ang = np.degrees(np.arccos(np.clip(cosang, -1, 1)))
glow = 0.9 * np.exp(-ang / 3.5) + 0.45 * np.exp(-ang / 16.0) + 0.22 * np.exp(-ang / 60.0)
core = np.clip((3.2 - ang) / 0.6, 0, 1)
sun_col = np.array([1.0, 0.93, 0.70])

# dust and thin hazy streaks, thicker low, none at the very top
haze = noise(H, W, 1.25, 3.0) * 0.6 + noise(H, W, 1.5, 7.0) * 0.4
band = np.exp(-((el - 8.0) / 22.0) ** 2) * 0.9 + 0.25 * np.clip((60.0 - el) / 60.0, 0, 1)
streak = (0.10 * haze * band)[..., None]

img = base * (1.0 + streak) + glow[..., None] * sun_col * (1.0 - 0.35 * np.clip(haze * band, -1, 1)[..., None] * 0.5)
img = img + core[..., None] * 1.5
img = img / (1.0 + 0.18 * np.clip(img - 1.0, 0, None))                 # soft shoulder: burnt, not clipped flat
img = np.clip(img, 0, 1)
Image.fromarray((img * 255 + 0.5).astype(np.uint8)).save(sys.argv[1], optimize=True)
print(sys.argv[1], W, H)
