#!/usr/bin/env python3
"""
Adversarial Empirical Test Suite: Rendered 3D CAD Deliverables Metrics & Reproducibility
Author: Challenger CAD M2 1 Gen2

Validates:
1. File sizes strictly > 500 KB and bit-for-bit synchronization to artifact storage
2. Pixel resolutions, aspect ratios, dynamic ranges, and Shannon image entropy
3. Multi-subpanel luminance analysis verifying absence of pitch-black / unlit faces
4. Absence of wireframe mesh artifacts (verifying pure opaque Lambertian solid shading)
5. Watertight planar boolean cut caps quantification in engineering coral
6. Deterministic reproducibility and execution benchmark (< 30 seconds) of cad/render_solid_shaded.py
"""

import hashlib
import os
import subprocess
import time

import numpy as np
import pytest
from PIL import Image

PROJECT_ROOT = "/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/Project/tmp/esp32-scooter-dashboard"
CAD_DIR = os.path.join(PROJECT_ROOT, "cad")
ARTIFACT_DIR = "/home/nixon/.gemini/antigravity/brain/e2bb3c05-9555-48b4-becf-e02993ca0fbe"
RENDER_SCRIPT = os.path.join(CAD_DIR, "render_solid_shaded.py")

DELIVERABLES = [
    "solid_shaded_audit.png",
    "assembly_solid_views.png",
    "assembly_cross_sections.png"
]


def test_deliverable_file_existence_and_sizes():
    """Validates that all 3 deliverables exist in both cad/ and brain/ and strictly exceed 500 KB."""
    min_size_threshold = 500 * 1024  # 512,000 bytes (500 KB)

    for fname in DELIVERABLES:
        cad_path = os.path.join(CAD_DIR, fname)
        art_path = os.path.join(ARTIFACT_DIR, fname)

        assert os.path.isfile(cad_path), f"Missing CAD deliverable: {cad_path}"
        assert os.path.isfile(art_path), f"Missing artifact copy: {art_path}"

        cad_size = os.path.getsize(cad_path)
        art_size = os.path.getsize(art_path)

        assert cad_size > min_size_threshold, (
            f"{fname} size {cad_size} bytes ({cad_size/1024:.1f} KB) fails threshold (> 500 KB)"
        )
        assert art_size > min_size_threshold, (
            f"Artifact {fname} size {art_size} bytes fails threshold (> 500 KB)"
        )

        # Bit-for-bit consistency between cad/ and artifact dir
        with open(cad_path, "rb") as f_cad, open(art_path, "rb") as f_art:
            cad_hash = hashlib.sha256(f_cad.read()).hexdigest()
            art_hash = hashlib.sha256(f_art.read()).hexdigest()

        assert cad_hash == art_hash, f"Hash mismatch between cad/ and artifact dir for {fname}"


def test_deliverable_resolutions_and_aspect_ratios():
    """Validates high-resolution dimensions, channels, and aspect ratios."""
    expected_metrics = {
        "solid_shaded_audit.png": {
            "min_w": 3500, "min_h": 1700, "aspect_range": (1.8, 2.2)
        },
        "assembly_solid_views.png": {
            "min_w": 2800, "min_h": 2700, "aspect_range": (0.9, 1.1)
        },
        "assembly_cross_sections.png": {
            "min_w": 4000, "min_h": 1300, "aspect_range": (2.8, 3.2)
        }
    }

    for fname, exp in expected_metrics.items():
        img_path = os.path.join(CAD_DIR, fname)
        img = Image.open(img_path)
        w, h = img.size
        aspect = w / h

        assert img.mode in ("RGB", "RGBA"), f"Unexpected mode {img.mode} for {fname}"
        assert w >= exp["min_w"], f"{fname} width {w} < {exp['min_w']}"
        assert h >= exp["min_h"], f"{fname} height {h} < {exp['min_h']}"
        assert exp["aspect_range"][0] <= aspect <= exp["aspect_range"][1], (
            f"{fname} aspect ratio {aspect:.3f} outside expected {exp['aspect_range']}"
        )


def test_deliverable_entropy_and_global_dynamic_range():
    """Computes Shannon image entropy and global dynamic range across all 3 deliverables."""
    for fname in DELIVERABLES:
        img_path = os.path.join(CAD_DIR, fname)
        img = Image.open(img_path)
        rgb = np.array(img)[:, :, :3]

        # Calculate luminance (ITU-R BT.601)
        lum = 0.299 * rgb[:, :, 0] + 0.587 * rgb[:, :, 1] + 0.114 * rgb[:, :, 2]
        dyn_range = lum.max() - lum.min()

        # Global dynamic range must be full span (0 to 255 due to white background and shaded foreground)
        assert dyn_range >= 200.0, f"{fname} dynamic range span {dyn_range:.1f} < 200.0"

        # Shannon Entropy across 256 intensity bins
        hist, _ = np.histogram(rgb.flatten(), bins=256, range=(0, 256), density=True)
        hist = hist[hist > 0]
        entropy = -np.sum(hist * np.log2(hist))
        assert entropy > 1.5, f"{fname} image entropy {entropy:.3f} too low (< 1.5 bits/channel)"


def test_subpanel_illumination_no_pitch_black_surfaces():
    """
    Evaluates individual subpanels in all deliverables to verify absence of pitch-black unlit surfaces.
    Camera-relative lighting with ambient=0.25 guarantees foreground model pixels have L >= 10.0.
    """
    subpanel_specs = [
        # (filename, rows, cols, title_y_pct, bottom_trim_pct)
        ("solid_shaded_audit.png", 2, 4, 0.16, 0.04),
        ("assembly_solid_views.png", 2, 2, 0.14, 0.02),
        ("assembly_cross_sections.png", 1, 3, 0.14, 0.02),
    ]

    for fname, rows, cols, title_trim, bottom_trim in subpanel_specs:
        img_path = os.path.join(CAD_DIR, fname)
        img = Image.open(img_path)
        rgb = np.array(img)[:, :, :3]
        h, w, _ = rgb.shape

        y_start = int(0.08 * h)
        grid_y = np.linspace(y_start, h, rows + 1, dtype=int)
        grid_x = np.linspace(0, w, cols + 1, dtype=int)

        for r in range(rows):
            for c in range(cols):
                y0, y1 = grid_y[r], grid_y[r+1]
                x0, x1 = grid_x[c], grid_x[c+1]
                sub = rgb[y0:y1, x0:x1]
                sub_h = y1 - y0

                # Crop strictly to 3D model viewport (excluding title text and row boundary labels)
                y_crop_start = int(title_trim * sub_h)
                y_crop_end = int((1.0 - bottom_trim) * sub_h)
                model_zone = sub[y_crop_start:y_crop_end, :]

                # Foreground: non-background pixels (background is white 255,255,255)
                is_fg = (model_zone[:, :, 0] < 245) | (model_zone[:, :, 1] < 245) | (model_zone[:, :, 2] < 245)
                fg_pixels = model_zone[is_fg]

                assert len(fg_pixels) > 50_000, (
                    f"{fname} cell ({r},{c}) has too few foreground pixels ({len(fg_pixels)})"
                )

                fg_lum = 0.299 * fg_pixels[:, 0] + 0.587 * fg_pixels[:, 1] + 0.114 * fg_pixels[:, 2]

                # Strict test: zero unlit pitch-black pixels (< 5.0 luminance) on the 3D model
                pitch_black_count = np.sum(fg_lum < 5.0)
                assert pitch_black_count == 0, (
                    f"{fname} cell ({r},{c}) has {pitch_black_count} pitch-black unlit pixels!"
                )

                # Ensure minimum foreground luminance is well-supported by ambient/fill light
                assert fg_lum.min() >= 10.0, (
                    f"{fname} cell ({r},{c}) minimum luminance {fg_lum.min():.1f} < 10.0"
                )

                # Ensure good contrast / dynamic spread across each panel
                panel_spread = fg_lum.max() - fg_lum.min()
                assert panel_spread >= 150.0, (
                    f"{fname} cell ({r},{c}) insufficient luminance spread ({panel_spread:.1f})"
                )


def test_wireframe_elimination_and_solid_shading():
    """
    Confirms that renders have eliminated wireframe meshes (edgecolor='none', alpha=1.0).
    A wireframe render is characterized by pitch-black edge lines (L < 5.0) tracing triangle boundaries.
    Evaluates 3D model viewports across all panels, excluding matplotlib text annotation zones.
    """
    subpanel_specs = [
        # (filename, rows, cols, title_y_pct, bottom_trim_pct)
        ("solid_shaded_audit.png", 2, 4, 0.16, 0.05),
        ("assembly_solid_views.png", 2, 2, 0.14, 0.03),
        ("assembly_cross_sections.png", 1, 3, 0.14, 0.03),
    ]

    for fname, rows, cols, title_trim, bottom_trim in subpanel_specs:
        img_path = os.path.join(CAD_DIR, fname)
        img = Image.open(img_path)
        arr = np.array(img)
        rgb = arr[:, :, :3]
        h, w, _ = rgb.shape

        # Verify alpha channel is fully opaque
        if arr.shape[2] == 4:
            alpha = arr[:, :, 3]
            assert np.all(alpha == 255), f"{fname} has transparent/translucent pixels in alpha channel!"

        y_start = int(0.08 * h)
        grid_y = np.linspace(y_start, h, rows + 1, dtype=int)
        grid_x = np.linspace(0, w, cols + 1, dtype=int)

        total_fg_pixels = 0
        total_black_wireframe_pixels = 0

        for r in range(rows):
            for c in range(cols):
                y0, y1 = grid_y[r], grid_y[r+1]
                x0, x1 = grid_x[c], grid_x[c+1]
                sub = rgb[y0:y1, x0:x1]
                sub_h = y1 - y0

                y_crop_start = int(title_trim * sub_h)
                y_crop_end = int((1.0 - bottom_trim) * sub_h)
                model_zone = sub[y_crop_start:y_crop_end, :]

                is_fg = (model_zone[:, :, 0] < 245) | (model_zone[:, :, 1] < 245) | (model_zone[:, :, 2] < 245)
                fg_px = model_zone[is_fg]

                if len(fg_px) > 0:
                    lum = 0.299 * fg_px[:, 0] + 0.587 * fg_px[:, 1] + 0.114 * fg_px[:, 2]
                    black_px = np.sum(lum < 5.0)
                    total_fg_pixels += len(fg_px)
                    total_black_wireframe_pixels += black_px

        assert total_fg_pixels > 200_000, f"{fname} has too few foreground model pixels ({total_fg_pixels})"
        assert total_black_wireframe_pixels == 0, (
            f"{fname} contains {total_black_wireframe_pixels} black edge pixels on 3D models indicative of wireframe!"
        )


def test_cross_sections_coral_capped_cutaways():
    """
    Verifies that Deliverable 3 (assembly_cross_sections.png) contains prominent,
    watertight planar capped cut faces rendered in High-Contrast Engineering Coral (#f97316).
    """
    img_path = os.path.join(CAD_DIR, "assembly_cross_sections.png")
    img = Image.open(img_path)
    rgb = np.array(img)[:, :, :3]
    _h, w, _ = rgb.shape

    # Coral color is [0.95, 0.45, 0.15] modulated by lighting:
    # High red (>140), medium green (<160), low blue (<90), R > 1.3 * G
    coral_mask = (rgb[:, :, 0] > 140) & (rgb[:, :, 1] < 160) & (rgb[:, :, 2] < 90) & (rgb[:, :, 0] > 1.3 * rgb[:, :, 1])

    total_coral = np.sum(coral_mask)
    assert total_coral > 300_000, f"Expected >300,000 coral section pixels, found {total_coral:,}"

    # Check each of the 3 cut panels individually
    grid_x = np.linspace(0, w, 4, dtype=int)
    for i in range(3):
        x0, x1 = grid_x[i], grid_x[i+1]
        p_coral = np.sum(coral_mask[:, x0:x1])
        assert p_coral > 100_000, (
            f"Panel {i+1} has insufficient coral section cap pixels ({p_coral:,} < 100,000)"
        )


def test_deterministic_reproducibility_and_benchmark():
    """
    Executes cad/render_solid_shaded.py directly, benchmarks execution time (< 30 seconds),
    and proves deterministic bit-for-bit reproducibility via SHA-256 hash comparison.
    """
    # 1. Compute initial hashes
    initial_hashes = {}
    for fname in DELIVERABLES:
        with open(os.path.join(CAD_DIR, fname), "rb") as f:
            initial_hashes[fname] = hashlib.sha256(f.read()).hexdigest()

    # 2. Run render script and measure execution duration
    t_start = time.perf_counter()
    cmd = [
        "uv", "run",
        "--with", "manifold3d",
        "--with", "trimesh",
        "--with", "matplotlib",
        "--with", "pillow",
        "--with", "scipy",
        "python", RENDER_SCRIPT
    ]
    proc = subprocess.run(cmd, cwd=PROJECT_ROOT, capture_output=True, text=True, check=False)
    t_elapsed = time.perf_counter() - t_start

    assert proc.returncode == 0, f"render_solid_shaded.py failed with stderr:\n{proc.stderr}"
    assert t_elapsed < 30.0, f"Execution time {t_elapsed:.2f}s exceeded 30.0s threshold!"

    # 3. Check regenerated hashes
    for fname in DELIVERABLES:
        with open(os.path.join(CAD_DIR, fname), "rb") as f:
            new_hash = hashlib.sha256(f.read()).hexdigest()
        assert new_hash == initial_hashes[fname], (
            f"Non-deterministic render detected for {fname}! Hash changed: {initial_hashes[fname]} -> {new_hash}"
        )


if __name__ == "__main__":
    pytest.main(["-v", __file__])
