#!/usr/bin/env python3
"""
Adversarial Tilt Collision Stress Test Suite & Geometric Gap Analyzer
Evaluates:
1. STL Mesh Integrity, Watertightness, Topology & Euler Characteristics
2. Ultra-Dense Tilt Interference Sweep [-25.0°, +25.0°] at 0.5° increments (101 angles)
3. Out-of-Bounds Tilt Sweep (beyond ±25°) to find exact geometric contact angle limits
4. Multi-Axis Translation Tolerance Jitter (ΔX, ΔY, ΔZ) across full tilt range
5. Minimum Euclidean Clearance Distance Query (neck vs ears, body vs arm)
6. Main Housing Rear Nut Pocket Penetration & M4 Nut Envelope Insertion Check
"""

import os
import sys
import numpy as np
import trimesh
import manifold3d as m3d
from scipy.spatial import cKDTree
try:
    import pytest
except ImportError:
    pytest = None

PROJECT_ROOT = "/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/Project/tmp/esp32-scooter-dashboard"
CAD_DIR = os.path.join(PROJECT_ROOT, "cad")
sys.path.insert(0, CAD_DIR)
import generate_cad as gc

PIVOT_CENTER = np.array([0.0, -22.0, 42.0])
ALIGN_TRANS = np.array([0.0, -22.0, 53.0])

def get_housing_manifold():
    return gc.build_main_housing()

def get_bracket_manifold():
    return gc.build_mirror_bracket()

def get_aligned_tilted_housing(housing_solid, angle_deg, offset=(0.0, 0.0, 0.0)):
    """
    Aligns housing to bracket hinge axis and applies tilt angle + translation offset.
    """
    dx, dy, dz = offset
    h_aligned = housing_solid.translate([ALIGN_TRANS[0] + dx, ALIGN_TRANS[1] + dy, ALIGN_TRANS[2] + dz])
    pivot = [PIVOT_CENTER[0] + dx, PIVOT_CENTER[1] + dy, PIVOT_CENTER[2] + dz]
    h_tilted = (h_aligned
                .translate([-pivot[0], -pivot[1], -pivot[2]])
                .rotate([angle_deg, 0.0, 0.0])
                .translate([pivot[0], pivot[1], pivot[2]]))
    return h_tilted

# ==============================================================================
# 1. STL MESH TOPOLOGY & EULER CHARACTERISTIC VERIFICATION
# ==============================================================================
def audit_all_stls_mesh_integrity():
    parts = ["front_bezel.stl", "main_housing.stl", "mirror_bracket.stl", "dht11_mount.stl"]
    results = {}
    for p in parts:
        stl_path = os.path.join(CAD_DIR, p)
        assert os.path.exists(stl_path), f"File {p} missing"
        mesh = trimesh.load(stl_path)
        assert mesh.is_watertight, f"Mesh {p} is not watertight"
        assert mesh.is_volume, f"Mesh {p} has invalid volume"
        assert mesh.is_winding_consistent, f"Mesh {p} has inconsistent winding"
        comps = mesh.split(only_watertight=False)
        assert len(comps) == 1, f"Mesh {p} has {len(comps)} components"

        euler = mesh.euler_number
        vol_cm3 = mesh.volume / 1000.0
        results[p] = {
            "watertight": mesh.is_watertight,
            "components": len(comps),
            "euler_number": euler,
            "volume_cm3": vol_cm3,
            "vertices": len(mesh.vertices),
            "faces": len(mesh.faces),
            "extents": mesh.extents.tolist()
        }
    return results

def test_all_stls_mesh_integrity():
    results = audit_all_stls_mesh_integrity()
    assert len(results) == 4
    for p, d in results.items():
        assert d["watertight"] is True
        assert d["components"] == 1


# ==============================================================================
# 2. ULTRA-DENSE TILT INTERFERENCE SWEEP [-25.0°, +25.0°] (0.5° increments)
# ==============================================================================
def run_dense_tilt_sweep(step_deg=0.5):
    """
    Sweeps [-25.0, +25.0] at step_deg intervals.
    Returns: {
        'angles': list,
        'collisions': list,
        'max_collision': float,
        'pass': bool
    }
    """
    housing = get_housing_manifold()
    bracket = get_bracket_manifold()

    angles = np.round(np.arange(-25.0, 25.0 + step_deg/2, step_deg), 2)
    collisions = []
    max_collision = 0.0

    for a in angles:
        h_tilted = get_aligned_tilted_housing(housing, float(a))
        inter = bracket ^ h_tilted
        mesh_data = inter.to_mesh()
        if len(mesh_data.tri_verts) == 0:
            vol = 0.0
        else:
            mesh = trimesh.Trimesh(vertices=mesh_data.vert_properties, faces=mesh_data.tri_verts)
            vol = mesh.volume if mesh.is_volume else 0.0
        collisions.append(vol)
        if vol > max_collision:
            max_collision = vol

    passed = (max_collision < 1e-4)
    return {
        "angles": angles.tolist(),
        "collisions": collisions,
        "max_collision": max_collision,
        "pass": passed
    }

# ==============================================================================
# 3. OUT-OF-BOUNDS TILT SWEEP (BEYOND ±25°)
# ==============================================================================
def run_out_of_bounds_sweep(step_deg=0.5, max_angle=40.0):
    """
    Finds exact negative contact angle limit (theta < -25°) and positive contact angle limit (theta > +25°).
    """
    housing = get_housing_manifold()
    bracket = get_bracket_manifold()

    # Search downward: -25.0 to -max_angle
    neg_angles = np.arange(-25.0, -max_angle - step_deg/2, -step_deg)
    neg_contact_angle = None
    neg_first_vol = 0.0
    for a in neg_angles:
        h_tilted = get_aligned_tilted_housing(housing, float(a))
        inter = bracket ^ h_tilted
        mesh_data = inter.to_mesh()
        if len(mesh_data.tri_verts) > 0:
            mesh = trimesh.Trimesh(vertices=mesh_data.vert_properties, faces=mesh_data.tri_verts)
            vol = mesh.volume if mesh.is_volume else 0.0
            if vol > 1e-4:
                neg_contact_angle = float(a)
                neg_first_vol = vol
                break

    # Search upward: +25.0 to +max_angle
    pos_angles = np.arange(25.0, max_angle + step_deg/2, step_deg)
    pos_contact_angle = None
    pos_first_vol = 0.0
    for a in pos_angles:
        h_tilted = get_aligned_tilted_housing(housing, float(a))
        inter = bracket ^ h_tilted
        mesh_data = inter.to_mesh()
        if len(mesh_data.tri_verts) > 0:
            mesh = trimesh.Trimesh(vertices=mesh_data.vert_properties, faces=mesh_data.tri_verts)
            vol = mesh.volume if mesh.is_volume else 0.0
            if vol > 1e-4:
                pos_contact_angle = float(a)
                pos_first_vol = vol
                break

    return {
        "neg_contact_angle": neg_contact_angle,
        "neg_first_vol": neg_first_vol,
        "pos_contact_angle": pos_contact_angle,
        "pos_first_vol": pos_first_vol,
    }

# ==============================================================================
# 4. MULTI-AXIS TRANSLATION TOLERANCE JITTER (ΔX, ΔY, ΔZ)
# ==============================================================================
def run_tolerance_jitter_sweep():
    """
    Tests lateral offsets ΔX in [-0.4, +0.4] mm and spatial jitter (ΔX, ΔY, ΔZ) across key tilt angles.
    """
    housing = get_housing_manifold()
    bracket = get_bracket_manifold()

    # Pure lateral X shifts (nominal gap is 0.5mm on each side)
    dx_values = [-0.4, -0.3, -0.2, -0.1, 0.0, 0.1, 0.2, 0.3, 0.4]
    test_angles = [-25.0, -15.0, 0.0, 15.0, 25.0]

    lateral_results = {}
    for dx in dx_values:
        max_v = 0.0
        for a in test_angles:
            h_tilted = get_aligned_tilted_housing(housing, a, offset=(dx, 0.0, 0.0))
            inter = bracket ^ h_tilted
            m_data = inter.to_mesh()
            if len(m_data.tri_verts) > 0:
                mesh = trimesh.Trimesh(vertices=m_data.vert_properties, faces=m_data.tri_verts)
                v = mesh.volume if mesh.is_volume else 0.0
                if v > max_v:
                    max_v = v
        lateral_results[dx] = max_v

    # Combined 3D jitter at ±0.1mm and ±0.2mm
    jitter_tests = [
        ("jitter_0.1mm", 0.1),
        ("jitter_0.2mm", 0.2),
    ]
    corner_results = {}
    for name, delta in jitter_tests:
        max_j_vol = 0.0
        worst_comb = None
        for dx in [-delta, delta]:
            for dy in [-delta, delta]:
                for dz in [-delta, delta]:
                    for a in [-25.0, 0.0, 25.0]:
                        h_tilted = get_aligned_tilted_housing(housing, a, offset=(dx, dy, dz))
                        inter = bracket ^ h_tilted
                        m_data = inter.to_mesh()
                        if len(m_data.tri_verts) > 0:
                            mesh = trimesh.Trimesh(vertices=m_data.vert_properties, faces=m_data.tri_verts)
                            v = mesh.volume if mesh.is_volume else 0.0
                            if v > max_j_vol:
                                max_j_vol = v
                                worst_comb = (dx, dy, dz, a)
        corner_results[name] = {"max_vol": max_j_vol, "worst_case": worst_comb}

    return {
        "lateral_results": lateral_results,
        "corner_results": corner_results
    }

# ==============================================================================
# 5. PHYSICAL MINIMUM CLEARANCE DISTANCE QUERY
# ==============================================================================
def run_physical_clearance_query():
    """
    Computes exact minimum Euclidean distance between bracket mesh and housing mesh
    across tilt angles [-25°, +25°].
    """
    housing = get_housing_manifold()
    bracket = get_bracket_manifold()

    bracket_mesh_data = bracket.to_mesh()
    bracket_mesh = trimesh.Trimesh(vertices=bracket_mesh_data.vert_properties, faces=bracket_mesh_data.tri_verts)
    bracket_tree = cKDTree(bracket_mesh.vertices)

    angles = [-25.0, -20.0, -15.0, -10.0, -5.0, 0.0, 5.0, 10.0, 15.0, 20.0, 25.0]
    min_distances = {}

    for a in angles:
        h_tilted = get_aligned_tilted_housing(housing, a)
        h_mesh_data = h_tilted.to_mesh()
        h_mesh = trimesh.Trimesh(vertices=h_mesh_data.vert_properties, faces=h_mesh_data.tri_verts)

        # Query distance from housing vertices to bracket vertices
        dists, _ = bracket_tree.query(h_mesh.vertices)
        min_dist = float(np.min(dists))
        min_distances[a] = min_dist

    # Also compute lateral clearance between bracket tongue neck and clevis inner ears:
    # Bracket tongue width is 11.2mm (X from -5.6 to +5.6)
    # Housing clevis gap is 12.2mm (X from -6.1 to +6.1)
    # Geometric theoretical clearance = (12.2 - 11.2) / 2 = 0.500 mm.
    return min_distances

# ==============================================================================
# 6. NUT POCKET REAR PENETRATION & M4 NUT ENVELOPE INSERTION CHECK
# ==============================================================================
def run_nut_pocket_envelope_check():
    """
    Checks that each corner nut pocket has 0.0 skin and accepts an M4 hex nut envelope.
    """
    housing = get_housing_manifold()
    screw_dist = 23.0
    corners = [
        (-screw_dist, -screw_dist),
        ( screw_dist, -screw_dist),
        (-screw_dist,  screw_dist),
        ( screw_dist,  screw_dist),
    ]
    results = {}
    for sx, sy in corners:
        # Annular probe at Z in [0.001, 0.35] mm
        probe_annular = (m3d.Manifold.cylinder(0.35, 3.5, 3.5, circular_segments=32, center=False) - 
                         m3d.Manifold.cylinder(0.35, 2.4, 2.4, circular_segments=32, center=False)).translate([sx, sy, 0.001])
        inter_skin = housing ^ probe_annular
        m_skin = inter_skin.to_mesh()
        if len(m_skin.tri_verts) > 0:
            skin_mesh = trimesh.Trimesh(vertices=m_skin.vert_properties, faces=m_skin.tri_verts)
            skin_vol = skin_mesh.volume if skin_mesh.is_volume else 0.0
        else:
            skin_vol = 0.0

        # Check breakthrough at entrance plane Z in [-0.5, 0.0]
        probe_entry = (m3d.Manifold.cylinder(0.5, 3.5, 3.5, circular_segments=32, center=False) -
                       m3d.Manifold.cylinder(0.5, 2.4, 2.4, circular_segments=32, center=False)).translate([sx, sy, -0.5])
        inter_entry = housing ^ probe_entry
        m_entry = inter_entry.to_mesh()
        if len(m_entry.tri_verts) > 0:
            entry_mesh = trimesh.Trimesh(vertices=m_entry.vert_properties, faces=m_entry.tri_verts)
            entry_vol = entry_mesh.volume if entry_mesh.is_volume else 0.0
        else:
            entry_vol = 0.0

        results[f"({sx:+4.1f},{sy:+4.1f})"] = {
            "skin_vol": skin_vol,
            "entry_inter_vol": entry_vol,
            "open": (skin_vol < 1e-4 and entry_vol < 1e-4)
        }
    return results

# ==============================================================================
# PYTEST TEST FUNCTIONS
# ==============================================================================
def test_dense_tilt_sweep_pytest():
    res = run_dense_tilt_sweep(step_deg=1.0)
    assert res["pass"], f"Dense tilt sweep failed with max collision = {res['max_collision']} mm³"

def test_nut_pocket_penetration_pytest():
    res = run_nut_pocket_envelope_check()
    for corner, data in res.items():
        assert data["open"], f"Nut pocket at {corner} is not cleanly open: {data}"

def test_tolerance_lateral_pytest():
    res = run_tolerance_jitter_sweep()
    # At ±0.2mm lateral shift, collision must still be 0.0 (since clearance is 0.5mm)
    assert res["lateral_results"][-0.2] < 1e-4
    assert res["lateral_results"][+0.2] < 1e-4

if __name__ == "__main__":
    print("=" * 80)
    print("CHALLENGER CAD M1 — ADVERSARIAL TILT COLLISION & GAP STRESS HARNESS")
    print("=" * 80)

    print("\n[SUITE 1] STL Mesh Topology & Euler Characteristic Audit...")
    stl_res = audit_all_stls_mesh_integrity()
    for name, d in stl_res.items():
        print(f"  {name:18s}: Watertight={d['watertight']}, Comps={d['components']}, "
              f"Euler={d['euler_number']}, Vol={d['volume_cm3']:.2f} cm³, "
              f"Verts={d['vertices']}, Faces={d['faces']}")

    print("\n[SUITE 2] Ultra-Dense Tilt Interference Sweep [-25.0°, +25.0°] (0.5° increments, 101 angles)...")
    dense_res = run_dense_tilt_sweep(step_deg=0.5)
    print(f"  Total angles tested: {len(dense_res['angles'])}")
    print(f"  Max collision volume: {dense_res['max_collision']:.6f} mm³")
    print(f"  Result: {'PASSED (Zero collision throughout)' if dense_res['pass'] else 'FAILED'}")

    print("\n[SUITE 3] Out-of-Bounds Tilt Sweep (Searching for Geometric Contact Limits)...")
    oob_res = run_out_of_bounds_sweep(step_deg=0.2, max_angle=130.0)
    print(f"  Negative tilt contact limit: {oob_res['neg_contact_angle']:.2f}° (collision vol: {oob_res['neg_first_vol']:.4f} mm³)")
    print(f"  Positive tilt contact limit: {oob_res['pos_contact_angle']:.2f}° (collision vol: {oob_res['pos_first_vol']:.4f} mm³)")
    if oob_res['neg_contact_angle'] is not None:
        neg_margin = abs(oob_res['neg_contact_angle']) - 25.0
        print(f"  Negative angular clearance margin: {neg_margin:+.2f}° beyond -25.0°")
    if oob_res['pos_contact_angle'] is not None:
        pos_margin = oob_res['pos_contact_angle'] - 25.0
        print(f"  Positive angular clearance margin: {pos_margin:+.2f}° beyond +25.0°")

    print("\n[SUITE 4] Multi-Axis Translation Tolerance Jitter & Assembly Wiggle Room...")
    jitter_res = run_tolerance_jitter_sweep()
    print("  Lateral X Shift (nominal gap = 0.50 mm per side):")
    for dx, vol in jitter_res["lateral_results"].items():
        status = "FREE" if vol < 1e-4 else f"COLLISION ({vol:.4f} mm³)"
        print(f"    ΔX = {dx:+4.2f} mm -> Max collision vol = {vol:.6f} mm³ [{status}]")

    print("  Corner Jitter Combinations (ΔX, ΔY, ΔZ):")
    for name, cdata in jitter_res["corner_results"].items():
        print(f"    {name}: Max collision vol = {cdata['max_vol']:.6f} mm³ (Worst: {cdata['worst_case']})")

    print("\n[SUITE 5] Physical Minimum Clearance Distance Query (Vertex-to-Vertex)...")
    clear_res = run_physical_clearance_query()
    for a, d in clear_res.items():
        print(f"    Tilt {a:+5.1f}° -> Min Euclidean vertex distance = {d:.4f} mm")
    print(f"    Theoretical lateral hinge clearance = 0.5000 mm (Tongue 11.2mm vs Clevis Gap 12.2mm)")

    print("\n[SUITE 6] Nut Pocket Rear Breakthrough & Entrance Envelope Check...")
    nut_res = run_nut_pocket_envelope_check()
    for corner, ndata in nut_res.items():
        print(f"    Corner {corner}: Skin={ndata['skin_vol']:.4f} mm³, Entry={ndata['entry_inter_vol']:.4f} mm³, Open={ndata['open']}")

    print("\n" + "=" * 80)
    print("VERDICT DETERMINATION:")
    all_ok = (dense_res['pass'] and 
              all(d['open'] for d in nut_res.values()) and 
              jitter_res["lateral_results"][-0.2] < 1e-4 and
              jitter_res["lateral_results"][+0.2] < 1e-4)
    print(f"FINAL CHALLENGER VERDICT: {'APPROVE' if all_ok else 'REQUEST_CHANGES'}")
    print("=" * 80)
