#!/usr/bin/env python3
import os
import manifold3d as m3d
import trimesh
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

def render_mesh(ax, mesh, color, light_dir=np.array([0.5, -0.6, 0.8]), alpha=1.0):
    vertices = mesh.vertices
    faces = mesh.faces
    normals = mesh.face_normals
    light_dir = light_dir / np.linalg.norm(light_dir)
    diffuse = np.clip(np.dot(normals, light_dir), 0.0, 1.0)
    fill = np.clip(np.dot(normals, np.array([-0.4, 0.7, 0.3])), 0.0, 1.0) * 0.35
    intensity = np.clip(diffuse * 0.65 + fill + 0.3, 0.0, 1.0)
    face_colors = np.outer(intensity, color)
    poly = Poly3DCollection(vertices[faces], facecolors=face_colors, edgecolors='none', alpha=alpha)
    ax.add_collection3d(poly)

def main():
    mh = trimesh.load("cad/main_housing.stl")
    mb = trimesh.load("cad/mirror_bracket.stl")

    fig = plt.figure(figsize=(18, 9), dpi=150)
    fig.patch.set_facecolor('#181a1f')

    C_HOUSING = np.array([0.34, 0.40, 0.48])
    C_BRACKET = np.array([0.20, 0.22, 0.26])

    # PANEL 1: Main Housing in print orientation (Front rim on bed, clevis ears up)
    ax1 = fig.add_subplot(1, 2, 1, projection='3d')
    ax1.set_facecolor('#181a1f')

    # Rotate housing so front rim Z=26 is on bed (Z_bed = 0)
    # Z_rot = 26 - Z
    mh_print = mh.copy()
    mh_print.apply_transform([
        [1,  0,  0, 0],
        [0,  1,  0, 0],
        [0,  0, -1, 26],
        [0,  0,  0, 1]
    ])
    render_mesh(ax1, mh_print, C_HOUSING, light_dir=np.array([-0.4, -0.7, 0.6]))
    ax1.view_init(elev=20, azim=-140)
    ax1.set_xlim([-35, 35])
    ax1.set_ylim([-38, 32])
    ax1.set_zlim([0, 50])
    ax1.set_title("1. MAIN HOUSING (Orientasi Cetak di Bed Bambu A1 Mini)\nLubang Engsel Teardrop 45° mengarah ke atas — 100% BEBAS SUPPORT!", color='#00e676', fontsize=12, pad=10)
    ax1.axis('off')

    # PANEL 2: Mirror Bracket in print orientation (Base plate on bed)
    ax2 = fig.add_subplot(1, 2, 2, projection='3d')
    ax2.set_facecolor('#181a1f')
    render_mesh(ax2, mb, C_BRACKET, light_dir=np.array([-0.6, -0.5, 0.7]))
    ax2.view_init(elev=25, azim=-45)
    ax2.set_xlim([-25, 25])
    ax2.set_ylim([-35, 40])
    ax2.set_zlim([0, 60])
    ax2.set_title("2. MIRROR BRACKET (Orientasi Cetak di Bed)\nLubang Lidah Teardrop 45° mengarah ke atas — Baut M4 Pas Presisi!", color='#00e676', fontsize=12, pad=10)
    ax2.axis('off')

    plt.suptitle("IMPLEMENTASI LUBANG TEARDROP 45° SELF-SUPPORTING (FDM OPTIMIZED)", color='#00e5ff', fontsize=15, fontweight='bold', y=0.96)
    plt.tight_layout()

    out_path = "cad/renders/teardrop_holes_implemented.png"
    plt.savefig(out_path, dpi=160, facecolor=fig.get_facecolor(), edgecolor='none')
    print("Saved:", out_path)

if __name__ == "__main__":
    main()
