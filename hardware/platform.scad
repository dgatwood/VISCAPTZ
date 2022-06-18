plate_1_translate = [0, 0, 8];
plate_2_translate = [90, 0, 0];
// plate_3_translate = [0, 0, 0];
plate_3_translate = [-60, -60, -8];
circle_facets = 40;
clamp_hole_size = 6.25;  // Measured at 6, but we lose about .5mm from PETG bleed.
hex_nut_size = 3.45;  // Measured at 6.35 tip-to-tip (5.56 across), but we lose slightly more than .5mm from PETG bleed.

union() {
translate(plate_1_translate) rotate([0, 180, 0]) difference() {
    union() {
        // Square part of plate
        cube([41, 36, 8]);
        translate([0, 18, 4]) cylinder(h=8, r=18, center=true, $fn = circle_facets);
        
        // Expansion of plate to hold encoder post
        translate([19, -54, 0]) cube([22, 55, 8]);        

        // Post for encoder
        // 11.8 width
        // 15 down
        // 40 across
        translate([32.5, -46.5, -1.05]) cylinder(h=18, r=5.95, center=true, $fn = circle_facets);
    
    }
    // Motor hole
    translate([0, 18, -10]) cylinder(h = 100, r = 11, $fn = circle_facets);

    // Screw hole #1
    translate([0, 3.5, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
    translate([0, 3.5, 3]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

    // Screw hole #2
    translate([0, 32.5, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
    translate([0, 32.5, 3]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

    // ---- Clamp holes ----

    // Clamp (post) hole
    translate([32.5, 8.5, -10]) cylinder(h = 100, r = clamp_hole_size, $fn = circle_facets);

    // Clamp gap
    translate([31, 13.5, -1]) cube([3, 23, 10]);
    
    // Clamp screw hole M6 (unthreaded, 6.1mm)
    translate([32.5, 28.95, 4]) rotate([0,90,0]) cylinder(h = 100, r = 3.1, $fn = circle_facets);

    // Clamp screw hole M6 (threaded, 5mm)
    translate([21, 29, 4]) rotate([0,90,0]) cylinder(h = 100, r = 2.5, $fn = circle_facets);
};

translate(plate_2_translate) rotate([0, 0, 0]) difference() {
    union() {
        // Upper square part of plate
        translate(plate_3_translate) translate([0, -3, 8]) cube([41, 42, 8]);

        // Lower square part of plate
        translate([-20, -3, 0]) cube([31, 42, 8]);

        // Round part of plate
        translate([-20, 18, 4]) cylinder(h=8, r=21, center=true, $fn = circle_facets);
    }
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

    // Screw hole #1
    translate([-20, 3.5, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
    translate([-20, 3.5, 3]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

    // Screw hole #2
    // 150 degrees from x axis: x = -12.99, y = 7.5
    translate([-7.01, 25.5, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
    translate([-7.01, 25.5, 3]) cylinder(h = 5.01, r = 3, $fn = circle_facets);
    
    // Screw hole #3
    // 30 degrees from x axis:  x = 12.99, y = 7.5
    translate([-32.99, 25.5, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
    translate([-32.99, 25.5, 3]) cylinder(h = 5.01, r = 3, $fn = circle_facets);
    
    // ---- Screw holes between parts ----
    
    // Screw hole #4 (through hole between parts)
    translate([5, 33, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
    translate(plate_3_translate) translate([5, 33, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
    
    // Screw hole #4 (head bore between parts)
    translate(plate_3_translate) translate([5, 33, 11]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

    // Screw hole #4 (hex nut) 2.54mm thick, but allow 2.7 for bleed.
    translate([5, 33, 5.3]) cylinder(h = 5.01, r = hex_nut_size, $fn = 6);

    // Screw hole #5 (through hole between parts)
    translate([5, 3, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
    translate(plate_3_translate) translate([5, 3, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
    
    // Screw hole #5 (head bore between parts)
    translate(plate_3_translate) translate([5, 3, 11]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

    // Screw hole #5 (hex nut) 2.54mm thick, but allow 2.7 for bleed.
    translate([5, 3, 5.3]) cylinder(h = 5.01, r = hex_nut_size, $fn = 6);

    // ---- Clamp holes ----

    // Clamp (post) hole
    translate(plate_3_translate) translate([32.5, 8.5, -10]) cylinder(h = 100, r = clamp_hole_size, $fn = circle_facets);
    
    // Clamp gap
    translate(plate_3_translate) translate([31, 13.5, -1]) cube([3, 31, 18]);
    
    // Clamp screw hole M6 (unthreaded, 6.1mm)
    translate(plate_3_translate) translate([32.5, 28.95, 12]) rotate([0,90,0]) cylinder(h = 100, r = 3.1, $fn = circle_facets);

    // Clamp screw hole M6 (threaded, 5mm)
    translate(plate_3_translate) translate([21, 29, 12]) rotate([0,90,0]) cylinder(h = 100, r = 2.5, $fn = circle_facets);
    
    // Encoder details:
    // Body is 38mm wide (measured), 39mm per spec.
    // Raised ring around shaft is 20mm wide.
    // Shaft is 6mm across.
    // Three holes in body at equal intervals on a 30mm circle.
    // Uses 3mm screws.
    // Edge is 2.5mm in from edge of body.
    // 
};
};
