"""The main menu's Act 3 sky: a heavy, overcast dusk over the jungle and Kurast's docks, as the act's own painted card
(ui_skybox/act3_sky - slate grey cloud masses, a cold blue-grey light breaking low along the horizon, damp mist over
the water), one 360-degree picture (equirectangular, 2:1), its bottom row at -10 degrees and its top at 90 ([frontend]
pano_low_3 / pano_high_3). The clouds lie on a flat layer overhead (so they crowd together towards the horizon); the
light breaks low ahead and a little to the right of the act's camera (it looks down -x: u 0.75). Ours, from noise.

  python gen_front_act3_sky.py <out.png> [width]
"""
import sys
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates

W = int(sys.argv[2]) if len(sys.argv) > 2 else 4096
H = W // 2
LOW, HIGH = -10.0, 90.0
LIGHT_U, LIGHT_EL = 0.75 + 12.0 / 360.0, 5.0
rng = np.random.default_rng(3003)
N = 1024


def noise(n, slope):
    """Periodic fractal noise on an n x n tile, mean 0, deviation 1."""
    f = np.fft.fft2(rng.standard_normal((n, n)))
    k = np.sqrt(np.fft.fftfreq(n)[:, None] ** 2 + np.fft.fftfreq(n)[None, :] ** 2) * n
    k[0, 0] = 1.0
    t = np.real(np.fft.ifft2(f / k ** slope))
    return (t - t.mean()) / t.std()


el = HIGH - (np.arange(H)[:, None] + 0.5) / H * (HIGH - LOW) * np.ones((1, W))   # degrees
az = (np.arange(W)[None, :] + 0.5) / W * np.ones((H, 1))                           # 0..1 round
# the cloud layer: a flat sheet overhead, seen at each direction's distance (crowding to the horizon)
d = 1.0 / np.tan(np.radians(np.clip(el, 1.5, 90.0)))
px, py = d * np.cos(az * 2 * np.pi), d * np.sin(az * 2 * np.pi)
masses, detail, holes = noise(N, 1.45), noise(N, 1.9), noise(N, 1.2)


def sample(t, scale):
    return map_coordinates(t, [(py * scale) % N, (px * scale) % N], order=1, mode='wrap')


m = 0.7 * sample(masses, 160.0) + 0.3 * sample(detail, 420.0)
g = sample(holes, 90.0)
cover = np.clip((m + 0.55) / 1.1, 0, 1) * np.clip((g + 1.4) / 1.2, 0, 1)        # 1 thick, 0 a gap
cover *= np.clip((el - 1.0) / 5.0, 0, 1) ** 1.5                                   # none below the horizon (into the mist)
tops = np.clip(0.5 + 0.5 * sample(detail, 260.0), 0, 1)                         # lit tops, dark undersides

# behind the clouds: mist at the horizon, slate higher up; the light low ahead-right
stops = np.array([-10, 0, 3, 10, 25, 50, 90], float)
cols = np.array([[66, 74, 74], [128, 138, 140], [122, 134, 140], [98, 110, 118],
                 [76, 84, 90], [58, 63, 67], [48, 52, 55]], float) / 255.0
base = np.stack([np.interp(el[:, 0], stops, cols[:, c]) for c in range(3)], -1)[:, None, :]
e, l_el = np.radians(el), np.radians(LIGHT_EL)
cosang = np.sin(e) * np.sin(l_el) + np.cos(e) * np.cos(l_el) * np.cos((az - LIGHT_U) * 2 * np.pi)
ang = np.degrees(np.arccos(np.clip(cosang, -1, 1)))
glow = (0.30 * np.exp(-ang / 10.0) + 0.14 * np.exp(-ang / 40.0))[..., None] * np.array([0.82, 0.92, 1.0])

cloud = np.interp(el, [-10, 4, 15, 40, 90], [0.40, 0.36, 0.30, 0.25, 0.21])[..., None] * np.array([0.97, 1.0, 1.02])
cloud = cloud * (0.70 + 0.55 * tops[..., None]) + glow * 0.6 * tops[..., None]
sky = base + glow
img = sky * (1 - cover[..., None]) + cloud * cover[..., None]
# mist low over the water: lighter and flatter below 4 degrees
mist = np.clip((4.0 - el) / 9.0, 0, 1)[..., None]
img = img * (1 - 0.6 * mist) + np.array([0.44, 0.48, 0.49]) * 0.6 * mist
img = np.clip(img, 0, 1)
Image.fromarray((img * 255 + 0.5).astype(np.uint8)).save(sys.argv[1], optimize=True)
print(sys.argv[1], W, H)
