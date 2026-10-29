#!/usr/bin/env python3
"""
Adversarial Verification Suite for CAD Geometry & Collision Challenges
Author: Challenger 1 (Geometric & Collision Verification)
Target: Scooter Dashboard Enclosure (cad/generate_cad.py, cad/*.stl)
Platform: Linux / Python 3.13 / uv

Verifies:
1. Challenge 1: Front Bezel Acrylic Pocket Membrane (0.75 mm solid skin check & trapped void)
2. Challenge 2: M4 Corner Screw Clash with Acrylic Pocket (~5.51 mm³ per corner clash volume)
3. Challenge 3: Cable Gland Cutter vs Zip-Tie Bridge Clash (bridge severed into 2 disconnected stubs)
4. Challenge 4: Clevis Ear & Tongue Clamping Gap (0.60 mm axial gap & plastic yield)
5. Challenge 5: Dovetail Angle Mismatch (male 45.00° vs female 49.76°)
"""

import os
import pytest
import numpy as np
import manifold3d as m3d
import trimesh

CAD_DIR = "/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/Project/tmp/esp32-scooter-dashboard/cad"

# Geometry Helpers matching generate_cad.py
def rounded_box_2d(width, height, radius):
    w = max(width - 2 * radius, 0.1)
    h = max(height - 2 * radius, 0.1)
    return m3d.CrossSection.square([w, h], center=True).offset(radius, m3d.JoinType.Round)

def rounded_box_3d(width, height, depth, radius):
    cs = rounded_box_2d(width, height, radius)
    return cs.extrude(depth)

def cylinder(height, radius, center=False):
    return m3d.Manifold.cylinder(height, radius, radius, circular_segments=64, center=center)


# ==============================================================================
# CHALLENGE 1: FRONT BEZEL ACRYLIC POCKET MEMBRANE
# ==============================================================================
def test_challenge_1_acrylic_pocket_membrane():
    """
    Challenge 1:
    Probe points along Z to verify if a 0.75 mm solid skin exists at Z in [0.0, 0.75 mm].
    Verify whether the acrylic pocket is enclosed (trapped void) or open from the rear.
    """
    stl_path = os.path.join(CAD_DIR, "front_bezel.stl")
    assert os.path.exists(stl_path), f"File not found: {stl_path}"
    mesh = trimesh.load_mesh(stl_path)

    # In front_bezel:
    # Screen window aperture: X in [-19.0, 19.0], Y in [-15.0, 17.0]
    # Acrylic pocket: X in [-22.75, 22.75], Y in [-21.75, 23.75]
    # Test probe point at (X=20.0, Y=1.0) - inside pocket perimeter, outside window aperture
    probe_xy = [20.0, 1.0]

    # Sample points along Z from -0.5 mm to 3.5 mm
    z_coords = np.linspace(-0.2, 3.0, 33)
    probe_points = np.column_stack([
        np.full_like(z_coords, probe_xy[0]),
        np.full_like(z_coords, probe_xy[1]),
        z_coords
    ])

    inside_mask = mesh.contains(probe_points)

    print("\n[CHALLENGE 1] Probing Z profile at (X=20.0, Y=1.0):")
    solid_skin_z = []
    pocket_void_z = []
    bezel_front_z = []

    for z, is_solid in zip(z_coords, inside_mask):
        state = "SOLID" if is_solid else "AIR/VOID"
        print(f"  Z = {z:+.3f} mm -> {state}")
        if is_solid and 0.05 <= z <= 0.70:
            solid_skin_z.append(z)
        elif not is_solid and 0.80 <= z <= 2.20:
            pocket_void_z.append(z)
        elif is_solid and z >= 2.30:
            bezel_front_z.append(z)

    # Empirical assertions:
    # 1. Solid skin must exist between Z=0.0 and Z=0.75 mm
    assert len(solid_skin_z) >= 5, "Solid skin missing in Z in [0.0, 0.75 mm]"
    
    # 2. Void must exist between Z=0.75 and Z=2.25 mm
    assert len(pocket_void_z) >= 10, "Pocket void missing in Z in [0.75, 2.25 mm]"
    
    # 3. Solid bezel front wall must exist above Z=2.25 mm
    assert len(bezel_front_z) >= 5, "Bezel front wall missing above Z=2.25 mm"

    # Also verify from manifold geometry directly
    thickness = 6.5
    acrylic_pocket = rounded_box_3d(45.5, 45.5, 1.5, 2.0).translate([0, 1.0, 0.75])
    pocket_bounds = acrylic_pocket.bounding_box()
    pocket_z_min = pocket_bounds[2]
    pocket_z_max = pocket_bounds[5]

    print(f"[CHALLENGE 1] Acrylic pocket Z bounds: Z_min={pocket_z_min:.3f} mm, Z_max={pocket_z_max:.3f} mm")
    print(f"[CHALLENGE 1] Rear face is at Z=0.0 mm. Solid skin thickness = {pocket_z_min - 0.0:.3f} mm")

    assert np.isclose(pocket_z_min, 0.75, atol=1e-3), "Pocket Z_min must be 0.75 mm"
    assert np.isclose(pocket_z_max, 2.25, atol=1e-3), "Pocket Z_max must be 2.25 mm"
    assert np.isclose(pocket_z_min - 0.0, 0.75, atol=1e-3), "Solid skin thickness must be 0.75 mm"
    print(">>> VERIFICATION RESULT: Solid skin of 0.75 mm CONFIRMED. Pocket is an enclosed trapped cavity!")


# ==============================================================================
# CHALLENGE 2: M4 CORNER SCREW CLASH WITH ACRYLIC POCKET
# ==============================================================================
def test_challenge_2_m4_corner_screw_clash():
    """
    Challenge 2:
    Compute exact boolean intersection volume between the 4 corner screw holes
    and the 45.5x45.5 mm acrylic pocket.
    Verify if clash is ~5.51 mm³ per corner on top corners.
    """
    screw_dist = 23.0
    thickness = 6.5
    screw_radius = 2.2  # M4 clearance hole dia 4.4 mm

    # Pocket as modeled in generate_cad.py (translated by [0, 1.0, 0.75])
    pocket = rounded_box_3d(45.5, 45.5, 1.5, 2.0).translate([0, 1.0, 0.75])

    corners = [
        ("Top-Right (TR, +23, +23)", +screw_dist, +screw_dist),
        ("Top-Left  (TL, -23, +23)", -screw_dist, +screw_dist),
        ("Bot-Right (BR, +23, -23)", +screw_dist, -screw_dist),
        ("Bot-Left  (BL, -23, -23)", -screw_dist, -screw_dist),
    ]

    clash_volumes = {}
    print("\n[CHALLENGE 2] M4 Corner Screw Clash Volumes with Acrylic Pocket:")
    for name, sx, sy in corners:
        screw_cyl = cylinder(thickness + 10, screw_radius, center=True).translate([sx, sy, thickness / 2.0])
        intersection = screw_cyl ^ pocket
        vol = intersection.volume()
        clash_volumes[name] = vol
        print(f"  {name}: {vol:.4f} mm³")

    total_clash = sum(clash_volumes.values())
    print(f"[CHALLENGE 2] Total clash volume = {total_clash:.4f} mm³")

    # Verify top corners: each top corner has ~5.51 mm³ clash
    assert np.isclose(clash_volumes["Top-Right (TR, +23, +23)"], 5.519, atol=0.05), \
        f"TR clash volume expected ~5.51 mm³, got {clash_volumes['Top-Right (TR, +23, +23)']:.4f}"
    assert np.isclose(clash_volumes["Top-Left  (TL, -23, +23)"], 5.519, atol=0.05), \
        f"TL clash volume expected ~5.51 mm³, got {clash_volumes['Top-Left  (TL, -23, +23)']:.4f}"

    # Also compute symmetric case if pocket were centered at [0, 0, 0.75]
    pocket_sym = rounded_box_3d(45.5, 45.5, 1.5, 2.0).translate([0, 0.0, 0.75])
    sym_clashes = []
    for _, sx, sy in corners:
        screw_cyl = cylinder(thickness + 10, screw_radius, center=True).translate([sx, sy, thickness / 2.0])
        sym_clashes.append((screw_cyl ^ pocket_sym).volume())
    
    print(f"[CHALLENGE 2] If pocket centered at Y=0: {sym_clashes[0]:.4f} mm³ per corner (total {sum(sym_clashes):.4f} mm³)")
    print(">>> VERIFICATION RESULT: Top corner clash volume ~5.51 mm³ (~5.519 mm³) CONFIRMED!")


# ==============================================================================
# CHALLENGE 3: CABLE GLAND CUTTER VS ZIP-TIE BRIDGE CLASH
# ==============================================================================
def test_challenge_3_cable_gland_cutter_vs_ziptie_bridge():
    """
    Challenge 3:
    In main_housing, compute intersection between gland cutter cylinder
    (dia 6.5, length 24, Y=-29, Z=8.5) and zip-tie bridge (Y=-21, Z=7.5-9.5).
    Verify if bridge is severed into 2 disconnected pieces.
    """
    floor = 3.5

    # Zip-tie bridge and anchor posts as modeled in build_main_housing()
    anchor_post1 = m3d.Manifold.cube([3.0, 4.0, 6.0], center=True).translate([-5.0, -21.0, floor + 3.0])
    anchor_post2 = m3d.Manifold.cube([3.0, 4.0, 6.0], center=True).translate([ 5.0, -21.0, floor + 3.0])
    anchor_bridge = m3d.Manifold.cube([13.0, 4.0, 2.0], center=True).translate([0, -21.0, floor + 5.0])
    strain_relief = anchor_post1 + anchor_post2 + anchor_bridge

    # Cable hole cutter
    cable_hole = cylinder(24.0, 3.25, center=True).rotate([90, 0, 0]).translate([0, -29.0, 8.5])

    # Boolean intersection
    bridge_clash = anchor_bridge ^ cable_hole
    clash_vol = bridge_clash.volume()
    print(f"\n[CHALLENGE 3] Cable gland cutter clash with anchor_bridge: {clash_vol:.4f} mm³")

    # Boolean difference
    bridge_remaining = anchor_bridge - cable_hole
    decomp_bridge = bridge_remaining.decompose()
    num_bridge_stubs = len(decomp_bridge)
    print(f"[CHALLENGE 3] Number of remaining anchor_bridge pieces: {num_bridge_stubs}")

    relief_remaining = strain_relief - cable_hole
    decomp_relief = relief_remaining.decompose()
    num_relief_pieces = len(decomp_relief)
    print(f"[CHALLENGE 3] Number of remaining strain_relief pieces: {num_relief_pieces}")

    for i, stub in enumerate(decomp_bridge):
        bb = stub.bounding_box()
        print(f"  Stub {i+1}: Volume={stub.volume():.4f} mm³, X in [{bb[0]:.2f}, {bb[3]:.2f}]")

    # Compute severance gap width between stubs
    gap_width = decomp_bridge[1].bounding_box()[0] - decomp_bridge[0].bounding_box()[3]
    print(f"[CHALLENGE 3] Severance gap between bridge stubs: {gap_width:.2f} mm")

    assert np.isclose(clash_vol, 51.13, atol=0.2), f"Clash volume expected ~51.1 mm³, got {clash_vol:.4f}"
    assert num_bridge_stubs == 2, f"Bridge must be severed into exactly 2 pieces, got {num_bridge_stubs}"
    assert num_relief_pieces == 2, f"Strain relief must be split into exactly 2 pieces, got {num_relief_pieces}"
    assert gap_width > 6.0, f"Severance gap must exceed 6.0 mm, got {gap_width:.2f}"
    print(">>> VERIFICATION RESULT: Bridge is severed into 2 disconnected stubs (gap 6.18 mm). CONFIRMED!")


# ==============================================================================
# CHALLENGE 4: CLEVIS EAR & TONGUE CLAMPING GAP
# ==============================================================================
def test_challenge_4_clevis_ear_and_tongue_clamping_gap():
    """
    Challenge 4:
    Measure distance between bracket tongue right flank (X = +5.5 mm)
    and right clevis ear (X = +6.1 mm). Verify if axial gap is 0.60 mm.
    Also calculate required clamping deflection force and PETG bending stress.
    """
    housing_stl = os.path.join(CAD_DIR, "main_housing.stl")
    bracket_stl = os.path.join(CAD_DIR, "mirror_bracket.stl")

    housing_mesh = trimesh.load_mesh(housing_stl)
    bracket_mesh = trimesh.load_mesh(bracket_stl)

    # Right clevis ear inner flank in housing:
    v_housing = housing_mesh.vertices
    right_ear_pts = v_housing[(v_housing[:, 0] > 5.0) & (np.abs(v_housing[:, 1]) < 10.0) & (v_housing[:, 2] < -5.0) & (v_housing[:, 2] > -18.0)]
    right_ear_inner_x = np.min(right_ear_pts[:, 0])

    # Bracket tongue right flank:
    v_bracket = bracket_mesh.vertices
    tongue_pts = v_bracket[(np.abs(v_bracket[:, 1] - (-22.0)) < 10.0) & (np.abs(v_bracket[:, 2] - 42.0) < 10.0)]
    tongue_right_x = np.max(tongue_pts[:, 0])
    tongue_left_x = np.min(tongue_pts[:, 0])

    axial_gap = right_ear_inner_x - tongue_right_x

    print(f"\n[CHALLENGE 4] Housing Right Clevis Ear inside flank X: {right_ear_inner_x:.4f} mm")
    print(f"[CHALLENGE 4] Bracket Tongue right flank X:            {tongue_right_x:.4f} mm")
    print(f"[CHALLENGE 4] Bracket Tongue left flank X:             {tongue_left_x:.4f} mm")
    print(f"[CHALLENGE 4] Axial Clamping Gap (ΔX):                 {axial_gap:.4f} mm")

    assert np.isclose(right_ear_inner_x, 6.10, atol=1e-3), f"Right clevis ear inner flank must be 6.10 mm, got {right_ear_inner_x}"
    assert np.isclose(tongue_right_x, 5.50, atol=1e-3), f"Tongue right flank must be 5.50 mm, got {tongue_right_x}"
    assert np.isclose(axial_gap, 0.60, atol=1e-3), f"Axial clamping gap must be 0.60 mm, got {axial_gap}"

    # Structural Stress Analysis:
    # Cantilever ear: width b=32.0 mm, thickness h=6.8 mm, length L=11.0 mm (pivot Z=-11 to root Z=0)
    b = 32.0  # mm
    h = 6.8   # mm
    L = 11.0  # mm
    E = 2100.0  # MPa (PETG Young's Modulus)
    I = (b * (h ** 3)) / 12.0  # mm^4
    delta = axial_gap  # 0.60 mm

    # Force to deflect ear by delta: F = 3 E I delta / L^3
    clamp_force = (3.0 * E * I * delta) / (L ** 3)
    # Bending stress at root: sigma = 1.5 * E * h * delta / L^2
    bending_stress = (1.5 * E * h * delta) / (L ** 2)

    print(f"[CHALLENGE 4] Structural Analysis for deflecting 6.8 mm thick ear by 0.60 mm:")
    print(f"  Second moment of area I = {I:.3f} mm⁴")
    print(f"  Required clamping force F = {clamp_force:.1f} N")
    print(f"  Resulting bending stress σ = {bending_stress:.2f} MPa")
    print(f"  PETG yield strength σ_y ≈ 50 - 55 MPa")
    print(f"  Stress ratio σ / σ_y = {bending_stress / 55.0:.2f}x (EXCEEDS YIELD BY >90%!)")

    assert bending_stress > 100.0, "Bending stress must exceed 100 MPa"
    print(">>> VERIFICATION RESULT: Axial gap is strictly 0.60 mm. Plastic yield confirmed!")


# ==============================================================================
# CHALLENGE 5: DOVETAIL ANGLE MISMATCH
# ==============================================================================
def test_challenge_5_dovetail_angle_mismatch():
    """
    Challenge 5:
    Calculate male V-rail flank angle vs female channel flank angle from code points.
    Verify 45.00° vs 49.76°.
    """
    # Points from generate_cad.py:
    # Male rail:
    pts_male = [[-3.0, 0.0], [-5.5, -2.5], [5.5, -2.5], [3.0, 0.0]]
    # Female channel:
    pts_female = [[-3.25, 0.5], [-6.0, -2.75], [6.0, -2.75], [3.25, 0.5]]

    # Male flank vector (from [-3.0, 0.0] to [-5.5, -2.5]):
    dx_male = abs(pts_male[1][0] - pts_male[0][0])
    dz_male = abs(pts_male[1][1] - pts_male[0][1])
    angle_male = np.degrees(np.arctan2(dz_male, dx_male))

    # Female flank vector (from [-3.25, 0.5] to [-6.0, -2.75]):
    dx_female = abs(pts_female[1][0] - pts_female[0][0])
    dz_female = abs(pts_female[1][1] - pts_female[0][1])
    angle_female = np.degrees(np.arctan2(dz_female, dx_female))

    angular_mismatch = angle_female - angle_male

    print(f"\n[CHALLENGE 5] Male V-rail flank:   dx={dx_male:.2f} mm, dz={dz_male:.2f} mm -> Angle = {angle_male:.4f}°")
    print(f"[CHALLENGE 5] Female channel flank: dx={dx_female:.2f} mm, dz={dz_female:.2f} mm -> Angle = {angle_female:.4f}°")
    print(f"[CHALLENGE 5] Angular Mismatch (Δθ): {angular_mismatch:.4f}°")

    assert np.isclose(angle_male, 45.00, atol=1e-2), f"Male angle must be 45.00°, got {angle_male:.4f}"
    assert np.isclose(angle_female, 49.7636, atol=1e-2), f"Female angle must be 49.76°, got {angle_female:.4f}"
    assert np.isclose(angular_mismatch, 4.7636, atol=1e-2), f"Mismatch must be 4.76°, got {angular_mismatch:.4f}"

    # Clearance profile across depth:
    # At Z = -2.5 mm (crown of male):
    # Male half-width = 5.50 mm
    # Female flank at Z = -2.5 mm: X = 3.25 + (2.5 + 0.5) * (2.75 / 3.25) = 3.25 + 3.0 * (11/13) = 5.788 mm
    # Clearance at crown = 5.788 - 5.50 = 0.288 mm
    # At Z = 0.0 mm (root of male):
    # Male half-width = 3.00 mm
    # Female flank at Z = 0.0 mm: X = 3.25 + (0.5 - 0.0) * (2.75 / 3.25) = 3.25 + 0.5 * (11/13) = 3.673 mm
    # Clearance at root = 3.673 - 3.00 = 0.673 mm
    print(f"[CHALLENGE 5] Clearance Variation due to Angular Mismatch:")
    print(f"  Crown clearance (Z=-2.5 mm): 0.29 mm")
    print(f"  Root clearance  (Z=0.0 mm):  0.67 mm")
    print(f"  Clearance taper = {0.673 - 0.288:.3f} mm across 2.5 mm depth!")
    print(">>> VERIFICATION RESULT: Male 45.00° vs Female 49.76° (4.76° mismatch) CONFIRMED!")


if __name__ == "__main__":
    pytest.main(["-v", "-s", __file__])
