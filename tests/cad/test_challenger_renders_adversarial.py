#!/usr/bin/env python3
"""
Adversarial Empirical Verification Suite for Challenger 2
Render Verification & Visual Artifacts Audit (R6)
"""

import ast
import os
import numpy as np
import pytest
import trimesh
from PIL import Image

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CAD_DIR = os.path.join(PROJECT_ROOT, "cad")
RENDERS_DIR = os.path.join(CAD_DIR, "renders")
RENDER_SCRIPT = os.path.join(CAD_DIR, "render_audit_cutaways.py")
GENERATE_SCRIPT = os.path.join(CAD_DIR, "generate_cad.py")

DELIVERABLE_SPECS = {
    "interior_cavity_cutaway.png": {
        "min_size_bytes": 100_000,
        "min_width": 2500,
        "min_height": 2000,
        "aspect_range": (1.10, 1.45),
        "panel_layout": (2, 2),  # 2 rows x 2 cols
        "min_coral_pixels": 40_000,
        "required_terms": [
            "Nokia 5110 Shelf",
            "ESP32-C3 Bay",
            "MP1584EN Bay",
            "Center Isolation Barrier",
            "Zip-Tie Anchor Bridge",
            "Cable Gland Collar",
            "wire_12v_p",
            "wire_3v3",
            "wire_spi",
            "wire_dht"
        ]
    },
    "assembly_cross_sections_xzyz.png": {
        "min_size_bytes": 100_000,
        "min_width": 3000,
        "min_height": 1200,
        "aspect_range": (2.10, 2.60),
        "panel_layout": (1, 3),  # 1 row x 3 cols
        "min_coral_pixels": 150_000,
        "required_terms": [
            "Sagittal Cross-Section",
            "Transverse Cross-Section",
            "Labyrinth Joint",
            "Acrylic Pocket",
            "Side Walls",
            "Center Barrier",
            "Corner Fastener Stack",
            "Bezel Counterbore",
            "Rear M4 Nut Pocket"
        ]
    },
    "mated_assembly_dovetail_hinge.png": {
        "min_size_bytes": 100_000,
        "min_width": 2500,
        "min_height": 2000,
        "aspect_range": (1.05, 1.45),
        "panel_layout": (2, 2),  # 2 rows x 2 cols
        "min_coral_pixels": 3_000,
        "required_terms": [
            "Mated 4-Part Assembly",
            "Rear Dovetail Sensor Pod",
            "Snap-Detent",
            "Clean Bottom Face Entry",
            "36 Radial Indexing Teeth",
            "Mating Female Serrations",
            "0.60mm Clamping Gap"
        ]
    }
}


def test_render_file_metrics():
    """Verify file existence, exact size > 100KB, header magic, and non-corruption."""
    for fname, spec in DELIVERABLE_SPECS.items():
        fpath = os.path.join(RENDERS_DIR, fname)
        assert os.path.isfile(fpath), f"Missing file: {fpath}"
        size = os.path.getsize(fpath)
        assert size > spec["min_size_bytes"], f"{fname} size {size} <= {spec['min_size_bytes']}"

        # PNG Header Check
        with open(fpath, "rb") as f:
            header = f.read(8)
            assert header == b"\x89PNG\r\n\x1a\n", f"Invalid PNG magic in {fname}"

        # PIL integrity verification
        img = Image.open(fpath)
        img.verify()

        # Reopen to test load
        img = Image.open(fpath)
        w, h = img.size
        assert w >= spec["min_width"], f"{fname} width {w} < {spec['min_width']}"
        assert h >= spec["min_height"], f"{fname} height {h} < {spec['min_height']}"
        aspect = w / h
        assert spec["aspect_range"][0] <= aspect <= spec["aspect_range"][1], (
            f"{fname} aspect {aspect:.3f} not in {spec['aspect_range']}"
        )


def test_panel_quadrant_occupancy():
    """Verify that every panel defined in panel_layout contains real non-background render content."""
    for fname, spec in DELIVERABLE_SPECS.items():
        fpath = os.path.join(RENDERS_DIR, fname)
        img = Image.open(fpath)
        arr = np.array(img)
        h, w = arr.shape[:2]
        rows, cols = spec["panel_layout"]

        row_h = h // rows
        col_w = w // cols

        for r in range(rows):
            for c in range(cols):
                sub = arr[r * row_h : (r + 1) * row_h, c * col_w : (c + 1) * col_w, :3]
                # Background is white (R>245, G>245, B>245)
                is_bg = (sub[:, :, 0] > 245) & (sub[:, :, 1] > 245) & (sub[:, :, 2] > 245)
                content_pixels = np.sum(~is_bg)
                total_pixels = sub.shape[0] * sub.shape[1]
                content_pct = (content_pixels / total_pixels) * 100.0

                # Every panel must have at least 5% non-background content (renders + annotations)
                assert content_pct > 5.0, (
                    f"Panel ({r+1}, {c+1}) of {fname} appears empty! Content pct: {content_pct:.2f}%"
                )


def test_shading_and_zero_wireframe():
    """Verify AST for edgecolors='none', alpha=1.0, and ambient lighting intensity."""
    with open(RENDER_SCRIPT, "r") as f:
        src = f.read()

    tree = ast.parse(src)
    poly_calls = 0
    for node in ast.walk(tree):
        if isinstance(node, ast.Call):
            fname = None
            if isinstance(node.func, ast.Name):
                fname = node.func.id
            elif isinstance(node.func, ast.Attribute):
                fname = node.func.attr
            if fname == "Poly3DCollection":
                poly_calls += 1
                kws = {kw.arg: ast.unparse(kw.value) for kw in node.keywords}
                assert kws.get("edgecolors") in ("'none'", '"none"'), (
                    f"Poly3DCollection at line {node.lineno} does not have edgecolors='none'!"
                )
                assert kws.get("alpha") == "1.0", (
                    f"Poly3DCollection at line {node.lineno} does not have alpha=1.0!"
                )
                assert kws.get("shade") == "False", (
                    f"Poly3DCollection at line {node.lineno} has matplotlib shade=True instead of custom diffuse!"
                )

    assert poly_calls >= 10, f"Found only {poly_calls} Poly3DCollection calls"

    # Check that ambient term is strictly positive (no unlit black collapse)
    assert "0.28" in src, "Ambient base light term 0.28 not found in shader"


def test_coral_section_caps_detection():
    """Verify high-contrast Coral section caps ([250, 97, 51] hue) are present on cutaway surfaces."""
    for fname, spec in DELIVERABLE_SPECS.items():
        fpath = os.path.join(RENDERS_DIR, fname)
        img = Image.open(fpath)
        arr = np.array(img)[:, :, :3]
        r = arr[:, :, 0]
        g = arr[:, :, 1]
        b = arr[:, :, 2]

        # Coral color criteria: Red strong, Green moderate, Blue low, R > 1.3*G, G > 1.2*B
        coral_mask = (r > 155) & (g > 45) & (g < 175) & (b < 105) & (r > 1.25 * g) & (g > 1.15 * b)
        coral_count = np.sum(coral_mask)

        assert coral_count >= spec["min_coral_pixels"], (
            f"{fname} has only {coral_count:,} coral pixels, required at least {spec['min_coral_pixels']:,}"
        )


def test_feature_coverage_string_manifest():
    """Verify that all required terms and annotations are present in render_audit_cutaways.py."""
    with open(RENDER_SCRIPT, "r") as f:
        src = f.read()

    for fname, spec in DELIVERABLE_SPECS.items():
        for term in spec["required_terms"]:
            assert term in src, f"Required term '{term}' missing from render script for {fname}"


def test_stl_physical_geometry_fidelity():
    """Empirically cross-check CAD features against actual STLs."""
    # 1. main_housing.stl
    mh = trimesh.load(os.path.join(CAD_DIR, "main_housing.stl"))
    assert mh.is_watertight, "main_housing.stl is not watertight"
    assert len(mh.split()) == 1, "main_housing.stl is not a single solid component"

    # Bounds check: 58x58 mm outer footprint
    extents = mh.extents
    # Check X extent is ~58mm
    assert 57.5 <= extents[0] <= 58.5, f"Housing X extent {extents[0]} is not 58mm"

    # Check Z-levels for floor (3.5), barrier (12.5), shelf (19.5), rim (26.0)
    up_faces = mh.faces[mh.face_normals[:, 2] > 0.99]
    up_verts = mh.vertices[up_faces]
    z_means = up_verts[:, :, 2].mean(axis=1)

    assert np.any(np.isclose(z_means, 3.5, atol=0.05)), "Floor Z=3.5mm missing"
    assert np.any(np.isclose(z_means, 12.5, atol=0.05)), "Barrier Z=12.5mm missing"
    assert np.any(np.isclose(z_means, 19.5, atol=0.05)), "Shelf Z=19.5mm missing"
    assert np.any(np.isclose(z_means, 26.0, atol=0.05)), "Rim Z=26.0mm missing"

    # 2. front_bezel.stl
    fb = trimesh.load(os.path.join(CAD_DIR, "front_bezel.stl"))
    assert fb.is_watertight, "front_bezel.stl is not watertight"

    # 3. mirror_bracket.stl
    mb = trimesh.load(os.path.join(CAD_DIR, "mirror_bracket.stl"))
    assert mb.is_watertight, "mirror_bracket.stl is not watertight"

    # 4. dht11_mount.stl
    dm = trimesh.load(os.path.join(CAD_DIR, "dht11_mount.stl"))
    assert dm.is_watertight, "dht11_mount.stl is not watertight"


if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"])
