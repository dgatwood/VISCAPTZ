
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
        
        /* Hole for tripod pin - 14mm towards the center from one of the holes, 6mm counterclockwise. */
        translate([5.5, -13.3, -1]) cylinder(5, 5.2 / 2, 5.2 / 2, false, $fn=100);

        /* Holes for mounting */
        /* Radius is 27.5mm for one, 26.5 mm for the other two, and one of those two is about .5mm too far right. */
        translate([0, -27.5, -1]) cylinder(5, 3/2, 3/2, false, $fn=100);
        translate([23.4496732003, 13.25, -1]) cylinder(5, 3/2, 3/2, false, $fn=100);
        translate([-22.9496732003, 13.25, -1]) cylinder(5, 3/2, 3/2, false, $fn=100);

        /* Make the head holes */
        translate([0, -26.5, -1]) cylinder(2, 5/2, 5/2, false, $fn=100);
        translate([23.4496732003, 13.25, -1]) cylinder(2, 5/2, 5/2, false, $fn=100);
        translate([-22.9496732003, 13.25, -1]) cylinder(2, 5/2, 5/2, false, $fn=100);
        
        /* Bevel the head holes */
        translate([0, -26.5, .99]) cylinder(0.51, 5/2, 3/2, false, $fn=100);
        translate([23.4496732003, 13.25, .99]) cylinder(0.51, 5/2, 3/2, false, $fn=100);
        translate([-22.9496732003, 13.25, .99]) cylinder(0.51, 5/2, 3/2, false, $fn=100);
        
        /* Holes for screw heads underneath (1 cm from hub at same angles) */
        translate([0, -10, -1]) cylinder(3, 5/2, 5/2, false, $fn=100);
        translate([8.66025403784, 5, -1]) cylinder(3, 5/2, 5/2, false, $fn=100);
        translate([-8.66025403784, 5, -1]) cylinder(3, 5/2, 5/2, false, $fn=100);
    }
    // translate([-50, -50, 2]) cube([100, 100, 100], false);
}
