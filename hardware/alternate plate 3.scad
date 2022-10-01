plate_1_translate = [0, 0, 8];
plate_2_translate = [90, 0, 0];
// plate_3_translate = [0, 0, 0];
plate_3_translate = [-60, -60, -8];
circle_facets = 40;
clamp_hole_size = 6.25;  // Measured at 6, but we lose about .5mm from PETG bleed.
hex_nut_size = 3.45;  // Measured at 6.35 tip-to-tip (5.56 across), but we lose slightly more than .5mm from PETG bleed.


translate(plate_2_translate) rotate([0, 0, 0]) difference() {
    union() {
        // Upper square part of plate
        translate(plate_3_translate) translate([0, -3, 8]) cube([41, 42, 8]);
    }
    
    // ---- Screw holes between parts ----
    
    // Screw hole #4 (through hole between parts)
    translate(plate_3_translate) translate([5, 33, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
    
    // Screw hole #4 (head bore between parts)
    translate(plate_3_translate) translate([5, 33, 11]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

    // Screw hole #5 (through hole between parts)
    translate(plate_3_translate) translate([5, 3, -10]) cylinder(h = 100, r = 1.5, $fn = circle_facets);
    
    // Screw hole #5 (head bore between parts)
    translate(plate_3_translate) translate([5, 3, 11]) cylinder(h = 5.01, r = 3, $fn = circle_facets);

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