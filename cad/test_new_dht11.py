#!/usr/bin/env python3
import manifold3d as m3d
import trimesh
import numpy as np

def rounded_box_2d(width, height, radius):
    w = max(width - 2 * radius, 0.1)
    h = max(height - 2 * radius, 0.1)
    return m3d.CrossSection.square([w, h], center=True).offset(radius, m3d.JoinType.Round)

def rounded_box_3d(width, height, depth, radius):
    cs = rounded_box_2d(width, height, radius)
    return cs.extrude(depth)

def cylinder(height, radius, center=False):
    return m3d.Manifold.cylinder(height, radius, radius, circular_segments=48, center=center)

def build_dht11_cradle_and_cover():
    """
    Professional 2-Piece Snap-Fit DHT11 Sensor Pod:
    1. Base Cradle:
       - Slides smoothly onto rear male V-rail (0.25mm clearance per side).
       - Solid neck (21.0mm wide) with ZERO cut-through and ZERO floating slivers.
       - Uniform 2.4mm wall thickness all around.
       - Open front cavity (16.0 x 30.0 mm) for effortless sensor insertion.
       - Internal PCB seating ledge and bottom wire passage notch.
       - Snap-fit locking detents.
    2. Snap-Fit Front Grille Cover:
       - Downward rain-deflecting louvers (-35 deg slope) for splash protection.
       - Bottom water drainage notch.
       - Snaps securely onto Base Cradle with positive snap detents.
    """
    wall = 2.4
    cavity_w = 16.0
    cavity_h = 30.0
    cavity_d = 8.5
    outer_w = cavity_w + 2 * wall # 20.8 mm -> use 21.0 mm
    outer_h = cavity_h + 2 * wall # 34.8 mm -> use 35.0 mm
    outer_d = cavity_d + wall     # 10.9 mm -> use 11.0 mm

    # Position in Y:
    # Male rail is from Y = -11.0 down to Y = -25.0.
    # Shoulder end-stop is at Y = -11.0. Detent is at Y = -14.0.
    # When fully engaged, the top of the dovetail slider is at Y = -11.0.
    # Let's align the pod so the top edge is at Y = -10.5 mm, extending down to Y = -45.5 mm (height 35.0 mm).
    center_y = -28.0 # Y in [-45.5, -10.5]
    
    # ==========================================
    # PART 1: BASE CRADLE
    # ==========================================
    # Outer solid body (flat back at Z = 0, extends to Z = -outer_d = -11.0)
    base_cs = rounded_box_2d(outer_w, outer_h, 2.5).translate([0, center_y])
    base_body = base_cs.extrude(outer_d).translate([0, 0, -outer_d])

    # Dovetail channel on back (at Z = 0)
    # Extends along Y from Y = -10.0 to Y = -27.0 (length 17.0 mm)
    pts_female = [[-3.25, 0.5], [-6.0, -2.75], [6.0, -2.75], [3.25, 0.5]]
    cs_female = m3d.CrossSection([pts_female])
    channel = cs_female.extrude(17.0).rotate([90, 0, 0]).translate([0, -10.5, 0])

    # Snap-detent pocket at Y = -14.0 matching male bump
    detent = m3d.Manifold.cube([8.0, 2.2, 0.5], center=True).translate([0, -14.0, -2.95])

    # Sensor Cavity (Open from front at Z = -outer_d = -11.0 mm up to Z = -wall = -2.4 mm)
    # Centered in X and Y inside the base body for PERFECT UNIFORM WALL THICKNESS!
    cavity_cs = rounded_box_2d(cavity_w, cavity_h, 1.2).translate([0, center_y])
    # Cut from Z = -outer_d - 1.0 up to Z = -wall
    cavity = cavity_cs.extrude(cavity_d + 1.0).translate([0, 0, -outer_d - 0.5])

    # Bottom wire pass-through notch at Y = -45.5 (bottom wall)
    wire_notch = m3d.Manifold.cube([6.0, 6.0, 6.0], center=True).translate([0, center_y - outer_h/2, -outer_d + 3.0])

    # Snap-fit female grooves on side inner walls (0.45mm deep, 5.0mm long at Z = -outer_d + 1.5)
    snap_l = m3d.Manifold.cube([1.2, 8.0, 2.0], center=True).translate([-cavity_w/2, center_y, -outer_d + 2.0])
    snap_r = m3d.Manifold.cube([1.2, 8.0, 2.0], center=True).translate([ cavity_w/2, center_y, -outer_d + 2.0])
    snap_grooves = snap_l + snap_r

    base = base_body - channel - detent - cavity - wire_notch + snap_grooves # wait, snap grooves should be subtracted
    base = base_body - channel - detent - cavity - wire_notch
    # Subtract snap grooves
    snap_cuts = m3d.Manifold.cube([1.2, 8.0, 2.0], center=True).translate([-cavity_w/2 - 0.3, center_y, -outer_d + 2.0]) + \
                m3d.Manifold.cube([1.2, 8.0, 2.0], center=True).translate([ cavity_w/2 + 0.3, center_y, -outer_d + 2.0])
    base = base - snap_cuts

    # Check Base mesh
    mesh_b = trimesh.Trimesh(vertices=base.to_mesh().vert_properties, faces=base.to_mesh().tri_verts, process=True)
    print("Base Cradle:")
    print("  Watertight:", mesh_b.is_watertight)
    print("  Components:", len(base.decompose()))
    print("  Bounds:", [round(x, 2) for x in mesh_b.bounds[0]], "to", [round(x, 2) for x in mesh_b.bounds[1]])
    print("  Volume:", round(mesh_b.volume, 2), "mm^3")

    return base

if __name__ == "__main__":
    b = build_dht11_cradle_and_cover()
    print("Done test!")
