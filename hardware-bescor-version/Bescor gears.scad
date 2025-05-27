// Uses https://github.com/chrisspen/gears

// Current gear height is 13.  Original was 15, but that ran into a support post on the vertical axis.

include <gears/gears.scad>

short_gear = true;
long_gear = true;

plastic_compensation = 0.2;  // 0.2 for plastic, 0 for milling

module bescor_gear(gear_height) {
    difference() {
        union() {
            spur_gear(0.747, 9, gear_height, 3.175 + plastic_compensation);
            translate([1.2, -1, 0]) cube([1, 3, gear_height], false);
            translate([1.8, -2.2, gear_height - 6]) cube([1.25, 4.4, 4], false);
        }
        translate([0, 0, gear_height - 4]) rotate([0, 90, 0]) cylinder(100, 1, 1);
    }
};

// The tilt encoder gear is short to avoid running into one of the support posts.
if (short_gear) bescor_gear(13);

// The pan encoder gear is longer to reach the drive gear.
if (long_gear) translate([10, 0, 0]) bescor_gear(15);
