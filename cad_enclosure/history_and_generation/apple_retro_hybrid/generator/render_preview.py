"""Render reproducible z-buffered previews for the current enclosure."""

from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
import trimesh

import generate_hybrid as cad


ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "latest_output" / "apple_retro_hybrid"
PREVIEWS = Path(__file__).resolve().parent / "previews"
PREVIEWS.mkdir(parents=True, exist_ok=True)
BACKGROUND = np.array([244, 240, 232], dtype=np.uint8)

PARTS = [
    ("01_lower_base_body.stl", "lower base", (211, 197, 169)),
    ("02_inset_bottom_cover.stl", "bottom cover", (181, 166, 139)),
    ("03_upper_shell.stl", "upper shell", (226, 211, 180)),
    ("04_upper_front_panel.stl", "front panel", (235, 222, 193)),
    ("05_upper_back_cover.stl", "rear cover", (202, 185, 154)),
]


def load_parts(add_context=True):
    parts=[
        (trimesh.load_mesh(OUT/filename,process=True),label,np.array(color))
        for filename,label,color in PARTS
    ]
    if not add_context:
        return parts

    # Preview-only LCD and keycaps show how the user's existing hardware sits
    # in the enclosure.  They are never exported as printable parts.
    lcd_bezel=cad.transform_front_part(cad.rounded_xz(
        53.2,41.0,3.0,0.8,-0.35,cz=cad.front_local_z(cad.SCREEN_Z)
    ))
    lcd_active=cad.transform_front_part(cad.rounded_xz(
        48.8,36.5,2.2,0.5,-0.9,cz=cad.front_local_z(cad.SCREEN_Z)
    ))
    key_plate=cad.transform_key_part(cad.rounded_xy(
        62.0,23.0,1.5,0.7,24.0,cy=cad.KEY_Y
    ))
    micro_mouth=cad.rounded_xz(
        cad.MICRO_PORT_W,cad.MICRO_PORT_H,0.8,0.7,
        cad.BASE_D/2-0.1,cx=cad.MICRO_PORT_X,cz=cad.MICRO_PORT_CENTER_Z,
    )
    cooler_mouth=cad.rounded_xz(
        cad.COOLER_PORT_W,cad.COOLER_PORT_H,1.6,0.7,
        cad.BASE_D/2-0.1,cx=cad.COOLER_PORT_X,cz=cad.COOLER_PATTERN_CENTER_Z,
    )
    esp_mouth=cad.rounded_xz(
        cad.ESP_USB_PORT_W,cad.ESP_USB_PORT_H,cad.ESP_USB_PORT_R,0.7,
        cad.BASE_D/2-0.1,cx=cad.ESP_USB_PORT_X,
        cz=cad.ESP_USB_PORT_CENTER_Z,
    )
    floppy_shadow=cad.transform_front_part(cad.rounded_xz(
        37.6,3.0,1.1,0.25,-0.32,cx=cad.FLOPPY_CENTER_X,
        cz=cad.front_local_z(cad.APPLE_LOGO_CENTER_Z),
    ))
    logo_shadows=[
        cad.transform_front_part(cad.extrude_xz_polygon(p,0.22,-0.32))
        for p in cad.apple_logo_polygons(
            cad.APPLE_LOGO_H/np.cos(cad.FRONT_TILT_RAD),
            cad.APPLE_LOGO_CENTER_X,
            cad.front_local_z(cad.APPLE_LOGO_CENTER_Z),
        )
    ]
    parts.extend([
        (lcd_bezel,"display bezel",np.array([38,38,37])),
        (lcd_active,"display active area",np.array([126,151,162])),
        (key_plate,"holder top",np.array([42,40,36])),
        (micro_mouth,"micro USB mouth",np.array([38,38,37])),
        (esp_mouth,"ESP32 USB-C mouth",np.array([38,38,37])),
        (cooler_mouth,"cooler USB-C mouth",np.array([38,38,37])),
        (floppy_shadow,"floppy recess shadow",np.array([74,70,62])),
    ])
    parts.extend([
        (mesh,"logo recess shadow",np.array([93,83,67]))
        for mesh in logo_shadows
    ])
    for x in (-20.0,0.0,20.0):
        keycap=cad.rounded_xy_loft((
            (24.2,17.4,17.4,2.0),(34.0,14.8,14.8,2.2)
        ),cx=x,cy=cad.KEY_Y)
        keycap=cad.transform_key_part(keycap)
        parts.append((keycap,"keycap",np.array([225,216,198])))
    return parts


def camera_basis(view):
    view=np.asarray(view,dtype=float); view/=np.linalg.norm(view)
    up_hint=np.array([0.0,0.0,1.0])
    if abs(view@up_hint)>0.97:
        up_hint=np.array([0.0,1.0,0.0])
    right=np.cross(up_hint,view); right/=np.linalg.norm(right)
    up=np.cross(view,right); up/=np.linalg.norm(up)
    return view,right,up


def rasterize(parts,view,size=650,margin=36):
    """Orthographically rasterize triangles with a real per-pixel z buffer."""
    view,right,up=camera_basis(view)
    all_vertices=np.vstack([mesh.vertices for mesh,_,_ in parts])
    projected=np.column_stack([
        np.einsum("ij,j->i",all_vertices,right),
        np.einsum("ij,j->i",all_vertices,up),
    ])
    lo=projected.min(axis=0); hi=projected.max(axis=0)
    center=(lo+hi)/2; span=max(hi-lo)
    scale=(size-2*margin)/span

    color=np.broadcast_to(BACKGROUND,(size,size,3)).copy()
    depth=np.full((size,size),-np.inf,dtype=float)
    light=np.array([-0.45,-0.65,0.75]); light/=np.linalg.norm(light)

    for mesh,_,base in parts:
        vertices=mesh.vertices
        screen_x=(np.einsum("ij,j->i",vertices,right)-center[0])*scale+size/2
        screen_y=size/2-(np.einsum("ij,j->i",vertices,up)-center[1])*scale
        screen_z=np.einsum("ij,j->i",vertices,view)
        tri=vertices[mesh.faces]
        normals=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0])
        lengths=np.linalg.norm(normals,axis=1)
        good=lengths>1e-12
        normals[good]/=lengths[good,None]
        normals[~good]=[0,0,1]
        facing=np.einsum("ij,j->i",normals,view)>1e-7
        shade=0.62+0.38*np.clip(np.einsum("ij,j->i",normals,light),0,1)

        for face_index in np.flatnonzero(facing):
            indices=mesh.faces[face_index]
            x=screen_x[indices]; y=screen_y[indices]; z=screen_z[indices]
            xmin=max(margin,int(np.floor(x.min())))
            xmax=min(size-margin-1,int(np.ceil(x.max())))
            ymin=max(margin,int(np.floor(y.min())))
            ymax=min(size-margin-1,int(np.ceil(y.max())))
            if xmin>xmax or ymin>ymax:
                continue
            denominator=(y[1]-y[2])*(x[0]-x[2])+(x[2]-x[1])*(y[0]-y[2])
            if abs(denominator)<1e-9:
                continue
            yy,xx=np.mgrid[ymin:ymax+1,xmin:xmax+1]
            w0=((y[1]-y[2])*(xx-x[2])+(x[2]-x[1])*(yy-y[2]))/denominator
            w1=((y[2]-y[0])*(xx-x[2])+(x[0]-x[2])*(yy-y[2]))/denominator
            w2=1.0-w0-w1
            inside=(w0>=-1e-7)&(w1>=-1e-7)&(w2>=-1e-7)
            candidate=w0*z[0]+w1*z[1]+w2*z[2]
            current=depth[ymin:ymax+1,xmin:xmax+1]
            update=inside&(candidate>current)
            if not np.any(update):
                continue
            current[update]=candidate[update]
            region=color[ymin:ymax+1,xmin:xmax+1]
            rgb=np.clip(base*shade[face_index],0,255).astype(np.uint8)
            region[update]=rgb
    return Image.fromarray(color)


def font(size,bold=False):
    path=(
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold
        else "/System/Library/Fonts/Supplemental/Arial.ttf"
    )
    return ImageFont.truetype(path,size)


def titled_grid(images,titles,heading,path):
    cell=650; title_h=46; header_h=86
    canvas=Image.new("RGB",(cell*2,header_h+(cell+title_h)*2),tuple(BACKGROUND))
    draw=ImageDraw.Draw(canvas)
    draw.text((cell,24),heading,fill=(44,42,38),font=font(28,True),anchor="ma")
    for index,(image,title) in enumerate(zip(images,titles)):
        col=index%2; row=index//2
        x=col*cell; y=header_h+row*(cell+title_h)
        draw.text((x+cell/2,y+8),title,fill=(62,58,51),font=font(22),anchor="ma")
        canvas.paste(image,(x,y+title_h))
    canvas.save(path,optimize=True)
    return path


def assembled_preview(parts):
    views=[(0,-1,0),(0,1,0),(-1,0,0),(1,-1,0.55)]
    images=[rasterize(parts,view) for view in views]
    return titled_grid(
        images,["Front","Rear","Left","Front three-quarter"],
        f"Apple Retro Hybrid {cad.VERSION} — reconstructed Macintosh enclosure",
        PREVIEWS/f"macintosh_controller_{cad.VERSION}_geometry.png",
    )


def exploded_preview(parts):
    moved=[]
    offsets={
        "lower base":(0,0,0),"bottom cover":(0,0,-14),
        "upper shell":(0,0,0),"front panel":(0,-18,0),
        "rear cover":(0,18,0),
    }
    for mesh,label,color in parts:
        copy=mesh.copy(); copy.apply_translation(offsets[label])
        moved.append((copy,label,color))
    image=rasterize(moved,(1,-1,0.55),size=900,margin=55)
    canvas=Image.new("RGB",(900,980),tuple(BACKGROUND)); canvas.paste(image,(0,80))
    draw=ImageDraw.Draw(canvas)
    draw.text((450,25),f"Apple Retro Hybrid {cad.VERSION} — five structural parts",
              fill=(44,42,38),font=font(28,True),anchor="ma")
    path=PREVIEWS/f"macintosh_controller_{cad.VERSION}_exploded.png"
    canvas.save(path,optimize=True)
    return path


def rear_usb_mount_preview():
    """Show the centre USB-C hard clamp through a preview-only side cutaway."""
    base=trimesh.load_mesh(OUT/"01_lower_base_body.stl",process=True)
    cover=trimesh.load_mesh(OUT/"02_inset_bottom_cover.stl",process=True)
    # Retain the left half of each printable part so a +X view exposes the
    # centre plane without changing either exported STL.
    clip=cad.box([112.0,26.0,34.0],center=[-56.0,39.5,14.0])
    base_cut=cad.intersection(base,clip)
    cover_cut=cad.intersection(cover,clip)
    pcb_rear_y=cad.BASE_D/2-cad.ESP_USB_REAR_SKIN_T
    pcb_front_y=pcb_rear_y-cad.ESP_USB_PCB_D
    pcb=cad.box(
        [cad.ESP_USB_PCB_W,cad.ESP_USB_PCB_D,cad.ESP_USB_PCB_T],
        center=[0.0,(pcb_front_y+pcb_rear_y)/2,
                cad.ESP_USB_PCB_TOP_Z-cad.ESP_USB_PCB_T/2],
    )
    receptacle=cad.box(
        [cad.ESP_USB_PORT_W,cad.ESP_USB_SHELL_DEPTH,
         cad.ESP_USB_CONNECTOR_H],
        center=[0.0,pcb_rear_y-cad.ESP_USB_SHELL_DEPTH/2,
                cad.ESP_USB_MODULE_BOTTOM_Z+cad.ESP_USB_CONNECTOR_H/2],
    )
    colors={
        "base":np.array([211,197,169]),
        "cover":np.array([181,166,139]),
        "pcb":np.array([41,77,58]),
        "receptacle":np.array([118,122,121]),
    }
    assembled=[
        (base_cut,"base cutaway",colors["base"]),
        (cover_cut,"cover",colors["cover"]),
        (pcb,"measured PCB",colors["pcb"]),
        (receptacle,"Type-C receptacle",colors["receptacle"]),
    ]
    opened=[]
    for mesh,label,color in assembled:
        copy=mesh.copy()
        if label == "cover":
            copy.apply_translation([0,0,-10])
        opened.append((copy,label,color))
    images=[
        rasterize(assembled,(1,0,0.15),size=650,margin=55),
        rasterize(opened,(1,0,0.15),size=650,margin=55),
    ]
    canvas=Image.new("RGB",(1300,760),tuple(BACKGROUND))
    draw=ImageDraw.Draw(canvas)
    draw.text((650,24),f"{cad.VERSION} centre rear USB-C hard clamp",
              fill=(44,42,38),font=font(28,True),anchor="ma")
    for index,(image,title) in enumerate(zip(
        images,("Cover closed — 4.3 mm hard gap","Cover lowered — full 16.8 mm platform")
    )):
        x=index*650
        draw.text((x+325,72),title,fill=(62,58,51),
                  font=font(22),anchor="ma")
        canvas.paste(image,(x,110))
    path=PREVIEWS/f"rear_usb_c_mount_{cad.VERSION}.png"
    canvas.save(path,optimize=True)
    return path


def rear_bevel_preview():
    """Show the rear shell bevel with the complete cover fitted and removed."""
    shell=trimesh.load_mesh(OUT/"03_upper_shell.stl",process=True)
    cover=trimesh.load_mesh(OUT/"05_upper_back_cover.stl",process=True)
    shell_color=np.array([226,211,180])
    cover_color=np.array([158,145,122])
    installed=[
        (shell,"upper shell",shell_color),
        (cover,"complete rear cover",cover_color),
    ]
    removed=[(shell,"upper shell",shell_color)]
    images=[
        rasterize(installed,(0,1,0.12),size=650,margin=55),
        rasterize(removed,(0.45,1,0.12),size=650,margin=55),
    ]
    canvas=Image.new("RGB",(1300,760),tuple(BACKGROUND))
    draw=ImageDraw.Draw(canvas)
    draw.text((650,24),f"{cad.VERSION} rear shell bevel — 45° on all four sides",
              fill=(44,42,38),font=font(28,True),anchor="ma")
    for index,(image,title) in enumerate(zip(
        images,("Complete cover installed","Cover removed — planar rear side bevel")
    )):
        x=index*650
        draw.text((x+325,72),title,fill=(62,58,51),
                  font=font(22),anchor="ma")
        canvas.paste(image,(x,110))
    path=PREVIEWS/f"rear_shell_bevel_{cad.VERSION}.png"
    canvas.save(path,optimize=True)
    return path


if __name__ == "__main__":
    print(assembled_preview(load_parts(add_context=True)))
    print(exploded_preview(load_parts(add_context=False)))
    print(rear_usb_mount_preview())
    print(rear_bevel_preview())
