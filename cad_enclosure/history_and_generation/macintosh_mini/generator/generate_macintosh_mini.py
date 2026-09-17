"""Generate a Macintosh 128K-inspired mini ESP32 desktop controller.

This is a separate design and does not modify enclosure/. Units are millimetres.
Coordinates: X left/right, Y front/back, Z bottom/top. Front is Y=0.
"""

from pathlib import Path
import math
import numpy as np
import trimesh
from shapely.geometry import box as shapely_box


# Keep generated STL files in the project's current-output directory.  The
# script itself lives under cad_enclosure/history_and_generation/.
OUT = Path(__file__).resolve().parents[3] / "latest_output" / "macintosh_mini"
OUT.mkdir(parents=True, exist_ok=True)

WIDTH = 100.0
HEIGHT = 140.0
DEPTH = 84.0
WALL = 2.8
FRONT = 4.0
CORNER_R = 8.0
BACK_COVER_T = 2.4
CLEARANCE = 0.5

SCREEN_CX = 0.0
SCREEN_CZ = 92.0
SCREEN_OPEN_W = 50.0   # 2.4-inch panel rotated to landscape
SCREEN_OPEN_H = 37.8
SCREEN_RECESS_W = 64.0
SCREEN_RECESS_H = 50.0

MX_HOLE = 14.2
MX_PITCH = 19.05
MX_Z = 27.0

BACK_SCREW_X = (-40.0, 40.0)
BACK_SCREW_Z = (28.0, 119.0)


def union(*meshes):
    return trimesh.boolean.union(list(meshes), engine="manifold")


def difference(mesh, *cuts):
    return trimesh.boolean.difference([mesh, *cuts], engine="manifold")


def intersection(*meshes):
    return trimesh.boolean.intersection(list(meshes), engine="manifold")


def box(extents, center=(0, 0, 0), axes=None):
    transform = np.eye(4)
    if axes is not None:
        transform[:3, :3] = np.asarray(axes)
    transform[:3, 3] = center
    return trimesh.creation.box(extents=extents, transform=transform)


def cylinder(radius, height, center, direction=(0, 0, 1), sections=48):
    mesh = trimesh.creation.cylinder(radius=radius, height=height, sections=sections)
    direction = np.asarray(direction, dtype=float)
    direction /= np.linalg.norm(direction)
    mesh.apply_transform(trimesh.geometry.align_vectors([0, 0, 1], direction))
    mesh.apply_translation(center)
    return mesh


def rounded_rect_polygon(width, height, radius, cx=0.0, cz=0.0, resolution=8):
    core = shapely_box(
        cx - width / 2 + radius,
        cz - height / 2 + radius,
        cx + width / 2 - radius,
        cz + height / 2 - radius,
    )
    return core.buffer(radius, resolution=resolution, join_style=1)


def rounded_prism_xz(width, height, radius, depth, y0, cx=0.0, cz=0.0):
    """Rounded rectangle in XZ, extruded from y0 by depth."""
    local = trimesh.creation.extrude_polygon(
        rounded_rect_polygon(width, height, radius, cx, cz), height=depth
    )
    # local (polygon X, polygon Y, extrusion Z) -> global (X, Y, Z)
    transform = np.array([
        [1, 0, 0, 0],
        [0, 0, 1, y0],
        [0, 1, 0, 0],
        [0, 0, 0, 1],
    ], dtype=float)
    local.apply_transform(transform)
    if local.volume < 0:
        local.invert()
    return local


def rounded_prism_xy(width, depth, radius, height, z0, cx=0.0, cy=0.0):
    local = trimesh.creation.extrude_polygon(
        rounded_rect_polygon(width, depth, radius, cx, cy), height=height
    )
    local.apply_translation([0, 0, z0])
    return local


def countersunk_cutter_y(x, z, outside_y=DEPTH + 0.1, sections=48):
    """Closed M3 cutter whose wide countersink opens on the rear face."""
    # Build along local Z, then point local +Z toward global -Y.
    rings = [(3.2, 0.0), (1.7, 1.5), (1.7, BACK_COVER_T + 5.0)]
    vertices = []
    for radius, depth in rings:
        for i in range(sections):
            a = 2 * math.pi * i / sections
            vertices.append([radius * math.cos(a), radius * math.sin(a), depth])
    vertices.extend([[0, 0, rings[0][1]], [0, 0, rings[-1][1]]])
    bc, tc = len(vertices) - 2, len(vertices) - 1
    faces = []
    for r in range(len(rings) - 1):
        a0, b0 = r * sections, (r + 1) * sections
        for i in range(sections):
            j = (i + 1) % sections
            faces.extend([[a0+i, b0+i, b0+j], [a0+i, b0+j, a0+j]])
    for i in range(sections):
        j = (i + 1) % sections
        faces.extend([[bc, i, j], [tc, (len(rings)-1)*sections+j,
                                    (len(rings)-1)*sections+i]])
    cutter = trimesh.Trimesh(vertices=vertices, faces=faces, process=True)
    if cutter.volume < 0:
        cutter.invert()
    transform = trimesh.geometry.align_vectors([0, 0, 1], [0, -1, 0])
    cutter.apply_transform(transform)
    cutter.apply_translation([x, outside_y, z])
    return cutter


def build_body():
    # Rounded compact-Mac shell, open at the rear.
    outer = rounded_prism_xz(WIDTH, HEIGHT, CORNER_R, DEPTH, 0, cz=HEIGHT/2)
    inner = rounded_prism_xz(
        WIDTH - 2*WALL, HEIGHT - 2*WALL, CORNER_R - WALL,
        DEPTH - FRONT + 2.0, FRONT, cz=HEIGHT/2,
    )
    body = difference(outer, inner)

    # CRT-like two-stage recess and landscape LCD opening.
    recess = rounded_prism_xz(
        SCREEN_RECESS_W, SCREEN_RECESS_H, 5.0, 2.0, -0.5,
        cx=SCREEN_CX, cz=SCREEN_CZ,
    )
    opening = rounded_prism_xz(
        SCREEN_OPEN_W, SCREEN_OPEN_H, 2.8, FRONT + 3.0, -1.0,
        cx=SCREEN_CX, cz=SCREEN_CZ,
    )
    body = difference(body, recess, opening)

    # Shallow top handle recess, another recognisable compact-Mac detail.
    handle_recess = rounded_prism_xy(46.0, 20.0, 5.0, 1.6, HEIGHT-1.2,
                                     cx=0.0, cy=55.0)
    body = difference(body, handle_recess)

    # Three standard MX plate openings, horizontal and centred.
    key_cuts = [
        box([MX_HOLE, FRONT + 4.0, MX_HOLE], center=[x, FRONT/2, MX_Z])
        for x in (-MX_PITCH, 0.0, MX_PITCH)
    ]

    # Macintosh-inspired floppy slot and small badge recess. Decorative only.
    floppy = rounded_prism_xz(36.0, 3.2, 1.4, 1.2, -0.3, cx=21.0, cz=55.0)
    badge = rounded_prism_xz(17.0, 7.0, 1.2, 0.8, -0.2, cx=-34.0, cz=55.0)
    body = difference(body, *key_cuts, floppy, badge)

    # Hidden LCD clamp posts; screws are inserted from inside, so no front holes.
    screen_posts = []
    screen_pilots = []
    for x in (-34.0, 34.0):
        for z in (SCREEN_CZ - 21.0, SCREEN_CZ + 21.0):
            screen_posts.append(cylinder(3.8, 6.0, [x, 6.0, z], [0, 1, 0]))
            screen_pilots.append(cylinder(1.1, 5.0, [x, 7.2, z], [0, 1, 0]))

    # Rear-cover posts joined to side walls by rectangular ribs.
    rear_posts, rear_ribs, rear_pilots = [], [], []
    for x in BACK_SCREW_X:
        for z in BACK_SCREW_Z:
            rear_posts.append(cylinder(4.0, 8.0, [x, DEPTH-6.8, z], [0, 1, 0]))
            rib_x = math.copysign(46.0, x)
            rear_ribs.append(box([8.0, 8.0, 8.0], center=[rib_x, DEPTH-6.8, z]))
            rear_pilots.append(cylinder(1.3, 9.0, [x, DEPTH-6.3, z], [0, 1, 0]))

    body = union(body, *screen_posts, *rear_posts, *rear_ribs)
    body = difference(body, *screen_pilots, *rear_pilots)

    # Four wide feet spread toward the corners. Each has a recessed pocket for
    # an adhesive 10-11 mm silicone pad, preventing the light enclosure from
    # sliding backward when a mechanical switch is pressed.
    feet = [
        cylinder(7.0, 2.4, [x, y, -0.9])
        for x in (-35.0, 35.0)
        for y in (18.0, 66.0)
    ]
    body = union(body, *feet)
    rubber_pockets = [
        cylinder(5.6, 1.2, [x, y, -1.65])
        for x in (-35.0, 35.0)
        for y in (18.0, 66.0)
    ]
    return difference(body, *rubber_pockets)


def build_back_cover():
    cover_w = WIDTH - 2*WALL - CLEARANCE
    cover_h = HEIGHT - 2*WALL - CLEARANCE
    cover = rounded_prism_xz(
        cover_w, cover_h, CORNER_R-WALL-0.25, BACK_COVER_T,
        DEPTH-BACK_COVER_T, cz=HEIGHT/2,
    )

    cuts = [countersunk_cutter_y(x, z) for x in BACK_SCREW_X for z in BACK_SCREW_Z]

    # Horizontal ventilation slots in the upper rear panel.
    for z in np.linspace(106.0, 126.0, 7):
        cuts.append(rounded_prism_xz(52.0, 1.8, 0.8, BACK_COVER_T+2,
                                     DEPTH-BACK_COVER_T-1, cz=float(z)))

    # Two USB-C cable openings and one general cable pass-through.
    for x in (-15.0, 7.0):
        cuts.append(rounded_prism_xz(14.0, 8.0, 2.0, BACK_COVER_T+2,
                                     DEPTH-BACK_COVER_T-1, cx=x, cz=15.0))
    cuts.append(cylinder(4.5, BACK_COVER_T+2, [31.0, DEPTH-BACK_COVER_T/2, 15.0],
                         [0, 1, 0]))
    return difference(cover, *cuts)


def build_lcd_clamp():
    # Fits the common ~60.5 x 42.5 mm ILI9341 PCB; adjust after measuring.
    frame = rounded_prism_xz(76.0, 50.0, 3.0, 2.4, 0.0, cz=SCREEN_CZ)
    centre = rounded_prism_xz(59.0, 40.5, 2.0, 4.0, -0.8, cz=SCREEN_CZ)
    holes = [
        cylinder(1.5, 5.0, [x, 1.2, z], [0, 1, 0])
        for x in (-34.0, 34.0)
        for z in (SCREEN_CZ-21.0, SCREEN_CZ+21.0)
    ]
    return difference(frame, centre, *holes)


def export(mesh, name):
    mesh.remove_unreferenced_vertices()
    path = OUT / f"{name}.stl"
    mesh.export(path)
    check = trimesh.load(path, force="mesh", process=True)
    print({
        "part": name,
        "size": np.round(mesh.extents, 2).tolist(),
        "watertight": bool(check.is_watertight),
        "volume": round(float(mesh.volume), 1),
        "faces": len(mesh.faces),
    })


if __name__ == "__main__":
    export(build_body(), "01_macintosh_body")
    export(build_back_cover(), "02_inset_back_cover")
    export(build_lcd_clamp(), "03_internal_lcd_clamp")
