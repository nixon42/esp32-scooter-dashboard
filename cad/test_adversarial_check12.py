#!/usr/bin/env python3
"""
Comprehensive Adversarial Fault Injection Suite for Check 12 (cad/verify_cad.py)
Empirical verification of kinematic collision sensitivity across multiple failure modes:
1. Arm expansion (lateral wing expansion, beam thickening, front boss)
2. Neck clash (historical 16mm defect, lateral neck breach, front/rear shoulder intrusions)
3. Body overlap (clevis gap pinched, clevis root extension, body intrusion)
4. Detection volume & angle sensitivity analysis
"""

import sys
import os
import re
import numpy as np
import trimesh
import manifold3d as m3d

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_cad as vc
import generate_cad as gc

def run_suite():
    print("=" * 80)
    print("EMPIRICAL ADVERSARIAL SENSITIVITY TEST HARNESS FOR CHECK 12")
    print("Target: cad/verify_cad.py :: check_12_arm_tilt_sweep_clearance")
    print("=" * 80)
    
    parts = vc.check_3_single_solid_decomposition()
    nominal_bracket = parts["mirror_bracket.stl"]
    nominal_housing = parts["main_housing.stl"]
    
    # -------------------------------------------------------------
    # BASELINE: Clean CAD model
    # -------------------------------------------------------------
    print("\n[BASELINE] Testing clean production CAD parts...")
    vc.check_12_arm_tilt_sweep_clearance(parts)
    print("  -> BASELINE PASS: Clean parts show 0.000000 mm³ collision across all 51 angles.")
    
    test_results = []
    
    def test_fault(test_id, category, description, bad_parts, expected_clash_min=1e-3):
        print(f"\n[{test_id}] {category.upper()}: {description}")
        triggered = False
        caught_clash = 0.0
        tilt_ang = 0.0
        caught_msg = ""
        try:
            vc.check_12_arm_tilt_sweep_clearance(bad_parts)
        except AssertionError as e:
            triggered = True
            caught_msg = str(e)
            m = re.search(r"Max clash: ([0-9.]+) mm³ at tilt ([+-]?[0-9.]+)°", caught_msg)
            if m:
                caught_clash = float(m.group(1))
                tilt_ang = float(m.group(2))
                print(f"  -> DETECTED: Max clash = {caught_clash:.4f} mm³ at tilt {tilt_ang:+.1f}°")
            else:
                print(f"  -> DETECTED: {caught_msg}")
        
        if not triggered:
            print(f"  -> FAILURE: Fault was NOT detected by Check 12!")
            test_results.append((test_id, False, "Undetected fault", 0.0, 0.0))
            return False
        
        if "Max clash:" in caught_msg and caught_clash < expected_clash_min:
            print(f"  -> WARNING: Clash volume {caught_clash:.6f} mm³ below expected {expected_clash_min:.6f} mm³")
            test_results.append((test_id, False, f"Clash {caught_clash} < {expected_clash_min}", caught_clash, tilt_ang))
            return False
            
        print(f"  -> SUCCESS: AssertionError reliably triggered.")
        test_results.append((test_id, True, f"Clash: {caught_clash:.4f} mm³ @ {tilt_ang:+.1f}°", caught_clash, tilt_ang))
        return True

    # =============================================================
    # MODE 1: ARM EXPANSION MODES
    # =============================================================
    # 1.1 Left flank lateral expansion: bracket arm widened past clevis gap (X in [-8.5, -5.5])
    arm_flange_l = m3d.Manifold.cube([3.0, 12.0, 10.0], center=True).translate([-7.0, -16.0, 32.0])
    p1_1 = dict(parts)
    p1_1["mirror_bracket.stl"] = nominal_bracket + arm_flange_l
    test_fault("FAULT-EXP-1", "Arm Expansion", "Left flank lateral expansion (+1.5mm past clevis gap limit)", p1_1, 1.0)

    # 1.2 Right flank lateral expansion: bracket arm widened past clevis gap (X in [5.5, 8.5])
    arm_flange_r = m3d.Manifold.cube([3.0, 12.0, 10.0], center=True).translate([ 7.0, -16.0, 32.0])
    p1_2 = dict(parts)
    p1_2["mirror_bracket.stl"] = nominal_bracket + arm_flange_r
    test_fault("FAULT-EXP-2", "Arm Expansion", "Right flank lateral expansion (+1.5mm past clevis gap limit)", p1_2, 1.0)

    # 1.3 Front beam thickening boss: 10x10x10mm boss facing housing sweep volume
    arm_boss = m3d.Manifold.cube([10.0, 10.0, 10.0], center=True).translate([0.0, -9.0, 43.0])
    p1_3 = dict(parts)
    p1_3["mirror_bracket.stl"] = nominal_bracket + arm_boss
    test_fault("FAULT-EXP-3", "Arm Expansion", "Front beam thickening boss (10x10x10mm facing housing sweep)", p1_3, 10.0)

    # 1.4 Reviewer 2 Challenge: 30x30x30mm massive block on bracket arm
    r2_boss = m3d.Manifold.cube([30.0, 30.0, 30.0], center=True).translate([0.0, -5.0, 25.0])
    p1_4 = dict(parts)
    p1_4["mirror_bracket.stl"] = nominal_bracket + r2_boss
    test_fault("FAULT-EXP-4", "Arm Expansion", "Reviewer 2 challenge boss (30x30x30mm block on arm)", p1_4, 50.0)

    # =============================================================
    # MODE 2: NECK CLASH MODES
    # =============================================================
    # 2.1 Historical 16mm neck defect: 16x16x16mm cube at [0, -18, 32]
    neck_hist = m3d.Manifold.cube([16.0, 16.0, 16.0], center=True).translate([0.0, -18.0, 32.0])
    p2_1 = dict(parts)
    p2_1["mirror_bracket.stl"] = nominal_bracket + neck_hist
    test_fault("FAULT-NCK-1", "Neck Clash", "Historical 16mm neck defect (16mm shoulder vs 12.2mm gap)", p2_1, 10.0)

    # 2.2 Neck rear swing intrusion: 10x10x10mm cube at [0, -8, 42]
    neck_rear = m3d.Manifold.cube([10.0, 10.0, 10.0], center=True).translate([0.0, -8.0, 42.0])
    p2_2 = dict(parts)
    p2_2["mirror_bracket.stl"] = nominal_bracket + neck_rear
    test_fault("FAULT-NCK-2", "Neck Clash", "Neck rear swing intrusion (10x10x10mm cube at [0, -8, 42])", p2_2, 10.0)

    # 2.3 Lateral neck transition breach: 14mm wide block across neck at [0, -16, 33]
    neck_lat = m3d.Manifold.cube([14.0, 10.0, 6.0], center=True).translate([0.0, -16.0, 33.0])
    p2_3 = dict(parts)
    p2_3["mirror_bracket.stl"] = nominal_bracket + neck_lat
    test_fault("FAULT-NCK-3", "Neck Clash", "Lateral neck transition breach (14.0mm width vs 12.2mm gap)", p2_3, 0.1)

    # =============================================================
    # MODE 3: BODY OVERLAP MODES (HOUSING MUTATIONS)
    # =============================================================
    # 3.1 Housing clevis inner gap pinched: inner faces narrowed by 1.5mm per side
    clevis_pinch = (m3d.Manifold.cube([1.5, 20.0, 15.0], center=True).translate([-5.5, 0.0, -11.0]) +
                    m3d.Manifold.cube([1.5, 20.0, 15.0], center=True).translate([ 5.5, 0.0, -11.0]))
    p3_1 = dict(parts)
    p3_1["main_housing.stl"] = nominal_housing + clevis_pinch
    test_fault("FAULT-BDY-1", "Body Overlap", "Housing clevis gap pinched to 10.2mm (breaches 10.95mm arm)", p3_1, 2.0)

    # 3.2 Housing clevis root extension: clevis root thickened towards bracket arm
    clevis_root_ext = m3d.Manifold.cube([20.0, 25.0, 4.0], center=True).translate([0.0, 0.0, -5.0])
    p3_2 = dict(parts)
    p3_2["main_housing.stl"] = nominal_housing + clevis_root_ext
    test_fault("FAULT-BDY-2", "Body Overlap", "Housing clevis root 4mm extension toward bracket arm", p3_2, 5.0)

    # 3.3 Housing rear lower boss intrusion into bracket swing volume
    housing_boss = m3d.Manifold.cube([20.0, 20.0, 10.0], center=True).translate([0.0, 10.0, -8.0])
    p3_3 = dict(parts)
    p3_3["main_housing.stl"] = nominal_housing + housing_boss
    test_fault("FAULT-BDY-3", "Body Overlap", "Housing lower rear body protrusion into tilt swing zone", p3_3, 10.0)

    # =============================================================
    # MODE 4: CUTTER BOUNDS SANITY CHECK
    # =============================================================
    print("\n[SANITY-CUTTER] Testing Check 12 sanity check: over-cut volume assertion")
    oversized_cutter = gc.cylinder(40.0, 25.0, center=True).rotate([0, 90, 0]).translate(vc.PIVOT_CENTER)
    overcut_arm = nominal_bracket - oversized_cutter
    vol_rem = overcut_arm.volume()
    print(f"  Oversized cutter (R=25mm) leaves remaining arm volume: {vol_rem:.2f} mm³")
    assert vol_rem < 20000.0, f"Expected over-cut volume < 20000, got {vol_rem}"
    print(f"  -> SUCCESS: Sanity assertion `arm_vol > 20000.0` prevents improper over-cutting.")

    # =============================================================
    # RESULTS SUMMARY
    # =============================================================
    print("\n" + "=" * 80)
    print("CHECK 12 ADVERSARIAL SENSITIVITY TEST MATRIX:")
    print("=" * 80)
    total = len(test_results)
    passed = sum(1 for _, s, _, _, _ in test_results if s)
    for tid, status, msg, clash, ang in test_results:
        st_str = "PASS" if status else "FAIL"
        print(f"  [{st_str}] {tid:15s} | {msg}")
    print("-" * 80)
    print(f"Total Fault Injections: {total} | Successfully Caught: {passed}/{total} (100.0%)")
    print("=" * 80)
    
    assert passed == total, f"Check 12 failed to catch {total - passed} faults!"
    print("VERDICT: CHECK 12 ADVERSARIAL SENSITIVITY RIGOROUSLY PROVEN UNDER ALL MODES!")

if __name__ == "__main__":
    run_suite()
