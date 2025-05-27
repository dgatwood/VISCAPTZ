plate_thickness = 6;

// One screw goes into a hole in the plate, which is initially untapped, but is the perfect for tapping an M2.5 screw thread.
// You will need an M2.5 x 8mm screw for that one.
// The other is an M2.5 x 12mm screw with a washer and nut.  This screw hole is off center from the pin under it to accommodate
// the washer.  If part of the pin breaks away, no big deal.

translate([0, 0, plate_thickness]) rotate([0, 180, 0]) difference() {
 union(){ 
    cube([30, 42, plate_thickness], false);
    translate([24, 4.5, -2]) cylinder(3, 2, 2, false, $fn=200);  // Fills hole at edge of plate.  Screw NOT centered.
  }
  // 19
  union() {
    // Screw holes
    translate([5, 38, -1]) cylinder(100, 1.45, 1.45, false, $fn=200);  // At top of plate (2.5mm with leeway)
    translate([24, 5, -10]) cylinder(100, 1.45, 1.45, false, $fn=200);  // At edge of plate; note off center from pin.
    
    // Post
    translate([8, 12.5, -1]) cylinder(100, 3.5, 1.25, false, $fn=200);
    translate([-5, 9, -1]) cube([13, 7, 100], false, $fn=200);
    
    // Encoder hole is centered 14 mm in from the edge of the metal plate, 17mm in from the top of the metal plate.
    // The screw holes are 12mm in from the edge of the plate (24mm from the far edge of this plastic plate).
    // and 8.5mm in from the top (38mm from bottom of this plastic plate).
    // So the center should be at 22, 33.  Nope.  Off a bit.  Adjusting.
    // 
    // Encoder hole (Not in correct place)
    translate([21, 32, -1]) {
        cylinder(100, 5, 5, false, $fn=200);
        // Larger hole for nut and lock washer, leaving thickness 2 remaining (including the -1 offset above)
        cylinder(plate_thickness - 1, 8, 8, false, $fn=200);
    }
    // Notch for the support structure where the vertical shaft went in originally.
    translate([0, 0, -1]) cylinder(100, 8, 3, $fn=200);
  }
}