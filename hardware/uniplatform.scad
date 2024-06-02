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

module plate(spacing, title) {
    difference() {
        union() {
            // Square part of plate 1
            cube([41, 36, 8]);
            translate([0, 18, 4]) cylinder(h=8, r=18, center=true, $fn = circle_facets);

            // Expansion of plate to hold encoder post
            translate([19, -44, 0]) cube([22, 45, 8]);
            // translate([19, -62.5, 0]) cylinder(h = 5, r = 10.25, center=false);
        };
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
//             translate([-26, 18, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
//             translate([-26, 18, 5.3]) rotate([0, 0, 30]) cylinder(h = 5.01, r = hex_nut_size, $fn = 6);
        }; // Holes in plate 1
       
        // ---- Position badge ----
        // translate([5, 18, 4]) rotate([0, 0, 90]) linear_extrude(height = 5) { text("PAN", size = 7, font = "Verdana", halign = "center", valign = "center", $fn = 16); }
        translate([30, -20, 4]) rotate([0, 0, 90]) linear_extrude(height = 5) { text(title, size = 7, font = "Verdana", halign = "center", valign = "center", $fn = 16); }
    };


    // Plate 2: Encoder mount.

    translate([39, -spacing, 0]) rotate([0, 0, 0]) difference() {
        union() {  // Encoder mount plate
            // Lower square part of plate
            translate([-20, -3, 0]) cube([22, 42, 8]);

            // Round part of plate
            translate([-20, 18, 4]) cylinder(h=8, r=21, center=true, $fn = circle_facets);        
        }
        union() {  // Encoder mount plate holes
            // Encoder hole
            translate([-20, 18, -10]) cylinder(h = 100, r = 10.5, $fn = circle_facets);

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
            translate([-20, 3.5, -10]) cylinder(h = 100, r = 1.6, $fn = circle_facets);
            translate([-20, 3.5, 6]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

            // Encoder screw hole #2
            // 150 degrees from x axis: x = -12.99, y = 7.5
            translate([-7.01, 25.5, -10]) cylinder(h = 100, r = 1.6, $fn = circle_facets);
            translate([-7.01, 25.5, 6]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

            // Encoder screw hole #3
            // 30 degrees from x axis:  x = 12.99, y = 7.5
            translate([-32.99, 25.5, -10]) cylinder(h = 100, r = 1.6, $fn = circle_facets);
            translate([-32.99, 25.5, 6]) cylinder(h = 5.01, r = 3, $fn = circle_facets);
        }  // Encoder mount plate holes
    };
};


// 76 is ~2mm too long for tilt.
// 76 is ~2mm too short for pan.
// 73.5 is ~2mm  too short for both.

// For tilt, try 74.75,
// For pan, try 78mm.  Just barely too short.  Will stretch enough to barely go on.
// Going with 79mm.

// Uncomment to generate 2D projection for printing a flat screw template
// projection()
translate([-20, -20, 0]) rotate([0, 0, 180]) plate(79, "PAN");

// Uncomment to generate 2D projection for printing a flat screw template
// projection()
translate([20, 20, 0]) plate(74.75, "TILT");

