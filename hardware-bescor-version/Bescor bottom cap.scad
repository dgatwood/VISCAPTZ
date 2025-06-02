
// Inside 85mm
tower_height = 51.01;  // Keep in sync with riser.
cap_height = tower_height + 0.8;

difference() {
    union() {
        cylinder(20, 68.5 / 2, 68.5 / 2, false, $fn=3000);
        translate([0, 0, 7]) cylinder(cap_height, 89.5 / 2, 89.5 / 2, false, $fn=3000);
    }
    union() {
        /* Hollow it out */
        translate([0, 0, 9]) cylinder(cap_height+1, 86 / 2, 86 / 2, false, $fn=3000);
        translate([0, 0, 3]) cylinder(cap_height+1, 65 / 2, 65 / 2, false, $fn=3000);

        /* Hole for tripod thread */
        translate([0, 0, -1]) cylinder(5, 8.2 / 2, 8.2 / 2, false, $fn=100);
        
        /* Hole for tripod pin */        
        translate([0, 14, -1]) cylinder(5, 5.2 / 2, 5.2 / 2, false, $fn=100);

        /* Holes for mounting */
        /* Radius is 26.5 mm. */
        translate([0, -26.5, -1]) cylinder(5, 3/2, 3/2, false, $fn=100);
        translate([21.575795719, 15.3861963816, -1]) cylinder(5, 3/2, 3/2, false, $fn=100);
        translate([-21.575795719, 15.3861963816, -1]) cylinder(5, 3/2, 3/2, false, $fn=100);

        /* Make the head holes */
        translate([0, -26.5, -1]) cylinder(2, 5/2, 5/2, false, $fn=100);
        translate([21.575795719, 15.3861963816, -1]) cylinder(2, 5/2, 5/2, false, $fn=100);
        translate([-21.575795719, 15.3861963816, -1]) cylinder(2, 5/2, 5/2, false, $fn=100);
        
        /* Bevel the head holes */
        translate([0, -26.5, .99]) cylinder(0.51, 5/2, 3/2, false, $fn=100);
        translate([21.575795719, 15.3861963816, .99]) cylinder(0.51, 5/2, 3/2, false, $fn=100);
        translate([-21.575795719, 15.3861963816, .99]) cylinder(0.51, 5/2, 3/2, false, $fn=100);
    }
}
