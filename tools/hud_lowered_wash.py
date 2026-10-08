"""python tools/hud_lowered_wash.py gamedata/textures/ui/ui_hud_wpn_lowered.dds  (writes wash_mock.png preview to cwd)

ui_hud_wpn_lowered.dds: the dark cover of the lowered weapon over the HUD's ammo screen (CUIHudStatesWnd,
static_wpn_lowered). Its shape is tools/hud_lowered_wash_mask.png (black = wash), 139x39 in the HUD back's own
pixels (ui_hud atlas, ui_hud2_rad01_back @ 758,903 266x121), lying at X0,Y0 of the back -- which is where the
static sits in maingame.xml. PAD_L more columns on the left carry each row's left edge further out (the
mask stopped short of the frame there): the static is x=18-PAD_L y=73 (139+PAD_L)x39, maingame_16.xml the
same times 222/266.
It dims the screen evenly (COVER_RGB at COVER_A), icon and count under it included -- the screen "on standby"."""
import os, struct, sys
from PIL import Image, ImageFilter
G = "I:/SteamLibrary/steamapps/common/STALKER Clear Sky/gamedata/textures/"
X0, Y0 = 18, 73
PAD_L = 3
OW, OH = 512, 128
COVER_RGB = (4, 6, 12)                  # the screen's own dark navy
COVER_A = 0.62

src = Image.open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "hud_lowered_wash_mask.png")).convert("RGB")
W0, H = src.size
W = W0 + PAD_L
X0 -= PAD_L
m = Image.new("L", (W, H), 0)
for y in range(H):
    row = [sum(src.getpixel((x, y))) < 200 for x in range(W0)]
    for x in range(W0):
        if row[x]:
            m.putpixel((PAD_L + x, y), 255)
    if True in row:                                             # the row's left edge, PAD_L further out
        for x in range(row.index(True), row.index(True) + PAD_L):
            m.putpixel((x, y), 255)
# the bottom edge fades out over its last FADE_B rows (not over the fire-mode square, where the wash meets the frame)
FADE_B = 2
for x in range(W):
    col = [m.getpixel((x, y)) for y in range(H)]
    if 255 not in col:
        continue
    bot = H - 1 - col[::-1].index(255)
    if bot < H - 4:                     # over the fire-mode square: no fade (it showed as a gap above the frame)
        continue
    for i in range(FADE_B):
        y = bot - i
        if y >= 0 and col[y] == 255:
            m.putpixel((x, y), int(round(255 * (i + 1) / (FADE_B + 1.0))))
m = m.resize((OW, OH), Image.BICUBIC).filter(ImageFilter.GaussianBlur(1.6))   # a soft edge

out = Image.new("RGBA", (OW, OH))
px = out.load(); mp = m.load()
for y in range(OH):
    for x in range(OW):
        k = mp[x, y] / 255.0
        px[x, y] = COVER_RGB + (int(round(255 * k * COVER_A)),)

# A8R8G8B8 dds, no mips
hdr = struct.pack('<4sIIIIIII44sIIIIIIIIIIIII', b'DDS ', 124, 0x100F, OH, OW, OW * 4, 0, 1, b'\0' * 44,
                  32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 0x1000, 0, 0, 0, 0)
data = bytearray()
for (r, g, bb, a) in out.get_flattened_data():
    data += bytes((bb, g, r, a))
open(sys.argv[1], 'wb').write(hdr + bytes(data))

# preview over the back
b = Image.open(G + "ui/ui_hud.dds").convert("RGBA").crop((758, 903, 758 + 266, 903 + 121))
pv = Image.new("RGBA", (266, 121), (60, 60, 60, 255)); pv.alpha_composite(b)
pv.alpha_composite(out.resize((W, H), Image.BILINEAR), (X0, Y0))
pv.resize((266 * 3, 121 * 3), Image.LANCZOS).save("wash_mock.png")
