import manifold3d as m3d
import trimesh
import numpy as np

def make_radial_teeth(num_teeth=36, r_in=3.2, r_out=10.0, tooth_h=0.7):
    """
    Creates a radial rosette spline (indexing teeth) around the Z axis.
    Each tooth is a triangular wedge radiating from r_in to r_out.
    """
    wedge_angle = 360.0 / num_teeth
    teeth = []
    
    # 2D cross section of a single wedge along radius
    # In R-Z plane: tooth peaks at center of angle, troughs at boundaries
    for i in range(num_teeth):
        ang_deg = i * wedge_angle
        # A triangular tooth modeled as a tapered prism/hull
        # Inner apex and base
        a_mid = np.radians(ang_deg)
        a_left = np.radians(ang_deg - wedge_angle / 2)
        a_right = np.radians(ang_deg + wedge_angle / 2)
        
        # Vertices of the tooth prism
        # Apex line: r_in to r_out at z = tooth_h
        # Valley lines: z = 0 at left and right angles
        p_in_apex = [r_in * np.cos(a_mid), r_in * np.sin(a_mid), tooth_h]
        p_out_apex = [r_out * np.cos(a_mid), r_out * np.sin(a_mid), tooth_h]
        
        p_in_l = [r_in * np.cos(a_left), r_in * np.sin(a_left), 0.0]
        p_out_l = [r_out * np.cos(a_left), r_out * np.sin(a_left), 0.0]
        
        p_in_r = [r_in * np.cos(a_right), r_in * np.sin(a_right), 0.0]
        p_out_r = [r_out * np.cos(a_right), r_out * np.sin(a_right), 0.0]
        
        # We can construct the tooth via convex hull of these 6 points!
        # In manifold3d, points -> Mesh -> Manifold -> hull()
        pts = np.array([p_in_apex, p_out_apex, p_in_l, p_out_l, p_in_r, p_out_r], dtype=np.float32)
        # 8 faces of a triangular prism
        faces = np.array([
            [0, 2, 4], # inner triangle
            [1, 5, 3], # outer triangle
            [0, 1, 3], [0, 3, 2], # left slope
            [0, 4, 5], [0, 5, 1], # right slope
            [2, 3, 5], [2, 5, 4]  # bottom
        ], dtype=np.uint32)
        tooth_mesh = m3d.Mesh(np.ascontiguousarray(pts), np.ascontiguousarray(faces))
        teeth.append(m3d.Manifold(tooth_mesh))
        
    rosette = m3d.Manifold.compose(teeth)
    # Cut through center hole for M4 bolt (dia 4.4)
    hole = m3d.Manifold.cylinder(tooth_h * 4, 2.2, 2.2, circular_segments=36, center=True)
    return rosette - hole

rosette = make_radial_teeth(36, 3.0, 10.0, 0.7)
print("Rosette volume:", round(rosette.volume(), 2), "mm^3")
assert rosette.volume() > 0
print("Rosette generated successfully!")
