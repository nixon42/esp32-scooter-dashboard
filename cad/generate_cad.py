#!/usr/bin/env python3
"""
Scooter Dashboard Enclosure & Mirror Mount CAD Generator v4.2 (Mechanical Refinement Edition)
Target: Bambu Lab A1 Mini (PETG)

Features & Upgrades:
- R1: Clean single-radius housing outer corners (0.0000 mm³ standoff protrusion outside body)
- R1: M4 corner nut pockets cleanly breaking through rear face Z=0 with zero skin
- R2: Pristine bottom wall (only cylindrical cable gland collar, clip dock removed)
- R2: Rear male interlocking V-rail (45° undercut wings, root 6.0mm, crown 11.0mm, depth 2.5mm,
      end-stop shoulder at Y=-11.0mm, 0.38mm snap-detent bump at Y=-14.0mm)
- R2: Redesigned female sliding pod (dht11_mount.stl) with 0.25mm sliding clearance,
      DHT11 bay (15.5x29.0x11.5mm), -35° rain louvers, drainage hole, and wire exit
- R3: 36 radial rosette indexing teeth @ 10° pitch (R in [3.5, 9.5]mm, h=0.65mm) on housing
      left clevis ear and bracket tongue left flank; 0.60mm smooth clearance on right flank
- R4: 100% watertight single manifold solids for all 4 STLs
"""

import os
import manifold3d as m3d
import numpy as np
import trimesh

# ==========================================
# GEOMETRY HELPERS
# ==========================================

def rounded_box_2d(width, height, radius):
    """Creates a 2D rounded rectangle cross section centered at origin."""
    w = max(width - 2 * radius, 0.1)
    h = max(height - 2 * radius, 0.1)
    return m3d.CrossSection.square([w, h], center=True).offset(radius, m3d.JoinType.Round)

def rounded_box_3d(width, height, depth, radius):
    """Extrudes a 2D rounded rectangle into a 3D solid from z=0 to z=depth."""
    cs = rounded_box_2d(width, height, radius)
    return cs.extrude(depth)

def cylinder(height, radius, center=False):
    """Creates a cylinder along Z."""
    return m3d.Manifold.cylinder(height, radius, radius, circular_segments=48, center=center)

def teardrop_hole_x(length, radius, apex_direction='-y', num_segments=36):
    """
    Creates a 3D teardrop hole extruded along X with a 45-degree self-supporting apex.
    - apex_direction == '-y': apex points in -Y (optimal for main_housing printed standing upright on Y=+29.0)
    - apex_direction == '-z': apex points in -Z
    - apex_direction == '+z': apex points in +Z (optimal for mirror_bracket printed base-down on print bed)
    """
    pts = []
    angles = np.linspace(3*np.pi/4, 2*np.pi + np.pi/4, num_segments)
    for a in angles:
        pts.append([radius * np.cos(a), radius * np.sin(a)])
    pts.append([0.0, radius * np.sqrt(2)])
    cs = m3d.CrossSection([pts])
    cyl = cs.extrude(length).translate([0, 0, -length/2])
    if apex_direction == '-y':
        return cyl.rotate([180, 0, 0]).rotate([0, 90, 0])
    elif apex_direction == '-z':
        return cyl.rotate([-90, 0, 0]).rotate([0, 0, -90])
    elif apex_direction == '+z':
        return cyl.rotate([90, 0, 0]).rotate([0, 0, 90])
    else:
        raise ValueError(f"Unknown apex_direction: {apex_direction}")

def teardrop_hole_z(length, radius, apex_direction='-y', num_segments=36):
    """
    Creates a 3D teardrop hole extruded along Z with a 45-degree self-supporting apex.
    - apex_direction == '-y': apex points in -Y (optimal for main_housing corner bolts printed standing upright on Y=+29.0)
    - apex_direction == '+y': apex points in +Y
    """
    pts = []
    angles = np.linspace(3*np.pi/4, 2*np.pi + np.pi/4, num_segments)
    for a in angles:
        pts.append([radius * np.cos(a), radius * np.sin(a)])
    pts.append([0.0, radius * np.sqrt(2)])
    cs = m3d.CrossSection([pts])
    cyl = cs.extrude(length).translate([0, 0, -length/2])
    if apex_direction == '-y':
        return cyl.rotate([0, 0, 180])
    elif apex_direction == '+y':
        return cyl
    else:
        raise ValueError(f"Unknown apex_direction: {apex_direction}")

def make_teeth(num=36, r_in=3.5, r_out=9.5, h=0.65, x_base=-6.1, embed=0.2, direction=1.0, phase=0.0):
    """
    Creates radial rosette indexing teeth around the X axis in the Y-Z plane.
    Each tooth is a triangular wedge radiating from r_in to r_out.
    """
    wedge = 360.0 / num
    teeth = []
    x_apex = x_base + direction * h
    x_root = x_base - direction * embed
    for i in range(num):
        a = np.radians(i * wedge + phase)
        al = np.radians(i * wedge + phase - wedge / 2.0)
        ar = np.radians(i * wedge + phase + wedge / 2.0)
        pts = np.array([
            [x_apex, r_in * np.cos(a),  r_in * np.sin(a)],
            [x_apex, r_out * np.cos(a), r_out * np.sin(a)],
            [x_root, r_in * np.cos(al),  r_in * np.sin(al)],
            [x_root, r_out * np.cos(al), r_out * np.sin(al)],
            [x_root, r_in * np.cos(ar),  r_in * np.sin(ar)],
            [x_root, r_out * np.cos(ar), r_out * np.sin(ar)],
        ], dtype=np.float32)
        teeth.append(m3d.Manifold.hull_points(pts))
    return m3d.Manifold.batch_boolean(teeth, m3d.OpType.Add)

def save_mesh(solid, filepath):
    """Converts a Manifold solid into a watertight STL file."""
    assert solid.status() == m3d.Error.NoError, f"Manifold error: {solid.status()}"
    mesh_data = solid.to_mesh()
    mesh = trimesh.Trimesh(
        vertices=mesh_data.vert_properties,
        faces=mesh_data.tri_verts,
        process=True
    )
    decomp = solid.decompose()
    assert mesh.is_watertight, f"Mesh {filepath} is not watertight!"
    assert len(decomp) == 1, f"Mesh {filepath} has {len(decomp)} solid components!"
    mesh.export(filepath)
    print(f"[{os.path.basename(filepath)}] Exported successfully:")
    print(f"  - Watertight: {mesh.is_watertight}")
    print(f"  - Solid Components: {len(decomp)} (should be 1)")
    print(f"  - Bounding Box: {[round(float(x), 1) for x in mesh.bounds[0]]} to {[round(float(x), 1) for x in mesh.bounds[1]]}")
    print(f"  - Volume: {round(float(mesh.volume) / 1000, 2)} cm^3")

# ==========================================
# PART 1: FRONT BEZEL (M4 CORNER COUNTERBORES)
# ==========================================
def build_front_bezel():
    width = 58.0
    height = 58.0
    thickness = 6.5
    radius = 5.0

    # 1. Main sleek flat bezel plate
    bezel = rounded_box_3d(width, height, thickness, radius)

    # 2. Single Clean Screen Window (38 x 32 mm)
    win_main = m3d.Manifold.cube([38.0, 32.0, thickness + 10], center=True).translate([0, 1.0, thickness/2])

    # 3. Inside Acrylic Sheet Pocket (45.5 x 45.5 x 1.5 mm deep)
    acrylic_pocket = rounded_box_3d(45.5, 45.5, 1.5, 2.0).translate([0, 1.0, 0.75])

    # 4. Interlocking Waterproof Tongue Lip (protrudes 2.0mm backward into Housing groove)
    lip_outer = rounded_box_3d(53.2, 53.2, 2.0, 3.5).translate([0, 0, -2.0])
    lip_inner = rounded_box_3d(49.6, 49.6, 4.0, 2.5).translate([0, 0, -3.0])
    lip = lip_outer - lip_inner
    bezel = bezel + lip

    # 5. 4x Corner Screws for M4 (Through-hole dia 4.4mm, Counterbore dia 8.0mm x 3.8mm deep)
    screw_dist = 23.0
    screws = []
    for sx in [-screw_dist, screw_dist]:
        for sy in [-screw_dist, screw_dist]:
            hole = cylinder(thickness + 10, 2.2, center=True).translate([sx, sy, thickness/2])
            cb = cylinder(4.0, 4.0, center=False).translate([sx, sy, thickness - 3.8])
            screws.append(hole + cb)

    all_screws = m3d.Manifold.compose(screws)
    bezel = bezel - win_main - acrylic_pocket - all_screws

    return bezel

# ==========================================
# PART 2: MAIN HOUSING (M4 PIVOT, V-RAIL & M4 CORNER NUTS)
# ==========================================
def build_main_housing():
    width = 58.0
    height = 58.0
    depth = 26.0
    radius = 5.0
    wall = 3.2
    floor = 3.5

    # 1. Outer Solid Body
    body = rounded_box_3d(width, height, depth, radius)

    # 2. Main Internal Cavity (Floor at Z = floor = 3.5)
    inner_w = width - 2 * wall
    inner_h = height - 2 * wall
    cavity = rounded_box_3d(inner_w, inner_h, depth - floor + 1.0, radius - 1.5).translate([0, 0, floor])

    # 3. Mating Labyrinth Groove (top rim)
    groove_outer = rounded_box_3d(53.6, 53.6, 2.5, 3.5).translate([0, 0, depth - 2.4])
    groove_inner = rounded_box_3d(49.2, 49.2, 4.0, 2.2).translate([0, 0, depth - 3.0])
    groove = groove_outer - groove_inner

    # 4. Corner Screw Standoff Pillars CLIPPED strictly to outer body (R1 requirement: 0.0000 mm³ protrusion)
    screw_dist = 23.0
    standoffs = []
    for sx in [-screw_dist, screw_dist]:
        for sy in [-screw_dist, screw_dist]:
            post = rounded_box_3d(11.0, 11.0, depth - floor, 2.0).translate([sx, sy, floor])
            standoffs.append(post)
    standoffs_solid = m3d.Manifold.compose(standoffs) ^ body

    # 5. INTERNAL COMPONENT TRAYS (TATAKAN)
    # --- Nokia 5110 PCB Continuous 3-Sided Shelf (Left, Right, Top) at Z = 16.5 ---
    shelf_l = m3d.Manifold.cube([3.5, 42.0, 6.0], center=True).translate([-22.25, 0, 16.5])
    shelf_r = m3d.Manifold.cube([3.5, 42.0, 6.0], center=True).translate([ 22.25, 0, 16.5])
    shelf_t = m3d.Manifold.cube([42.0, 3.5, 6.0], center=True).translate([0,  22.25, 16.5])

    # --- Center Isolation Barrier (separates 12V buck from 3.3V ESP32) ---
    barrier = m3d.Manifold.cube([2.5, 36.0, 9.0], center=True).translate([0, 0.0, floor + 4.5])

    # --- ESP32-C3 SuperMini Tray (Left Bay: X = -12.5) ---
    esp_wall_l = m3d.Manifold.cube([2.0, 24.0, 3.5], center=True).translate([-23.5, 0.0, floor + 1.75])
    esp_wall_f = m3d.Manifold.cube([22.0, 2.0, 3.5], center=True).translate([-12.5, 12.0, floor + 1.75])
    esp_wall_b = m3d.Manifold.cube([22.0, 2.0, 3.5], center=True).translate([-12.5, -12.0, floor + 1.75])

    # --- MP1584EN Step-Down Tray (Right Bay: X = +12.5) ---
    pwr_wall_r = m3d.Manifold.cube([2.0, 24.0, 3.5], center=True).translate([ 23.5, 0.0, floor + 1.75])
    pwr_wall_f = m3d.Manifold.cube([22.0, 2.0, 3.5], center=True).translate([ 12.5, 12.0, floor + 1.75])
    pwr_wall_b = m3d.Manifold.cube([22.0, 2.0, 3.5], center=True).translate([ 12.5, -12.0, floor + 1.75])

    # --- Cable Zip-Tie Anchor Bridge inside bottom wall ---
    anchor_post1 = m3d.Manifold.cube([3.0, 4.0, 6.0], center=True).translate([-5.0, -21.0, floor + 3.0])
    anchor_post2 = m3d.Manifold.cube([3.0, 4.0, 6.0], center=True).translate([ 5.0, -21.0, floor + 3.0])
    anchor_bridge = m3d.Manifold.cube([13.0, 4.0, 2.0], center=True).translate([0, -21.0, floor + 5.0])
    strain_relief = anchor_post1 + anchor_post2 + anchor_bridge

    # 6. Bottom Cable Gland Collar (R2: bottom wall has only this collar; clip dock removed)
    gland_boss = cylinder(6.0, 6.0, center=False).rotate([90, 0, 0]).translate([0, -29.0, 8.5])
    cable_hole = cylinder(24.0, 3.25, center=True).rotate([90, 0, 0]).translate([0, -29.0, 8.5])

    # 7. REAR INTERLOCKING MALE V-RAIL (DOVETAIL) SENSOR MOUNT (R2 requirement)
    # Positioned in collision-free lower rear zone Y in [-25.0, -11.0], X in [-5.5, +5.5]
    # 45-deg undercut wings: root width 6.0mm at Z=0, crown width 11.0mm at Z=-2.5mm
    pts_male = [[-3.0, 0.0], [-5.5, -2.5], [5.5, -2.5], [3.0, 0.0]]
    cs_male = m3d.CrossSection([pts_male])
    rail = cs_male.extrude(14.0).rotate([90, 0, 0]).translate([0, -11.0, 0])
    # Positive mechanical end-stop shoulder at Y = -11.0mm
    shoulder = m3d.Manifold.cube([12.0, 1.5, 2.5], center=False).translate([-6.0, -11.0, -2.5])
    # 0.38mm tactile snap-detent bump at Y = -14.0mm
    bump_cyl = cylinder(7.0, 1.2, center=False).rotate([0, 90, 0]).translate([-3.5, -14.0, -2.5 + (1.2 - 0.38)])
    bump_box = m3d.Manifold.cube([7.0, 3.0, 0.5], center=False).translate([-3.5, -15.5, -2.95])
    male_dovetail = rail + shoulder + (bump_cyl ^ bump_box)

    # 8. SCULPTED AUTOMOTIVE REAR HINGE CLEVIS WITH RADIAL ROSETTE INDEXING TEETH (R3 requirement)
    pivot_z = -11.0
    clevis_ears = []
    for sx in [-9.5, 9.5]:
        head = cylinder(6.8, 11.0, center=True).rotate([0, 90, 0]).translate([sx, 0, pivot_z])
        root = m3d.Manifold.cube([6.8, 32.0, 4.0], center=True).translate([sx, 0, -2.0])
        clevis_ears.append((head + root).hull())
    
    # 36 radial rosette indexing teeth @ 10-deg pitch on inner face of left clevis ear (X = -6.10, protruding +X)
    housing_teeth = make_teeth(36, 3.5, 9.5, 0.65, -6.10, 0.2, +1.0, 0.0).translate([0, 0, pivot_z])
    clevis_solid = clevis_ears[0] + clevis_ears[1] + housing_teeth

    # M4 Pivot Hole (Teardrop 45-deg self-supporting hole for support-free FDM printing, apex pointing towards -Y)
    m4_pivot = teardrop_hole_x(40.0, 2.2, apex_direction='-y').translate([0, 0, pivot_z])
    clevis = clevis_solid - m4_pivot

    # 9. Corner Screws & M4 Hex Nut Pockets on back (7.2mm across flats, starting at Z = -1.0mm for flush breakthrough)
    # 4x corner through-holes use 45-deg teardrop apex pointing towards -Y for 100% self-supporting FDM printing
    m4_nut_r = 7.2 / (3**0.5) # approx 4.16mm
    corner_cuts = []
    for sx in [-screw_dist, screw_dist]:
        for sy in [-screw_dist, screw_dist]:
            hole = teardrop_hole_z(depth + 10, 2.2, apex_direction='-y').translate([sx, sy, depth/2])
            nut = m3d.Manifold.cylinder(5.2, m4_nut_r, m4_nut_r, circular_segments=6, center=False).translate([sx, sy, -1.0])
            corner_cuts.append(hole + nut)
    corner_cuts_solid = m3d.Manifold.compose(corner_cuts)

    # Assemble everything into 1 single solid
    body = (body - cavity) + standoffs_solid + shelf_l + shelf_r + shelf_t + barrier
    body = body + esp_wall_l + esp_wall_f + esp_wall_b + pwr_wall_r + pwr_wall_f + pwr_wall_b
    body = body + strain_relief + gland_boss + male_dovetail + clevis
    body = body - groove - cable_hole - corner_cuts_solid

    return body

# ==========================================
# PART 3: 1-PIECE MIRROR BRACKET (RADIAL SERRATIONS & NUT TRAP)
# ==========================================
def build_mirror_bracket():
    """
    Constructs a truly continuous, single-piece bracket:
    Thick base (under M10 mirror stalk) -> Heavy-Duty 45-deg Beam -> Pivot Tongue (M4)
    Features 36 radial rosette indexing teeth on left flank mating with housing left clevis ear,
    and 0.60mm smooth clearance gap on right flank for M4 clamping take-up.
    100% unobstructed vertical clearance over the M10 bolt hole!
    """
    base_w = 32.0
    base_l = 48.0
    base_t = 8.5
    
    # 1. Base plate at Z: 0 to 8.5, Y: -12 to +36
    cs = m3d.CrossSection.square([base_w - 12, base_l - 12], center=True).offset(6.0, m3d.JoinType.Round)
    base = cs.extrude(base_t).translate([0, 12.0, 0])
    
    # 2. Pivot Head (Tongue) at (0, -22, 42)
    # Spans from X = -6.10 (at teeth crests) to X = +5.50 (right flank), leaving 0.60mm gap to right ear at X = +6.10
    pivot_center = [0.0, -22.0, 42.0]
    tongue_cyl_full = cylinder(11.60, 11.0, center=False).rotate([0, 90, 0]).translate([-6.10, pivot_center[1], pivot_center[2]])
    tongue_stem = m3d.Manifold.cube([10.95, 16.0, 16.0], center=True).translate([0.025, -18.0, 34.0])
    tongue_part = (tongue_cyl_full + tongue_stem).hull()
    
    # 3. Heavy-Duty Arm: Anchored from Y = -12 to Y = +6 (Completely away from M10 hole at Y=24!)
    arm_root = m3d.Manifold.cube([18.0, 18.0, 12.0], center=True).translate([0, -3.0, 6.0])
    arm_mid = m3d.Manifold.cube([10.95, 16.0, 14.0], center=True).translate([0.025, -11.0, 22.0])
    arm_lower = (arm_root + arm_mid).hull()
    arm_neck = (arm_mid + tongue_stem).hull()
    arm_solid = arm_lower + arm_neck
    
    # 4. Underside / Rear Reinforcement Gusset (towards negative Y, opposite mirror hole)
    rear_gusset = (m3d.Manifold.cube([10.95, 12.0, 4.0], center=True).translate([0.025, -8.0, 6.0]) +
                   m3d.Manifold.cube([10.95, 8.0, 16.0], center=True).translate([0.025, -16.0, 24.0])).hull()
    
    # Lateral Side Buttress Wings (left & right)
    side_l = (m3d.Manifold.cube([6.0, 14.0, 4.0], center=True).translate([-10.0, -3.0, 6.0]) +
              m3d.Manifold.cube([4.0, 10.0, 12.0], center=True).translate([-6.0, -8.0, 14.0])).hull()
    side_r = (m3d.Manifold.cube([6.0, 14.0, 4.0], center=True).translate([ 10.0, -3.0, 6.0]) +
              m3d.Manifold.cube([4.0, 10.0, 12.0], center=True).translate([ 6.0, -8.0, 14.0])).hull()
    
    bracket_solid = base + arm_solid + tongue_part + rear_gusset + side_l + side_r
    
    # 5. Radial Indexing Teeth Mating Cutter & Recesses
    # Housing teeth cutter at pivot center: cuts exact mating negative serrations into tongue left flank
    teeth_cutter = make_teeth(36, 3.5, 9.5, 0.65, -6.10, 0.2, +1.0, 0.0).translate([0, pivot_center[1], pivot_center[2]])
    recess_in = cylinder(0.70, 3.3, center=False).rotate([0, 90, 0]).translate([-6.15, pivot_center[1], pivot_center[2]])
    recess_out = (cylinder(0.70, 12.0, center=False).rotate([0, 90, 0]).translate([-6.15, pivot_center[1], pivot_center[2]]) -
                  cylinder(0.80, 9.7, center=False).rotate([0, 90, 0]).translate([-6.20, pivot_center[1], pivot_center[2]]))

    # 6. Clearance Holes and Recesses
    # M10 Mirror Bolt Hole at Y = 24.0
    m10_hole = cylinder(30.0, 5.25, center=True).translate([0, 24.0, base_t/2])
    # Washer pocket AND full 50mm vertical clearance cylinder above it!
    washer_clearance = cylinder(50.0, 11.2, center=False).translate([0, 24.0, base_t - 2.5])
    
    # M4 Pivot Cross-Bolt Hole (Teardrop 45-deg self-supporting hole, apex towards +Z)
    m4_hole = teardrop_hole_x(35.0, 2.2, apex_direction='+z').translate(pivot_center)
    
    # M4 Hex Nut Pocket (Right side: 7.2mm across flats, 4.0mm deep)
    m4_nut_r = 7.2 / (3**0.5)
    m4_nut = m3d.Manifold.cylinder(6.0, m4_nut_r, m4_nut_r, circular_segments=6, center=False).rotate([0, 90, 0]).translate([1.5, pivot_center[1], pivot_center[2]])
    
    bracket = bracket_solid - m10_hole - washer_clearance - m4_hole - m4_nut - teeth_cutter - recess_in - recess_out
    return bracket

# ==========================================
# PART 4: DHT11 EXTERNAL POD (FEMALE SLIDING V-RAIL POD)
# ==========================================
def build_dht11_base():
    """
    Part 4A: Base Cradle for DHT11 sensor:
    - 0.25 mm bilateral sliding clearance for snug FDM PETG fit onto rear male V-rail.
    - Captive female dovetail channel with snap-detent locking groove (at Y = -14.0mm).
    - Open front cavity (16.0 x 27.2 x 9.6 mm) for easy sensor drop-in.
    - Bottom wire pass-through notch (5.0 x 5.0 mm) aligned toward housing cable gland.
    - Uniform 2.4 mm wall thickness all around.
    - 100% solid neck with ZERO cut-through and ZERO floating slivers.
    """
    wall = 2.4
    outer_w = 20.8
    outer_h = 32.0
    outer_d = 12.0
    center_y = -32.0

    # 1. Main outer body from Y = -48.0 to Y = -16.0
    body_cs = rounded_box_2d(outer_w, outer_h, 2.5).translate([0, center_y])
    body = body_cs.extrude(outer_d).translate([0, 0, -outer_d])

    # 2. Upper neck between clevis ears: Y in [-16.0, -11.0], X in [-6.0, 6.0], Z in [-5.0, 0.0]
    tongue = m3d.Manifold.cube([12.0, 5.0, 5.0], center=False).translate([-6.0, -16.0, -5.0])
    cradle_solid = body + tongue

    # 3. Female Dovetail Sliding Channel (0.25mm clearance per side)
    pts_female = [[-3.25, 0.5], [-5.75, -2.75], [5.75, -2.75], [3.25, 0.5]]
    cs_female = m3d.CrossSection([pts_female])
    channel = cs_female.extrude(16.0).rotate([90, 0, 0]).translate([0, -11.0, 0])

    # 4. Snap-Detent Pocket (matching 0.38mm male bump at Y = -14.0mm)
    detent = m3d.Manifold.cube([8.0, 2.2, 0.5], center=True).translate([0, -14.0, -2.95])

    # 5. Sensor Cavity (Open from front at Z = -outer_d up to Z = -wall)
    cav_w = outer_w - 2 * wall  # 16.0 mm
    cav_h = outer_h - 2 * wall  # 27.2 mm
    cav_cs = rounded_box_2d(cav_w, cav_h, 1.2).translate([0, center_y])
    cavity = cav_cs.extrude(outer_d - wall + 1.0).translate([0, 0, -outer_d - 0.5])

    # 6. Bottom wire pass-through notch at bottom wall
    wire_notch = m3d.Manifold.cube([5.0, 6.0, 6.0], center=True).translate([0, center_y - outer_h/2, -outer_d + 3.0])

    # 7. Snap cuts on left and right inside walls for cover retention
    snap_cuts = m3d.Manifold.cube([1.2, 8.0, 2.0], center=True).translate([-cav_w/2 - 0.3, center_y, -outer_d + 2.0]) + \
                m3d.Manifold.cube([1.2, 8.0, 2.0], center=True).translate([ cav_w/2 + 0.3, center_y, -outer_d + 2.0])

    base = cradle_solid - channel - detent - cavity - wire_notch - snap_cuts
    return base

def build_dht11_cover():
    """
    Part 4B: Snap-Fit Front Grille Cover for DHT11 sensor:
    - 4 downward rain-deflecting louvers (-35 deg slope) for splash protection.
    - Lateral snap-fit detents that click into Base Cradle.
    - Bottom water drainage and wire relief notch.
    - Uniform 2.0-2.4 mm wall thickness.
    """
    outer_w = 20.8
    outer_h = 32.0
    outer_d = 12.0
    center_y = -32.0
    t_plate = 2.0

    # 1. Main face plate
    plate_cs = rounded_box_2d(outer_w, outer_h, 2.5).translate([0, center_y])
    plate = plate_cs.extrude(t_plate).translate([0, 0, -outer_d - t_plate])

    # 2. Inward plug rim
    rim_cs = rounded_box_2d(15.6, 26.8, 1.0).translate([0, center_y])
    rim_solid = rim_cs.extrude(2.5).translate([0, 0, -outer_d])
    rim_cavity = rounded_box_2d(12.6, 23.8, 0.8).translate([0, center_y]).extrude(3.5).translate([0, 0, -outer_d - 0.2])
    rim = rim_solid - rim_cavity

    # 3. Snap detent bumps
    bump_l = m3d.Manifold.cube([0.45, 6.0, 1.2], center=True).translate([-15.6/2 - 0.2, center_y, -outer_d + 1.25])
    bump_r = m3d.Manifold.cube([0.45, 6.0, 1.2], center=True).translate([ 15.6/2 + 0.2, center_y, -outer_d + 1.25])
    bumps = bump_l + bump_r

    # 4. Downward rain louvers
    louvers = []
    for ly in [-38.0, -34.0, -30.0, -26.0]:
        slit = m3d.Manifold.cube([9.0, 1.6, 5.0], center=True).rotate([-35, 0, 0]).translate([0, ly, -outer_d - t_plate/2])
        louvers.append(slit)
    louver_cut = m3d.Manifold.compose(louvers)

    # 5. Bottom drain & wire notch
    drain = m3d.Manifold.cube([5.0, 4.0, 3.0], center=True).translate([0, center_y - outer_h/2, -outer_d - t_plate/2])

    cover = plate + rim + bumps - louver_cut - drain
    return cover

def build_dht11_mount():
    """
    Constructs the complete DHT11 sensor pod assembly:
    Returns the Base Cradle connected to the Cover via a breakaway runner bridge
    for printing both parts together on 1 build plate as 1 watertight manifold solid.
    """
    base = build_dht11_base()
    # Place cover alongside base on print bed with a thin breakaway sprue
    cover = build_dht11_cover().rotate([180, 0, 0]).translate([26.0, -64.0, -14.0])
    sprue = m3d.Manifold.cube([7.0, 3.0, 1.5], center=True).translate([13.0, -32.0, -1.0])
    return base + cover + sprue

# ==========================================
# MAIN EXECUTION
# ==========================================
def main():
    cad_dir = "/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/Project/tmp/esp32-scooter-dashboard/cad"
    os.makedirs(cad_dir, exist_ok=True)

    print("Generating Part 1: Front Bezel (M4 Corner Counterbores)...")
    front = build_front_bezel()
    save_mesh(front, os.path.join(cad_dir, "front_bezel.stl"))

    print("\nGenerating Part 2: Main Housing (M4 Nut Pockets, Rear V-Rail & Indexing Hinge)...")
    housing = build_main_housing()
    save_mesh(housing, os.path.join(cad_dir, "main_housing.stl"))

    print("\nGenerating Part 3: Mirror Mount Bracket (M4 Pivot, Rosette Teeth & Nut Trap)...")
    bracket = build_mirror_bracket()
    save_mesh(bracket, os.path.join(cad_dir, "mirror_bracket.stl"))

    print("\nGenerating Part 4: DHT11 Sensor Pod (2-Piece Snap-Fit Assembly)...")
    dht_pod = build_dht11_mount()
    save_mesh(dht_pod, os.path.join(cad_dir, "dht11_mount.stl"))

    # Also save separate STLs for maximum slicer flexibility
    base = build_dht11_base()
    save_mesh(base, os.path.join(cad_dir, "dht11_mount_base.stl"))

    cover = build_dht11_cover()
    save_mesh(cover, os.path.join(cad_dir, "dht11_mount_cover.stl"))

    print("\n==========================================")
    print("ALL CAD MODELS (v4.3 SENSOR POD REFINED) GENERATED SUCCESSFULLY!")
    print("==========================================")

if __name__ == "__main__":
    main()
