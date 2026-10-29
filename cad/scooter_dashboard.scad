// ========================================================
// Parametric Scooter Dashboard Enclosure & Mirror Mount v4.2
// Target 3D Printer: Bambu Lab A1 Mini (PETG Recommended)
// LCD: Nokia 5110 (PCD8544 84x48)
// MCU: ESP32-C3 SuperMini + MP1584EN Step-Down (5V)
// Fasteners: 100% UNIFIED TO M4 BOLTS, NUTS & WASHERS!
// Mount: Scooter Mirror Stalk (M10 Thread)
// Features: Rear Male V-Rail Dovetail, Female Sliding Sensor Pod,
//           Radial Rosette Indexing Teeth (36 teeth @ 10° pitch)
// ========================================================

$fn = 48;

case_w = 58.0;
case_h = 58.0;
case_r = 5.0;
wall_t = 3.2;
floor_t = 3.5;

module rounded_rect(w, h, d, r) {
    hull() {
        translate([-w/2 + r, -h/2 + r, 0]) cylinder(r=r, h=d);
        translate([ w/2 - r, -h/2 + r, 0]) cylinder(r=r, h=d);
        translate([-w/2 + r,  h/2 - r, 0]) cylinder(r=r, h=d);
        translate([ w/2 - r,  h/2 - r, 0]) cylinder(r=r, h=d);
    }
}

// --------------------------------------------------------
// PART 1: FRONT BEZEL (M4 COUNTERBORES)
// --------------------------------------------------------
module front_bezel() {
    difference() {
        union() {
            // Sleek flat bezel face (thickness 6.5mm)
            rounded_rect(case_w, case_h, 6.5, case_r);
            // Interlocking tongue lip (protrudes 2.0mm backward)
            translate([0, 0, -2.0])
                difference() {
                    rounded_rect(53.2, 53.2, 2.0, 3.5);
                    translate([0, 0, -0.5]) rounded_rect(49.6, 49.6, 3.0, 2.5);
                }
        }
        // Single Clean LCD Screen Cutout (38 x 32 mm)
        translate([0, 1.0, -5]) cube([38.0, 32.0, 20], center=true);
        // Inside Acrylic Sheet Pocket (45.5 x 45.5 x 1.5mm deep)
        translate([0, 1.0, 0.75]) rounded_rect(45.5, 45.5, 1.5, 2.0);
        // 4x Corner Screws for M4 (Through-hole dia 4.4mm, Counterbore dia 8.0mm)
        for (sx = [-23, 23]) for (sy = [-23, 23]) {
            translate([sx, sy, -5]) cylinder(r=2.2, h=20);
            translate([sx, sy, 2.7]) cylinder(r=4.0, h=4.0);
        }
    }
}

// --------------------------------------------------------
// PART 2: MAIN HOUSING (M4 PIVOT, V-RAIL & M4 CORNER NUTS)
// --------------------------------------------------------
module main_housing() {
    difference() {
        union() {
            // Main Outer Body (Depth 26.0mm, wall 3.2mm)
            rounded_rect(case_w, case_h, 26.0, case_r);
            
            // Sculpted Aerodynamic Rear Clevis Lugs (Hull-blended)
            for (sx = [-9.5, 9.5]) {
                hull() {
                    translate([sx, 0, -11.0]) rotate([0, 90, 0]) cylinder(r=11.0, h=6.8, center=true);
                    translate([sx, 0, -2.0]) cube([6.8, 32.0, 4.0], center=true);
                }
            }

            // Radial Rosette Teeth on Left Clevis Ear Inner Face (X = -6.10mm, 36 teeth @ 10 deg)
            translate([-6.10, 0, -11.0]) rotate([0, 90, 0]) {
                for (i = [0:35]) {
                    rotate([0, 0, i * 10]) {
                        hull() {
                            translate([0, 3.5, 0.65]) cube([0.05, 0.05, 0.05], center=true);
                            translate([0, 9.5, 0.65]) cube([0.05, 0.05, 0.05], center=true);
                            rotate([0, 0, -5]) {
                                translate([0, 3.5, -0.20]) cube([0.05, 0.05, 0.05], center=true);
                                translate([0, 9.5, -0.20]) cube([0.05, 0.05, 0.05], center=true);
                            }
                            rotate([0, 0, 5]) {
                                translate([0, 3.5, -0.20]) cube([0.05, 0.05, 0.05], center=true);
                                translate([0, 9.5, -0.20]) cube([0.05, 0.05, 0.05], center=true);
                            }
                        }
                    }
                }
            }

            // Rear Male V-Rail (Dovetail) with 45° Undercut Wings
            // Root width 6.0mm at Z=0, Crown width 11.0mm at Z=-2.5mm, Y from -25.0 to -11.0mm
            translate([0, -11.0, 0]) rotate([90, 0, 0]) {
                linear_extrude(14.0) {
                    polygon(points=[[-3.0, 0.0], [-5.5, -2.5], [5.5, -2.5], [3.0, 0.0]]);
                }
            }
            // End-stop shoulder at Y = -11.0mm
            translate([-6.0, -11.0, -2.5]) cube([12.0, 1.5, 2.5]);
            // 0.38mm tactile snap-detent bump at Y = -14.0mm
            translate([0, -14.0, -2.5 + (1.2 - 0.38)]) rotate([0, 90, 0]) cylinder(r=1.2, h=7.0, center=true);

            // Center Isolation Barrier (12V buck vs 3.3V ESP32)
            translate([0, 0.0, floor_t + 4.5]) cube([2.5, 36.0, 9.0], center=true);
            
            // ESP32-C3 SuperMini Tray Compartment (Left)
            translate([-23.5, 0.0, floor_t + 1.75]) cube([2.0, 24.0, 3.5], center=true);
            translate([-12.5, 12.0, floor_t + 1.75]) cube([22.0, 2.0, 3.5], center=true);
            translate([-12.5, -12.0, floor_t + 1.75]) cube([22.0, 2.0, 3.5], center=true);

            // MP1584EN Step-Down Tray Compartment (Right)
            translate([ 23.5, 0.0, floor_t + 1.75]) cube([2.0, 24.0, 3.5], center=true);
            translate([ 12.5, 12.0, floor_t + 1.75]) cube([22.0, 2.0, 3.5], center=true);
            translate([ 12.5, -12.0, floor_t + 1.75]) cube([22.0, 2.0, 3.5], center=true);

            // LCD Nokia 5110 3-Sided Perimeter Shelf (Left, Right, Top)
            translate([-22.25, 0, 16.5]) cube([3.5, 42.0, 6.0], center=true);
            translate([ 22.25, 0, 16.5]) cube([3.5, 42.0, 6.0], center=true);
            translate([0,  22.25, 16.5]) cube([42.0, 3.5, 6.0], center=true);

            // Bottom Cable Gland Collar (dia 12.0mm OD, dia 6.5mm ID) - pristine bottom face!
            translate([0, -case_h/2, 8.5]) rotate([90, 0, 0]) cylinder(r=6.0, h=6.0);
        }
        // Main Interior Cavity (Floor at Z = 3.5mm)
        translate([0, 0, floor_t])
            rounded_rect(case_w - 2*wall_t, case_h - 2*wall_t, 27.0, case_r - 1.5);
        // Top Interlocking Groove for Front Bezel
        translate([0, 0, 23.6])
            difference() {
                rounded_rect(53.6, 53.6, 2.5, 3.5);
                translate([0, 0, -0.5]) rounded_rect(49.2, 49.2, 4.0, 2.2);
            }
        // Single Bottom Cable Gland Hole (dia 6.5mm)
        translate([0, -case_h/2, 8.5]) rotate([90, 0, 0]) cylinder(r=3.25, h=25, center=true);
        // 4x Corner Screws for M4 & M4 Hex Nut Pockets (7.2mm across flats, starting at Z = -1.0mm for flush breakthrough)
        for (sx = [-23, 23]) for (sy = [-23, 23]) {
            translate([sx, sy, -5]) cylinder(r=2.2, h=35);
            translate([sx, sy, -1.0]) cylinder(r=4.16, h=5.2, $fn=6);
        }
        // M4 Pivot Cross-Bolt Hole (dia 4.4mm)
        translate([0, 0, -11.0]) rotate([0, 90, 0]) cylinder(r=2.2, h=40, center=true);
    }
}

// --------------------------------------------------------
// PART 3: 1-PIECE MIRROR BRACKET (ROSETTE SERRATIONS & NUT TRAP)
// --------------------------------------------------------
module mirror_bracket() {
    difference() {
        union() {
            // Reinforced Base Plate (8.5mm thick PETG, M10 mount)
            translate([0, 12.0, 0]) rounded_rect(32.0, 48.0, 8.5, 6.0);
            
            // Continuous Heavy-Duty Arm & Tongue
            hull() {
                translate([0, -3.0, 6.0]) cube([18.0, 18.0, 12.0], center=true);
                translate([0, -11.0, 22.0]) cube([10.95, 16.0, 14.0], center=true);
            }
            hull() {
                translate([0, -11.0, 22.0]) cube([10.95, 16.0, 14.0], center=true);
                translate([0, -18.0, 34.0]) cube([10.95, 16.0, 16.0], center=true);
                translate([-0.30, -22.0, 42.0]) rotate([0, 90, 0]) cylinder(r=11.0, h=11.6, center=true);
            }
            
            // Rear / Underside Stiffening Gusset (Away from M10 hole)
            hull() {
                translate([0, -8.0, 6.0]) cube([10.95, 12.0, 4.0], center=true);
                translate([0, -16.0, 24.0]) cube([10.95, 8.0, 16.0], center=true);
            }
        }
        // M10 Mirror Bolt Hole (dia 10.5mm)
        translate([0, 24.0, -2]) cylinder(r=5.25, h=20);
        // M10 Washer Recess & Full 50mm Vertical Clearance Cylinder (Zero Obstruction!)
        translate([0, 24.0, 6.0]) cylinder(r=11.2, h=50.0);
        // M4 Pivot Hole (dia 4.4mm)
        translate([0, -22.0, 42.0]) rotate([0, 90, 0]) cylinder(r=2.2, h=35, center=true);
        // M4 Hex Nut Pocket on Right Flank (7.2mm across flats)
        translate([1.5, -22.0, 42.0]) rotate([0, 90, 0]) cylinder(r=4.16, h=6.0, $fn=6);
    }
}

// --------------------------------------------------------
// PART 4: DHT11 EXTERNAL POD (FEMALE SLIDING V-RAIL POD)
// --------------------------------------------------------
module dht11_mount() {
    difference() {
        union() {
            // Upper neck (11.8mm width) and lower bay body (20.0mm width)
            translate([0, -13.5, -3.0]) rounded_rect(11.8, 5.0, 6.0, 2.0);
            translate([0, -32.0, -9.0]) rounded_rect(20.0, 32.0, 18.0, 3.0);
        }
        // Female Dovetail Sliding Channel (0.25mm bilateral clearance)
        translate([0, -11.0, 0]) rotate([90, 0, 0]) {
            linear_extrude(16.0) {
                polygon(points=[[-3.25, 0.5], [-6.0, -2.75], [6.0, -2.75], [3.25, 0.5]]);
            }
        }
        // Detent Snap Pocket (0.45mm deep at Y = -14.0mm)
        translate([0, -14.0, -2.95]) cube([8.0, 2.0, 0.45], center=true);
        // Internal DHT11 Sensor Bay (15.5 x 29.0 x 11.5 mm)
        translate([0, -31.0, -16.5]) rounded_rect(15.5, 29.0, 11.5, 1.5);
        // Downward Angled Weather Louvers (-35 deg)
        for (ly = [-38.0, -32.0, -26.0]) {
            translate([0, ly, -16.5]) rotate([-35, 0, 0]) cube([11.0, 2.2, 8.0], center=true);
        }
        // Bottom Cable Exit Hole (dia 5.0mm) aligned with housing cable gland
        translate([0, -47.0, -10.75]) rotate([90, 0, 0]) cylinder(r=2.5, h=12, center=true);
        // Bottom Drainage Hole (dia 2.5mm)
        translate([4.5, -47.0, -14.5]) rotate([90, 0, 0]) cylinder(r=1.25, h=12, center=true);
    }
}

// Preview All 4 Parts Separated
translate([  0,  70, 0]) front_bezel();
translate([  0,   0, 0]) main_housing();
translate([ 70,   0, 0]) mirror_bracket();
translate([-70,   0, 0]) dht11_mount();
