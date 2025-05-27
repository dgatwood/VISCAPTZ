
tower_height = 51.01;  // Keep in sync with cap.
tower_offset = tower_height - .02;

motor_height = 23;

screwdriver_size = 3.8;  // "3mm" with a lot of extra leeway for bleed.
screw_hole_size = 2.4;  // 2mm with a decent amount of leeway.
screw_head_size = 6;

enable_screwdriver_holes = true;  // Must be true for printing, false if you need to debug screw hole to slot alignment.

top_rotation = 180;

union() {

// TEMPORARY
// translate([0, 0, -40])
difference() {
    union() {
        translate([-0.125, 0.125, 0]) cylinder(tower_height, 84.75/2, 84.75/2, false, $fn=2000);
        rotate([0, 0, top_rotation]) translate([10, -2.5, tower_offset]) cube([25, 25, 8], false);
    }
    

    // Top half (tilt)
    union() {
        // Screw holes to hold the riser in place.
        // The people who designed this are the reason I've learned to hate hardware engineers.  :-D
        // Holes are 2mm in diameter (M2 fits perfectly)
        // Holes are 56 mm apart end to end (long direction)
        // Holes are 57mm apart side to side (small direction)
        // One hole is 3mm farther out lengthwise and farther in width-wise.
        translate([28.5, 28, -1]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=20);
        translate([28, -28.5, -1]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=20);
        translate([-25, -31.5, -1]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=20);  // Not square!
        translate([-28, 28.5, -1]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=20);
        
        // Remove a tiny bit of excess plastic that always breaks off anyway.
        translate([-26.5, -40.5, tower_height-3]) cube([5, 10, 22], false);
        
        // Hole for the swinging arm
        translate([8, -25.5, -1]) cube([36, 51, 22], false);
        
        // Gap for the metal plate that holds the tilt mechanism
        translate([8, -38.5, -1]) cube([3, 77, 20], false);
        
        // Extra area for the part that holds the far end of the worm gear
        translate([7.5, 25, -1]) rotate([0, 0, -10]) cube([13, 12, 5], false);        
        
        // Hole for the encoder converter
        translate([-35, -21, -1]) cube([35, 42, 25], false);
                
        // Screw holes for the encoder converter (25m x 30mm hole spacing)
        translate([-4, -14, 0]) cylinder(200, screw_hole_size/2, screw_hole_size/2, false, $fn=20);
        // Disabled because there's a deep pit here for the encoder, and also it collides with the hole for the encoder.
        // translate([-29, -14, 0]) cylinder(200, screw_hole_size/2, screw_hole_size/2, false, $fn=20);
        translate([-4, 16, 0]) cylinder(200, screw_hole_size/2, screw_hole_size/2, false, $fn=20);
        translate([-29, 16, 0]) cylinder(200, screw_hole_size/2, screw_hole_size/2, false, $fn=20);
        
        // Screwdriver and screw head holes for the encoder converter (25m x 30mm hole spacing)
        translate([-4, -14, 27]) cylinder(200, screw_head_size/2, screw_head_size/2, false, $fn=20);
        // Disabled because there's a deep pit here for the encoder, and also it collides with the hole for the encoder.
        // translate([-29, 14, 27]) cylinder(200, screw_head_size/2, screw_head_size/2, false, $fn=20);
        translate([-4, 16, 27]) cylinder(200, screw_head_size/2, screw_head_size/2, false, $fn=20);
        translate([-29, 16, 27]) cylinder(200, screw_head_size/2, screw_head_size/2, false, $fn=20);
        
        // Notch for the encoder converter digital wires
        translate([-23, 20, -1]) cube([13, 10, 25], false);
        
        // Notch for the encoder converter tilt analog wires
        translate([-15, -22, -1]) cube([6, 10, 32], false);

        // Tilt encoder plate notch
        translate([-15, -36, -1]) cube([25, 36, 25]);        
        
        // Tilt encoder gear notch
        translate([9, -30, -1]) cube([15, 36, 4]);        

        // Larger screw holes to hold the riser in place; not full depth
        if (enable_screwdriver_holes) {
        translate([28, 28.5, 14]) cylinder(tower_height, screwdriver_size/2, screwdriver_size/2, false, $fn=20);
        translate([28, -28.5, 14]) cylinder(tower_height , screwdriver_size/2, screwdriver_size/2, false, $fn=20);
        translate([-25, -31.5, 14]) cylinder(tower_height, screwdriver_size/2, screwdriver_size/2, false, $fn=20);  // Not square!
        translate([-28, 28.5, 14]) cylinder(tower_height, screwdriver_size/2, screwdriver_size/2, false, $fn=20);
        }
    }
    
    // Global
    union() {
        // Giant middle hole for the shaft (15mm)
        translate([0, 0, -1]) cylinder(100, 15/2, 15/2, false, $fn=200);
        
        // Larger middle hole for the top part of the shaft (15mm with extra leeway)
        translate([0, 0, tower_height - 12]) cylinder(100, 18/2, 15/2, false, $fn=200);
        
        // Gap for annoying pins
        translate([-4, 10, tower_height - 4]) cube([6, 6.5, 25]);
        translate([-9.8, -17, tower_height - 4]) cube([6, 6, 25]);        
        
        // Wiring harness notch
        translate([-35, -55, -20]) rotate([0, 33.5, 0]) cube([10, 25, 100]);
        translate([0, -34, tower_height - 5]) cube([24, 30, 15]);
            
        // Anti-rotation notch for alignment
        translate([0, -42, -1]) cylinder(100, 1.2, 1.2, $fn=20);  // @@@ CHECK LOCATION
    }
    
    // Bottom half (rotation)
    rotate([0, 0, top_rotation]) union() {
        // Notch for pin in original swing pit.  Removed because we made the tower not as wide.
        // translate([14, -6, tower_height + 4]) cylinder(100, screw_hole_size/2, 3, false, $fn=20);
        
        // Notch for wires on limit switches (unused)
        translate([3, -43, tower_height-3]) cube([21, 21, 5]); // 26.5
        translate([3, 23, tower_height-3]) cube([21, 20, 5]); // 26.5

        // Motor pit
        translate([0, 0, tower_height - motor_height + .01]) difference() {
            union() {
//                translate([-20, 0, -.01]) cube([20.02, 34, motor_height], false);  // Anything above 20mm is clear of the motor.
                intersection() {
                    translate([0, 0, -1]) cylinder(motor_height + 1, 81/2, 81/2, false, $fn=200);
                    translate([-120, -105, -0.01]) cube([120, 120, 120], false);
                }
            }
            translate([-32.5, -38.5, -0.01]) cube([10, 10, 120], false);
        }
        
        // Screw holes to hold the rotating block in place.  Note that these are NOT square.  And because this design
        // rotates the bottom part by 180 degrees to make the wires reach (and to reduce the total height, that means
        // two of the screws that hold the riser in place do not line up with the screw holes that hold the bottom
        // in place.  These are marked, and that is why the depth of those is shallower, starting above the build plate,
        // because it is confusing to have an unused hole right next to the correct screw hole.
        translate([28.5, 28, 40]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=20);  // Not aligned
        translate([28, -28.5, 40]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=20);
        translate([-25, -31.5, 40]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=20);  // Not aligned.
        translate([-28, 28.5, 40]) cylinder(100, screw_hole_size/2, screw_hole_size/2, false, $fn=20);

        // Nut slots
        translate([27.9, 23.9, tower_height - 5]) rotate([0, 0, 45]) cube([13, 5.2, 3]);
        translate([24.1, -28.05, tower_height - 4]) rotate([0, 0, -45]) translate([0, -0.1, 0]) cube([20, 5.2, 3]);
        translate([-30.5, -40.85, tower_height - 4]) rotate([0, 0, 45]) cube([13, 5.2, 3]);
        translate([-37, 33.8, tower_height - 4]) rotate([0, 0, -45]) cube([13, 5.2, 3]);
      
        // Screw slots
        translate([26, 32.5, 14]) cylinder(23, 4, 4, false, $fn=20);  //  Not aligned.  See explanation above.
        translate([28, -28.5, 14]) cylinder(23, 4, 4, false, $fn=20);  // A little off.
        translate([-28.5, -29.3, 14]) cylinder(23, 2.7, 2.7, false, $fn=20);  // Not aligned.  See explanation above.
        translate([-28, 28.5, 14]) cylinder(23, 4, 4, false, $fn=20);
  
        // Encoder pit
        translate([13, 0, -1]) cube([20, 20, tower_height + 6], false);
        
        // Round hole for encoder
//        translate([24, 10, 0]) cylinder(100, 5, 5, false, $fn = 200);
        translate([22.5, 7.5, 0]) cylinder(100, 5, 5, false, $fn = 200);
        
        // 1 cm from left of tower (farthest from slanted wire slot)
        // 1.25 cm from outer edge of tower (farthest from center hole)
    }
    
    // TEMPORARY
//    translate([-60, -60, 10]) cube([120, 120, 200]);
//    translate([-60, -60, -160]) cube([120, 120, 200]);
    
    // Encoder wiring notch
//    translate([0, 19, -1]) cube([20, 30, 25]);
//    translate([-20, 22.5, 22]) cube([40, 10, 17]);
//     translate([10, 18.5, 22]) cube([10, 10, 17]);
    
}
// TEMPORARY
//    translate([-5, -40, 5]) cube([5, 70, 5]);
//    translate([-20, -25, 5]) cube([20, 10, 5]);    
}
