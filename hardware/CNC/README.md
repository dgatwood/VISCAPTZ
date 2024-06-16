
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

