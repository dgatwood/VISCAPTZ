
Main Cutting And Etching
========================

In this folder, you will find the CNC files for milling aluminum versions of the
uniplatform mount.  In addition, I've included the SVG files for each of the cuts.
To maximize performance, each hole is in a separate file.  (Before that,
Snapmaker Luban was doing a little bit of each hole, then a little bit of the next
hole, etc. and spending all its time moving the toolhead around for no reason.)

The depth information is as follows:

1.  Etching ("PAN" and "TILT" labels):
    - Processing mode: Relief
    - Method: N/A
    - Depth: 2mm
    - Bit size: 1.5mm

2.  Screw heads:
    - Processing mode: Relief
    - Method: N/A
    - Depth: 2.5mm
    - Bit size: 3.125mm

3.  All other SVG files:
    - Processing mode: Vector
    - Method: On the Path
    - Depth: 8.6mm (full 8mm depth and a bit over)
    - Bit size: 3.125mm

Note that the SVG is hand-edited to ensure that Luban does not waste time making
hundreds of plunges per drill hole.  The bit size is close enough to the target
hole size that a tiny dot gives the correct results.

The same is true for all lines on the SVG.  It is assumed that every line will
have an effective stroke width of 3.125m, even though the stroke width is
0.01mm to ensure that it is just a single pass per layer.


Side Drilling
=============

The Drill_CNC and Drill_SVG directories contain CNC files and SVG files for
drilling a hole in the side of the plates for the screw that holds it onto
the gimbal.

There are two ways to approach this.  You can drill the holes manually, or
you can use these files.

If you drill the holes manually, you should drill about a 6.3mm hole in the
thin part and a 5mm hole all the way through that into the body at least 20mm
deep (and ideally, a bit deeper).  Note that drilling these holes manually is
error-prone.  If the drill decides to grip the material and rip it out of your
hand, you *will* bend the plate at the hole.  There's just not enough
material to avoid that problem.  For this reason, I decided to use my Snapmaker
to drill these holes.

To do this automatically, first cut out the plate fully and clean up the
rough edges.  (You will have stray metal sticking out at the bottom of the
plate in various places, which will likely require a Dremel or similar tool
with a grinding bit to remove).

Next, you will need to mount the plate horizontally with the flat side up.
You can either come up with some custom mounting setup or, if you have a
fourth (rotary) axis attachment, you can use that.

Unfortunately, the Snapmaker rotary attachment doesn't lend itself to this
task.  The hole has to be drilled too close to the end to drill it near
the headstock, and the tailstock pin ends up in the middle of the gap, and
thus useless.

To solve this problem, I took two of the large metal discs that got cut out
for the encoder/motor holes and placed them flat against the end of the
metal plate to cover the gap.  For the shorter plate, a single disc is
adequate.  For the longer plate, because of the need to move the tailstock
farther out, I ended up using two discs stacked atop one another.

Either way, once you have mounted the plate with the long, flat edge facing
upwards and as close as possible to perfectly horizontal on top, and
perfectly vertical on the sides, the next step is to find the exact middle
of the material near the drill hole end.  To do this,

1.  Measure about 8mm in from the end of the plate and move the CNC module's
    3.175mm flat end mill bit close to that point.

2.  Move the bit down on one side of the plate.  Slowly move it towards the
    plate, using the plastic spacer card between the plate and the bit until you
    can no longer move the spacer card.  Write down the X axis position.

3.  Repeat on the other side of the plate.

4.  Compute the midpoint between the two sides, where x1 is the larger x value,
    as (x2 + ((x1 - x2) / 2)).

5.  Move the bit up above the plate and set the X position at that value, with
    the Y position about 8mm in from the end.

6.  Slowly lower the end mill down (again using the white plastic card) until
    it touches.

7.  Set that point as the work origin.  This will be the center of the hole.

8.  Ensure everything is tight and start the grinding.  Check after a couple of
    passes to make sure that it really is centered.

When the milling is done, use an M6 tap (available from Amazon) to tap the
hole.

