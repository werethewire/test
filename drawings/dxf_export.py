"""Convert matplotlib sheets (paper mm @1:100) into one real-size DXF (1 unit = 1 mm)."""
import ezdxf
from ezdxf.enums import TextEntityAlignment
from matplotlib.colors import to_rgb
from matplotlib.lines import Line2D
from matplotlib.patches import Circle, Patch
from matplotlib.text import Annotation, Text

SCALE = 100.0  # paper mm -> real mm

LAYERS = {  # name: ACI colour
    "A-WALL": 7, "A-GLASS": 1, "A-COLS": 8, "A-FURN": 4, "A-WOOD": 30, "A-PLANT": 3,
    "A-CEIL": 9, "A-DIMS": 2, "A-TEXT": 7, "A-FILL": 254, "A-FRAME": 7,
}
CMAP = {"#c8102e": "A-GLASS", "#3c6e27": "A-PLANT", "#8a5a2b": "A-WOOD",
        "#7fb069": "A-PLANT", "#cfe3c4": "A-PLANT", "#93c47d": "A-PLANT", "#a9d18e": "A-PLANT",
        "#a8a8a8": "A-COLS", "#9e9e9e": "A-WALL", "#666666": "A-CEIL", "#888888": "A-CEIL"}

def _hex(c):
    r, g, b = to_rgb(c)
    return "#%02x%02x%02x" % (round(r * 255), round(g * 255), round(b * 255))

def _layer(art, colour):
    z = art.get_zorder()
    ls = getattr(art, "get_linestyle", lambda: "-")()
    if z >= 10 and z < 11 and not isinstance(art, Text):
        return "A-FRAME"
    if z >= 9 and z < 10:
        return "A-DIMS"
    h = _hex(colour)
    if h in CMAP:
        return CMAP[h]
    if ls not in ("-", "solid"):
        return "A-CEIL"
    if isinstance(art, Line2D) and art.get_linewidth() >= 1.0:
        return "A-WALL"
    return "A-FURN"

class Exporter:
    def __init__(self):
        self.doc = ezdxf.new("R2018", setup=True)
        self.doc.header["$INSUNITS"] = 4
        self.doc.styles.add("JP", font="msgothic.ttc")
        for n, c in LAYERS.items():
            if n not in self.doc.layers:
                self.doc.layers.add(n, color=c)
        self.msp = self.doc.modelspace()

    def add_sheet(self, ax, dx=0.0, frame_name=""):
        to_data = lambda T: T - ax.transData
        P = lambda x, y: ((x * SCALE) + dx, y * SCALE)
        for art in ax.get_children():
            if not art.get_visible():
                continue
            if isinstance(art, Annotation):
                self._text(art, P, ax)
                x0, y0 = art.xy
                xt, yt = art.get_position()
                if art.arrow_patch is not None:
                    self.msp.add_line(P(xt, yt), P(x0, y0), dxfattribs={"layer": "A-TEXT"})
                continue
            if isinstance(art, Text):
                self._text(art, P, ax)
                continue
            if isinstance(art, Line2D):
                xy = art.get_xydata()
                if len(xy) < 2:
                    continue
                pts = [P(*p) for p in xy]
                lay = _layer(art, art.get_color())
                self.msp.add_lwpolyline(pts, dxfattribs={"layer": lay})
                continue
            if isinstance(art, Patch):
                ec, fc = art.get_edgecolor(), art.get_facecolor()
                lay = _layer(art, fc[:3]) if fc[3] > 0 and _hex(fc[:3]) in CMAP else _layer(art, ec[:3] if ec[3] > 0 else fc[:3])
                T = art.get_transform() - ax.transData
                if isinstance(art, Circle):
                    c = T.transform((0.0, 0.0))
                    r = abs(T.transform((1.0, 0.0))[0] - c[0])
                    self.msp.add_circle(P(*c), r * SCALE, dxfattribs={"layer": lay})
                    polys = None
                else:
                    polys = art.get_path().to_polygons(T)
                    for poly in polys:
                        pts = [P(*p) for p in poly]
                        if len(pts) >= 2:
                            self.msp.add_lwpolyline(pts, close=True, dxfattribs={"layer": lay})
                # solid fill (coloured areas only) on its own layer
                if fc[3] > 0 and _hex(fc[:3]) not in ("#ffffff",) and polys:
                    h = self.msp.add_hatch(dxfattribs={"layer": "A-FILL"})
                    r, g, b = [round(v * 255) for v in fc[:3]]
                    h.rgb = (r, g, b)
                    for poly in polys:
                        h.paths.add_polyline_path([P(*p) for p in poly], is_closed=True)
                    h.set_solid_fill(rgb=(r, g, b))

    def _text(self, art, P, ax):
        s = art.get_text()
        if not s.strip():
            return
        x, y = art.get_position()
        size_mm = art.get_fontsize() * 25.4 / 72
        ha, va = art.get_ha(), art.get_va()
        align = {("center", "center"): TextEntityAlignment.MIDDLE_CENTER,
                 ("left", "center"): TextEntityAlignment.MIDDLE_LEFT,
                 ("right", "center"): TextEntityAlignment.MIDDLE_RIGHT,
                 ("left", "top"): TextEntityAlignment.TOP_LEFT,
                 ("center", "top"): TextEntityAlignment.TOP_CENTER,
                 ("left", "bottom"): TextEntityAlignment.BOTTOM_LEFT,
                 ("left", "baseline"): TextEntityAlignment.BOTTOM_LEFT,
                 ("center", "baseline"): TextEntityAlignment.BOTTOM_CENTER}.get((ha, va), TextEntityAlignment.MIDDLE_LEFT)
        lines = s.split("\n")
        h = size_mm * 0.75 * SCALE
        for i, ln in enumerate(lines):
            off = (i - (len(lines) - 1) / 2) * size_mm * 1.3 if va == "center" else i * size_mm * 1.3
            t = self.msp.add_text(ln, height=h, rotation=art.get_rotation(),
                                  dxfattribs={"layer": "A-TEXT", "style": "JP"})
            t.set_placement(P(x, y - off), align=align)

    def save(self, path):
        self.doc.saveas(path)
