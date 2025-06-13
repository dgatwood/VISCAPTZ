
// If you want to make a cut-out for USB or USB-C, here are the Rock Pi E measurements as based I could measure them.

// 4mm from middle of screw to USB port.  13mm to other side.
// 10mm from top of riser to USB port.  25mm to bottom.
// 13mm to USB-C port.  23mm to far side.
// 21mm to top o USB-C port.  25mm to bottom.

enable_body = true;
enable_top_plate = true;
test_top_plate = false;

bottom_offset = test_top_plate ? 0: 90;
bottom_voffset = test_top_plate ? -71: 0;
screw_post_size = 4.3;  // 3mm with a little bit of leeway.
screwdriver_size = 3.8;  // 3mm with a lot of extra leeway.
screw_head_size = 6;
screw_hole_size = 2.2;  // 2mm with a decent amount of leeway.
sloppy_screw_hole_size = 2.6;  // 2mm with a more leeway.
jack_hole_size = 8.3;  // 8mm with a bit of leeway.

// Top plate
if (enable_top_plate) {
    rotate([0, test_top_plate ? 180: 0, test_top_plate ? 180: 0]) translate([bottom_offset, test_top_plate ? -72 : 0, 0]) difference() {
        union() {
            translate([-10, 0, bottom_voffset]) cube([46, 72, 10]);
            translate([36, 0, bottom_voffset]) cube([46, 72, 7]);
        }
        union() {
            translate([-31.5, 36, -1 + bottom_voffset]) cylinder(100, 87/2, 89/2, false, $fn=2000);
            translate([-31.5, 36, 4 + bottom_voffset]) cylinder(7.1, 89/2, 89/2, false, $fn=2000);
            
            // Ethernet ports
            translate([56.5, 20.5, -1 + bottom_voffset]) cube([15, 17, 10]);
            translate([56.5, 39.5, -1 + bottom_voffset]) cube([15, 17, 10]);
            
            // Screw holes for Pi cover
            translate([78.5, 17, -1 + bottom_voffset]) cylinder(100, screw_hole_size / 2, screw_hole_size / 2, $fn=100);
            translate([78.5, 55, -1 + bottom_voffset]) cylinder(100, screw_hole_size / 2, screw_hole_size / 2, $fn=100);
            translate([42.5, 17, -1 + bottom_voffset]) cylinder(100, screw_hole_size / 2, screw_hole_size / 2, $fn=100);
            translate([42.5, 55, -1 + bottom_voffset]) cylinder(100, screw_hole_size / 2, screw_hole_size / 2, $fn=100);
            
            // Indent in plate
            translate([16, 3, 3]) cube([18, 66, 10.1]);
            translate([10.5, 3, 3]) cube([18, 10, 10.1]);
            translate([12.5, 3, 3]) cube([18, 15, 10.1]);
            translate([10.5, 59, 3]) cube([18, 10, 10.1]);
            translate([12.5, 54, 3]) cube([18, 15, 10.1]);
        }
    }
}

if (enable_body) {
    /* rotate([0, 180, 0]) translate([0, 0, -70]) */ translate([0, 5, 3]) difference() {
        union() {

            difference() {
                union() {
                    cube([36, 62, 63]);  
                    translate([36, -5, -3]) cube([46, 72, 69]);
                }
                translate([-31.5, 31, -1]) cylinder(100, 91/2, 91/2, false, $fn=2000);
                
                // Alignment notches.  50mm apart.
                translate([7, 7, -1]) cylinder(100, 2, 2, false, $fn=200);
                translate([7, 56, -1]) cylinder(100, 2, 2, false, $fn=200);
                
                // Alignment notches.  20mm in.
                translate([15, -2, -1]) cube([3, 3.5, 100], false);
//                translate([17, -.5, -1]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=200);
//                translate([16, -.5, -1]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=200);
//                translate([17, 62.5, -1]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=200);
//                translate([16, 62.5, -1]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=200);
                translate([15, 60.5, -1]) cube([3, 3.5, 100], false);

                // Edge notches.  0mm in.
                translate([31.5, -1, -1]) cube([4.51, 4, 100], false);
                translate([31.5, 59, -1]) cube([4.51, 4, 100], false);

                // 15mm Edge notches.  0mm in.
                translate([21, -.01, -1]) cube([15.01, 15.01, 10], false);
                translate([21, 47, -1]) cube([15.01, 15.01, 10], false);
                
                // Screw holes
                translate([14, 12, -1]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=200);
                translate([14, 50, -1]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=200);

                translate([14, 12, 8]) cylinder(100, screwdriver_size/2, screwdriver_size/2, false, $fn=200);
                translate([14, 50, 8]) cylinder(100, screwdriver_size/2, screwdriver_size/2, false, $fn=200);
                
                // Holes for screw posts -- Removed because we removed the entire bottom.
                // translate([12, 12, -.01]) cylinder(6.01, screw_post_size/2, screw_post_size/2, false, $fn=200);
                // translate([12, 50, -.01]) cylinder(6.01, screw_post_size/2, screw_post_size/2, false, $fn=200);
              
                // Space behind power jack   
                translate([0, 2.5, 35]) cube([20, 52.5, 20]);
                // Space in front of power jack   
                translate([0, 58, 38]) cube([20, 52.5, 13]);
                
            //     translate([35, 3, 0]) cube([10, 60, 65]);
                // Hollowed out cavity inside main body (for voltage regulators and similar)
                translate([15.99, 6, 12]) cube([18.52, 50, 71]);  // Was 66 when top was unified
                
                // Gap for wires (you will have to widen the hole).
                translate([0, 14, -1]) cube([18.52, 34, 71]);  // Was 66 when top was unified
                            
                // Hole for voltage regulator and self-stick pad
                translate([0, 2.5, 20]) cube([16, 57.5, 16]);
                
                // Hole for wiring harness
                translate([33.01, 18, 42]) cube([15, 26, 30]);
//                translate([36.01, 57, 52]) cube([10, 3, 20]);
//                translate([28.01, 42, 52]) cube([13, 16, 20]);

// 58mm from screw to screw vertically.
                // Pi mounting holes (larger because of needing to accommodate flex in the plastic posts.
                translate([0, 5.75, 63]) rotate([0, 90, 0]) cylinder(50, sloppy_screw_hole_size / 2, sloppy_screw_hole_size / 2, false, $fn=200);
                translate([0, 54.75, 63]) rotate([0, 90, 0]) cylinder(50, sloppy_screw_hole_size / 2, sloppy_screw_hole_size / 2, false, $fn=200);
                translate([0, 5.75, 5]) rotate([0, 90, 0]) cylinder(50, sloppy_screw_hole_size / 2, sloppy_screw_hole_size / 2, false, $fn=200);
                translate([0, 54.75, 5]) rotate([0, 90, 0]) cylinder(50, sloppy_screw_hole_size / 2, sloppy_screw_hole_size / 2, false, $fn=200);
                
                // Slope avoidance
    //             translate([-0.01, -0.01, -0.01]) cube([34, 7, 7], false);
    //             translate([-0.01, 55.01, -0.01]) cube([34, 7, 7], false);
                
                // Remove bottom to make life easier (no holes for the posts).
                translate([-0.01, -0.01, -0.01]) cube([35, 62, 5], false);
                
                // Remove corners for sanity
                translate([-0.01, -0.01, -0.01]) cube([9, 66, 100], false);
                
                // Hole for Pi
                translate([45, 2, -1]) cube([31.5, 58, 72]);
                
                // Screw holes for Pi cover
                translate([78.5, 12, 60]) cylinder(100, screw_hole_size / 2, screw_hole_size / 2, $fn=100);
                translate([78.5, 50, 60]) cylinder(100, screw_hole_size / 2, screw_hole_size / 2, $fn=100);
                translate([42.5, 12, 60]) cylinder(100, screw_hole_size / 2, screw_hole_size / 2, $fn=100);
                translate([42.5, 50, 60]) cylinder(100, screw_hole_size / 2, screw_hole_size / 2, $fn=100);
                            
                // Nut slots
                translate([76, 9.8, 60]) cube([5.1, 4.4, 4]);
                translate([76, 47.8, 60]) cube([5.1, 4.4, 4]);
                translate([40.01, 9.8, 60]) cube([5.1, 4.4, 4]);
                translate([40.01, 47.8, 60]) cube([5.1, 4.4, 4]);
                
                // Bottom sloped cut-ins (exterior part of the module)
                translate([-40, -5.2, -32]) rotate([25, 0, 0]) cube([200, 20, 500], false);
                translate([-40, 56.8, -7]) rotate([-25, 0, 0]) cube([200, 20, 500], false);

                // Bottom sloped cut-ins (interior part of the module)
                translate([-40, -0.2, -28]) rotate([25, 0, 0]) cube([76, 20, 500], false);
                translate([-40, 51.8, -3]) rotate([-25, 0, 0]) cube([76, 20, 500], false);            
            }

            // Bottom tab
            translate([34.5, 10, 0]) cube([1.5, 42, 42], false);  // 42mm wide, out of 62mm total
            
            // More material by power jack
            translate([15, 55, 35]) cube([8, 5, 3], false);
            translate([15, 55, 52]) cube([8, 5, 11], false);
            translate([20, 55, 36]) cube([3, 5, 18], false);
            translate([15, 55, 36]) cube([8, 3, 18], false);
            
            // Bottom sloped fill-ins (exterior)
            translate([36, 3.31, -2.94]) rotate([25, 0, 0]) cube([45, 2, 19.6], false);
            translate([36, 56.86, -2.18]) rotate([-25, 0, 0]) cube([45, 2, 19.6], false);
        }
        union() {
            // Hole for power jack 44mm up
            translate([14, 65, 44]) rotate([90, 0, 0]) cylinder(20, jack_hole_size/2, jack_hole_size/2, false, $fn=200);
            
            // Pi mounting screwdriver holes
            translate([0, 5.75, 63]) rotate([0, 90, 0]) cylinder(42, screwdriver_size / 2, screwdriver_size / 2, false, $fn=200);
            translate([0, 54.75, 63]) rotate([0, 90, 0]) cylinder(42, screwdriver_size / 2, screwdriver_size / 2, false, $fn=200);
            translate([0, 5.75, 5]) rotate([0, 90, 0]) cylinder(42, screwdriver_size / 2, screwdriver_size / 2, false, $fn=200);
            translate([0, 54.75, 5]) rotate([0, 90, 0]) cylinder(42, screwdriver_size / 2, screwdriver_size / 2, false, $fn=200);


            // Pi mounting screw head holes
            translate([30, 5.75, 63]) rotate([0, 90, 0]) cylinder(12, screw_head_size / 2, screw_head_size / 2, false, $fn=200);
            translate([30, 54.75, 63]) rotate([0, 90, 0]) cylinder(12, screw_head_size / 2, screw_head_size / 2, false, $fn=200);
            translate([30, 5.75, 5]) rotate([0, 90, 0]) cylinder(12, screw_head_size / 2, screw_head_size / 2, false, $fn=200);
            translate([30, 54.75, 5]) rotate([0, 90, 0]) cylinder(12, screw_head_size / 2, screw_head_size / 2, false, $fn=200);
        }
    }
}