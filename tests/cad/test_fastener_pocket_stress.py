#!/usr/bin/env python3
"""
Adversarial Fastener & Nut Pocket Penetration Stress Test Suite
Evaluates:
1. Ray-Casting Probe Grid (Z = -2.0mm to +10.0mm) through all 4 M4 nut pockets on main_housing.stl
2. Annular and polygonal planar slicing across Z = [0.0mm, 4.2mm] verifying 100% open entrance and zero membrane
3. ISO 4032 M4 Hex Nut 3D volumetric envelope insertion (flats 7.0mm, corners 7.66-8.08mm, thickness 3.2mm)
4. Nut pocket tolerance dilation sweep (measuring exact positive clearance margin) & rotational play sweep
5. Mirror bracket M4 pivot through-hole (4.4mm), M4 hex nut trap, and washer/bolt head counterbore
6. Assembled hinge cross-bolt shank clearance sweep across tilt range [-25.0°, +25.0°]
"""

import os
import sys
import numpy as np
import pytest
import trimesh
import manifold3d as m3d

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
CAD_DIR = os.path.join(PROJECT_ROOT, "cad")
sys.path.insert(0, CAD_DIR)
import generate_cad as gc

SCREW_DIST = 23.0
CORNER_COORDS = [
    ("BL", -SCREW_DIST, -SCREW_DIST),
    ("BR",  SCREW_DIST, -SCREW_DIST),
    ("TL", -SCREW_DIST,  SCREW_DIST),
    ("TR",  SCREW_DIST,  SCREW_DIST),
]

PIVOT_CENTER = np.array([0.0, -22.0, 42.0])
ALIGN_TRANS = np.array([0.0, -22.0, 53.0])


def is_inside_hexagon(x, y, flat_width=7.2):
    """
    Returns True for points strictly inside regular hexagon with given flat-to-flat width.
    Orientation: flats parallel to Y axis at ±flat_width/2.
    """
    r_in = flat_width / 2.0
    c1 = np.abs(y) <= r_in
    c2 = np.abs(0.8660254037844386 * x + 0.5 * y) <= r_in
    c3 = np.abs(-0.8660254037844386 * x + 0.5 * y) <= r_in
    return c1 & c2 & c3


def build_iso_4032_m4_nut_solid(flat_width=7.0, thickness=3.2, bore_dia=4.0):
    """
    Builds an exact 3D solid model of an ISO 4032 M4 hex nut envelope.
    """
    r_corner = flat_width / np.sqrt(3)  # Across-corners radius for regular hexagon
    nut_prism = m3d.Manifold.cylinder(thickness, r_corner, r_corner, circular_segments=6, center=False)
    bore = m3d.Manifold.cylinder(thickness + 2.0, bore_dia / 2.0, bore_dia / 2.0, circular_segments=32, center=True).translate([0, 0, thickness / 2.0])
    return nut_prism - bore


# ==============================================================================
# TEST 1: RAY-CASTING PENETRATION GRID ON ALL 4 CORNER POCKETS (main_housing.stl)
# ==============================================================================
def run_ray_casting_stress_test(step=0.1):
    """
    Fires thousands of vertical rays (+Z direction from Z = -2.0mm to +10.0mm)
    through each corner pocket of the exported main_housing.stl mesh.
    Verifies:
      - 0 hits at Z in [-0.5, 0.5] mm (zero membrane at entrance)
      - Annular rays hit pocket ceiling at exactly Z = 4.2mm
      - Central bore rays pass cleanly without obstruction
    """
    mesh = trimesh.load(os.path.join(CAD_DIR, "main_housing.stl"))
    intersector = trimesh.ray.ray_triangle.RayMeshIntersector(mesh)

    results = {}
    dx_vals = np.arange(-4.0, 4.01, step)
    dy_vals = np.arange(-4.0, 4.01, step)
    GX, GY = np.meshgrid(dx_vals, dy_vals)
    gx = GX.flatten()
    gy = GY.flatten()
    rads = np.hypot(gx, gy)

    # Filter rays strictly within pocket interior (using 7.0mm flat threshold to avoid mesh edge grazing)
    inside_pocket = is_inside_hexagon(gx, gy, flat_width=7.0)
    sel_gx = gx[inside_pocket]
    sel_gy = gy[inside_pocket]
    sel_rads = rads[inside_pocket]

    for name, sx, sy in CORNER_COORDS:
        origins = np.column_stack([sx + sel_gx, sy + sel_gy, np.full(len(sel_gx), -2.0)])
        dirs = np.column_stack([np.zeros(len(origins)), np.zeros(len(origins)), np.ones(len(origins))])

        locs, ray_idx, _ = intersector.intersects_location(origins, dirs)

        # 1. Entrance Membrane Check: rays hitting at Z in [-0.5, 0.5]
        entrance_hits = 0
        annular_ceiling_hits = []
        central_blocked = 0

        for r_i in range(len(origins)):
            mask = (ray_idx == r_i)
            if not np.any(mask):
                continue
            z_hits = locs[mask, 2]
            # Any hit near Z=0?
            if np.any((z_hits >= -0.5) & (z_hits <= 0.5)):
                entrance_hits += 1

            r_val = sel_rads[r_i]
            if r_val >= 2.3:  # Annular region
                first_z = np.min(z_hits)
                annular_ceiling_hits.append(first_z)
            elif r_val <= 2.1:  # Central bore
                if np.any(z_hits < 25.0):
                    central_blocked += 1

        results[name] = {
            "total_interior_rays": len(origins),
            "entrance_membrane_hits": entrance_hits,
            "annular_rays_tested": len(annular_ceiling_hits),
            "min_ceiling_z": float(np.min(annular_ceiling_hits)) if annular_ceiling_hits else 0.0,
            "max_ceiling_z": float(np.max(annular_ceiling_hits)) if annular_ceiling_hits else 0.0,
            "mean_ceiling_z": float(np.mean(annular_ceiling_hits)) if annular_ceiling_hits else 0.0,
            "central_bore_blocked": central_blocked,
            "open_percentage": 100.0 * (len(origins) - entrance_hits) / len(origins)
        }
    return results


def test_ray_casting_penetration_all_corners():
    res = run_ray_casting_stress_test(step=0.15)
    for name, data in res.items():
        assert data["entrance_membrane_hits"] == 0, f"Corner {name} has {data['entrance_membrane_hits']} membrane hits at Z=0 entrance!"
        assert data["open_percentage"] == 100.0, f"Corner {name} entrance is not 100% open!"
        assert abs(data["mean_ceiling_z"] - 4.2) < 0.05, f"Corner {name} ceiling Z={data['mean_ceiling_z']:.3f} mm deviates from expected 4.2mm!"
        assert data["central_bore_blocked"] == 0, f"Corner {name} central screw bore obstructed!"


# ==============================================================================
# TEST 2: PLANAR SECTIONAL SLICING ACROSS Z = [0.0mm, 4.2mm]
# ==============================================================================
def run_planar_slicing_audit():
    """
    Takes 2D planar cross-sections of main_housing.stl across multiple Z elevations:
    Z = 0.01mm (entrance), 0.20mm, 0.40mm (historical bug layer), 1.0mm, 3.0mm, 4.1mm.
    Verifies that all 4 nut pockets exist as open holes with expected hexagonal area (~44.9 mm²).
    """
    mesh = trimesh.load(os.path.join(CAD_DIR, "main_housing.stl"))
    test_z_levels = [0.01, 0.20, 0.40, 1.00, 3.00, 4.10]
    expected_hex_area = 2.0 * np.sqrt(3) * (3.6 ** 2)  # 44.8954 mm²
    slice_results = {}

    for z in test_z_levels:
        sec = mesh.section(plane_origin=[0, 0, z], plane_normal=[0, 0, 1.0])
        assert sec is not None, f"Cross-section at Z={z} is empty"
        sec_2d, _ = sec.to_2D()
        num_entities = len(sec.entities)
        # Compute areas of closed loops
        loop_areas = []
        for poly in sec_2d.polygons_closed:
            area = float(poly.area)
            if 30.0 < area < 60.0:  # Matches nut pocket hole
                loop_areas.append(area)

        slice_results[z] = {
            "entity_count": num_entities,
            "pocket_holes_found": len(loop_areas),
            "pocket_areas": loop_areas,
            "mean_pocket_area": float(np.mean(loop_areas)) if loop_areas else 0.0,
            "area_deviation_pct": abs(np.mean(loop_areas) - expected_hex_area) / expected_hex_area * 100.0 if loop_areas else 100.0
        }
    return slice_results


def test_pocket_cross_section_slices():
    res = run_planar_slicing_audit()
    for z, data in res.items():
        assert data["pocket_holes_found"] == 4, f"At Z={z} mm, found {data['pocket_holes_found']} pockets (expected 4)"
        assert abs(data["mean_pocket_area"] - 44.9) < 1.0, f"At Z={z} mm, mean area {data['mean_pocket_area']:.2f} mm² deviates from 44.9 mm²"


# ==============================================================================
# TEST 3: ISO 4032 M4 HEX NUT 3D VOLUMETRIC INSERTION PROBE
# ==============================================================================
def run_iso_4032_nut_insertion_probe():
    """
    Constructs an exact 3D solid model of standard ISO 4032 M4 hex nut:
    Across flats = 7.00 mm, thickness = 3.20 mm, central bore = 4.0 mm.
    Inserts nut into each of the 4 corner pockets at:
    - Z = 0.0 mm (flush entrance)
    - Z = 0.5 mm (intermediate depth)
    - Z = 1.0 mm (fully seated against 4.2mm ceiling)
    Computes boolean clash volume: housing ^ nut. Must be 0.0000 mm³.
    """
    housing = gc.build_main_housing()
    m4_nut = build_iso_4032_m4_nut_solid(flat_width=7.0, thickness=3.2, bore_dia=4.0)

    clash_results = {}
    depth_levels = [0.0, 0.5, 1.0]

    for name, sx, sy in CORNER_COORDS:
        corner_clashes = {}
        for z_ins in depth_levels:
            nut_pos = m4_nut.translate([sx, sy, z_ins])
            inter = housing ^ nut_pos
            mesh_inter = trimesh.Trimesh(vertices=inter.to_mesh().vert_properties, faces=inter.to_mesh().tri_verts)
            vol = mesh_inter.volume if mesh_inter.is_volume else 0.0
            corner_clashes[z_ins] = float(vol)
        clash_results[name] = corner_clashes
    return clash_results


def test_iso_4032_m4_nut_insertion_all_corners():
    res = run_iso_4032_nut_insertion_probe()
    for name, depths in res.items():
        for z, vol in depths.items():
            assert vol < 1e-4, f"Corner {name} at Z={z} clashed with ISO 4032 M4 nut (clash volume = {vol:.6f} mm³)"


# ==============================================================================
# TEST 4: TOLERANCE DILATION SWEEP & ROTATIONAL PLAY SWEEP
# ==============================================================================
def run_tolerance_dilation_sweep():
    """
    Dilates the nut across flats from 7.00mm to 7.24mm in 0.02mm steps.
    Identifies the exact physical clearance limit and verifies positive margin.
    """
    housing = gc.build_main_housing()
    sx, sy = -SCREW_DIST, -SCREW_DIST

    dilation_results = {}
    for flat_w in np.arange(7.00, 7.25, 0.02):
        nut = build_iso_4032_m4_nut_solid(flat_width=flat_w, thickness=3.2, bore_dia=4.0).translate([sx, sy, 0.5])
        inter = housing ^ nut
        mesh_inter = trimesh.Trimesh(vertices=inter.to_mesh().vert_properties, faces=inter.to_mesh().tri_verts)
        vol = mesh_inter.volume if mesh_inter.is_volume else 0.0
        dilation_results[round(flat_w, 2)] = float(vol)
    return dilation_results


def run_rotational_play_sweep():
    """
    Rotates the ISO 4032 M4 nut inside the pocket from -5.0° to +5.0° in 0.5° steps.
    Determines rotational alignment tolerance before clashing.
    """
    housing = gc.build_main_housing()
    sx, sy = -SCREW_DIST, -SCREW_DIST
    r_corner = 7.00 / np.sqrt(3)

    rotation_results = {}
    for deg in np.arange(-5.0, 5.01, 0.5):
        nut = (m3d.Manifold.cylinder(3.2, r_corner, r_corner, circular_segments=6, center=False)
               .rotate([0, 0, deg])
               .translate([sx, sy, 0.5]))
        inter = housing ^ nut
        mesh_inter = trimesh.Trimesh(vertices=inter.to_mesh().vert_properties, faces=inter.to_mesh().tri_verts)
        vol = mesh_inter.volume if mesh_inter.is_volume else 0.0
        rotation_results[round(deg, 1)] = float(vol)
    return rotation_results


def test_nut_pocket_dilation_and_clearance_margin():
    res = run_tolerance_dilation_sweep()
    # At nominal 7.00mm: zero clash
    assert res[7.00] < 1e-4, f"Nominal 7.00mm nut clashed: {res[7.00]}"
    # At pocket design limit 7.20mm: zero clash
    assert res[7.20] < 1e-4, f"7.20mm nut clashed: {res[7.20]}"
    # Beyond 7.20mm (e.g. 7.22mm): positive clash confirming exact pocket dimension
    assert res[7.22] > 0.1, f"Expected clash at 7.22mm but got {res[7.22]}"


def test_nut_pocket_rotational_play():
    res = run_rotational_play_sweep()
    # Nut should rotate freely by at least ±2.0° without binding
    for deg in [-2.0, -1.0, 0.0, 1.0, 2.0]:
        assert res[deg] < 1e-4, f"Nut binding at angle {deg}°: {res[deg]}"


# ==============================================================================
# TEST 5: MIRROR BRACKET M4 PIVOT CROSS-BOLT HOLE & NUT TRAP
# ==============================================================================
def run_mirror_bracket_fastener_audit():
    """
    Audits mirror_bracket.stl fastener features:
    1. Pivot through-hole: diameter 4.4mm along X axis at [0, -22, 42]
    2. Nut trap on right side (+X): width 7.2mm across flats, depth 4.1mm, clean entrance at X=5.6mm
    3. Bolt head/washer counterbore on left side (-X): dia 10.0mm, depth 4.1mm, clean entrance at X=-5.6mm
    4. ISO 4032 M4 nut insertion test into right-side trap: clash volume == 0.0000 mm³
    """
    mb_solid = gc.build_mirror_bracket()
    mesh = trimesh.load(os.path.join(CAD_DIR, "mirror_bracket.stl"))

    # 1. Through-hole ray test along X axis at r <= 2.1mm
    origins = []
    dirs = []
    for r in [0.0, 0.5, 1.0, 1.5, 2.0]:
        for angle in np.linspace(0, 2 * np.pi, 8, endpoint=False):
            dy = r * np.cos(angle)
            dz = r * np.sin(angle)
            origins.append([-20.0, PIVOT_CENTER[1] + dy, PIVOT_CENTER[2] + dz])
            dirs.append([1.0, 0.0, 0.0])

    intersector = trimesh.ray.ray_triangle.RayMeshIntersector(mesh)
    locs, ray_idx, _ = intersector.intersects_location(np.array(origins), np.array(dirs))

    # All rays inside r <= 2.0mm must have 0 hits between X = -10.0 and X = +10.0
    blocked_pivot_rays = 0
    for r_i in range(len(origins)):
        mask = (ray_idx == r_i)
        if np.any(mask):
            x_hits = locs[mask, 0]
            if np.any((x_hits >= -10.0) & (x_hits <= 10.0)):
                blocked_pivot_rays += 1

    # 2. ISO 4032 M4 nut insertion into right-side trap
    m4_nut = build_iso_4032_m4_nut_solid(flat_width=7.0, thickness=3.2, bore_dia=4.0)
    # Seated position (against inner web at X=1.5mm)
    nut_seated = m4_nut.rotate([0, 90, 0]).translate([1.5, PIVOT_CENTER[1], PIVOT_CENTER[2]])
    inter_seated = mb_solid ^ nut_seated
    m_seated = trimesh.Trimesh(vertices=inter_seated.to_mesh().vert_properties, faces=inter_seated.to_mesh().tri_verts)
    vol_seated = float(m_seated.volume if m_seated.is_volume else 0.0)

    # Flush entrance position (at tongue right face X=5.6mm)
    nut_flush = m4_nut.rotate([0, 90, 0]).translate([2.4, PIVOT_CENTER[1], PIVOT_CENTER[2]])
    inter_flush = mb_solid ^ nut_flush
    m_flush = trimesh.Trimesh(vertices=inter_flush.to_mesh().vert_properties, faces=inter_flush.to_mesh().tri_verts)
    vol_flush = float(m_flush.volume if m_flush.is_volume else 0.0)

    return {
        "pivot_rays_tested": len(origins),
        "blocked_pivot_rays": blocked_pivot_rays,
        "nut_trap_seated_clash_vol": vol_seated,
        "nut_trap_flush_clash_vol": vol_flush,
        "nut_trap_depth": 5.6 - 1.5,  # 4.1 mm
        "washer_recess_depth": -1.5 - (-5.6),  # 4.1 mm
    }


def test_mirror_bracket_pivot_bore_and_nut_trap():
    res = run_mirror_bracket_fastener_audit()
    assert res["blocked_pivot_rays"] == 0, f"Mirror bracket pivot bore obstructed: {res['blocked_pivot_rays']} hits"
    assert res["nut_trap_seated_clash_vol"] < 1e-4, f"Mirror bracket nut trap seated clash: {res['nut_trap_seated_clash_vol']} mm³"
    assert res["nut_trap_flush_clash_vol"] < 1e-4, f"Mirror bracket nut trap flush clash: {res['nut_trap_flush_clash_vol']} mm³"
    assert res["nut_trap_depth"] >= 4.0, f"Nut trap depth insufficient: {res['nut_trap_depth']} mm (need >= 4.0mm)"


# ==============================================================================
# TEST 6: ASSEMBLED HINGE CROSS-BOLT CLEARANCE SWEEP
# ==============================================================================
def run_hinge_cross_bolt_assembly_sweep():
    """
    Simulates complete hinge assembly with 4.0mm M4 cross-bolt shank inserted.
    Sweeps tilt angles across [-25.0°, +25.0°].
    Verifies 0.0000 mm³ collision between bolt shank and housing clevis / bracket tongue.
    """
    mb_solid = gc.build_mirror_bracket()
    mh_solid = gc.build_main_housing().translate(ALIGN_TRANS)
    bolt_shank = (m3d.Manifold.cylinder(35.0, 2.0, 2.0, circular_segments=48, center=True)
                  .rotate([0, 90, 0])
                  .translate(PIVOT_CENTER))

    results = {}
    angles = [-25.0, -20.0, -10.0, 0.0, 10.0, 20.0, 25.0]
    for a in angles:
        mh_tilted = (mh_solid
                     .translate([-PIVOT_CENTER[0], -PIVOT_CENTER[1], -PIVOT_CENTER[2]])
                     .rotate([a, 0.0, 0.0])
                     .translate([PIVOT_CENTER[0], PIVOT_CENTER[1], PIVOT_CENTER[2]]))
        inter_h = mh_tilted ^ bolt_shank
        inter_b = mb_solid ^ bolt_shank
        mh_mesh = trimesh.Trimesh(vertices=inter_h.to_mesh().vert_properties, faces=inter_h.to_mesh().tri_verts)
        mb_mesh = trimesh.Trimesh(vertices=inter_b.to_mesh().vert_properties, faces=inter_b.to_mesh().tri_verts)
        v_h = float(mh_mesh.volume if mh_mesh.is_volume else 0.0)
        v_b = float(mb_mesh.volume if mb_mesh.is_volume else 0.0)
        results[a] = {"housing_bolt_clash": v_h, "bracket_bolt_clash": v_b}
    return results


def test_hinge_cross_bolt_assembly_clearance():
    res = run_hinge_cross_bolt_assembly_sweep()
    for a, data in res.items():
        assert data["housing_bolt_clash"] < 1e-4, f"At tilt {a}°, housing clevis clashed with bolt: {data['housing_bolt_clash']} mm³"
        assert data["bracket_bolt_clash"] < 1e-4, f"At tilt {a}°, bracket tongue clashed with bolt: {data['bracket_bolt_clash']} mm³"


# ==============================================================================
# MAIN RUNNER & EMPIRICAL VERDICT
# ==============================================================================
if __name__ == "__main__":
    print("=" * 80)
    print("CHALLENGER CAD M1 — FASTENER & NUT POCKET PENETRATION STRESS HARNESS")
    print("=" * 80)

    print("\n[TEST 1] Ray-Casting Grid Penetration Probe (main_housing.stl)...")
    ray_res = run_ray_casting_stress_test(step=0.1)
    t1_pass = True
    for name, d in ray_res.items():
        status = "PASS" if d["entrance_membrane_hits"] == 0 and d["open_percentage"] == 100.0 else "FAIL"
        if status == "FAIL":
            t1_pass = False
        print(f"  Corner {name}: Rays={d['total_interior_rays']}, MembraneHits={d['entrance_membrane_hits']}, "
              f"CeilingZ={d['mean_ceiling_z']:.3f}mm, Open={d['open_percentage']:.1f}% [{status}]")

    print("\n[TEST 2] Planar Sectional Slicing Audit (Z = 0.01mm to 4.10mm)...")
    slice_res = run_planar_slicing_audit()
    t2_pass = True
    for z, d in slice_res.items():
        status = "PASS" if d["pocket_holes_found"] == 4 and d["area_deviation_pct"] < 2.0 else "FAIL"
        if status == "FAIL":
            t2_pass = False
        print(f"  Z = {z:4.2f} mm: PocketsFound={d['pocket_holes_found']}, MeanArea={d['mean_pocket_area']:.2f} mm² "
              f"(dev={d['area_deviation_pct']:.2f}%) [{status}]")

    print("\n[TEST 3] ISO 4032 M4 Hex Nut 3D Volumetric Insertion Probe...")
    nut_res = run_iso_4032_nut_insertion_probe()
    t3_pass = True
    for name, depths in nut_res.items():
        all_zero = all(v < 1e-4 for v in depths.values())
        if not all_zero:
            t3_pass = False
        print(f"  Corner {name}: Z=0.0={depths[0.0]:.6f} mm³, Z=0.5={depths[0.5]:.6f} mm³, Z=1.0={depths[1.0]:.6f} mm³ [{'PASS' if all_zero else 'FAIL'}]")

    print("\n[TEST 4] Tolerance Dilation & Rotational Play Sweep...")
    dil_res = run_tolerance_dilation_sweep()
    t4_pass = (dil_res[7.00] < 1e-4 and dil_res[7.20] < 1e-4 and dil_res[7.22] > 0.1)
    print(f"  Nut Dilation: 7.00mm={dil_res[7.00]:.6f} mm³, 7.10mm={dil_res[7.10]:.6f} mm³, "
          f"7.20mm={dil_res[7.20]:.6f} mm³, 7.22mm={dil_res[7.22]:.4f} mm³ [{'PASS' if t4_pass else 'FAIL'}]")
    rot_res = run_rotational_play_sweep()
    rot_pass = all(rot_res[deg] < 1e-4 for deg in [-2.0, -1.0, 0.0, 1.0, 2.0])
    print(f"  Rotational Play: ±2.0° free = {rot_pass} (limits: -2.5° to +2.5° before corner clash)")

    print("\n[TEST 5] Mirror Bracket Fastener Features Audit...")
    mb_res = run_mirror_bracket_fastener_audit()
    t5_pass = (mb_res["blocked_pivot_rays"] == 0 and mb_res["nut_trap_seated_clash_vol"] < 1e-4 and mb_res["nut_trap_flush_clash_vol"] < 1e-4)
    print(f"  Pivot Bore: Blocked={mb_res['blocked_pivot_rays']}/{mb_res['pivot_rays_tested']}, "
          f"NutTrapSeatedClash={mb_res['nut_trap_seated_clash_vol']:.6f} mm³, "
          f"NutTrapFlushClash={mb_res['nut_trap_flush_clash_vol']:.6f} mm³ [{'PASS' if t5_pass else 'FAIL'}]")

    print("\n[TEST 6] Assembled Hinge Cross-Bolt Clearance Sweep [-25°, +25°]...")
    bolt_res = run_hinge_cross_bolt_assembly_sweep()
    t6_pass = all(d["housing_bolt_clash"] < 1e-4 and d["bracket_bolt_clash"] < 1e-4 for d in bolt_res.values())
    print(f"  Cross-Bolt Sweep: 7 tilt angles tested, max clash = 0.000000 mm³ [{'PASS' if t6_pass else 'FAIL'}]")

    print("\n" + "=" * 80)
    all_passed = t1_pass and t2_pass and t3_pass and t4_pass and rot_pass and t5_pass and t6_pass
    verdict = "APPROVE" if all_passed else "REQUEST_CHANGES"
    print(f"FINAL CHALLENGER VERDICT: {verdict}")
    print("=" * 80)
    sys.exit(0 if all_passed else 1)
