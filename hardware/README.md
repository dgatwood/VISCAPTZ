Building the Hardware:
======================

This software was designed to be used with custom mounting plates that
let you attach BRT38-COM4069D24-RT1 rotary encoders to a Vidpro MH-430
motorized pan and tilt gimbal.  The models in this directory are
various versions of those custom mounting plates.

There are three versions of the mounting plates:

1.  The original design: "platform".
2.  The updated design: "uniplatform".
3.  CNC versions of "uniplatform".


You will probably want to use the second or third versions, because they
ensure that the gears remain engaged at all times regardless of how much
you tighten the screw that holds the plate onto the rest of the gimbal.


# Version 2: The Uniplatform (Current Hardware)

The second version incorporates the motor mount and the encoder mount into a
single unified mount platform.  This version, because it has zero room for
adjustment, will need to be modified if the design of the rest of the hardware
changes even slightly.  However, it is also much easier to install, not
requiring you to turn multiple thumb screws and hook a spring around a nub.

Also, because the gears are in a fixed position relative to each other and
the swivel pin, the thumb screw doesn't have to prevent rotation around
that swivel pin, because the gear on both sides of the main gear makes
rotation impossible.  The screw's only job is to keep the platform
from sliding outwards far enough for either gear to stop making contact.
This makes the design much more robust.

There is one negative consequence, however, which is that the tilt and
pan plates are no longer physically identical.  (It was always necessary
to distinguish between the two, because if the encoders get swapped,
things will go horribly wrong, so this is mostly a concern because of
the need to keep a spare replacement for both plates instead of a single,
generic plate.)

The result is that the spacing between the motor and encoder differs by about
4.25 mm between the pan and tilt axes.

The model is generated in OpenSCAD, and is also exported invarious forms
for 3D printing or CNC milling.

## 3D Printing

There are three STL files provided for 3D printing: a tilt version, a pan
version, and a file that contains both models in a single file.  I suggest
printing them separately on Snapmaker, because it tends to create a wall
between the two pieces when printing them together.

Thus far, I have only printed this model in PETG, and even that has
problems with the plastic breaking in the clamp part.  Nylon or some other
similarly flexible plastic might be a better choice.

On the other hand, PETG is also flexible enough that you can flex the
model enough to make the gears skip even with uniplatform.  This is
probably solvable by changing the width of the middle section of the
platform and the clamp part, but I have not bothered, because I'm
redoing it in metal anyway.

## CNC Milling

For CNC milling, I provided G-code for Snapmaker, a Luban project file,
and a set of SVG files.  For details on what all of these files are,
see the file NOTES inside the CNC directory.


## Screws (for 3D printing or CNC milling)

* The threaded thumb screw on the clamp is M6, to match the thread of the
  original wing screws (so that you don't have to remember which tap to use).
  You will need two extra washers on the original screw and four washers
  on the new screw, because the hole cannot be made deep enough to accommodate
  the full length of the screw for plastic strength reasons.

* The motor screws are M3 12mm.  The original motor screws are too short
  without making the plastic unacceptably thin.  (I actually had one of the
  heads pull through with the previous design.  Even now, do not overtorque
  any of the screws!)


# Original Platform:

The original platform design is relatively simple to make, but harder to use.
It was designed for 3D printing in PETG.  I used 100% infill to maximize the
strength of the model.  YMMV.  It consists of three pieces:

1.  A replacement for the original plate with a long stalk sticking out the
    back and a post similar to the post on the Vidpro gimbal.
2.  A mounting platform for the encoder.
3.  A piece that attaches on top of the encoder platform with a hole similar
    to the motor mount, for allowing the encoder platform to swing relative
    to the motor mount.

The problem with the original platform is that, as with the original Vidpro
hardware, nothing keeps the motor platform from rotating and allowing the
gears to jump loose except for the thumb screw.  That means you have to
tighten the thumb screw very tightly to avoid that risk.

I tried improving on the design using a spring to pull the two pieces
together, hoping that it would stabilize things, and it did somewhat,
but I'm still scared of it jumping out as it has done a couple of times
in the past, and simultaneously scared to tighten down PETG enough to
not feel scared of the gears slipping.

There are few things more horrifying to a videographer than hearing a gear
grinding noise and seeing an expensive camera suddenly pointing straight up.
After about the third time, I decided that I needed a better design.

That said, this version has been heavily tested, and the dual hinge design
ensures that it can tolerate some amount of manufacturing variation.
(Uniplatform will fail to mesh if the position of the motor or encoder is
off by even half a millimeter.)  So this still remains a viable alternative
if your hardware is somehow different enough from mine to cause problems.

Note: The two STL files are generated from the same OpenSCAD file.  The only
difference is which text string ("PAN" or "TILT") was uncommented at the time
of export.

## Screws:

* The screws holding the two plastic parts together are M3 16mm screws with M3
  nuts.  (If I had 14mm, that would be better, but 12mm is too short.)
* The screws holding the spring are M3 12mm screws.  One post has M3 and M4
  washers to give a better grip on the spring.  The other side hooks over
  after assembly.
* The spring is 5/16" x 1 1/8".  (Initially, I tried a 11/32" x 1 7/16" spring,
  but it didn't quite provide enough tension.)
* The threaded thumb screw on the clamp is M6, to match the thread of the
  original wing screws (so that you don't have to remember which tap to use).
  You will need two extra washers on the original screw and four washers
  on the new screw, because the hole isn't deep enough for plastic strength
  reasons.
* The motor screws are M3 12mm.  The original motor screws are too short
  without making the plastic unacceptably thin.  (I actually had one of the
  heads pull through with the previous design.  Even now, do not overtorque
  any of the screws!)

