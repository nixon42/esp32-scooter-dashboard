#!/usr/bin/env python3
import os
import manifold3d as m3d
import trimesh
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

def cylinder(height, radius, center=False):
    return m3d.Manifold.cylinder(height, radius, radius, circular_segments=48, center=center)

def teardrop_hole_x_apex_minus_z(length, radius, num_segments=36):
    pts = []
    angles = np.linspace(3*np.pi/4, 2*np.pi + np.pi/4, num_segments)
    for a in angles:
        pts.append([radius * np.cos(a), radius * np.sin(a)])
    pts.append([0.0, radius * np.sqrt(2)])
    cs = m3d.CrossSection([pts])
    cyl = cs.extrude(length).translate([0, 0, -length/2])
    return cyl.rotate([-90, 0, 0]).rotate([0, 0, -90])

def render_mesh(ax, mesh, color, light_dir=np.array([0.5, -0.6, 0.8])):
    vertices = mesh.vertices
    faces = mesh.faces
    normals = mesh.face_normals
    light_dir = light_dir / np.linalg.norm(light_dir)
    diffuse = np.clip(np.dot(normals, light_dir), 0.0, 1.0)
    fill = np.clip(np.dot(normals, np.array([-0.4, 0.7, 0.3])), 0.0, 1.0) * 0.35
    intensity = np.clip(diffuse * 0.65 + fill + 0.3, 0.0, 1.0)
    face_colors = np.outer(intensity, color)
    poly = Poly3DCollection(vertices[faces], facecolors=face_colors, edgecolors='none', alpha=1.0)
    ax.add_collection3d(poly)

def main():
    pivot_z = -11.0
    # Single ear model
    head = cylinder(6.8, 11.0, center=True).rotate([0, 90, 0]).translate([-9.5, 0, pivot_z])
    root = m3d.Manifold.cube([6.8, 32.0, 4.0], center=True).translate([-9.5, 0, -2.0])
    ear_solid = (head + root).hull()

    # 1. Round hole (Standard)
    round_hole = cylinder(20.0, 2.2, center=True).rotate([0, 90, 0]).translate([0, 0, pivot_z])
    ear_round = ear_solid - round_hole

    # 2. Teardrop hole (Overhang 45 deg)
    td_hole = teardrop_hole_x_apex_minus_z(20.0, 2.2).translate([0, 0, pivot_z])
    ear_teardrop = ear_solid - td_hole

    mr = trimesh.Trimesh(vertices=ear_round.to_mesh().vert_properties, faces=ear_round.to_mesh().tri_verts, process=True)
    mt = trimesh.Trimesh(vertices=ear_teardrop.to_mesh().vert_properties, faces=ear_teardrop.to_mesh().tri_verts, process=True)

    fig = plt.figure(figsize=(16, 8), dpi=150)
    fig.patch.set_facecolor('#181a1f')
    color = np.array([0.75, 0.78, 0.82])

    ax1 = fig.add_subplot(1, 2, 1, projection='3d')
    ax1.set_facecolor('#181a1f')
    render_mesh(ax1, mr, color, light_dir=np.array([-0.7, 0.2, 0.6]))
    ax1.view_init(elev=0, azim=180) # Straight side view into hole
    ax1.set_xlim([-15, -4])
    ax1.set_ylim([-15, 15])
    ax1.set_zlim([-23, 0])
    ax1.set_title("A. LUBANG BULAT STANDAR (Bisa Meleleh/Droop di Puncak)\nPuncak lengkungan 180° menggantung bebas tanpa tumpuan", color='#ff5252', fontsize=12, pad=10)
    ax1.axis('off')

    ax2 = fig.add_subplot(1, 2, 2, projection='3d')
    ax2.set_facecolor('#181a1f')
    render_mesh(ax2, mt, color, light_dir=np.array([-0.7, 0.2, 0.6]))
    ax2.view_init(elev=0, azim=180) # Straight side view into hole
    ax2.set_xlim([-15, -4])
    ax2.set_ylim([-15, 15])
    ax2.set_zlim([-23, 0])
    ax2.set_title("B. OPSI 1: LUBANG TEARDROP (45° Self-Supporting)\nPuncak segitiga 45° mengarah ke atas cetak — 100% TANPA SUPPORT!", color='#00e676', fontsize=12, pad=10)
    ax2.axis('off')

    plt.suptitle("PERBANDINGAN GEOMETRI LUBANG BAUT M4 ENGSEL (FDM PRINT ORIENTATION)", color='#00e5ff', fontsize=14, fontweight='bold', y=0.96)
    plt.tight_layout()

    out_path = "/run/media/nixon/6fde869e-c24a-48e3-80a4-6e3a943a14db/Project/tmp/esp32-scooter-dashboard/cad/renders/teardrop_hole_comparison.png"
    plt.savefig(out_path, dpi=150, facecolor=fig.get_facecolor(), edgecolor='none')
    print("Saved render:", out_path)

if __name__ == "__main__":
    main()
