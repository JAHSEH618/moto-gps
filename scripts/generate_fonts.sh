#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
symbols='建国路东三环中朝阳门外大街进入岛，第二出口已到达目的地前方拥堵缓行畅通偏航重新规划断网恢复直左右转掉头米公里分钟高速辅主道匝隧桥请保持车驶向终点当前路线热点在线离线获取定位无信号等待距下一动作打开手机应用正在建立连接成功选择'

cd "${repo_dir}"
npx --yes lv_font_conv \
  --font third_party/lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf \
  --size 16 \
  --bpp 4 \
  --format lvgl \
  --no-compress \
  --symbols "${symbols}" \
  -o shared/nav_ui/assets/moto_font_nav_16.c

# Display face. The device used LVGL's Montserrat, which is a geometric
# regular: too light to carry a round instrument panel in daylight. Barlow
# Condensed ExtraBold is the face DESIGN.md already specifies for the phone's
# Heat theme, so the radar and the phone read as one product.
display_symbols='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,:/+-%()[]<>?!°·—'
# Text face for phone-supplied strings. The instrument fonts are Latin-only;
# a Chinese track title used to render as empty boxes. GB2312 level 1 covers
# the common Chinese character set, and the ASCII block keeps Latin titles on
# the same face when this file is used directly. Noto Sans SC Bold (SIL OFL)
# rather than Source Han Sans Normal: a regular-weight CJK title next to the
# Barlow ExtraBold hero read as a different product.
text_symbols="$(python3 -c '
chars = []
# Both GB2312 levels. Level 1 is the 3,755 everyday characters a UI string
# needs; level 2 is the 3,008 rarer ones that still turn up in song titles and
# place names (嚯, 鑫, 淼). Shipping only level 1 silently dropped those
# characters from a title — the font was confidently wrong, not merely thin.
for hi in range(0xB0, 0xF8):
    for lo in range(0xA1, 0xFF):
        try:
            chars.append(bytes([hi, lo]).decode("gb2312"))
        except UnicodeDecodeError:
            pass
extra = "，。！？：；、（）《》“”‘’—…·"
print("".join(chr(c) for c in range(0x20, 0x7F)) + extra + "".join(chars), end="")
')"
for size in 40 24; do
  npx --yes lv_font_conv \
    --font assets/fonts/NotoSansSC-Bold.otf \
    --size "${size}" \
    --bpp 2 \
    --format lvgl \
    --no-compress \
    --symbols "${text_symbols}" \
    --force-fast-kern-format \
    -o "shared/nav_ui/assets/moto_font_text_${size}.c"
done

# Hero face: the one number the rider reads at speed. 176 px of condensed
# extra bold fits "42°" inside the disc, and the subset keeps the flash cost
# of a display-size face down to the glyphs a readout can actually print.
hero_symbols='0123456789°.:+-/ NSEW'
npx --yes lv_font_conv \
  --font assets/fonts/BarlowCondensed-ExtraBold.ttf \
  --size 176 \
  --bpp 2 \
  --format lvgl \
  --no-compress \
  --symbols "${hero_symbols}" \
  --force-fast-kern-format \
  -o shared/nav_ui/assets/moto_font_hero_176.c

for spec in "72:2" "40:4" "24:4" "18:4"; do
  size="${spec%%:*}"
  bpp="${spec##*:}"
  npx --yes lv_font_conv \
    --font assets/fonts/BarlowCondensed-ExtraBold.ttf \
    --size "${size}" \
    --bpp "${bpp}" \
    --format lvgl \
    --no-compress \
    --symbols "${display_symbols}" \
    --force-fast-kern-format \
    -o "shared/nav_ui/assets/moto_font_display_${size}.c"
done
