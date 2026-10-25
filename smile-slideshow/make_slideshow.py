#!/usr/bin/env python3
"""世界中の人々の「笑顔・応援・拍手」写真で30秒のスライド映像を作る。

画像は Wikimedia Commons から自由ライセンス（CC0 / Public domain / CC BY / CC BY-SA）
のものだけを取得し、各スライドに作者とライセンスを表示する。

  python3 -I make_slideshow.py fetch    # 画像とクレジットを取得 -> work/
  python3 -I make_slideshow.py sheet    # 取得画像の一覧 (contact_sheet.jpg) を作って目視確認
  python3 -I make_slideshow.py build    # work/ の画像から slideshow.mp4 を生成

除外したい画像は work/credits.json から該当エントリを消して build し直す。
"""
import html
import json
import re
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

HERE = Path(__file__).resolve().parent
WORK = HERE / "work"
RAW = WORK / "raw"
FRAMES = WORK / "frames"
CREDITS = WORK / "credits.json"
OUT = HERE / "slideshow.mp4"

API = "https://commons.wikimedia.org/w/api.php"
UA = "SmileSlideshow/1.0 (https://github.com/werethewire/test)"

W, H, FPS = 1920, 1080, 30
TOTAL = 30.0   # 秒
XFADE = 0.5    # クロスフェード秒
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

# 国・地域 / 年代 / 性別 / 動作（笑顔・応援・拍手）が偏らないように並べた検索語。
# 1クエリにつき1枚採用し、上から順に並べる。
QUERIES = [
    "smiling girl portrait Africa",
    "football fans cheering stadium",
    "smiling elderly man portrait",
    "audience applause clapping",
    "smiling Japanese woman",
    "children cheering school",
    "smiling Indian grandmother",
    "people clapping hands celebration",
    "smiling Brazilian man",
    "fans cheering Olympic games",
    "smiling Mongolian child",
    "smiling Inuit woman",
    "elderly women laughing",
    "crowd cheering festival",
    "smiling man Middle East portrait",
    "smiling Aboriginal Australian",
    "students applauding graduation",
    "smiling Peruvian woman",
]
SLIDES = 15

OK_LICENSE = re.compile(r"^(cc0|public domain|pd|cc by(-sa)? [\d.]+)", re.I)


def api(params):
    params = {**params, "format": "json", "formatversion": "2"}
    req = urllib.request.Request(API + "?" + urllib.parse.urlencode(params),
                                 headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def strip_html(s):
    return html.unescape(re.sub(r"<[^>]+>", "", s or "")).strip()


def candidates(query):
    data = api({
        "action": "query", "generator": "search",
        "gsrsearch": f"filetype:bitmap {query}", "gsrnamespace": 6, "gsrlimit": 20,
        "prop": "imageinfo", "iiprop": "url|size|mime|extmetadata",
        "iiurlwidth": W,
    })
    pages = sorted(data.get("query", {}).get("pages", []), key=lambda p: p.get("index", 0))
    for p in pages:
        ii = (p.get("imageinfo") or [{}])[0]
        meta = ii.get("extmetadata", {})
        lic = strip_html(meta.get("LicenseShortName", {}).get("value"))
        if ii.get("mime") != "image/jpeg" or not OK_LICENSE.match(lic):
            continue
        if ii.get("width", 0) < 1200 or ii.get("width", 0) < ii.get("height", 1):
            continue  # 横長かつ十分な解像度のものだけ
        yield {
            "title": p["title"],
            "page": ii.get("descriptionurl"),
            "url": ii.get("thumburl") or ii["url"],
            "author": strip_html(meta.get("Artist", {}).get("value")) or "Unknown",
            "license": lic,
            "query": query,
        }


def fetch():
    RAW.mkdir(parents=True, exist_ok=True)
    picked, seen = [], set()
    for q in QUERIES:
        for c in candidates(q):
            if c["title"] in seen:
                continue
            dest = RAW / f"{len(picked):02d}.jpg"
            req = urllib.request.Request(c["url"], headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=60) as r:
                dest.write_bytes(r.read())
            c["file"] = dest.name
            picked.append(c)
            seen.add(c["title"])
            print(f"[{len(picked):2d}] {q} -> {c['title']} ({c['license']})")
            break
        else:
            print(f"[--] {q} -> 該当なし")
        time.sleep(1)  # Wikimedia への負荷配慮
    CREDITS.write_text(json.dumps(picked, ensure_ascii=False, indent=2))
    write_credits_md(picked)


def write_credits_md(items):
    lines = ["# Credits", "", "画像はすべて Wikimedia Commons より。", ""]
    for i, c in enumerate(items, 1):
        lines.append(f"{i}. [{c['title']}]({c['page']}) — {c['author']} / {c['license']}")
    (HERE / "CREDITS.md").write_text("\n".join(lines) + "\n")


def load_items():
    return json.loads(CREDITS.read_text())[:SLIDES]


def contact_sheet():
    items = load_items()
    tw, th, cols = 480, 270, 4
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new("RGB", (tw * cols, th * rows), "black")
    font = ImageFont.truetype(FONT, 22)
    for i, c in enumerate(items):
        im = ImageOps.fit(Image.open(RAW / c["file"]).convert("RGB"), (tw, th))
        ImageDraw.Draw(im).text((8, 8), f"{i:02d}", font=font, fill="white",
                                stroke_width=2, stroke_fill="black")
        sheet.paste(im, ((i % cols) * tw, (i // cols) * th))
    sheet.save(WORK / "contact_sheet.jpg", quality=85)
    print(WORK / "contact_sheet.jpg")


def render_frame(src, dest):
    # 2倍サイズで用意しておくと zoompan のガタつきが減る
    im = ImageOps.fit(Image.open(src).convert("RGB"), (W * 2, H * 2), Image.LANCZOS)
    im.save(dest, quality=92)


def render_caption(dest, caption):
    # ズームで切れないよう、クレジットはズーム後に重ねる透過レイヤーにする
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    font = ImageFont.truetype(FONT, 24)
    x0, y0, x1, y1 = d.textbbox((0, 0), caption, font=font)
    pad, margin = 10, 24
    bx, by = W - margin - pad - (x1 - x0), H - margin - pad - (y1 - y0)
    d.rounded_rectangle([bx - pad, by - pad, W - margin, H - margin], 6, fill=(0, 0, 0, 150))
    d.text((bx, by - y0), caption, font=font, fill=(240, 240, 240, 255))
    im.save(dest)


def build():
    items = load_items()
    n = len(items)
    if n < 2:
        sys.exit("画像が足りません。先に fetch を実行してください。")
    FRAMES.mkdir(parents=True, exist_ok=True)
    for i, c in enumerate(items):
        author = c["author"] if len(c["author"]) <= 40 else c["author"][:39] + "…"
        render_frame(RAW / c["file"], FRAMES / f"{i:02d}.jpg")
        render_caption(FRAMES / f"{i:02d}_cap.png",
                       f"Photo: {author} / {c['license']} via Wikimedia Commons")

    # n*dur - (n-1)*XFADE = TOTAL
    dur = (TOTAL + (n - 1) * XFADE) / n
    frames = round(dur * FPS)
    inputs, chains = [], []
    for i in range(n):
        inputs += ["-loop", "1", "-t", f"{dur:.3f}", "-i", str(FRAMES / f"{i:02d}.jpg"),
                   "-loop", "1", "-t", f"{dur:.3f}", "-i", str(FRAMES / f"{i:02d}_cap.png")]
        # 偶数はズームイン、奇数はズームアウトでゆっくり動かす
        z = (f"1+0.08*on/{frames}" if i % 2 == 0 else f"1.08-0.08*on/{frames}")
        chains.append(
            f"[{2 * i}:v]zoompan=z='{z}':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)'"
            f":d={frames}:s={W}x{H}:fps={FPS},setsar=1[z{i}];"
            f"[z{i}][{2 * i + 1}:v]overlay=0:0:shortest=1,format=yuv420p[v{i}]")
    prev = "v0"
    for i in range(1, n):
        off = i * (dur - XFADE)
        chains.append(f"[{prev}][v{i}]xfade=transition=fade:duration={XFADE}"
                      f":offset={off:.3f}[x{i}]")
        prev = f"x{i}"
    chains.append(f"[{prev}]fade=t=in:st=0:d=0.5,fade=t=out:st={TOTAL - 0.8}:d=0.8[out]")
    cmd = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", *inputs,
           "-filter_complex", ";".join(chains), "-map", "[out]",
           "-t", str(TOTAL), "-r", str(FPS), "-c:v", "libx264", "-preset", "medium",
           "-crf", "20", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(OUT)]
    subprocess.run(cmd, check=True)
    write_credits_md(items)
    print(OUT)


if __name__ == "__main__":
    cmds = {"fetch": fetch, "sheet": contact_sheet, "build": build}
    if len(sys.argv) != 2 or sys.argv[1] not in cmds:
        sys.exit(__doc__)
    cmds[sys.argv[1]]()
