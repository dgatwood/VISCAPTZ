plate_1_translate = [0, 0, 8];
plate_2_translate = [90, 0, 0];
// plate_3_translate = [0, 0, 0];
plate_3_translate = [-60, -60, -8];
circle_facets = 40;
clamp_hole_size = 6.25;  // Measured at 6, but we lose about .5mm from PETG bleed.
hex_nut_size = 3.45;  // Measured at 6.35 tip-to-tip (5.56 across), but we lose slightly more than .5mm from PETG bleed.

module rotate_around_center_point(angles, center_point) {
    translate(center_point)
        rotate(angles)
            translate(-center_point)
                children();   
}

// Encoder details:
// Body is 38mm wide (measured), 39mm per spec.
// Raised ring around shaft is 20mm wide.
// Shaft is 6mm across.
// Three holes in body at equal intervals on a 30mm circle.
// Uses 3mm screws.
// Edge is 2.5mm in from edge of body.

// Uncomment to generate 2D projection for printing a flat screw template
// projection()
union() {
    // Plate 1: Replacement motor mount.
    translate(plate_1_translate) rotate([0, 180, 0]) difference() {
        union() {
            // Square part of plate 1
            cube([41, 36, 8]);
            translate([0, 18, 4]) cylinder(h=8, r=18, center=true, $fn = circle_facets);

            // Expansion of plate to hold encoder post
            translate([19, -74, 0]) cube([22, 75, 8]);

            // Post for encoder
            // 11.8 width
            // 15 down
            // 40 across
            translate([32.5, -66.5, -1.05]) cylinder(h=18, r=5.95, center=true, $fn = circle_facets);

            // Extra tip for fastening spring
            translate([-26, 18, 4]) cylinder(h=8, r=6, center=true, $fn = circle_facets);
            translate([-26, 12, 0]) cube([10, 12, 8]);
        }
        union() {  // Holes in plate 1
            // Motor hole
            translate([0, 18, -10]) cylinder(h = 100, r = 10, $fn = circle_facets);

            rotate_around_center_point([0, 0, 45], [0, 18, -10]) {
                // Motor screw hole #1
                rotate([0, 0, 0]) {
                    translate([0, 4.0, -10]) cylinder(h = 100, r = 1.6, $fn = circle_facets);
                    translate([0, 4.0, 6]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

                // Motor screw hole #2
                    translate([0, 32.0, -10]) cylinder(h = 100, r = 1.6, $fn = circle_facets);
                    translate([0, 32.0, 6]) cylinder(h = 5.01, r = 3, $fn = circle_facets);
                }
            }

            // ---- Clamp holes ----

            // Clamp (post) hole
            translate([32.5, 8.5, -10]) cylinder(h = 100, r = clamp_hole_size, $fn = circle_facets);

            // Clamp gap
            translate([31, 13.5, -1]) cube([3, 23, 10]);

            // Clamp screw hole M6 (unthreaded, 6.1mm)
            translate([32.5, 28.95, 4]) rotate([0,90,0]) cylinder(h = 100, r = 3.1, $fn = circle_facets);

            // Clamp screw hole M6 (threaded, 5mm)
            translate([21, 29, 4]) rotate([0,90,0]) cylinder(h = 100, r = 2.5, $fn =
 circle_facets);
 
             // ---- Spring hole ----
            translate([-26, 18, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
            translate([-26, 18, 5.3]) rotate([0, 0, 30]) cylinder(h = 5.01, r = hex_nut_size, $fn = 6);
        }; // Holes in plate 1
    };


    // Plate 2: Encoder mount.
    translate(plate_2_translate) rotate([0, 0, 0]) difference() {
        union() {  // Encoder mount plate
            // Lower square part of plate
            translate([-20, -3, 0]) cube([31, 42, 8]);

            // Round part of plate
            translate([-20, 18, 4]) cylinder(h=8, r=21, center=true, $fn = circle_facets);
            
            // Extra tip for fastening spring
            translate([-44, 18, 4]) cylinder(h=8, r=10, center=true, $fn = circle_facets);
        }
        union() {  // Encoder mount plate holes
            // Encoder hole
            translate([-20, 18, -10]) cylinder(h = 100, r = 10.25, $fn = circle_facets);

            // Encoder body safety hole,  Delete after checking.
            // translate([-20, 18, 8]) cylinder(h = 100, r = 19, $fn = circle_facets);

            // Encoder screw ring - delete when done
            // translate([-20, 18, 6]) cylinder(h = 8, r = 15, $fn = circle_facets);

            // Center of plate is at 18mm
            // Screw circle is 15mm radius
            // Edge of circle is at 33
            // This is 270 degrees from the X axis.

            // ---- Encoder screw holes ----

            // Encoder screw hole #1
            translate([-20, 3.5, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
            translate([-20, 3.5, 6]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

            // Encoder screw hole #2
            // 150 degrees from x axis: x = -12.99, y = 7.5
            translate([-7.01, 25.5, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
            translate([-7.01, 25.5, 6]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

            // Encoder screw hole #3
            // 30 degrees from x axis:  x = 12.99, y = 7.5
            translate([-32.99, 25.5, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
            translate([-32.99, 25.5, 6]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

            // ---- Screw holes between parts ----

            // Screw hole #4 (through hole between parts)
            translate([5, 33, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);

            // Screw hole #4 (hex nut) 2.54mm thick, but allow 2.7 for bleed.
            translate([5, 33, 5.3]) cylinder(h = 5.01, r = hex_nut_size, $fn = 6);

            // Screw hole #5 (through hole between parts)
            translate([5, 3, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);

            // Screw hole #5 (hex nut) 2.54mm thick, but allow 2.7 for bleed.
            translate([5, 3, 5.3]) cylinder(h = 5.01, r = hex_nut_size, $fn = 6);
            
            // ---- Spring hole ----
            translate([-44, 18, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
            translate([-44, 18, 4]) rotate([0, 0, 30]) cylinder(h = 5.01, r = hex_nut_size, $fn = 6);
            
            // ---- Position badge ----
            // translate([5, 18, 4]) rotate([0, 0, 90]) linear_extrude(height = 5) { text("PAN", size = 7, font = "Verdana", halign = "center", valign = "center", $fn = 16); }
            translate([5, 18, 4]) rotate([0, 0, 90]) linear_extrude(height = 5) { text("TILT", size = 7, font = "Verdana", halign = "center", valign = "center", $fn = 16); }
        }  // Encoder mount plate holes
    };

    // Plate 3: Post-hole-gripping piece.
    // Initially positioned atop plate 2, then transformed into printing position
    // by plate_3_translate.
    translate(plate_2_translate) union()  {
        translate(plate_3_translate) translate([-.8, 25.6, 0]) rotate([0, 0, -45]) difference() {  // Clamp part of plate 3
            // Plate for clamp part of plate 3
            translate([20, -3, 8]) cube([41, 42, 8]);

            union() {  // Clamp holes in clamp part of plate 3
                // Clamp (post) hole
                translate([52.5, 8.5, -10]) cylinder(h = 100, r = clamp_hole_size, $fn = circle_facets);

                // Clamp gap
                translate([51, 13.5, -1]) cube([3, 31, 18]);

                // Clamp screw hole M6 (unthreaded, 6.1mm)
                translate([52.5, 28.95, 12]) rotate([0,90,0]) cylinder(h = 100, r = 3.1, $fn = circle_facets);

                // Clamp screw hole M6 (threaded, 5mm)
                translate([41, 29, 12]) rotate([0,90,0]) cylinder(h = 100, r = 2.5, $fn = circle_facets);
            }  // Plate 3 clamp holes
        };  // Clamp part of plate 3

        rotate([0, 0, 0]) difference() {
            // Through-screw part of plate
            translate(plate_3_translate) translate([0, -3, 8]) cube([41, 42, 8]);

            // Screw holes
            union() {
                // Screw hole #4 (head bore)
                translate(plate_3_translate) translate([5, 33, 14]) cylinder(h = 5.01, r = 3, $fn = circle_facets);
                // Screw hole #4 (main bore between parts)
                translate(plate_3_translate) translate([5, 33, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);

                // Screw hole #5 (head bore)
                translate(plate_3_translate) translate([5, 3, 14]) cylinder(h = 5.01, r = 3, $fn = circle_facets);
                // Screw hole #5 (main bore between parts)
                translate(plate_3_translate) translate([5, 3, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
            };
            translate(plate_3_translate) translate([32, -5, 4]) cube([10, 10, 16]);
        };
    };  // Plate 3
}; // All parts
