
# Before You Begin

This project is quite complicated, and requires a lot of very specialized parts and
tools.  I've tried to provide links where needed.

## Required tools:

1.  A 3D printer with a two-color print head (because of large unsupported areas).

    I used a Snapmaker A350T, and switched to a Creality K2 halfway through because of
    dual extruder clogging problems (design flaw in the early versions of that hardware)
    that became more problematic the more I printed with breakaway PLA support material.

2.  A Phillips screwdriver with a long, thin shaft, e.g. Amazon ASIN [B07D7CR5H2](
    https://www.amazon.com/uxcell-Phillips-Screwdriver-Comfortable-Handle/dp/B07D7CR5H2)
    to fasten the riser to the body of the device with the existing screws.

3.  A soldering iron, clippers, and a wire stripper.  I'm a big fan of the Iroda
    SolderPro 25LK, Amazon ASIN [B085T5DMPX](https://www.amazon.com/dp/B085T5DMPX),
    because it is cordless, charges with USB-C, and has decent battery life.  I did
    run it down, though, by also using it as a heat gun for shrink tubing on this
    project.

4.  A pair of needle-nose pliers to tighten the nuts on the encoders, install certain
    tiny nuts and screws, and remove breakaway supports from cavities inside the print.

5.  A set of metric taps (minimally, 2.5mm and 3mm) to thread one hole in the
    (Bescor-provided) metal tilt plate where a nut is infeasible, plus two holes in
    3D-printed gears, if you don't buy real gears.

    I used Rocaris taps (Amazon ASIN [B08F2Q3BYL](https://www.amazon.com/dp/B08F2Q3BYL))
    because they're relatively inexpensive.

6.  Allen wrenches for set screws and screwdrivers or allen wrenches for other screws.

7.  Highly recommended: A magnetic screw mat so you can write down where all the
    screws came from.  Don't say I didn't warn you.  :-D

8.  Highly recommended: A small metric nut driver set that works for 2mm. 2.5mm, and
    3.5mm nuts.

9.  Highly recommended: 2x Traxxas 6745 gears so you don't have to print them yourself.


## Required parts and supplies (roughly from most expensive to least expensive):

1.  Two rotary position encoders.  The design is built around the CALT P3015; other encoders
    will require modifications.  These cost about $35 each from Amazon.

    Amazon ASIN [B01G50OHZM](https://www.amazon.com/dp/B01G50OHZM)

2.  A Rock Pi E (I ordered the 2GB version with an 8GB eMMC, but the cheapest versions start at $22.)

    [AliExpress](https://www.aliexpress.us/item/3256806816819929.html).

3.  An analog input module that can handle greater than 5V input, e.g. Gravity: 0-10V
    15-Bit Dual-Channel High-Precision ADC Module for Arduino / Raspberry Pi / ESP32.

    [DFRobot](https://www.dfrobot.com/product-2917.html)

4.  A 12V power supply with a 5.5mm x 2.1mm plug, readily available from Amazon, Walmart, etc.

    The original device draws up to 6W, and the board draws up to 15W.  And you'll need to leave
    room for conversion loss.  If you use linear regulators, this can be huge.  I used one linear
    and one switched-mode regulator, because 6V switched-mode regulators are hard to come by.
    Based on that, I'd estimate a worst-case power consumption of 31W, but realistically way less
    than that.  Still, it's better to be safe than sorry, and 12V 3A power supplies are cheap
    enough.

    Another option is to use a power-over-Ethernet adapter, e.g this [RevoData](
    https://www.amazon.com/REVODATA-Splitter-Ethernet-IEEE802-3bt-PS5712BG/dp/B0F1F8JX4G)
    adapter, which would then let you power your panhead from your PoE supply.  If your camera
    is sufficiently low power, then that plus a [CyberData PoE switch/splitter](
    https://www.amazon.com/dp/B00CH3MLM8) might be enough to turn a camcorder into a
    PoE-powered PTZ camera.  Maybe.  I might try it with mine at some point and see.

5.  A 5V/3A switched-mode voltage regulator to power the Rock Pi board.  A switched-mode is
    strongly recommended to avoid wasting a lot of power.  I used a PSU5a 3A switched-mode
    regulator.

    The reason for this is because it isn't practical to use USB-C to power the Pi and a separate
    power supply to power the Bescor, so we drive the Pi using a 5V pin on the 40-pin connector.

    [EZSBC](https://ezsbc.shop/products/psu5a-5v-3a-regulator-in-to-220-form-factor)

6.  A 6V/1A voltage regulator to power the original hardware.  On 12V, this could draw up to 12W
    when the motors are both running, in theory, hence the beefy power supply above, so if you can
    find a source for 6V/1A switched-mode regulators, go with one of those instead.

7.  Male and female 3-pin connectors, e.g. Amazon ASIN [B0D4M6MTFT](
    https://www.amazon.com/Cermant-20Pairs-Connection-Terminal-Connector/dp/B0D4M6MTFT),

    These are used to connect the rotary encoders to the ADC and to connect the DC power to
    the main board (two wires used).  These are potentially optional, but strongly recommended
    for easier assembly.

8.  Some extra wire to go from those connectors to pins on the control input connector.

9.  A 5.5mm x 2.1mm jack, e.g. Amazon ASIN [B07CTCLKPP](
    https://www.amazon.com/Antrader-24pcs-Female-Socket-Connector/dp/B07CTCLKPP),
    because the original DC jack is part of the battery pack, which is removed in this mod.

10. A JST 2mm 4-pin connector pigtail for connecting the ADC to the Rock Pi,
    e.g. Amazon ASIN [B0732MMD7K](https://www.amazon.com/2-0MM-Female-Single-Connector-Wires/dp/B0732MMD7K).

11. A 40-pin 2.54mm female crimp pin housing for the Rock Pi.  These have become somewhat hard to
    find.  You can use a smaller 2.54mm crimp pin housing, since this only uses a small number of
    pins, but it's kind of nice to use the full connector from an "I didn't screw this up and plug
    it in off by one pin" perspective.  :-)

    [DigiKey](https://www.digikey.com/en/products/detail/amphenol-icc-fci-/65043-017LF/1002674)

12. Some female pins with pre-crimped wires to populate the 40-pin socket, unless you're good at
    crimping those things.

13. A longer 5.5mm x 2.5mm x 14mm power plug to put on your 12V power supply, because the deeply
    recessed power jack won't work with standard plugs.

    e.g. Amazon ASIN [B07FRXPZV9](
        https://www.amazon.com/Lzzvibo-5-5x2-5mm-Supply-Connector-Shrinkle/dp/B0DZHX92V2)

14. Four 2mm nuts and 2mm screws with length 20mm to fasten the pan motor block to the riser block.

    I would strongly recommend just buying a metric screw collection, e.g. [B09WJ14JWF](
    https://www.amazon.com/HELIFOUNER-Stainless-Hardware-Assortment-Wrenches/dp/B09WJ14JWF
    ), because that contains nearly all of the screws that I used, plus a lot of others.

15. Three 2.5mm x 10mm standoffs (male-female or female-female), 2.5mm x 8mm screws, and either
    nuts or 2.5mm x 6mm screws for attaching the ADC.  (One corner is unsupported because there
    is an encoder mounted underneath it.)

    The ADC must be mounted up from the base because of the length of the center shaft.  I used
    female-female standoffs with screws on both sides, but you can also use male-female standoffs
    with nuts on the board side.

    You can find these standoffs, 2.5mm x 6mm screws, and 2.5mm nuts in sets, e.g. Amazon ASIN
    [B07JYSFMRY](https://www.amazon.com/dp/B07JYSFMRY).

16. One 2.5mm x 8mm screw to fasten the plastic tilt plate to the threaded (small) hole in the
    (Bescor-provided) metal tilt plate.  (See link above.)

17. One 2.5mm x 12mm screw and nut and thin washer to mount the plastic tilt plate to the
    unthreaded (large) hole in the (Bescor-provided) metal tilt plate.  (See link above.)

18. Two M3x3 set screws.  I bought an assortment from Hapric, Amazon ASIN [B0D1CF7C1F](
    https://www.amazon.com/Hapric-Metric-Assortment-Socket-Cup-Point/dp/B0D1CF7C1F),
    because I wanted a collection of metric and standard set screws; they can be great
    to have around.

19. Four M2.5x20mm standoffs, four M2.5 nuts, and four M2.5x6mm or 8mm screws for holding the Pi.
    You can find those in sets, e.g. Amazon ASIN [B07JYSFMRY](https://www.amazon.com/dp/B07JYSFMRY).

20. Rubber cement to reattach the rubber grip plate on the bottom when you're done.


# Printing the models

You will need to print the following models:

1.  Bescor riser - the main block that extends the height of the tower to make room for the encoders.
2.  Bescor tilt plate - A small plastic block that fastens onto the existing metal tilt plate.
3.  Bescor new bottom cap - A much taller version of the bottom cap to hide all the electronics.
4.  Bescor electronics module - holds the Rock Pi E board, the power jack, and voltage regulators.
5.  Bescor electronics module cover - the lid for the electronics module.
6.  Bescor short and long gears - If you did not buy a pair of Traxxas 6745 gears, you will need to
    print these.  Note that these gears are largely untested, because I bought actual metal gears.
    However, they did work at one time early in the development process.  If you use these, note that
    the taller of the two gears is the pan gear.


# Steps to install:

## Disassembly

1.  Remove the rubber self-stick grip from the bottom part that sits on the tripod.  You will reglue this
    later, so don't worry that it won't restick.
2.  Remove the screws under it.
3.  Remove the plastic bottom cap.  Put this part away in case you later decide to reverse the mod.
    You will print a replacement.
5.  Remove the four screws on the black plastic frame underneath that.  Lift the pan motor assembly out.
6.  Optional: Cut all of the wires between the pan motor block and the electronics and install inline
    connectors to make it easier to work with.
7.  Cut the red and black power wires that go from the battery compartment to the main board and install
    inline connectors so you can reconnect them.
8.  Remove the screws from the bottom of the battery compartment and remove the cover.  Put this part
    away as well.  You will print a replacement.
9.  Deep inside the battery box, remove the two screws that hold the plastic battery holder in place.
    Put away the battery holder for safe keeping.  You never used it anyway, and we need the space.
10. On top of the body of the unit (just barely under the tilt plate where the camera attaches),
    remove both screws.  These hold the metal tilt plate.  Lift out the metal tilt plate.
11. Optional: Cut the pan module wires and install a connector.  This really isn't necessary, but
    you can do it if you want to; it might make things easier.  If you do this, put the connector
    as close to the top as possible, or at least extend the wires to be almost as long as the
    original wires are.  I did **not** cut the wires, FYI, though I did keep breaking the shorter
    ones over and over again.


## Preparing the components

1.  Solder a three-pin connector to both encoders.  This will save you pain later.

2.  Using a mated pair of connectors of your choosing, connect the DC input jack's center (+) pin
    to all of the following:

    - The input pin of the 6V regulator.
    - The input pin of the 5V regulator.

    I recommend a pair of single male and female pin headers for this, because they fit easily
    through the hole for the DC jack.  Or for maximum irony, use inline DC barrel connectors.

3.  Connect the DC input jack's outer (-) contact to all of the following:

    - The black (ground) wire leading to the Bescor's main control board.
    - The ground pin of the Rock Pi board.
    - The ground pin of the 5V regulator.
    - The ground pin of the 6V regulator.

    Note that the ADC's power supply (power and ground) come from separate 3.3V and
    ground pins on the Rock Pi.

4.  Connect the ADC (input) board's ground lug (analog side) to the ground pin on the two
    encoders (via the connector pigtail you added earlier).

5.  Connect the 5V regulator's output to all of the following:

    - The 5V pin on the Rock Pi 40-pin connector.
    - The Vcc pin on the two encoders (via the connector pigtail you added earlier).

6.  Connect the 6V regulator's output to the red wire going to the Bescor's original main board.

7.  Connect the remaining (output) pin of the encoders to the ADC input contacts.
    - The pan encoder should go to the lug marked AIN1.
    - The tilt encoder should go to the lug marked AIN2.

8.  Connect the SDA pin on the Rock Pi (pin 3) to the D/T pin on the ADC (pin 1).
9.  Connect the SCL pin on the Rock Pi (pin 5) to the C/R pin on the ADC (pin 2).
10. Connect a ground pin on the Rock Pi (pin 9) to the ground pin on the ADC (pin 3).
11. Connect the 3.3V pin on the Rock Pi (pin 1) to the Vcc pin on the ADC (pin 4).
12. Put the ADC into I2C mode by flipping the topmost switch to the left.  You may
    have to remove a piece of protective tape that covers the DIP switches.  Leave
    the other two switches (A0 and A1) switched to the right (0).
13. Pass the wires through the hole from the battery compartment.  You may have to enlarge the
    hole to make room for them, particularly if you put a connector on any of the wires.
14. Open the compartment that contains Bescor's main board.  Solder four wires to pins 2, 3, 5,
    and 6.  The unused pins are the far left, the far right, and the middle, which are
    speed, ground, and power, respectively.
15. Connect those to four GPIO pins.  You MUST choose four pins that are OFF by default, or
    else your hardware could be damaged while powering it on, as this mod disables the switches
    that would otherwise detect the tilt motor limit and replaces it with software.  For
    current versions of Rock Pi E, this means pins 7, 12, 15, and 36.  (In a pinch, pin 19 can
    also be used, as it provides only momentary power when the device is powered on, likely
    ending before the Bescor hardware's microcontroller would start handling input anyway.)

TODO: Update the above to describe the pin-to-pin mapping that I used after I get things wired
up and test it to verify the order that I attached the wires to those GPIO pins.


## Install the tilt encoder

1.  Using a 2.5mm tap, tap the small hole near the top of the tilt plate.  There's a gear behind
    it, so putting a nut there isn't practical.  But it taps very easily for 2.5mm screws.
2.  Install the encoder into the hole in the plastic tilt plate, with the lock nut under the nut,
    and tighten down the nut.  Be certain the wires come out the side farthest from the side of
    the unit so that they do not interfere with installing the riser block installation later.
3.  Put the plastic plate in place, with the plastic shaft sticking into the large hole in the
    metal plate and the other hole in the plastic plate lining up with the hole you just tapped.
4.  Put a 2.5mm screw into the tapped hole
5.  Put a 2.5mm screw through the other hole, and put a washer and a nut on the other end.
6.  Put the set screw into one of the gears (the shorter one if you're using 3D-printed gears)
    and install it on the encoder shaft, being certain to tighten the set screw adequately.
    Consider using a thread locker glue.
7.  Reinstall the tilt mechanism with the two screws from the top.  This takes some patience.  I
    recommend using a couple of small screwdrivers to make the holes line up first.  While
    reinstalling, be certain that the wires coming from the battery compartment pass **beside**
    the motor, **not** over it, to ensure that they do not interfere with installing the riser
    block later.


## Install the riser block

1.  Install the encoder into the top part of the riser block.  Connect the pigtail to the ADC,
    and tuck as much wire as possible down into the encoder hole.
2.  Put three 2.5mm screws into the three large screw holes from the top (outer surface) and
    attach the three standoffs on the other side.
3.  Put the ADC board on top of the standoffs with the large terminal block sticking downwards
    into the encoder hole, then attach it with short 2.5mm screws or nuts, depending on whether
    your standoffs are male-female or female-female. Tuck excess wire underneath as much as
    possible to keep them out of the way.
4.  Tape the bundle of wires from the pan motor together to make them easier to control.
5.  Tape any other wires (e.g from the tilt encoder) to the exposed underside of the ADC board
    to keep them out of the way.)
6.  Take the four original screws that originally held the pan motor in place, and put them into
    the four holes at the hollowed-out end of the riser block (the end that was sitting on the
    3D printer bed) using the tall slots on the side.  If they are snug, screw them in until
    they are about to come out the bottom.
7.  Pass the wire bundle through the slanted channel on the side of the riser block as you
    install the riser block where the pan motor assembly was originally, carefully lining up
    the alignment tab on the body of the unit with the slot on the side of the riser.  If the
    riser does not go in easily, check for wires getting crushed.
8.  Insert the screwdriver through the large holes and tighten the screws to fasten the riser
    block in place.  Do not overtighten.


## Install the pan motor block

1.  Install the other gear on the encoder shaft.  If you are using a metal gear, you will need
    to install it five millimeters or so out from the body of the encoder.
2.  Put the motor block in place.
3.  One at a time, insert the 2mm nuts into the slots near the top of the riser block, put
    2mm x 20mm screws through the holes in the pan block, and tighten down the screws.
4.  Install the replacement cap on the underside of the motor block.
5.  Use contact cement to reattach the grip material (or wait until you're certain that
    everything works).



## Install the Rock Pi  --  Finish me

1.  Attach the DC input jack and tighten the nut.
2.  Attach the 5V regulator to the side of the electronics module below the power jack
    using double-stick foam tape.
3.  Attach the 6V regulator to the first compartment of the electronics module using
    double-stick foam tape.
4.  Attach risers to the Rock Pi using 2.5mm nuts on the other side.  The risers should
    be on **top** of the Rock Pi.
5.  Pull the wiring harness through the open front face of the first part of the electronics
    module and out the open top face (where top is defined relative to the 3D printer bed).
6.  Connect the wiring harness to the Rock Pi.
7.  Slide the Rock Pi into the outer (closed-face) compartment of the electronics module.
8.  Using four M2.5x6mm or 8mm screws (??check length??), attach the pi to the electronics
    module.  Screwdriver holes are provided for your convenience.
9.  Slide the electronics module into the Bescor, pulling the wire up out of the way.
10. Tuck the excess wire into your choice of compartments.
11. Pass the wires through the slot near the edge of the electronics module.
12. Insert four 2.5mm nuts into the slots at the top of the electronics module, using tape
    to ensure that they do not fall out.
13. Put the top on the electronics module and use 2.5mm screws to secure it.  You may need
    to use a small piece of wire to align the nut with the screw hole.

