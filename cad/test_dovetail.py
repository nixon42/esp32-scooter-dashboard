import manifold3d as m3d
import trimesh
import numpy as np

def make_male_v_rail(length, base_w, crown_w, height):
    """
    Creates a male dovetail (V-rail) along Y axis.
    Cross-section in X-Z plane:
    At Z=0 (root attached to housing): width = base_w
    At Z=-height (crown protruding backwards): width = crown_w (where crown_w > base_w)
    Extruded along Y from Y=0 to Y=length.
    """
    pts = [
        [-base_w / 2, 0.0],
        [ base_w / 2, 0.0],
        [ crown_w / 2, -height],
        [-crown_w / 2, -height]
    ]
    cs = m3d.CrossSection([pts])
    rail = cs.extrude(length).rotate([90, 0, 0]).translate([0, length, 0])
    return rail

def make_female_v_slot(length, base_w, crown_w, height, clearance=0.25):
    """
    Creates a female dovetail channel cutter with clearance.
    """
    b_w = base_w + 2 * clearance
    c_w = crown_w + 2 * clearance
    h = height + clearance
    pts = [
        [-b_w / 2, clearance],
        [ b_w / 2, clearance],
        [ c_w / 2, -h],
        [-c_w / 2, -h]
    ]
    cs = m3d.CrossSection([pts])
    slot = cs.extrude(length + 2).rotate([90, 0, 0]).translate([0, length + 1, 0])
    return slot

rail = make_male_v_rail(20, 11, 14, 3.5)
slot = make_female_v_slot(20, 11, 14, 3.5, 0.25)
print("V-rail volume:", rail.volume())
print("V-slot volume:", slot.volume())
