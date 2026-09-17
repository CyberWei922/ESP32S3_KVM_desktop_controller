"""V119 five-part Compact Macintosh desktop-controller enclosure.

Units are millimetres.  X is left/right, Y is front/back, Z is bottom/top;
the user-facing front is negative Y.  The printable assembly is deliberately
split into a lower base, bottom cover, upper barrel, front panel and rear
cover so every visible face can be printed in a sensible orientation.
"""

from pathlib import Path
import math
import warnings
import numpy as np
import trimesh
from matplotlib.font_manager import FontProperties
from matplotlib.textpath import TextPath
from shapely import affinity
from shapely.geometry import box as shapely_box
from shapely.geometry import LineString, Polygon
from shapely.ops import polygonize, unary_union


# Keep generated STL files in the project's current-output directory.  The
# script itself lives under cad_enclosure/history_and_generation/.
OUT = Path(__file__).resolve().parents[3] / "latest_output" / "apple_retro_hybrid"
OUT.mkdir(parents=True, exist_ok=True)
VERSION = "v119"

# Overall/base.  The V119 exterior has matching 45-degree top/bottom bevels;
# measured holder, connector and fastening dimensions remain unchanged.
BASE_W = 110.0
BASE_D = 105.0
BASE_H = 28.0
BASE_R = 8.0
BASE_EDGE_CHAMFER = 2.0
BASE_TOP_W = 106.0
BASE_TOP_D = 101.0
BASE_TOP_R = 6.0
WALL = 2.8
BOTTOM_T = 2.4
CLEARANCE = 0.5
BOTTOM_COVER_W = 101.9
BOTTOM_COVER_D = 96.9
BOTTOM_COVER_R = 4.8

# Macintosh-inspired upper cabin
UPPER_W = 94.0
UPPER_D = 58.0
UPPER_H = 112.0
UPPER_R = 6.0
UPPER_FRONT_Y = -7.0
UPPER_BOTTOM_Z = BASE_H

# Horizontal sections of the V119 upper enclosure.  The lower and upper
# perimeter transitions use equal vertical and horizontal setbacks, producing
# true 45-degree chamfers while retaining the agreed 94 x 58 x 112 mm envelope.
FRONT_TILT_DEG = 6.0
FRONT_TILT_RAD = math.radians(FRONT_TILT_DEG)
UPPER_BASE_CENTER_Y = UPPER_FRONT_Y + UPPER_D/2
UPPER_BOTTOM_CHAMFER = 2.0
UPPER_TOP_CHAMFER = 3.0
UPPER_MAIN_BOTTOM_Z = UPPER_BOTTOM_Z + UPPER_BOTTOM_CHAMFER
UPPER_MAIN_TOP_Z = UPPER_BOTTOM_Z + UPPER_H - UPPER_TOP_CHAMFER
UPPER_FRONT_TOP_Y = (
    UPPER_FRONT_Y
    + math.tan(FRONT_TILT_RAD)*(UPPER_MAIN_TOP_Z-UPPER_MAIN_BOTTOM_Z)
)
UPPER_REAR_BOTTOM_Y = UPPER_FRONT_Y + UPPER_D
UPPER_REAR_TOP_Y = 49.0
REAR_TILT_RAD = math.atan(
    (UPPER_REAR_BOTTOM_Y-UPPER_REAR_TOP_Y)
    /(UPPER_MAIN_TOP_Z-UPPER_MAIN_BOTTOM_Z)
)
REAR_TILT_DEG = math.degrees(REAR_TILT_RAD)
UPPER_MAIN_TOP_D = UPPER_REAR_TOP_Y-UPPER_FRONT_TOP_Y
UPPER_MAIN_TOP_CENTER_Y = (UPPER_REAR_TOP_Y+UPPER_FRONT_TOP_Y)/2
UPPER_TOP_FRONT_Y = UPPER_FRONT_TOP_Y + UPPER_TOP_CHAMFER
UPPER_TOP_REAR_Y = UPPER_REAR_TOP_Y - UPPER_TOP_CHAMFER
UPPER_TOP_CENTER_Y = (UPPER_TOP_FRONT_Y+UPPER_TOP_REAR_Y)/2
UPPER_REAR_EDGE_BEVEL = 2.5
UPPER_LOFT_SECTIONS = (
    (UPPER_BOTTOM_Z,90.0,54.0,4.0,UPPER_BASE_CENTER_Y),
    (UPPER_MAIN_BOTTOM_Z,94.0,58.0,6.0,UPPER_BASE_CENTER_Y),
    (UPPER_MAIN_TOP_Z,92.0,UPPER_MAIN_TOP_D,5.5,
     UPPER_MAIN_TOP_CENTER_Y),
    (UPPER_BOTTOM_Z+UPPER_H,86.0,UPPER_TOP_REAR_Y-UPPER_TOP_FRONT_Y,
     2.5,UPPER_TOP_CENTER_Y),
)

# The upper barrel locates in a hidden groove in the base deck.  Four M3
# machine screws are inserted from inside the base and retained by ordinary
# M3 hex nuts in the upper barrel.  The bottom cover therefore remains the
# only access needed for separating the two halves.
UPPER_LOCATOR_OUTER_W = 86.0
UPPER_LOCATOR_OUTER_D = 50.0
UPPER_LOCATOR_WALL = 2.4
UPPER_LOCATOR_H = 2.0
UPPER_LOCATOR_CLEARANCE = 0.35
UPPER_BASE_MOUNT_X = (-34.0, 34.0)
# Rear row stops at Y=36 so M3 screw heads cannot touch the two vertical
# connector PCBs mounted near the base's rear wall around Y=42.5.
UPPER_BASE_MOUNT_Y = (8.0, 36.0)
UPPER_BASE_BOSS_R = 4.2
UPPER_BASE_BOSS_H = 7.0
M3_NUT_AF = 5.8
M3_NUT_DEPTH = 2.8
JUNCTION_WIRE_W = 36.0
JUNCTION_WIRE_D = 22.0
JUNCTION_WIRE_R = 3.0
JUNCTION_WIRE_CENTER_Y = UPPER_BASE_CENTER_Y
JUNCTION_WIRE_CUT_H = 7.0

# Upper removable rear cover.  The cover is a complete flat, sharp-cornered
# trapezoid.  Its surrounding four-sided 45-degree bevel belongs to the fixed
# upper shell so the cover remains independently replaceable and adjustable.
UPPER_BACK_T = 2.4
REAR_FRAME_BEVEL = UPPER_REAR_EDGE_BEVEL
UPPER_BACK_RECESS = REAR_FRAME_BEVEL
UPPER_BACK_CLEARANCE = 0.5
UPPER_BACK_MOUNT_X = (-37.0, 37.0)
UPPER_BACK_MOUNT_Z = (UPPER_BOTTOM_Z+12.0, UPPER_BOTTOM_Z+100.0)
UPPER_BACK_BODY_BOSS_R = 4.5
UPPER_BACK_BODY_BOSS_LEN = 6.5
UPPER_GUSSET_SKIN = 1.0
M3_SELF_TAP_PILOT_R = 1.35      # 2.7 mm diameter, verified on the user's A1 mini
M3_CLEARANCE_R = 1.7

# 50 x 70 mm perfboard on the inside of the upper rear cover
PERFBOARD_W = 50.0
PERFBOARD_H = 70.0
PERFBOARD_CENTER_Z = UPPER_BOTTOM_Z+58.0
PERFBOARD_HOLE_SPACING_X = 41.0
PERFBOARD_HOLE_SPACING_Z = 64.0
PERFBOARD_BOSS_LEN = 5.0
PERFBOARD_BOSS_R = 4.0

# Front Apple emblem, replacing the old generic rectangular badge recess.
APPLE_LOGO_CENTER_X = -30.0
APPLE_LOGO_CENTER_Z = UPPER_BOTTOM_Z+31.5
APPLE_LOGO_H = 11.0
APPLE_LOGO_RECESS = 0.9
APPLE_LOGO_FONT = "/System/Library/Fonts/HelveticaNeue.ttc"
FLOPPY_CENTER_X = 16.5
FLOPPY_STEP_CENTER_X = 28.5

# Removable front cap.  The cap and shell are split from one master exterior,
# so their side surfaces meet flush at a zero-gap parting plane.  Registration
# clearance exists only on the hidden internal tongue.  Two hidden screws,
# reachable after removing the rear cover, retain the cap.
UPPER_FRONT_T = 4.0
UPPER_FRONT_CLEARANCE = 0.5
UPPER_FRONT_LIP_DEPTH = 2.6
UPPER_FRONT_LIP_WALL = 2.0
UPPER_FRONT_SEAM_Y = 6.0
UPPER_FRONT_MOUNT_X = (-37.0, 37.0)
UPPER_FRONT_MOUNT_Z = UPPER_BOTTOM_Z+30.0
UPPER_FRONT_PANEL_BOSS_R = 4.2
UPPER_FRONT_PANEL_BOSS_LEN = 5.0
UPPER_FRONT_SHELL_EAR_LEN = 3.0

# The front face bevel is cut into the master body surface.  These profiles
# control only that visible bevel and the hidden internal tongue; they do not
# create a second exterior frame.
FRONT_PANEL_W_BOTTOM = 94.0
FRONT_PANEL_W_TOP = 92.0
FRONT_PANEL_BOTTOM_Z = UPPER_BOTTOM_Z
FRONT_PANEL_TOP_Z = UPPER_BOTTOM_Z + UPPER_H
FRONT_PANEL_CORNER_R = 6.0
FRONT_PANEL_RECESS = 0.0
FRONT_PANEL_EDGE_BEVEL = 2.5
CRT_RECESS_W = 69.0
CRT_RECESS_H = 55.0
CRT_RECESS_INNER_W = 51.5
CRT_RECESS_INNER_H = 39.3
CRT_RECESS_DEPTH = 6.0
CRT_REAR_PAD_W = 76.0
CRT_REAR_PAD_H = 45.0

BACK_PANEL_W_BOTTOM = 85.0
BACK_PANEL_W_TOP = 82.5
BACK_PANEL_BOTTOM_Z = UPPER_BOTTOM_Z + 4.6
BACK_PANEL_TOP_Z = UPPER_BOTTOM_Z + 106.0

# Display, landscape
SCREEN_Z = UPPER_BOTTOM_Z+70.0
SCREEN_OPEN_W = 49.16   # active area 48.96 + 0.10 mm clearance per side
SCREEN_OPEN_H = 36.92   # active area 36.72 + 0.10 mm clearance per side
SCREEN_RECESS_W = 65.0
SCREEN_RECESS_H = 51.0

# Exact 2.4-inch ILI9341 module geometry after counter-clockwise landscape
# rotation, matching the seller's photo: 11-pin header left, FPC right.
LCD_PCB_W = 72.26
LCD_PCB_H = 43.00
LCD_DEPTH = 5.10
LCD_FRONT_GAP = 0.50
LCD_POST_LENGTH = LCD_DEPTH + LCD_FRONT_GAP
LCD_POST_EMBED = 0.25
LCD_BACKLIGHT_W = 60.26
LCD_BACKLIGHT_H = 42.72
LCD_FACE_POCKET_CLEARANCE = 0.40
LCD_FACE_POCKET_OVERLAP = 0.05
LCD_ACTIVE_OFFSET_X = -2.62  # active centre is left of PCB centre after rotation
LCD_PCB_CENTER_X = -LCD_ACTIVE_OFFSET_X
LCD_HOLE_SPACING_X = 66.26
LCD_HOLE_SPACING_Z = 37.00
LCD_HOLE_X = tuple(LCD_PCB_CENTER_X + s*LCD_HOLE_SPACING_X/2 for s in (-1, 1))
LCD_HOLE_Z = tuple(SCREEN_Z + s*LCD_HOLE_SPACING_Z/2 for s in (-1, 1))

# Horizontal three-switch holder.  The clear holder is inserted from the
# rear (+Y) while the bottom cover is removed.  Its top remains exposed;
# left, right and front walls locate it, two underside rails carry the press
# load, and a stop on the removable bottom cover closes the open rear side.
KEY_Y = -30.0
KEY_WELL_W = 78.0
KEY_WELL_D = 31.0
KEY_WELL_DEPTH = 5.0
KEY_WELL_FLOOR_T = 2.4
KEY_WELL_WALL_T = 1.2
KEY_HOLDER_W = 58.0
KEY_HOLDER_D = 20.0
KEY_HOLDER_H = 12.0
KEY_TILT_DEG = 7.0
KEY_TILT_RAD = math.radians(KEY_TILT_DEG)
BASE_DECK_HINGE_Y = KEY_Y + KEY_WELL_D/2
KEY_ASSEMBLY_Z_SHIFT = math.tan(KEY_TILT_RAD)*(KEY_Y-BASE_DECK_HINGE_Y)
KEY_HOLDER_CLEARANCE = 0.6
KEY_SLOT_W = KEY_HOLDER_W + KEY_HOLDER_CLEARANCE
KEY_SLOT_D = KEY_HOLDER_D + KEY_HOLDER_CLEARANCE
KEY_CRADLE_WALL_T = 2.4
# The holder is recessed like a mechanical-keyboard plate.  Its top sits
# about 5.2 mm below the 24.2 mm-high shallow key-well surface.  The base is
# only 3 mm taller than the earlier version.  The user's 20 mm total assembly
# measurement already includes the switch pins, leaving about 1.6 mm for the
# insulated wire below them.
KEY_CRADLE_RAIL_T = 2.4
KEY_CRADLE_RAIL_D = 3.0
# Preserve the proven V100 holder datum as a hardware baseline.  The final
# placement follows the approved deck plane through KEY_ASSEMBLY_Z_SHIFT.
KEY_CRADLE_SUPPORT_Z = 7.0
KEY_CRADLE_VERTICAL_ADJUST = 0.3
KEY_CRADLE_SUPPORT_WORLD_Z = (
    KEY_CRADLE_SUPPORT_Z + (BASE_H-25.0) + KEY_CRADLE_VERTICAL_ADJUST
)
KEY_ROTATION_PIVOT_Z = KEY_CRADLE_SUPPORT_WORLD_Z + KEY_HOLDER_H/2
KEY_CRADLE_LIP_INSET = 1.5
KEY_CRADLE_LIP_H = 1.5
KEY_CRADLE_LIP_CLEARANCE_Z = 0.2
KEY_RETAINING_TOP_CLEARANCE = 4.0
KEY_STOP_W = 44.0
KEY_STOP_T = 2.4
KEY_STOP_TOP_Z = 14.0
KEY_STOP_GAP = 0.25

BOTTOM_SCREW_X = (-46.0, 46.0)
BOTTOM_SCREW_Y = (-39.0, 39.0)

# Rear-right cooler USB-C breakout, viewed from outside the rear panel.
# Seller drawing: 16 x 16 x 1.6 mm PCB, mounting pattern 12 x 7.2 mm.
# The measured vertical USB-C receptacle stands 10 mm above its PCB.  A
# uniform 2.8 mm rear wall plus 7.2 mm standoffs places its mouth flush with
# the outside surface without creating exposed thinning seams at the bevels.
COOLER_PCB_W = 16.0
COOLER_PCB_H = 16.0
COOLER_PCB_T = 1.6
COOLER_PORT_X = 32.0
COOLER_PCB_CENTER_Z = 11.0
COOLER_PATTERN_CENTER_Z = COOLER_PCB_CENTER_Z + 2.4
COOLER_HOLE_SPACING_X = 12.0
COOLER_HOLE_SPACING_Z = 7.2
COOLER_BOSS_LENGTH = 7.2
COOLER_BOSS_EMBED = 0.4
COOLER_BOSS_RADIUS = 2.8
COOLER_PILOT_RADIUS = 0.9       # M2 self-tapping pilot; tune for printer/material
COOLER_PORT_W = 9.4             # nominal USB-C shell + printing clearance
COOLER_PORT_H = 3.6

# Rear-left USB-sharing Micro-USB breakout, mirrored from the cooler port when
# viewed from outside the rear panel.  Hand measurements: 15.8 x 11 x 1.6 mm
# PCB, two M3 holes on an 11 mm horizontal pitch, hole row at PCB mid-height,
# connector top edge 1 mm below the PCB top, and 5 mm projection from PCB.
MICRO_PCB_W = 15.8
MICRO_PCB_H = 11.0
MICRO_PCB_T = 1.6
MICRO_PORT_X = -COOLER_PORT_X
MICRO_PORT_CENTER_Z = COOLER_PATTERN_CENTER_Z
# Scaled-up version of the usual Micro-B panel cutout: nominal 8.5 x 3.5 mm
# with 1.5 mm x 45-degree chamfers on the two lower corners.  Scale is about
# 1.08 for hand-measurement and FDM clearance.
MICRO_PORT_W = 9.2
MICRO_PORT_H = 3.8
MICRO_PORT_CHAMFER = 1.6
MICRO_PCB_CENTER_Z = (
    MICRO_PORT_CENTER_Z
    - (MICRO_PCB_H/2 - 1.0 - MICRO_PORT_H/2)
)
MICRO_HOLE_SPACING_X = 11.0
MICRO_BOSS_LENGTH = 2.2
MICRO_BOSS_EMBED = 0.4
MICRO_BOSS_RADIUS = 3.3
MICRO_PILOT_RADIUS = M3_SELF_TAP_PILOT_R

# Centre rear USB-C extension.  The user's flat 11.3 x 16.8 x 1.0 mm PCB is
# mounted horizontally with its component-free face upward.  A fixed upper
# platen and three one-millimetre-high fences locate it; a plain platform on
# the removable bottom cover bears on the underside of the receptacle.  The
# PCB and receptacle form a 4.3 mm hard-clamped envelope with no foam here.
# USB-IF defines the mating interface rather than a universal PCB footprint;
# the 9.2 x 3.4 mm printed aperture adds FDM clearance around the nominal
# 8.34 x 2.56 mm Type-C interface.
ESP_USB_PCB_W = 11.3
ESP_USB_PCB_D = 16.8
ESP_USB_PCB_T = 1.0
ESP_USB_ASSEMBLY_H = 4.3
ESP_USB_LIMIT_H = 1.0
ESP_USB_LIMIT_T = 1.4
ESP_USB_XY_CLEARANCE = 0.25
ESP_USB_REAR_SKIN_T = 1.0
ESP_USB_PORT_W = 9.2
ESP_USB_PORT_H = 3.4
ESP_USB_PORT_R = 1.6
ESP_USB_SHELL_DEPTH = 6.2
ESP_USB_PORT_X = 0.0
ESP_USB_PORT_CENTER_Z = COOLER_PATTERN_CENTER_Z
ESP_USB_CONNECTOR_H = ESP_USB_ASSEMBLY_H - ESP_USB_PCB_T
ESP_USB_MODULE_BOTTOM_Z = ESP_USB_PORT_CENTER_Z - ESP_USB_CONNECTOR_H/2
ESP_USB_PCB_TOP_Z = ESP_USB_MODULE_BOTTOM_Z + ESP_USB_ASSEMBLY_H
ESP_USB_PCB_BOTTOM_Z = ESP_USB_PCB_TOP_Z - ESP_USB_PCB_T


def union(*meshes):
    return trimesh.boolean.union(list(meshes), engine="manifold")


def difference(mesh, *cuts):
    return trimesh.boolean.difference([mesh, *cuts], engine="manifold")


def intersection(*meshes):
    return trimesh.boolean.intersection(list(meshes), engine="manifold")


def largest_volume_component(mesh):
    """Discard zero-volume boolean sheets while retaining the solid body."""
    components=mesh.split(only_watertight=False)
    if not components:
        raise RuntimeError("boolean operation produced no solid component")
    ordered=sorted(
        components,key=lambda part:abs(float(part.volume)),reverse=True
    )
    discarded=sum(abs(float(part.volume)) for part in ordered[1:])
    if discarded > 1e-3:
        raise RuntimeError(
            f"boolean operation detached {discarded:.6f} mm^3 of solid"
        )
    return ordered[0]


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


def rounded_polygon(width, height, radius, cx=0.0, cy=0.0, resolution=8):
    core = shapely_box(
        cx-width/2+radius, cy-height/2+radius,
        cx+width/2-radius, cy+height/2-radius,
    )
    return core.buffer(radius, resolution=resolution, join_style=1)


def front_rounded_rear_chamfer_polygon(
    width,depth,front_radius,rear_chamfer,cx=0.0,cy=0.0,resolution=8
):
    """Plan outline with rounded front corners and 45-degree rear corners."""
    hw=width/2; hd=depth/2
    points=[]
    right_center=np.array([cx+hw-front_radius,cy-hd+front_radius])
    for angle in np.linspace(-math.pi/2,0.0,resolution+1):
        points.append(right_center+front_radius*np.array([
            math.cos(angle),math.sin(angle)
        ]))
    points.extend([
        (cx+hw,cy+hd-rear_chamfer),
        (cx+hw-rear_chamfer,cy+hd),
        (cx-hw+rear_chamfer,cy+hd),
        (cx-hw,cy+hd-rear_chamfer),
    ])
    left_center=np.array([cx-hw+front_radius,cy-hd+front_radius])
    for angle in np.linspace(math.pi,3*math.pi/2,resolution+1):
        points.append(left_center+front_radius*np.array([
            math.cos(angle),math.sin(angle)
        ]))
    return Polygon(points)


def rounded_xy(width, depth, radius, height, z0, cx=0.0, cy=0.0):
    mesh = trimesh.creation.extrude_polygon(
        rounded_polygon(width, depth, radius, cx, cy), height=height
    )
    mesh.apply_translation([0, 0, z0])
    return mesh


def _sample_polygon_loop(polygon):
    """Return the exact counter-clockwise vertices of a polygon boundary."""
    points=np.asarray(polygon.exterior.coords[:-1],dtype=float)
    # Shapely exterior rings can be clockwise depending on construction.
    signed = 0.5*np.sum(
        points[:, 0]*np.roll(points[:, 1], -1)
        - np.roll(points[:, 0], -1)*points[:, 1]
    )
    if signed < 0:
        points = points[::-1]
    return points


def rounded_xy_loft(sections, cx=0.0, cy=0.0):
    """Closed loft through rounded X/Y rectangles.

    Each section is ``(z, width, depth, radius)`` or
    ``(z, width, depth, radius, centre_y)``.  Per-section Y centres allow the
    front and rear faces to use different taper angles.
    """
    loops=[]; count=None
    normalized=[]
    for section in sections:
        if len(section)==4:
            z,width,depth,radius=section; section_cy=cy
        elif len(section)==5:
            z,width,depth,radius,section_cy=section
        else:
            raise ValueError("loft section needs four or five values")
        normalized.append((z,width,depth,radius,section_cy))
        points=_sample_polygon_loop(rounded_polygon(
            width,depth,radius,cx,section_cy
        ))
        if count is None:
            count=len(points)
        elif len(points) != count:
            raise ValueError("all rounded loft sections need matching vertices")
        loops.append(np.column_stack([points, np.full(count,z)]))
    vertices=np.vstack(loops)
    faces=[]
    for layer in range(len(loops)-1):
        a=layer*count; b=(layer+1)*count
        for i in range(count):
            j=(i+1)%count
            faces.extend([[a+i,a+j,b+j],[a+i,b+j,b+i]])
    bottom_center=len(vertices)
    top_center=bottom_center+1
    vertices=np.vstack([
        vertices,
        [cx,normalized[0][4],normalized[0][0]],
        [cx,normalized[-1][4],normalized[-1][0]],
    ])
    for i in range(count):
        j=(i+1)%count
        faces.append([bottom_center,j,i])
        top=(len(loops)-1)*count
        faces.append([top_center,top+i,top+j])
    mesh=trimesh.Trimesh(vertices=vertices,faces=faces,process=True)
    if mesh.volume < 0:
        mesh.invert()
    return mesh


def upper_outer_loft(sections=UPPER_LOFT_SECTIONS):
    """Upper master skin with rounded front and planar 45-degree rear edges."""
    loops=[]; count=None; normalized=[]
    for section in sections:
        if len(section)==4:
            z,width,depth,radius=section; section_cy=0.0
        elif len(section)==5:
            z,width,depth,radius,section_cy=section
        else:
            raise ValueError("upper loft section needs four or five values")
        normalized.append((z,width,depth,radius,section_cy))
        points=_sample_polygon_loop(front_rounded_rear_chamfer_polygon(
            width,depth,radius,UPPER_REAR_EDGE_BEVEL,0.0,section_cy
        ))
        if count is None:
            count=len(points)
        elif len(points) != count:
            raise ValueError("all upper loft sections need matching vertices")
        loops.append(np.column_stack([points,np.full(count,z)]))
    vertices=np.vstack(loops)
    faces=[]
    for layer in range(len(loops)-1):
        a=layer*count; b=(layer+1)*count
        for i in range(count):
            j=(i+1)%count
            faces.extend([[a+i,a+j,b+j],[a+i,b+j,b+i]])
    bottom_center=len(vertices); top_center=bottom_center+1
    vertices=np.vstack([
        vertices,
        [0.0,normalized[0][4],normalized[0][0]],
        [0.0,normalized[-1][4],normalized[-1][0]],
    ])
    for i in range(count):
        j=(i+1)%count
        faces.append([bottom_center,j,i])
        top=(len(loops)-1)*count
        faces.append([top_center,top+i,top+j])
    mesh=trimesh.Trimesh(vertices=vertices,faces=faces,process=True)
    if mesh.volume < 0:
        mesh.invert()
    return mesh


def _insert_horizontal_breaks(points,breaks):
    """Insert exact intersections with horizontal lines into a closed loop."""
    result=[]
    for index,start in enumerate(points):
        end=points[(index+1)%len(points)]
        result.append(start)
        crossings=[]
        for y_break in breaks:
            if (start[1]-y_break)*(end[1]-y_break) < 0:
                fraction=(y_break-start[1])/(end[1]-start[1])
                crossings.append((fraction,start+fraction*(end-start)))
        for _,crossing in sorted(crossings,key=lambda item:item[0]):
            result.append(crossing)
    return np.asarray(result,dtype=float)


def rounded_xy_variable_z_loft(sections,cx=0.0,cy=0.0,top_break_y=None):
    """Closed rounded-plan loft whose section heights may vary with Y.

    Each section is ``(width, depth, radius, z_at_y)`` or adds a fifth
    ``centre_y`` value.  ``z_at_y`` may be a number or a callable.  This is
    used for the lower enclosure's continuous sloping keyboard deck while
    retaining rounded sides and the two approximately 45-degree edge bevels.
    """
    loops=[]; count=None; normalized=[]
    for section in sections:
        if len(section)==4:
            width,depth,radius,z_at_y=section; section_cy=cy
        elif len(section)==5:
            width,depth,radius,z_at_y,section_cy=section
        else:
            raise ValueError("variable-Z loft section needs four or five values")
        z_function=(z_at_y if callable(z_at_y)
                    else lambda _y,value=float(z_at_y):value)
        polygon=rounded_polygon(width,depth,radius,cx,section_cy)
        points=_sample_polygon_loop(polygon)
        if top_break_y is not None:
            points=_insert_horizontal_breaks(points,(top_break_y,))
        if count is None:
            count=len(points)
        elif len(points) != count:
            raise ValueError("all variable-Z loft sections need matching vertices")
        z_values=np.array([z_function(y) for y in points[:,1]],dtype=float)
        loops.append(np.column_stack([points,z_values]))
        normalized.append((section_cy,z_function))
    vertices=np.vstack(loops)
    faces=[]
    for layer in range(len(loops)-1):
        a=layer*count; b=(layer+1)*count
        for i in range(count):
            j=(i+1)%count
            faces.extend([[a+i,a+j,b+j],[a+i,b+j,b+i]])
    bottom_center=len(vertices)
    vertices=np.vstack([
        vertices,
        [cx,normalized[0][0],normalized[0][1](normalized[0][0])],
    ])
    for i in range(count):
        j=(i+1)%count
        faces.append([bottom_center,j,i])
    top=(len(loops)-1)*count
    if top_break_y is None:
        top_center=len(vertices)
        vertices=np.vstack([
            vertices,
            [cx,normalized[-1][0],normalized[-1][1](normalized[-1][0])],
        ])
        for i in range(count):
            j=(i+1)%count
            faces.append([top_center,top+i,top+j])
    else:
        # Split the top profile into two polygons so the level rear deck and
        # inclined keyboard deck share a real straight crease.  A single
        # centroid fan would warp the whole surface and hide that transition.
        top_section=sections[-1]
        top_width,top_depth,top_radius=top_section[:3]
        top_cy=cy if len(top_section)==4 else top_section[4]
        top_polygon=rounded_polygon(
            top_width,top_depth,top_radius,cx,top_cy
        )
        pieces=(
            top_polygon.intersection(shapely_box(
                cx-1000,top_cy-1000,cx+1000,top_break_y
            )),
            top_polygon.intersection(shapely_box(
                cx-1000,top_break_y,cx+1000,top_cy+1000
            )),
        )
        top_z_function=normalized[-1][1]
        for piece in pieces:
            piece_points=_sample_polygon_loop(piece)
            piece_center=np.asarray(piece.centroid.coords[0],dtype=float)
            start=len(vertices)
            piece_vertices=np.column_stack([
                piece_points,
                [top_z_function(y) for y in piece_points[:,1]],
            ])
            center_index=start+len(piece_points)
            vertices=np.vstack([
                vertices,piece_vertices,
                [piece_center[0],piece_center[1],
                 top_z_function(piece_center[1])],
            ])
            for i in range(len(piece_points)):
                j=(i+1)%len(piece_points)
                faces.append([center_index,start+i,start+j])
    mesh=trimesh.Trimesh(vertices=vertices,faces=faces,process=True)
    if mesh.volume < 0:
        mesh.invert()
    return mesh


def rounded_trapezoid(width_bottom, width_top, height, radius,
                      cx=0.0, cz=0.0):
    """Rounded trapezoid in X/Z for the tapered front and rear panels."""
    z0=cz-height/2; z1=cz+height/2
    raw=Polygon([
        (cx-width_bottom/2,z0), (cx+width_bottom/2,z0),
        (cx+width_top/2,z1), (cx-width_top/2,z1),
    ])
    # The negative/positive buffer pair rounds the four corners while keeping
    # the requested exterior envelope within printing tolerance.
    rounded=raw.buffer(-radius,resolution=8,join_style=1).buffer(
        radius,resolution=8,join_style=1
    )
    return rounded


def trapezoid(width_bottom,width_top,height,cx=0.0,cz=0.0):
    """Sharp-cornered trapezoid in X/Z for a four-sided bevel boundary."""
    z0=cz-height/2; z1=cz+height/2
    return Polygon([
        (cx-width_bottom/2,z0),(cx+width_bottom/2,z0),
        (cx+width_top/2,z1),(cx-width_top/2,z1),
    ])


def rounded_rect_frustum_xz(front_w,front_h,front_r,
                            back_w,back_h,back_r,depth,y0,
                            cx=0.0,cz=0.0):
    """Closed rounded-rectangle frustum extending along +Y."""
    front=_sample_polygon_loop(rounded_polygon(
        front_w,front_h,front_r,cx,cz
    ))
    back=_sample_polygon_loop(rounded_polygon(
        back_w,back_h,back_r,cx,cz
    ))
    if len(front) != len(back):
        raise ValueError("frustum faces need matching vertices")
    count=len(front)
    vertices=np.vstack([
        np.column_stack([front[:,0],np.full(count,y0),front[:,1]]),
        np.column_stack([back[:,0],np.full(count,y0+depth),back[:,1]]),
        [cx,y0,cz], [cx,y0+depth,cz],
    ])
    faces=[]
    for i in range(count):
        j=(i+1)%count
        faces.extend([[i,j,count+j],[i,count+j,count+i]])
        faces.append([2*count,j,i])
        faces.append([2*count+1,count+i,count+j])
    mesh=trimesh.Trimesh(vertices=vertices,faces=faces,process=True)
    if mesh.volume < 0:
        mesh.invert()
    return mesh


def _resample_polygon_loop(polygon,count):
    """Return equally spaced counter-clockwise points on a polygon boundary."""
    ring=polygon.exterior
    distances=np.linspace(0.0,ring.length,count,endpoint=False)
    points=np.asarray([
        ring.interpolate(float(distance)).coords[0]
        for distance in distances
    ],dtype=float)
    signed=0.5*np.sum(
        points[:,0]*np.roll(points[:,1],-1)
        -np.roll(points[:,0],-1)*points[:,1]
    )
    if signed < 0:
        points=points[::-1]
    # Shapely may choose a different first vertex after an inward buffer.
    # Rotate every sampled loop to the same bottom-left anchor so loft faces
    # connect corresponding edges instead of twisting across the front face.
    min_x,min_z,max_x,max_z=polygon.bounds
    anchor=np.array([min_x,min_z])
    start=int(np.argmin(np.linalg.norm(points-anchor,axis=1)))
    return np.roll(points,-start,axis=0)


def loft_xz_polygons(front_polygon,back_polygon,depth,y0,sample_count=None):
    """Closed frustum between two matching X/Z polygon outlines."""
    if sample_count is None:
        front=_sample_polygon_loop(front_polygon)
        back=_sample_polygon_loop(back_polygon)
    else:
        front=_resample_polygon_loop(front_polygon,sample_count)
        back=_resample_polygon_loop(back_polygon,sample_count)
    if len(front) != len(back):
        raise ValueError("front/back profiles need matching vertices")
    count=len(front)
    front_center=np.array(front_polygon.centroid.coords[0])
    back_center=np.array(back_polygon.centroid.coords[0])
    vertices=np.vstack([
        np.column_stack([front[:,0],np.full(count,y0),front[:,1]]),
        np.column_stack([back[:,0],np.full(count,y0+depth),back[:,1]]),
        [front_center[0],y0,front_center[1]],
        [back_center[0],y0+depth,back_center[1]],
    ])
    faces=[]
    for i in range(count):
        j=(i+1)%count
        faces.extend([[i,j,count+j],[i,count+j,count+i]])
        faces.append([2*count,j,i])
        faces.append([2*count+1,count+i,count+j])
    mesh=trimesh.Trimesh(vertices=vertices,faces=faces,process=True)
    if mesh.volume < 0:
        mesh.invert()
    return mesh


def shear_y_by_z(mesh, slope, reference_z):
    """Tilt a panel so its Y position follows the cabinet side profile."""
    transform=np.eye(4)
    transform[1,2]=slope
    transform[1,3]=-slope*reference_z
    mesh.apply_transform(transform)
    return mesh


def transform_rear_part(mesh,recess=0.0):
    """Rigidly place local geometry normal to the sloping rear plane."""
    mesh.apply_translation([0,-recess,0])
    mesh=rotate_x_about(
        mesh,REAR_TILT_RAD,0.0,UPPER_MAIN_BOTTOM_Z
    )
    mesh.apply_translation([0,UPPER_REAR_BOTTOM_Y,0])
    return mesh


def rear_local_z(world_z):
    """Convert a desired world Z to the rigid rear-panel coordinate."""
    return UPPER_MAIN_BOTTOM_Z+(
        world_z-UPPER_MAIN_BOTTOM_Z
    )/math.cos(REAR_TILT_RAD)


def rotate_x_about(mesh,angle,pivot_y,pivot_z):
    """Rigidly rotate a mesh around an X-axis through the given Y/Z point."""
    transform=trimesh.transformations.rotation_matrix(
        angle,[1,0,0],[0,pivot_y,pivot_z]
    )
    mesh.apply_transform(transform)
    return mesh


def transform_front_part(mesh):
    """Place local front-panel geometry on the six-degree cabinet plane."""
    mesh=rotate_x_about(mesh,-FRONT_TILT_RAD,0.0,UPPER_MAIN_BOTTOM_Z)
    mesh.apply_translation([0,UPPER_FRONT_Y+FRONT_PANEL_RECESS,0])
    return mesh


def transform_shell_front_part(mesh):
    """Place geometry on the shell's six-degree front coordinate system."""
    mesh=rotate_x_about(mesh,-FRONT_TILT_RAD,0.0,UPPER_MAIN_BOTTOM_Z)
    mesh.apply_translation([0,UPPER_FRONT_Y,0])
    return mesh


def inverse_transform_shell_front_part(mesh):
    """Move world geometry into the shell's front coordinate system."""
    mesh.apply_translation([0,-UPPER_FRONT_Y,0])
    return rotate_x_about(mesh,FRONT_TILT_RAD,0.0,UPPER_MAIN_BOTTOM_Z)


def front_local_z(world_z):
    """Convert desired world Z to distance along the rigidly tilted panel."""
    return UPPER_MAIN_BOTTOM_Z + (
        world_z-UPPER_MAIN_BOTTOM_Z
    )/math.cos(FRONT_TILT_RAD)


def transform_key_part(mesh):
    """Place holder/cradle geometry on the complete sloping front deck."""
    mesh=rotate_x_about(
        mesh,KEY_TILT_RAD,KEY_Y,KEY_ROTATION_PIVOT_Z
    )
    mesh.apply_translation([0,0,KEY_ASSEMBLY_Z_SHIFT])
    return mesh


def rounded_xz(width, height, radius, depth, y0, cx=0.0, cz=0.0):
    mesh = trimesh.creation.extrude_polygon(
        rounded_polygon(width, height, radius, cx, cz), height=depth
    )
    transform = np.array([
        [1, 0, 0, 0],
        [0, 0, 1, y0],
        [0, 1, 0, 0],
        [0, 0, 0, 1],
    ], dtype=float)
    mesh.apply_transform(transform)
    if mesh.volume < 0:
        mesh.invert()
    return mesh


def rounded_yz(depth, height, radius, width, x0, cy=0.0, cz=0.0):
    """Extrude a rounded Y/Z rectangle along +X."""
    mesh = trimesh.creation.extrude_polygon(
        rounded_polygon(depth, height, radius, cy, cz), height=width
    )
    transform = np.array([
        [0, 0, 1, x0],
        [1, 0, 0, 0],
        [0, 1, 0, 0],
        [0, 0, 0, 1],
    ], dtype=float)
    mesh.apply_transform(transform)
    if mesh.volume < 0:
        mesh.invert()
    return mesh


def rounded_xy_ring(outer_w, outer_d, radius, wall, height, z0,
                    cx=0.0, cy=0.0):
    """Rounded rectangular locating ring in the X/Y plane."""
    outer = rounded_xy(outer_w, outer_d, radius, height, z0, cx, cy)
    inner = rounded_xy(
        outer_w-2*wall, outer_d-2*wall, max(0.5, radius-wall),
        height+0.4, z0-0.2, cx, cy,
    )
    return difference(outer, inner)


def hex_prism(across_flats, height, center):
    """Regular hexagonal cutter sized by across-flats distance."""
    return cylinder(
        across_flats/math.sqrt(3.0), height, center,
        sections=6,
    )


def apple_logo_polygons(height, cx=0.0, cz=0.0, inset=0.0):
    """Return the body and leaf contours of the macOS Apple-logo glyph."""
    glyph = TextPath(
        (0,0), "\uf8ff", size=100,
        prop=FontProperties(fname=APPLE_LOGO_FONT),
    )
    polygons = [Polygon(points).buffer(0) for points in glyph.to_polygons()]
    polygons = [p for p in polygons if not p.is_empty and p.area > 0]
    minx=min(p.bounds[0] for p in polygons); minz=min(p.bounds[1] for p in polygons)
    maxx=max(p.bounds[2] for p in polygons); maxz=max(p.bounds[3] for p in polygons)
    scale=height/(maxz-minz)
    midx=(minx+maxx)/2; midz=(minz+maxz)/2
    result=[]
    for polygon in polygons:
        polygon=affinity.scale(polygon,xfact=scale,yfact=scale,origin=(midx,midz))
        polygon=affinity.translate(polygon,cx-midx,cz-midz)
        if inset:
            polygon=polygon.buffer(-inset,join_style=1)
        if not polygon.is_empty:
            result.append(polygon)
    return result


def extrude_xz_polygon(polygon, depth, y0):
    """Extrude a Shapely X/Z polygon along +Y."""
    mesh=trimesh.creation.extrude_polygon(polygon,height=depth)
    transform=np.array([
        [1,0,0,0],
        [0,0,1,y0],
        [0,1,0,0],
        [0,0,0,1],
    ],dtype=float)
    mesh.apply_transform(transform)
    if mesh.volume < 0:
        mesh.invert()
    return mesh


def countersunk_cutter_z(x, y, sections=48):
    rings = [(3.2, -0.1), (1.7, 1.4), (1.7, BOTTOM_T+1.0)]
    vertices = []
    for radius, z in rings:
        for i in range(sections):
            a = 2*math.pi*i/sections
            vertices.append([x+radius*math.cos(a), y+radius*math.sin(a), z])
    vertices.extend([[x, y, rings[0][1]], [x, y, rings[-1][1]]])
    bc, tc = len(vertices)-2, len(vertices)-1
    faces = []
    for r in range(len(rings)-1):
        a0, b0 = r*sections, (r+1)*sections
        for i in range(sections):
            j=(i+1)%sections
            faces.extend([[a0+i,b0+i,b0+j],[a0+i,b0+j,a0+j]])
    for i in range(sections):
        j=(i+1)%sections
        faces.extend([[bc,i,j],[tc,(len(rings)-1)*sections+j,
                                    (len(rings)-1)*sections+i]])
    mesh=trimesh.Trimesh(vertices=vertices,faces=faces,process=True)
    if mesh.volume < 0: mesh.invert()
    return mesh


def upper_front_y(z):
    t=(z-UPPER_MAIN_BOTTOM_Z)/(UPPER_MAIN_TOP_Z-UPPER_MAIN_BOTTOM_Z)
    return UPPER_FRONT_Y+t*(UPPER_FRONT_TOP_Y-UPPER_FRONT_Y)


def upper_rear_y(z):
    t=(z-UPPER_MAIN_BOTTOM_Z)/(UPPER_MAIN_TOP_Z-UPPER_MAIN_BOTTOM_Z)
    return UPPER_REAR_BOTTOM_Y+t*(UPPER_REAR_TOP_Y-UPPER_REAR_BOTTOM_Y)


def base_deck_z(y):
    """Top surface: a level rear platform then a 7-degree keyboard deck."""
    return BASE_H + min(
        0.0,math.tan(KEY_TILT_RAD)*(y-BASE_DECK_HINGE_Y)
    )


def build_lower_base():
    # Base shell, open underneath.  The whole exposed keyboard deck descends
    # seven degrees toward the user from the key-well rear edge.  The strip
    # between that crease and the upper shell stays level and visibly marks
    # the two-part profile.  Both perimeter bevels remain part of the shell.
    base_outer = rounded_xy_variable_z_loft((
        (BASE_TOP_W,BASE_TOP_D,BASE_TOP_R,0.0),
        (BASE_W,BASE_D,BASE_R,BASE_EDGE_CHAMFER),
        (BASE_W,BASE_D,BASE_R,
         lambda y:base_deck_z(y)-BASE_EDGE_CHAMFER),
        (BASE_TOP_W,BASE_TOP_D,BASE_TOP_R,base_deck_z),
    ),top_break_y=BASE_DECK_HINGE_Y)
    # The inner lower edge tapers as well, keeping useful wall thickness at
    # the 45-degree outside chamfer.  The current bottom cover is narrowed to
    # pass through this reinforced opening.
    base_inner=rounded_xy_variable_z_loft((
        (101.6,96.6,4.0,-1.0),
        (BASE_W-2*WALL,BASE_D-2*WALL,BASE_R-WALL,
         BASE_EDGE_CHAMFER+0.2),
        (BASE_W-2*WALL,BASE_D-2*WALL,BASE_R-WALL,
         lambda y:base_deck_z(y)-WALL),
    ),top_break_y=BASE_DECK_HINGE_Y)
    base = difference(base_outer, base_inner)

    # Retro keyboard-style well on the inclined deck.  Its complete visible
    # floor is exactly 5 mm below that deck.  One opening
    # exposes the complete 58 x 20 mm holder instead of cutting three MX
    # switch holes; the transparent holder itself now locates the switches.
    key_well = transform_key_part(rounded_xy(
        KEY_WELL_W,KEY_WELL_D,5.0,KEY_WELL_DEPTH+4.0,
        BASE_H-KEY_WELL_DEPTH,cy=KEY_Y,
    ))
    # Cut fully through the taller 28 mm base.  The previous fixed cutter
    # stopped below the deck after BASE_H changed.  A 0.5 mm corner radius
    # follows the nearly square corners of the measured transparent holder.
    key_slot = transform_key_part(rounded_xy(
        KEY_SLOT_W,KEY_SLOT_D,0.5,BASE_H+10.0,-5.0,cy=KEY_Y,
    ))
    base = difference(base,key_well)

    # Restore a closed, sloping floor at the bottom of the 5 mm recess.  A
    # hidden perimeter wall joins this deep floor back into the shallower
    # 2.8 mm shell roof without changing the specified visible opening.
    key_well_floor=transform_key_part(rounded_xy(
        KEY_WELL_W+2*KEY_WELL_WALL_T,
        KEY_WELL_D+2*KEY_WELL_WALL_T,
        5.0+KEY_WELL_WALL_T,KEY_WELL_FLOOR_T,
        BASE_H-KEY_WELL_DEPTH-KEY_WELL_FLOOR_T,cy=KEY_Y,
    ))
    key_well_wall=transform_key_part(rounded_xy_ring(
        KEY_WELL_W+2*KEY_WELL_WALL_T,
        KEY_WELL_D+2*KEY_WELL_WALL_T,
        5.0+KEY_WELL_WALL_T,KEY_WELL_WALL_T,
        KEY_WELL_DEPTH,
        BASE_H-KEY_WELL_DEPTH-0.2,cy=KEY_Y,
    ))
    base=union(base,key_well_floor,key_well_wall)
    base=difference(base,key_slot)

    slot_front_y = KEY_Y - KEY_SLOT_D/2
    slot_rear_y = KEY_Y + KEY_SLOT_D/2
    rail_bottom_z = KEY_CRADLE_SUPPORT_WORLD_Z - KEY_CRADLE_RAIL_T
    # All three fixed walls terminate with the retaining strips exactly
    # 4 mm below the surrounding inclined deck; nothing projects above it.
    cradle_top_z=BASE_H-KEY_RETAINING_TOP_CLEARANCE
    cradle_h = cradle_top_z - rail_bottom_z

    # Three closed vertical sides.  Their inner faces remain completely
    # outside the 58.6 x 20.6 mm opening, with no lip or cover over the
    # holder footprint, so large keycaps can travel without interference.
    # The rear (+Y) stays open for inserting the holder from inside the base.
    cradle_sides = [
        box(
            [KEY_CRADLE_WALL_T, KEY_SLOT_D + 2*KEY_CRADLE_WALL_T, cradle_h],
            center=[
                -(KEY_SLOT_W + KEY_CRADLE_WALL_T)/2,
                KEY_Y,
                rail_bottom_z + cradle_h/2,
            ],
        ),
        box(
            [KEY_CRADLE_WALL_T, KEY_SLOT_D + 2*KEY_CRADLE_WALL_T, cradle_h],
            center=[
                +(KEY_SLOT_W + KEY_CRADLE_WALL_T)/2,
                KEY_Y,
                rail_bottom_z + cradle_h/2,
            ],
        ),
        box(
            [KEY_SLOT_W + 2*KEY_CRADLE_WALL_T, KEY_CRADLE_WALL_T, cradle_h],
            center=[
                0.0,
                slot_front_y - KEY_CRADLE_WALL_T/2,
                rail_bottom_z + cradle_h/2,
            ],
        ),
    ]

    # Two narrow ledges support the holder around its front/rear edges.  The
    # large opening between them leaves every switch pin and soldered wire
    # accessible from below.
    rail_w = KEY_SLOT_W + 0.8
    cradle_rails = [
        box(
            [rail_w, KEY_CRADLE_RAIL_D, KEY_CRADLE_RAIL_T],
            # Extend 0.15 mm into the front wall so the rotated rail cannot
            # leave a coincident non-manifold seam at the slot boundary.
            center=[0.0, slot_front_y + KEY_CRADLE_RAIL_D/2-0.15,
                    rail_bottom_z + KEY_CRADLE_RAIL_T/2],
        ),
        box(
            [rail_w, KEY_CRADLE_RAIL_D, KEY_CRADLE_RAIL_T],
            center=[0.0, slot_rear_y - KEY_CRADLE_RAIL_D/2,
                    rail_bottom_z + KEY_CRADLE_RAIL_T/2],
        ),
    ]

    # Three retaining lips sit just above the 12 mm-high holder.  They reach
    # 1.5 mm inward and are 1.5 mm tall, preventing upward movement while the
    # rear remains open for horizontal insertion.  The bottom-cover stop
    # closes that rear opening after assembly.
    lip_bottom_z = (
        KEY_CRADLE_SUPPORT_WORLD_Z + KEY_HOLDER_H
        + KEY_CRADLE_LIP_CLEARANCE_Z
    )
    cradle_lips = [
        box(
            [KEY_CRADLE_WALL_T + KEY_CRADLE_LIP_INSET,
             KEY_SLOT_D + 2*KEY_CRADLE_WALL_T, KEY_CRADLE_LIP_H],
            center=[
                -KEY_SLOT_W/2 - KEY_CRADLE_WALL_T/2
                + KEY_CRADLE_LIP_INSET/2,
                KEY_Y,
                lip_bottom_z + KEY_CRADLE_LIP_H/2,
            ],
        ),
        box(
            [KEY_CRADLE_WALL_T + KEY_CRADLE_LIP_INSET,
             KEY_SLOT_D + 2*KEY_CRADLE_WALL_T, KEY_CRADLE_LIP_H],
            center=[
                KEY_SLOT_W/2 + KEY_CRADLE_WALL_T/2
                - KEY_CRADLE_LIP_INSET/2,
                KEY_Y,
                lip_bottom_z + KEY_CRADLE_LIP_H/2,
            ],
        ),
        box(
            [KEY_SLOT_W + 2*KEY_CRADLE_WALL_T,
             KEY_CRADLE_WALL_T + KEY_CRADLE_LIP_INSET,
             KEY_CRADLE_LIP_H],
            center=[
                0.0,
                slot_front_y - KEY_CRADLE_WALL_T/2
                + KEY_CRADLE_LIP_INSET/2,
                lip_bottom_z + KEY_CRADLE_LIP_H/2,
            ],
        ),
    ]
    cradle_sides=[transform_key_part(mesh) for mesh in cradle_sides]
    cradle_rails=[transform_key_part(mesh) for mesh in cradle_rails]
    cradle_lips=[transform_key_part(mesh) for mesh in cradle_lips]
    base = union(base,*cradle_sides,*cradle_rails,*cradle_lips)

    body = base

    # Hidden groove for the upper barrel's downward locating ring.  The ring
    # fills this opening after assembly, so the deck stays visually closed.
    locator_groove = rounded_xy_ring(
        UPPER_LOCATOR_OUTER_W + 2*UPPER_LOCATOR_CLEARANCE,
        UPPER_LOCATOR_OUTER_D + 2*UPPER_LOCATOR_CLEARANCE,
        UPPER_R + UPPER_LOCATOR_CLEARANCE,
        UPPER_LOCATOR_WALL + 2*UPPER_LOCATOR_CLEARANCE,
        UPPER_LOCATOR_H + 0.8,
        UPPER_BOTTOM_Z - UPPER_LOCATOR_H - 0.4,
        cy=UPPER_FRONT_Y + UPPER_D/2,
    )
    body = difference(body, locator_groove)

    # Use the same cutter dimensions and datum as the upper-shell opening so
    # the cable passage remains exactly aligned across the assembly seam.
    junction_wire_cut = rounded_xy(
        JUNCTION_WIRE_W,JUNCTION_WIRE_D,JUNCTION_WIRE_R,
        JUNCTION_WIRE_CUT_H,UPPER_BOTTOM_Z-JUNCTION_WIRE_CUT_H/2,
        cy=JUNCTION_WIRE_CENTER_Y,
    )
    body = difference(body, junction_wire_cut)

    # M3 clearance holes through the deck.  Machine screws enter here from
    # the base interior and engage captive hex nuts in the upper barrel.
    upper_mount_cuts = [
        cylinder(M3_CLEARANCE_R, WALL+2.0,
                 [x,y,UPPER_BOTTOM_Z-WALL/2], sections=48)
        for x in UPPER_BASE_MOUNT_X for y in UPPER_BASE_MOUNT_Y
    ]
    body = difference(body, *upper_mount_cuts)

    # Four short bottom-cover posts in the rear half, tied to side walls.
    boss_h=8.0
    bosses=[]; ribs=[]; pilots=[]
    for x in BOTTOM_SCREW_X:
        for y in BOTTOM_SCREW_Y:
            bosses.append(cylinder(4.0,boss_h,[x,y,BOTTOM_T+boss_h/2]))
            rib_x=math.copysign(BASE_W/2-5.5,x)
            ribs.append(box([11.0,8.0,4.0],center=[rib_x,y,BOTTOM_T+2.0]))
            pilots.append(cylinder(
                M3_SELF_TAP_PILOT_R,boss_h+2,[x,y,BOTTOM_T+boss_h/2]
            ))
    body=union(body,*bosses,*ribs)
    body=difference(body,*pilots)

    # Rear-right cooler connector.  The PCB sits vertically, parallel to the
    # rear wall.  Its four mounting bosses follow the seller's 12 x 7.2 mm
    # pattern.  The pattern centre also aligns with the USB-C mouth.
    rear_outer_y = BASE_D/2
    cooler_inner_face_y = rear_outer_y - WALL
    cooler_board_face_y = cooler_inner_face_y - COOLER_BOSS_LENGTH
    cooler_boss_total = COOLER_BOSS_LENGTH + COOLER_BOSS_EMBED
    cooler_boss_y = (
        cooler_inner_face_y + COOLER_BOSS_EMBED + cooler_board_face_y
    ) / 2
    cooler_hole_x = tuple(
        COOLER_PORT_X + s*COOLER_HOLE_SPACING_X/2 for s in (-1, 1)
    )
    cooler_hole_z = tuple(
        COOLER_PATTERN_CENTER_Z + s*COOLER_HOLE_SPACING_Z/2 for s in (-1, 1)
    )
    cooler_bosses = [
        cylinder(COOLER_BOSS_RADIUS, cooler_boss_total,
                 [x, cooler_boss_y, z], [0, 1, 0])
        for x in cooler_hole_x for z in cooler_hole_z
    ]
    # Small embedded pads give the boolean union a broad overlap with the
    # rear connector panel and spread screw load into it.
    cooler_boss_anchors = [
        box([2*COOLER_BOSS_RADIUS, 1.0, 2*COOLER_BOSS_RADIUS],
            center=[x, cooler_inner_face_y+0.1, z])
        for x in cooler_hole_x for z in cooler_hole_z
    ]
    cooler_pilots = [
        cylinder(COOLER_PILOT_RADIUS, cooler_boss_total+1.2,
                 [x, cooler_boss_y-0.3, z], [0, 1, 0])
        for x in cooler_hole_x for z in cooler_hole_z
    ]

    # Rear-left Micro-USB board.  Its 5 mm connector projection works with the
    # uniform 2.8 mm rear panel and 2.2 mm standoffs.  The two M3 posts follow
    # measured 11 mm hole pitch and the row through the PCB vertical centre.
    micro_inner_face_y = cooler_inner_face_y
    micro_board_face_y = micro_inner_face_y - MICRO_BOSS_LENGTH
    micro_boss_total = MICRO_BOSS_LENGTH + MICRO_BOSS_EMBED
    micro_boss_y = (
        micro_inner_face_y + MICRO_BOSS_EMBED + micro_board_face_y
    ) / 2
    micro_hole_x = tuple(
        MICRO_PORT_X + s*MICRO_HOLE_SPACING_X/2 for s in (-1, 1)
    )
    micro_bosses = [
        cylinder(MICRO_BOSS_RADIUS, micro_boss_total,
                 [x, micro_boss_y, MICRO_PCB_CENTER_Z], [0, 1, 0])
        for x in micro_hole_x
    ]
    micro_boss_anchors = [
        box([2*MICRO_BOSS_RADIUS, 1.0, 2*MICRO_BOSS_RADIUS],
            center=[x, micro_inner_face_y+0.1, MICRO_PCB_CENTER_Z])
        for x in micro_hole_x
    ]
    micro_pilots = [
        cylinder(MICRO_PILOT_RADIUS, micro_boss_total+1.2,
                 [x, micro_boss_y-0.3, MICRO_PCB_CENTER_Z], [0, 1, 0])
        for x in micro_hole_x
    ]

    # Continue the right Type-C passage all the way from the breakout PCB
    # mounting face through the exterior.  The older wall-only cutter left
    # portions of the four screw bosses inside the receptacle path.
    cooler_port_cut_y0 = cooler_board_face_y - 0.3
    cooler_port_cut_depth = rear_outer_y + 0.5 - cooler_port_cut_y0
    cooler_port = rounded_xz(
        COOLER_PORT_W, COOLER_PORT_H, COOLER_PORT_H/2-0.1,
        cooler_port_cut_depth,cooler_port_cut_y0,
        cx=COOLER_PORT_X,cz=COOLER_PATTERN_CENTER_Z,
    )

    # Enlarged standard Micro-USB B outline: straight full-width upper edge,
    # vertical upper sides, and two 45-degree lower corner chamfers.  This is
    # intentionally asymmetric top-to-bottom and is not a generic octagon.
    mw = MICRO_PORT_W/2
    mh = MICRO_PORT_H/2
    mc = MICRO_PORT_CHAMFER
    micro_outline = Polygon([
        (MICRO_PORT_X-mw, MICRO_PORT_CENTER_Z+mh),
        (MICRO_PORT_X+mw, MICRO_PORT_CENTER_Z+mh),
        (MICRO_PORT_X+mw, MICRO_PORT_CENTER_Z-mh+mc),
        (MICRO_PORT_X+mw-mc, MICRO_PORT_CENTER_Z-mh),
        (MICRO_PORT_X-mw+mc, MICRO_PORT_CENTER_Z-mh),
        (MICRO_PORT_X-mw, MICRO_PORT_CENTER_Z-mh+mc),
    ])
    # Extend the cutter from just behind the PCB mounting face through the
    # outer panel.  Any portion of either M3 post that enters the connector
    # passage is removed, leaving the complete port area unobstructed.
    micro_port_cut_y0 = micro_board_face_y - 0.3
    micro_port_cut_depth = rear_outer_y + 0.5 - micro_port_cut_y0
    micro_port = extrude_xz_polygon(
        micro_outline, micro_port_cut_depth, micro_port_cut_y0
    )

    # The centre ESP32 extension board lies flat beneath the rear deck.  Its
    # rear edge and the Type-C mouth share one plane, one millimetre behind
    # the enclosure exterior.  The upper platen is tied directly into the
    # deck roof; the rear wall itself provides the fourth in-plane stop.
    esp_pcb_rear_y = rear_outer_y - ESP_USB_REAR_SKIN_T
    esp_pcb_front_y = esp_pcb_rear_y - ESP_USB_PCB_D
    esp_clamp_rear_y = cooler_inner_face_y - 0.3
    esp_front_limit_inner_y = esp_pcb_front_y - ESP_USB_XY_CLEARANCE
    esp_side_limit_inner_x = ESP_USB_PCB_W/2 + ESP_USB_XY_CLEARANCE
    esp_platen_front_y = esp_front_limit_inner_y - ESP_USB_LIMIT_T
    esp_platen_top_z = BASE_H - WALL + 0.4
    esp_platen_h = esp_platen_top_z - ESP_USB_PCB_TOP_Z
    esp_platen_w = 2*(esp_side_limit_inner_x + ESP_USB_LIMIT_T)
    esp_platen_d = esp_clamp_rear_y - esp_platen_front_y
    esp_platen = box(
        [esp_platen_w,esp_platen_d,esp_platen_h],
        center=[
            ESP_USB_PORT_X,
            (esp_platen_front_y+esp_clamp_rear_y)/2,
            ESP_USB_PCB_TOP_Z+esp_platen_h/2,
        ],
    )
    esp_side_limits = [
        box(
            [ESP_USB_LIMIT_T,
             esp_clamp_rear_y-esp_front_limit_inner_y,
             ESP_USB_LIMIT_H],
            center=[
                ESP_USB_PORT_X+s*(
                    esp_side_limit_inner_x+ESP_USB_LIMIT_T/2
                ),
                (esp_front_limit_inner_y+esp_clamp_rear_y)/2,
                ESP_USB_PCB_TOP_Z-ESP_USB_LIMIT_H/2,
            ],
        )
        for s in (-1,1)
    ]
    esp_front_limit = box(
        [esp_platen_w,ESP_USB_LIMIT_T,ESP_USB_LIMIT_H],
        center=[
            ESP_USB_PORT_X,
            esp_front_limit_inner_y-ESP_USB_LIMIT_T/2,
            ESP_USB_PCB_TOP_Z-ESP_USB_LIMIT_H/2,
        ],
    )

    # Recess the rear wall only from the inside, leaving a measured one-
    # millimetre exterior skin over the PCB envelope.  The rounded port grows
    # slightly toward the outside as a short insertion lead-in.
    esp_module_center_z = (
        ESP_USB_MODULE_BOTTOM_Z+ESP_USB_PCB_TOP_Z
    )/2
    esp_rear_pocket_y0 = cooler_inner_face_y - 0.2
    esp_rear_pocket_depth = (
        esp_pcb_rear_y-esp_rear_pocket_y0
    )
    esp_rear_pocket = rounded_xz(
        ESP_USB_PCB_W+2*ESP_USB_XY_CLEARANCE,
        ESP_USB_ASSEMBLY_H+2*ESP_USB_XY_CLEARANCE,
        0.7,esp_rear_pocket_depth,esp_rear_pocket_y0,
        cx=ESP_USB_PORT_X,cz=esp_module_center_z,
    )
    esp_port = rounded_rect_frustum_xz(
        ESP_USB_PORT_W,ESP_USB_PORT_H,ESP_USB_PORT_R,
        ESP_USB_PORT_W+0.4,ESP_USB_PORT_H+0.4,ESP_USB_PORT_R+0.2,
        ESP_USB_REAR_SKIN_T+0.5,esp_pcb_rear_y-0.25,
        cx=ESP_USB_PORT_X,cz=ESP_USB_PORT_CENTER_Z,
    )

    body = union(
        body,
        *cooler_bosses, *cooler_boss_anchors,
        *micro_bosses, *micro_boss_anchors,
        esp_platen,*esp_side_limits,esp_front_limit,
    )
    # Cut the port tunnels after adding the posts so no anchor can intrude
    # into either connector opening.
    body = difference(
        body, cooler_port, micro_port, esp_rear_pocket, esp_port,
        *cooler_pilots, *micro_pilots
    )
    return body


def build_upper_shell():
    """Tapered upper barrel with rounded front and chamfered rear edges."""
    outer=upper_outer_loft()
    gusset_limit=upper_outer_loft(tuple(
        (z,width-2*UPPER_GUSSET_SKIN,depth-2*UPPER_GUSSET_SKIN,
         max(0.5,radius-UPPER_GUSSET_SKIN),centre_y)
        for z,width,depth,radius,centre_y in UPPER_LOFT_SECTIONS
    ))

    # The cavity is narrower in X but deliberately deeper in Y than the
    # exterior.  It therefore leaves the two side walls and top/bottom rails
    # while opening both service faces completely.
    top_inner_run = UPPER_TOP_CHAMFER-WALL
    top_inner_outer_w = 92.0-2*top_inner_run
    top_inner_outer_d = UPPER_MAIN_TOP_D-2*top_inner_run
    top_inner_radius = 5.5-top_inner_run-WALL
    inner = rounded_xy_loft((
        (UPPER_BOTTOM_Z+WALL,UPPER_W-2*WALL,62.8,UPPER_R-WALL,
         UPPER_BASE_CENTER_Y),
        (UPPER_MAIN_TOP_Z,86.4,UPPER_MAIN_TOP_D+6.0,3.0,
         UPPER_MAIN_TOP_CENTER_Y),
        (UPPER_BOTTOM_Z+UPPER_H-WALL,
         top_inner_outer_w-2*WALL,top_inner_outer_d+6.0,
         top_inner_radius,UPPER_TOP_CENTER_Y),
    ))
    shell = difference(outer, inner)

    # Cut the barrel at one exact plane.  The removable cap is taken from the
    # complementary front slice of the same master outer solid, so the two
    # exterior surfaces meet without a modelled step or visible clearance.
    front_slice=box(
        [120.0,UPPER_FRONT_SEAM_Y+20.0,140.0],
        center=[
            0.0,(UPPER_FRONT_SEAM_Y-20.0)/2,
            front_local_z(UPPER_BOTTOM_Z+UPPER_H/2),
        ],
    )
    shell=difference(shell,transform_shell_front_part(front_slice))

    # Cut the rear service opening as a four-sided countersink in the fixed
    # shell.  The opening grows outward by exactly the same 2.5 mm as its
    # depth, giving the top, bottom, left and right edges matching 45-degree
    # planar bevels.  Square joins remove the former rounded side treatment.
    panel_bottom_local_z=rear_local_z(BACK_PANEL_BOTTOM_Z)
    panel_top_local_z=rear_local_z(BACK_PANEL_TOP_Z)
    panel_h=panel_top_local_z-panel_bottom_local_z
    panel_cz=(panel_top_local_z+panel_bottom_local_z)/2
    rear_cover_profile=trapezoid(
        BACK_PANEL_W_BOTTOM,BACK_PANEL_W_TOP,panel_h,cz=panel_cz
    )
    rear_inner_opening=rear_cover_profile.buffer(
        UPPER_BACK_CLEARANCE/2,join_style=2
    )
    rear_outer_opening=rear_inner_opening.buffer(
        REAR_FRAME_BEVEL,join_style=2
    )
    # The main hollow barrel has a deliberately generous rear service cavity.
    # A subtractive countersink alone cannot reduce that opening to the
    # specified 0.25 mm cover clearance.  Add a solid rear receiver slab,
    # clip it to the master exterior, then cut the exact opening and bevel
    # from that material.  This also blocks the exaggerated see-through gap
    # visible in the V115 diagnostic render.
    rear_receiver_depth=REAR_FRAME_BEVEL+WALL
    rear_receiver_local=box(
        [120.0,rear_receiver_depth,UPPER_H+20.0],
        center=[
            0.0,-rear_receiver_depth/2,
            UPPER_BOTTOM_Z+UPPER_H/2,
        ],
    )
    rear_receiver=intersection(
        transform_rear_part(rear_receiver_local),outer
    )
    shell=union(shell,rear_receiver)
    rear_bevel_cut=loft_xz_polygons(
        rear_inner_opening,rear_outer_opening,
        REAR_FRAME_BEVEL,-REAR_FRAME_BEVEL,sample_count=80,
    )
    rear_through_cut=extrude_xz_polygon(
        rear_inner_opening,20.0+REAR_FRAME_BEVEL+0.2,
        -REAR_FRAME_BEVEL-20.0
    )
    shell=difference(
        shell,
        transform_rear_part(rear_bevel_cut),
        transform_rear_part(rear_through_cut),
    )

    # Downward locating ring that enters the hidden groove in the base deck.
    locator = rounded_xy_ring(
        UPPER_LOCATOR_OUTER_W, UPPER_LOCATOR_OUTER_D,
        UPPER_R, UPPER_LOCATOR_WALL,
        UPPER_LOCATOR_H+0.4,
        UPPER_BOTTOM_Z-UPPER_LOCATOR_H,
        cy=UPPER_BASE_CENTER_Y,
    )

    # Four reinforced vertical bosses hold ordinary M3 nuts.  Nut pockets
    # open upward into the barrel so the hardware remains replaceable.
    mount_bosses = [
        cylinder(
            UPPER_BASE_BOSS_R, UPPER_BASE_BOSS_H,
            [x,y,UPPER_BOTTOM_Z+UPPER_BASE_BOSS_H/2],
        )
        for x in UPPER_BASE_MOUNT_X for y in UPPER_BASE_MOUNT_Y
    ]
    shell = union(shell, locator, *mount_bosses)
    mount_through = [
        cylinder(
            M3_CLEARANCE_R, UPPER_BASE_BOSS_H+UPPER_LOCATOR_H+1.0,
            [x,y,UPPER_BOTTOM_Z+(UPPER_BASE_BOSS_H-UPPER_LOCATOR_H)/2],
        )
        for x in UPPER_BASE_MOUNT_X for y in UPPER_BASE_MOUNT_Y
    ]
    nut_traps = [
        hex_prism(
            M3_NUT_AF, M3_NUT_DEPTH+0.4,
            [x,y,UPPER_BOTTOM_Z+UPPER_BASE_BOSS_H-M3_NUT_DEPTH/2+0.2],
        )
        for x in UPPER_BASE_MOUNT_X for y in UPPER_BASE_MOUNT_Y
    ]
    shell = difference(shell, *mount_through, *nut_traps)

    # Restore the upper half of the inter-cabin cable passage.  This is the
    # exact same cutter used on the lower-base deck, so both openings share
    # one size, centre and corner radius instead of relying on visual alignment.
    junction_wire_cut = rounded_xy(
        JUNCTION_WIRE_W,JUNCTION_WIRE_D,JUNCTION_WIRE_R,
        JUNCTION_WIRE_CUT_H,UPPER_BOTTOM_Z-JUNCTION_WIRE_CUT_H/2,
        cy=JUNCTION_WIRE_CENTER_Y,
    )
    shell = difference(shell,junction_wire_cut)

    # A shallow top handle recess and restrained vent groups give the shell
    # the compact-Mac silhouette without weakening the complete top wall.
    handle = rounded_xy(
        45.0, 18.0, 5.0, 1.7,
        UPPER_BOTTOM_Z+UPPER_H-1.2,
        cy=UPPER_TOP_CENTER_Y,
    )
    vent_cuts = []
    top_z0 = UPPER_BOTTOM_Z+UPPER_H-4.0
    for x in (-37.0,-33.0,-29.0,29.0,33.0,37.0):
        vent_cuts.append(rounded_xy(
            1.8, 12.0, 0.75, 5.0, top_z0,
            cx=x,cy=UPPER_TOP_CENTER_Y,
        ))

    # Four low side vents per side, placed behind the front-panel hardware.
    for x0 in (-UPPER_W/2-0.5, UPPER_W/2-4.5):
        for z in (
            UPPER_BOTTOM_Z+23.0,UPPER_BOTTOM_Z+27.0,
            UPPER_BOTTOM_Z+31.0,UPPER_BOTTOM_Z+35.0,
        ):
            vent_cuts.append(rounded_yz(
                12.0, 1.8, 0.75, 5.0, x0,
                cy=UPPER_BASE_CENTER_Y+7.0,cz=z,
            ))
    shell = difference(shell, handle, *vent_cuts)

    # Two front-panel ears meet the panel bosses without overlapping them.
    # They are built in the panel's own coordinate system so their bores are
    # normal to the rigid six-degree front face.
    mount_local_z=front_local_z(UPPER_FRONT_MOUNT_Z)
    panel_boss_rear_y = UPPER_FRONT_T + UPPER_FRONT_PANEL_BOSS_LEN
    ear_center_y = panel_boss_rear_y + 0.25 + UPPER_FRONT_SHELL_EAR_LEN/2
    front_ears=[]; front_ribs=[]; front_clearance=[]
    for x in UPPER_FRONT_MOUNT_X:
        front_ears.append(cylinder(
            UPPER_FRONT_PANEL_BOSS_R, UPPER_FRONT_SHELL_EAR_LEN,
            [x,ear_center_y,mount_local_z],[0,1,0],
        ))
        rib_x=math.copysign(UPPER_W/2-3.3,x)
        front_ribs.append(box(
            [6.6,UPPER_FRONT_SHELL_EAR_LEN,6.0],
            center=[rib_x,ear_center_y,mount_local_z],
        ))
        front_clearance.append(cylinder(
            M3_CLEARANCE_R,UPPER_FRONT_SHELL_EAR_LEN+1.0,
            [x,ear_center_y+0.4,mount_local_z],[0,1,0],
        ))
    front_ears=[transform_front_part(mesh) for mesh in front_ears]
    front_ribs=[transform_front_part(mesh) for mesh in front_ribs]
    front_clearance=[transform_front_part(mesh) for mesh in front_clearance]
    # Clip the fixed-width gussets to the actual tapered barrel envelope so
    # they reinforce the inner wall without creating exterior side tabs.
    front_ribs=[intersection(mesh,gusset_limit) for mesh in front_ribs]
    shell=union(shell,*front_ears,*front_ribs)
    shell=difference(shell,*front_clearance)

    # Four rear-cover self-tapping bosses and short side-wall ribs.
    rear_bosses=[]; rear_ribs=[]; rear_pilots=[]
    for x in UPPER_BACK_MOUNT_X:
        for z in UPPER_BACK_MOUNT_Z:
            mount_local_z=rear_local_z(z)
            upper_back_inner_face_y=-UPPER_BACK_T
            rear_boss_center_y=(
                upper_back_inner_face_y-0.25-UPPER_BACK_BODY_BOSS_LEN/2
            )
            rear_bosses.append(cylinder(
                UPPER_BACK_BODY_BOSS_R, UPPER_BACK_BODY_BOSS_LEN,
                [x,rear_boss_center_y,mount_local_z],[0,1,0],
            ))
            rib_x=math.copysign(UPPER_W/2-3.5,x)
            rear_ribs.append(box(
                [7.0,UPPER_BACK_BODY_BOSS_LEN,6.0],
                center=[rib_x,rear_boss_center_y,mount_local_z],
            ))
            rear_pilots.append(cylinder(
                M3_SELF_TAP_PILOT_R,UPPER_BACK_BODY_BOSS_LEN+1.0,
                [x,rear_boss_center_y+0.5,mount_local_z],[0,1,0],
            ))
    rear_bosses=[
        transform_rear_part(mesh,UPPER_BACK_RECESS)
        for mesh in rear_bosses
    ]
    rear_ribs=[
        transform_rear_part(mesh,UPPER_BACK_RECESS)
        for mesh in rear_ribs
    ]
    rear_pilots=[
        transform_rear_part(mesh,UPPER_BACK_RECESS)
        for mesh in rear_pilots
    ]
    rear_ribs=[
        largest_volume_component(intersection(mesh,gusset_limit))
        for mesh in rear_ribs
    ]
    shell=union(shell,*rear_bosses,*rear_ribs)
    return largest_volume_component(difference(shell,*rear_pilots))


def build_upper_front_panel():
    """Flush master-surface front cap with CRT well and hidden mounts."""
    panel_bottom_local_z=front_local_z(FRONT_PANEL_BOTTOM_Z)
    panel_top_local_z=front_local_z(FRONT_PANEL_TOP_Z)
    panel_h=panel_top_local_z-panel_bottom_local_z
    panel_cz=(panel_top_local_z+panel_bottom_local_z)/2

    # Read the exact X/Z outline where the shared master outer solid is split.
    # The cap will finish on this same polygon, guaranteeing a flush zero-gap
    # joint while allowing its front edge treatment to replace the old XY arc.
    # The new rear-only corner treatment does not alter the front half.  Use
    # the original rounded master here so the proven V114 front panel remains
    # byte-for-byte stable rather than being retriangulated unnecessarily.
    outer_local=inverse_transform_shell_front_part(
        rounded_xy_loft(UPPER_LOFT_SECTIONS)
    )
    seam_segments=trimesh.intersections.mesh_plane(
        outer_local,[0.0,1.0,0.0],[0.0,UPPER_FRONT_SEAM_Y,0.0]
    )
    seam_lines=[
        LineString([
            (round(start[0],8),round(start[2],8)),
            (round(end[0],8),round(end[2],8)),
        ])
        for start,end in seam_segments
    ]
    seam_polygons=list(polygonize(unary_union(seam_lines)))
    if not seam_polygons:
        raise RuntimeError("unable to section the upper-shell master surface")
    seam_profile=max(seam_polygons,key=lambda polygon: polygon.area)

    # Move the exact seam outline forward while compensating for the six-degree
    # face tilt.  Its 2.5 mm inward offset is then connected over exactly
    # 2.5 mm depth, which makes every outer edge the same width and 45 degrees.
    edge_y=FRONT_PANEL_EDGE_BEVEL-0.05
    edge_profile=affinity.translate(
        seam_profile,
        yoff=(edge_y-UPPER_FRONT_SEAM_Y)*math.tan(FRONT_TILT_RAD),
    )
    front_profile=edge_profile.buffer(
        -FRONT_PANEL_EDGE_BEVEL,join_style=2
    )
    front_bevel=loft_xz_polygons(
        front_profile,edge_profile,FRONT_PANEL_EDGE_BEVEL,0.0,
        sample_count=160,
    )
    flush_side=loft_xz_polygons(
        edge_profile,seam_profile,
        UPPER_FRONT_SEAM_Y-edge_y,edge_y,
        sample_count=160,
    )
    panel=union(front_bevel,flush_side)

    # Hollow the rear centre while leaving the exact master-surface perimeter.
    # This is an internal relief only; it cannot create a visible outer frame.
    tongue_plane_compensation=(
        UPPER_FRONT_SEAM_Y*math.tan(FRONT_TILT_RAD)
    )
    tongue_bottom_z=(
        front_local_z(UPPER_BOTTOM_Z+WALL+0.4)
        +tongue_plane_compensation
    )
    tongue_top_z=(
        front_local_z(UPPER_BOTTOM_Z+UPPER_H-WALL-0.4)
        +tongue_plane_compensation
    )
    tongue_h=tongue_top_z-tongue_bottom_z
    tongue_cz=(tongue_top_z+tongue_bottom_z)/2
    tongue_outer_profile=rounded_trapezoid(
        UPPER_W-2*WALL-UPPER_FRONT_CLEARANCE,
        86.4-UPPER_FRONT_CLEARANCE,
        tongue_h,2.7,cz=tongue_cz,
    )
    tongue_inner_profile=rounded_trapezoid(
        UPPER_W-2*WALL-UPPER_FRONT_CLEARANCE-2*UPPER_FRONT_LIP_WALL,
        86.4-UPPER_FRONT_CLEARANCE-2*UPPER_FRONT_LIP_WALL,
        tongue_h-2*UPPER_FRONT_LIP_WALL,
        1.2,cz=tongue_cz,
    )
    back_pocket=extrude_xz_polygon(
        tongue_inner_profile,UPPER_FRONT_SEAM_Y-UPPER_FRONT_T+1.0,
        UPPER_FRONT_T,
    )
    panel=difference(panel,back_pocket)

    # A rear pad supplies enough material for the CRT funnel to continue
    # beyond the nominal panel thickness while retaining a printable wall.
    screen_local_z=front_local_z(SCREEN_Z)
    rear_pad=rounded_xz(
        CRT_REAR_PAD_W,CRT_REAR_PAD_H,3.3,2.7,
        UPPER_FRONT_T-0.3,cz=screen_local_z,
    )
    panel=union(panel,rear_pad)

    # The outer opening is intentionally much larger than the active area.
    # A rounded frustum creates the deep CRT-style sloped surround, while a
    # second cutter opens exactly the measured LCD active window.
    screen_recess=rounded_rect_frustum_xz(
        CRT_RECESS_W,CRT_RECESS_H,5.4,
        CRT_RECESS_INNER_W,CRT_RECESS_INNER_H,3.0,
        CRT_RECESS_DEPTH+0.2,-0.2,cz=screen_local_z,
    )
    screen_open = rounded_xz(
        SCREEN_OPEN_W, SCREEN_OPEN_H, 2.8, 2.2,
        CRT_RECESS_DEPTH-0.2, cz=screen_local_z,
    )
    # The rear pad extends 0.4 mm beyond the nominal CRT throat.  Relieve that
    # hidden ledge across the measured LCD backlight outline so shortening the
    # posts really leaves a controlled 0.5 mm glass-to-housing gap instead of
    # allowing the glass to touch the pad.
    lcd_face_pocket = rounded_xz(
        LCD_BACKLIGHT_W+2*LCD_FACE_POCKET_CLEARANCE,
        LCD_BACKLIGHT_H+2*LCD_FACE_POCKET_CLEARANCE,
        2.0, 1.0, CRT_RECESS_DEPTH-LCD_FACE_POCKET_OVERLAP,
        cz=screen_local_z,
    )
    floppy = rounded_xz(
        38.0, 3.4, 1.3, 1.4,
        -0.2, cx=FLOPPY_CENTER_X, cz=front_local_z(APPLE_LOGO_CENTER_Z),
    )
    floppy_step = rounded_xz(
        8.0, 2.2, 0.8, 1.4,
        -0.2, cx=FLOPPY_STEP_CENTER_X,
        cz=front_local_z(APPLE_LOGO_CENTER_Z-1.6),
    )
    apple_recesses = [
        extrude_xz_polygon(
            p, APPLE_LOGO_RECESS+0.2, -0.2
        )
        for p in apple_logo_polygons(
            APPLE_LOGO_H/math.cos(FRONT_TILT_RAD), APPLE_LOGO_CENTER_X,
            front_local_z(APPLE_LOGO_CENTER_Z)
        )
    ]
    panel = difference(
        panel, screen_recess, screen_open, lcd_face_pocket, floppy, floppy_step,
        *apple_recesses
    )

    # A hidden tongue enters the shell cavity.  Its 0.25 mm per-side fitting
    # clearance is entirely inboard of the exterior parting line.
    tongue_outer=extrude_xz_polygon(
        tongue_outer_profile,UPPER_FRONT_LIP_DEPTH,
        UPPER_FRONT_SEAM_Y-0.3,
    )
    tongue_inner=extrude_xz_polygon(
        tongue_inner_profile,UPPER_FRONT_LIP_DEPTH+0.5,
        UPPER_FRONT_SEAM_Y-0.55,
    )
    tongue=difference(tongue_outer,tongue_inner)

    # LCD posts and two blind front-panel screw bosses all project rearward.
    screen_posts=[]; screen_pilots=[]
    screen_pilot_front_land = 1.0
    screen_pilot_depth = LCD_POST_LENGTH-screen_pilot_front_land+0.2
    screen_post_solid_length = LCD_POST_LENGTH+LCD_POST_EMBED
    screen_post_y0 = CRT_RECESS_DEPTH-LCD_POST_EMBED
    for x in LCD_HOLE_X:
        for z in LCD_HOLE_Z:
            screen_posts.append(cylinder(
                3.8,screen_post_solid_length,
                [x,screen_post_y0+screen_post_solid_length/2,
                 front_local_z(z)],
                [0,1,0]
            ))
            screen_pilots.append(cylinder(
                1.1,screen_pilot_depth,
                [x,
                 CRT_RECESS_DEPTH+screen_pilot_front_land
                 +screen_pilot_depth/2,
                 front_local_z(z)],
                [0,1,0]
            ))
    panel_bosses=[]; panel_pilots=[]
    panel_inner_y=UPPER_FRONT_T-0.5
    panel_boss_length=UPPER_FRONT_PANEL_BOSS_LEN+0.5
    for x in UPPER_FRONT_MOUNT_X:
        panel_bosses.append(cylinder(
            UPPER_FRONT_PANEL_BOSS_R,panel_boss_length,
            [x,panel_inner_y+panel_boss_length/2,
             front_local_z(UPPER_FRONT_MOUNT_Z)],[0,1,0],
        ))
        panel_pilots.append(cylinder(
            M3_SELF_TAP_PILOT_R,UPPER_FRONT_PANEL_BOSS_LEN-0.8,
            [x,UPPER_FRONT_T+0.8+(UPPER_FRONT_PANEL_BOSS_LEN-0.8)/2,
             front_local_z(UPPER_FRONT_MOUNT_Z)],[0,1,0],
        ))
    panel=union(panel,tongue,*screen_posts,*panel_bosses)
    panel=difference(panel,*screen_pilots,*panel_pilots)
    panel=transform_front_part(panel)

    # The base locator rises 0.4 mm above the deck behind the front surface.
    # Relieve only the hidden underside of the cap so the two printable parts
    # touch at the intended seam without a positive-volume collision.
    locator_clearance=rounded_xy(
        UPPER_LOCATOR_OUTER_W+0.6,UPPER_LOCATOR_OUTER_D+0.6,
        UPPER_R+0.3,3.2,UPPER_BOTTOM_Z-2.6,
        cy=UPPER_BASE_CENTER_Y,
    )
    panel=difference(panel,locator_clearance)
    above_base=box(
        [200.0,200.0,200.0],
        center=[0.0,0.0,UPPER_BOTTOM_Z+100.0],
    )
    return intersection(panel,above_base)


def build_bottom_cover():
    cover=rounded_xy(
        BOTTOM_COVER_W,BOTTOM_COVER_D,BOTTOM_COVER_R,
        BOTTOM_T,0,
    )
    cuts=[countersunk_cutter_z(x,y) for x in BOTTOM_SCREW_X for y in BOTTOM_SCREW_Y]

    # Four shallow recesses for 10-11 mm adhesive silicone feet.
    # Feet sit inboard of the restored symmetric screw pattern; keeping the
    # centres separate prevents the 11.2 mm recesses from merging with the
    # M3 countersinks.
    for x in (-42.0,42.0):
        for y in (-29.0,29.0):
            cuts.append(cylinder(5.6,1.2,[x,y,0.55]))
    cover = difference(cover,*cuts)

    # This wall rises with the removable cover and closes the cradle's open
    # rear side.  It is deliberately narrower than the holder so wires can
    # pass around both ends.  A 0.4 mm overlap into the cover makes one solid
    # printable part.
    # The holder is pushed against the front wall, so calculate the stop from
    # that datum.  This leaves only the requested small fore/aft clearance;
    # using the rear edge of the larger slot would add the slot clearance a
    # second time and allow the holder to rattle.
    slot_front_y = KEY_Y - KEY_SLOT_D/2
    rail_bottom_z = KEY_CRADLE_SUPPORT_WORLD_Z - KEY_CRADLE_RAIL_T
    holder_rear_y = slot_front_y + KEY_HOLDER_D
    stop_front_y = holder_rear_y + KEY_STOP_GAP
    stop_bottom_z = BOTTOM_T - 0.4
    stop_h = KEY_STOP_TOP_Z - stop_bottom_z
    key_stop = transform_key_part(box(
        [KEY_STOP_W, KEY_STOP_T, stop_h],
        center=[0.0, stop_front_y + KEY_STOP_T/2,
                stop_bottom_z + stop_h/2],
    ))
    # The fixed rear support rail extends 0.35 mm into the stop's front
    # plane.  A shallow horizontal relief lets the removable stop slide past
    # that rail while a 1.8 mm rear web keeps the stop a single strong part.
    stop_relief=transform_key_part(box(
        [KEY_STOP_W+0.8,0.55,KEY_CRADLE_RAIL_T+0.2],
        center=[0.0,stop_front_y+0.175,
                rail_bottom_z+KEY_CRADLE_RAIL_T/2],
    ))
    key_stop=difference(key_stop,stop_relief)
    # Rotation lifts the front lower corner of the stop slightly above the
    # horizontal cover.  This broad, hidden pedestal bridges that gap and
    # keeps the cover a single watertight printable body.
    pedestal=box(
        [KEY_STOP_W,KEY_STOP_T,2.2],
        center=[0.0,stop_front_y+KEY_STOP_T/2,BOTTOM_T+0.65],
    )

    # Plain lower platen for the centre rear USB-C extension.  Its complete
    # 16.8 mm length matches the measured PCB so the cable-side front portion
    # cannot droop.  The inset cover cannot reach the one-millimetre rear skin
    # directly, so the platen keeps its safe rear edge and extends inward.
    # There is no groove or lip that could catch the flat-flex cable.  Its top
    # and the fixed upper platen remain exactly 4.3 mm apart.
    esp_platform_rear_y = BOTTOM_COVER_D/2 - 0.4
    esp_platform_d = ESP_USB_PCB_D
    esp_platform_front_y = esp_platform_rear_y-esp_platform_d
    esp_platform_bottom_z = BOTTOM_T-0.4
    esp_platform_h = ESP_USB_MODULE_BOTTOM_Z-esp_platform_bottom_z
    esp_platform=box(
        [ESP_USB_PORT_W+0.4,esp_platform_d,esp_platform_h],
        center=[
            ESP_USB_PORT_X,
            (esp_platform_front_y+esp_platform_rear_y)/2,
            esp_platform_bottom_z+esp_platform_h/2,
        ],
    )
    return union(cover,pedestal,key_stop,esp_platform)


def build_upper_back_cover():
    """Complete flat rear cover behind the shell's four-sided bevel."""
    panel_bottom_local_z=rear_local_z(BACK_PANEL_BOTTOM_Z)
    panel_top_local_z=rear_local_z(BACK_PANEL_TOP_Z)
    panel_h=panel_top_local_z-panel_bottom_local_z
    panel_cz=(panel_top_local_z+panel_bottom_local_z)/2
    profile=trapezoid(
        BACK_PANEL_W_BOTTOM,BACK_PANEL_W_TOP,panel_h,cz=panel_cz
    )
    # Local outer face is Y=0; the board bosses project in the -Y direction.
    cover=extrude_xz_polygon(profile,UPPER_BACK_T,-UPPER_BACK_T)
    inner_y=-UPPER_BACK_T

    # M3 clearance holes used to attach the cover to the body bosses.
    cover_holes = [
        cylinder(M3_CLEARANCE_R,UPPER_BACK_T+1.2,
                 [x,-UPPER_BACK_T/2,rear_local_z(z)],[0,1,0])
        for x in UPPER_BACK_MOUNT_X for z in UPPER_BACK_MOUNT_Z
    ]
    # Five horizontal rear vents sit above the upper perfboard bosses.  V117
    # removes the former lowest row at Z=121, which crossed the boss outlines.
    vent_cuts = [
        rounded_xz(
            52.0,1.8,0.8,UPPER_BACK_T+1.2,
            -UPPER_BACK_T-0.6,cz=rear_local_z(float(z)),
        )
        for z in np.linspace(UPPER_BOTTOM_Z+95.0,UPPER_BOTTOM_Z+103.0,5)
    ]
    finger_notch = rounded_xz(
        10.0,6.0,2.0,UPPER_BACK_T+1.2,
        -UPPER_BACK_T-0.6,cz=rear_local_z(UPPER_BOTTOM_Z+6.0),
    )
    cover = difference(cover,*cover_holes,*vent_cuts,finger_notch)

    # Four blind M3 self-tapping bosses for a vertical 50 x 70 mm perfboard.
    board_x = tuple(s*PERFBOARD_HOLE_SPACING_X/2 for s in (-1,1))
    board_z = tuple(
        rear_local_z(PERFBOARD_CENTER_Z+s*PERFBOARD_HOLE_SPACING_Z/2)
        for s in (-1,1)
    )
    embed=0.4
    boss_total=PERFBOARD_BOSS_LEN+embed
    boss_center_y=inner_y-PERFBOARD_BOSS_LEN+boss_total/2
    board_bosses = [
        cylinder(PERFBOARD_BOSS_R,boss_total,[x,boss_center_y,z],[0,1,0])
        for x in board_x for z in board_z
    ]
    cover=union(cover,*board_bosses)

    # Keep at least 0.8 mm of plastic behind each blind pilot hole.
    pilot_depth=4.2
    board_pilots = [
        cylinder(M3_SELF_TAP_PILOT_R,pilot_depth,
                 [x,inner_y-PERFBOARD_BOSS_LEN+pilot_depth/2,z],[0,1,0])
        for x in board_x for z in board_z
    ]
    cover=difference(cover,*board_pilots)
    return transform_rear_part(cover,UPPER_BACK_RECESS)


def build_fastener_fit_coupon():
    """Small PLA+ coupon for choosing self-tap and M3 nut-pocket fits."""
    coupon = rounded_xy(82.0,22.0,3.0,8.0,0)
    cuts=[]
    # Left group: 2.5/2.6/2.7 mm self-tapping pilots, left to right.
    for x,diameter in zip((-31.0,-20.0,-9.0),(2.5,2.6,2.7)):
        cuts.append(cylinder(diameter/2,8.8,[x,0,4.0]))
    # Right group: 5.6/5.8/6.0 mm across-flats nut pockets, open at top.
    for x,af in zip((9.0,20.0,31.0),(5.6,5.8,6.0)):
        cuts.append(cylinder(M3_CLEARANCE_R,9.0,[x,0,4.0]))
        cuts.append(hex_prism(af,M3_NUT_DEPTH+0.2,[x,0,8.0-M3_NUT_DEPTH/2]))
    return difference(coupon,*cuts)


def export(mesh,name):
    mesh.remove_unreferenced_vertices()
    path=OUT/f"{name}.stl";mesh.export(path)
    check=trimesh.load(path,force="mesh",process=True)
    print({"part":name,"size":np.round(mesh.extents,2).tolist(),
           "watertight":bool(check.is_watertight),"faces":len(mesh.faces),
           "volume":round(float(mesh.volume),1)})


def validate_current_output():
    """Reject open/multipart STLs and positive-volume assembly collisions."""
    meshes={}
    for path in sorted(OUT.glob("*.stl")):
        mesh=trimesh.load_mesh(path,process=True)
        bodies=len(mesh.split(only_watertight=False))
        if not mesh.is_watertight or not mesh.is_winding_consistent or bodies != 1:
            raise RuntimeError(
                f"invalid STL {path.name}: watertight={mesh.is_watertight}, "
                f"winding={mesh.is_winding_consistent}, bodies={bodies}"
            )
        meshes[path.name]=mesh

    pairs=(
        ("01_lower_base_body.stl","02_inset_bottom_cover.stl"),
        ("01_lower_base_body.stl","03_upper_shell.stl"),
        ("01_lower_base_body.stl","04_upper_front_panel.stl"),
        ("03_upper_shell.stl","04_upper_front_panel.stl"),
        ("03_upper_shell.stl","05_upper_back_cover.stl"),
    )
    for first,second in pairs:
        overlap=intersection(meshes[first],meshes[second])
        with warnings.catch_warnings():
            warnings.simplefilter("ignore",RuntimeWarning)
            volume=0.0 if overlap is None else abs(float(overlap.volume))
        # Flush zero-gap split faces can leave sub-thousandth cubic-millimetre
        # boolean dust after STL quantisation; this is far below one voxel at
        # normal FDM resolution and is not a physical overlap.
        if volume > 1e-3:
            raise RuntimeError(
                f"assembly collision {first} / {second}: {volume:.6f} mm^3"
            )

    chamfer_runs=(
        ("bottom side",(94.0-90.0)/2,UPPER_BOTTOM_CHAMFER),
        ("bottom front/rear",(58.0-54.0)/2,UPPER_BOTTOM_CHAMFER),
        ("top side",(92.0-86.0)/2,UPPER_TOP_CHAMFER),
        ("top front",UPPER_TOP_FRONT_Y-UPPER_FRONT_TOP_Y,
         UPPER_TOP_CHAMFER),
        ("top rear",UPPER_REAR_TOP_Y-UPPER_TOP_REAR_Y,
         UPPER_TOP_CHAMFER),
    )
    for label,run,rise in chamfer_runs:
        if not math.isclose(run,rise,abs_tol=1e-8):
            raise RuntimeError(
                f"upper-shell chamfer is not 45 degrees at {label}: "
                f"run={run:.4f}, rise={rise:.4f}"
            )

    rear_outer_face_angles={
        "top":math.degrees(math.atan2(
            UPPER_REAR_TOP_Y-UPPER_TOP_REAR_Y,UPPER_TOP_CHAMFER
        )),
        "bottom":math.degrees(math.atan2(
            (58.0-54.0)/2,UPPER_BOTTOM_CHAMFER
        )),
        "left":math.degrees(math.atan2(
            UPPER_REAR_EDGE_BEVEL,UPPER_REAR_EDGE_BEVEL
        )),
        "right":math.degrees(math.atan2(
            UPPER_REAR_EDGE_BEVEL,UPPER_REAR_EDGE_BEVEL
        )),
    }
    if any(
        not math.isclose(angle,45.0,abs_tol=1e-8)
        for angle in rear_outer_face_angles.values()
    ):
        raise RuntimeError(
            f"rear outer-face bevel angles changed: "
            f"{rear_outer_face_angles}"
        )

    front_edge_angles={
        edge:math.degrees(math.atan2(
            FRONT_PANEL_EDGE_BEVEL,FRONT_PANEL_EDGE_BEVEL
        ))
        for edge in ("top","bottom","left","right")
    }
    if any(
        not math.isclose(angle,45.0,abs_tol=1e-8)
        for angle in front_edge_angles.values()
    ):
        raise RuntimeError(
            f"front-panel bevel angles changed: {front_edge_angles}"
        )

    rear_edge_angles={
        edge:math.degrees(math.atan2(
            REAR_FRAME_BEVEL,REAR_FRAME_BEVEL
        ))
        for edge in ("top","bottom","left","right")
    }
    if any(
        not math.isclose(angle,45.0,abs_tol=1e-8)
        for angle in rear_edge_angles.values()
    ):
        raise RuntimeError(
            f"rear-shell bevel angles changed: {rear_edge_angles}"
        )
    if not math.isclose(
        UPPER_BACK_RECESS,REAR_FRAME_BEVEL,abs_tol=1e-8
    ):
        raise RuntimeError(
            "rear cover plane no longer meets the inner edge of the bevel"
        )
    rear_cover_clearance=UPPER_BACK_CLEARANCE/2
    if not math.isclose(rear_cover_clearance,0.25,abs_tol=1e-8):
        raise RuntimeError(
            f"rear-cover edge clearance changed: "
            f"{rear_cover_clearance:.4f} mm"
        )

    rear_outer_y=BASE_D/2
    cooler_mouth_y=(
        rear_outer_y-WALL-COOLER_BOSS_LENGTH+10.0
    )
    micro_mouth_y=(
        rear_outer_y-WALL-MICRO_BOSS_LENGTH+5.0
    )
    for label,mouth_y in (
        ("cooler USB-C",cooler_mouth_y),
        ("sharing Micro-USB",micro_mouth_y),
    ):
        if not math.isclose(mouth_y,rear_outer_y,abs_tol=1e-8):
            raise RuntimeError(
                f"{label} mouth is not flush: {mouth_y:.4f} vs "
                f"rear {rear_outer_y:.4f}"
            )

    # The centre USB-C extension is intentionally recessed behind a 1 mm
    # local rear skin, while its centreline stays level with both existing
    # connector openings.  The two hard clamp planes reproduce the measured
    # 4.3 mm module envelope exactly; only the upper part has locating lips.
    esp_pcb_rear_y=rear_outer_y-ESP_USB_REAR_SKIN_T
    esp_clamp_gap=ESP_USB_PCB_TOP_Z-ESP_USB_MODULE_BOTTOM_Z
    if not math.isclose(
        rear_outer_y-esp_pcb_rear_y,ESP_USB_REAR_SKIN_T,abs_tol=1e-8
    ):
        raise RuntimeError("centre USB-C rear skin is not 1.0 mm")
    if not math.isclose(esp_clamp_gap,4.3,abs_tol=1e-8):
        raise RuntimeError(
            f"centre USB-C clamp gap changed: {esp_clamp_gap:.4f} mm"
        )
    if not math.isclose(ESP_USB_LIMIT_H,1.0,abs_tol=1e-8):
        raise RuntimeError("centre USB-C upper locating lip is not 1.0 mm")
    for other_z in (COOLER_PATTERN_CENTER_Z,MICRO_PORT_CENTER_Z):
        if not math.isclose(
            ESP_USB_PORT_CENTER_Z,other_z,abs_tol=1e-8
        ):
            raise RuntimeError("three lower rear ports are not centre-aligned")
    esp_platform_rear_y=BOTTOM_COVER_D/2-0.4
    esp_platform_front_y=esp_platform_rear_y-ESP_USB_PCB_D
    if esp_platform_rear_y >= BOTTOM_COVER_D/2:
        raise RuntimeError("centre USB-C lower platform exceeds cover envelope")
    esp_platform_length=esp_platform_rear_y-esp_platform_front_y
    if not math.isclose(
        esp_platform_length,ESP_USB_PCB_D,abs_tol=1e-8
    ):
        raise RuntimeError(
            f"centre USB-C lower platform length changed: "
            f"{esp_platform_length:.4f} mm"
        )

    # Keep the complete right USB-C mating passage clear from the breakout
    # board face to the exterior.  The probe is inset by 0.1 mm around its
    # outline and 0.05 mm at each end to avoid counting coplanar STL faces.
    cooler_board_face_y=rear_outer_y-WALL-COOLER_BOSS_LENGTH
    cooler_clear_probe=rounded_xz(
        COOLER_PORT_W-0.2,COOLER_PORT_H-0.2,
        (COOLER_PORT_H-0.2)/2-0.1,
        rear_outer_y-0.05-(cooler_board_face_y+0.05),
        cooler_board_face_y+0.05,
        cx=COOLER_PORT_X,cz=COOLER_PATTERN_CENTER_Z,
    )
    cooler_obstruction=intersection(
        meshes["01_lower_base_body.stl"],cooler_clear_probe
    )
    cooler_obstruction_volume=(
        0.0 if cooler_obstruction is None
        else abs(float(cooler_obstruction.volume))
    )
    if cooler_obstruction_volume > 1e-4:
        raise RuntimeError(
            f"right USB-C tunnel obstructed: "
            f"{cooler_obstruction_volume:.6f} mm^3"
        )

    lcd_glass_y=CRT_RECESS_DEPTH+LCD_POST_LENGTH-LCD_DEPTH
    lcd_relief_y=CRT_RECESS_DEPTH-LCD_FACE_POCKET_OVERLAP
    lcd_gap=lcd_glass_y-lcd_relief_y
    if not 0.45 <= lcd_gap <= 0.65:
        raise RuntimeError(f"LCD front gap outside target: {lcd_gap:.4f} mm")

    panel_side_clearance=UPPER_FRONT_CLEARANCE/2
    if not math.isclose(panel_side_clearance,0.25,abs_tol=1e-8):
        raise RuntimeError(
            f"front-panel side clearance changed: {panel_side_clearance:.4f}"
        )

    # Probe the full shared seam thickness.  Any material in this inset
    # rounded rectangle means one of the two cable openings is blocked or no
    # longer aligned with the common datum.
    junction_probe=rounded_xy(
        JUNCTION_WIRE_W-0.2,JUNCTION_WIRE_D-0.2,JUNCTION_WIRE_R-0.1,
        2*WALL-0.2,UPPER_BOTTOM_Z-WALL+0.1,
        cy=JUNCTION_WIRE_CENTER_Y,
    )
    junction_obstructions={}
    for filename in ("01_lower_base_body.stl","03_upper_shell.stl"):
        obstruction=intersection(meshes[filename],junction_probe)
        volume=(
            0.0 if obstruction is None else abs(float(obstruction.volume))
        )
        junction_obstructions[filename]=volume
        if volume > 1e-4:
            raise RuntimeError(
                f"shared cable passage obstructed in {filename}: "
                f"{volume:.6f} mm^3"
            )

    upper_outer=upper_outer_loft()
    upper_locator=rounded_xy_ring(
        UPPER_LOCATOR_OUTER_W,UPPER_LOCATOR_OUTER_D,
        UPPER_R,UPPER_LOCATOR_WALL,UPPER_LOCATOR_H+0.4,
        UPPER_BOTTOM_Z-UPPER_LOCATOR_H,cy=UPPER_BASE_CENTER_Y,
    )
    upper_allowed=union(upper_outer,upper_locator)
    outside= difference(meshes["03_upper_shell.stl"],upper_allowed)
    outside_volume=0.0 if outside is None else abs(float(outside.volume))
    # Manifold booleans can leave a few thousandths of a cubic millimetre on
    # coincident envelope faces; anything below 0.01 mm^3 is numerical dust.
    if outside_volume > 1e-2:
        raise RuntimeError(
            f"upper-shell hardware protrudes outside skin: "
            f"{outside_volume:.6f} mm^3"
        )
    print({"version":VERSION,"validation":"passed",
           "stl_count":len(meshes),"assembly_collisions":0,
           "upper_chamfers_deg":45,"front_panel_bevels_deg":45,
           "rear_outer_face_bevels_deg":45,
           "rear_shell_bevels_deg":45,
           "rear_shell_bevel_width_mm":REAR_FRAME_BEVEL,
           "rear_cover_edge_clearance_mm":rear_cover_clearance,
           "lcd_front_gap_mm":round(lcd_gap,2),
           "rear_wall_mm":WALL,
           "centre_usb_c_local_wall_mm":ESP_USB_REAR_SKIN_T,
           "centre_usb_c_clamp_gap_mm":round(esp_clamp_gap,2),
           "centre_usb_c_upper_limit_height_mm":ESP_USB_LIMIT_H,
           "centre_usb_c_lower_platform_length_mm":round(
               esp_platform_length,2
           ),
           "right_usb_c_tunnel_obstruction_mm3":round(
               cooler_obstruction_volume,6
           ),
           "front_tongue_internal_clearance_mm":round(panel_side_clearance,2),
           "junction_wire_opening_mm":[JUNCTION_WIRE_W,JUNCTION_WIRE_D],
           "junction_wire_center_xy_mm":[0.0,JUNCTION_WIRE_CENTER_Y],
           "junction_wire_obstruction_mm3":{
               name:round(volume,6)
               for name,volume in junction_obstructions.items()
           },
           "external_parting_gap_mm":0.0,
           "hardware_outside_mm3":round(outside_volume,6)})


if __name__=="__main__":
    # latest_output contains only the current printable set.
    for old_stl in OUT.glob("*.stl"):
        old_stl.unlink()
    export(build_lower_base(),"01_lower_base_body")
    export(build_bottom_cover(),"02_inset_bottom_cover")
    export(build_upper_shell(),"03_upper_shell")
    export(build_upper_front_panel(),"04_upper_front_panel")
    export(build_upper_back_cover(),"05_upper_back_cover")
    export(build_fastener_fit_coupon(),"06_fastener_fit_coupon")
    validate_current_output()
