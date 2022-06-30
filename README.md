VISCAPTZ
========

VISCAPTZ is an attempt to turn my Panasonic AG-CX350 into a full-blown PTZ camera,
complete with the ability to store and recall preset locations.

Currently, this uses the Panasonic camera's PTZ CGI to support zoom control,
and has motor control capabilities and encoder capabilities that are currently
not compiled in.

Because I'm testing this without the actual camcorder handy, this also includes
some emulator CGI scripts that mimic the zoom behavior of the Panasonic
camcorder (roughly), which the CGI portions have been tested against.

And because the hardware isn't finished, none of the hardware bits are currently
turned on.  I hope to get there over the next few weeks.

The design is intended to be modular, so that other cameras could theoretically
be supported, so long as they provide the ability to not only control the zoom
speed, but also obtain the current zoom position.  (Or, if you don't care about
being able to store and recall speeds, you could easily use an Arduino as a
LANC interpreter off of a serial connection.)

I also added crude, completely untested support for Panasonic PTZ cameras, just
in case somebody wants to use this as a translator between VISCA and Panasonic's
CGI-based protocol.

Finally, I included a completely untested Arduino project that does the same
thing.  I gave up on using the Arduino because I didn't want to have to modify
that many shields that all use the same chip select pins, half of which don't
pass through the correct signals to other boards at all, etc.  You'll notice
that a lot of the function names are the same, and lots of code moved back
and forth between the two implementations because I couldn't decide which
hardware to use.


Hardware:
---------

The main hardware, apart from the camera, is a Vidpro MH-430 motorized gimbal.
This hardware provides the motors and frame for moving the camera.  It will be
controlled by a Waveshare Motor Driver HAT.

This also uses a Waveshare RS485 CAN HAT to communicate with a pair of
BRT38-COM4069D24-RT1 encoders that provide absolute positioning.  I'm going
to attach a pair of 0.4M 60-tooth 6mm shaft gears, which you can find here:

    https://www.aliexpress.com/item/2255800029508174.html

I didn't provide links for the others, because they're easy to find, but
that little search took *hours*.  :-)

I also had to create some 3D-printed replacements for the two mounting plates
that hold the motors onto the gimbal.  I've included the model files in the
/hardware/ directory.  Eventually, it would be nice to mill them out of
aluminum, but for now, the PETG is doing the job.


Status:
-------

This software can now properly drive both motors and read the position
values from both CAN encoders, and can reprogram the CAN encoders to change
their IDs.

The next step is to install the gears on the encoders so that I can do
real-world testing of the absolute positioning capabilities and do whatever
motor speed tuning is necessary to make it behave sensibly.

After that tuning is done, the only remaining piece of functionality is
a thread to obtain tally light information from OBS (via OBS-Websocket) or
Tricaster software (by polling) and push that tally light state to the camera.

Installation:
-------------

First, add the following bits to your /boot/config.txt file:

    # Waveshare Motor Driver Hat
    dtparam=i2c_arm=on

    # Waveshare RS485 CAN Hat
    dtoverlay=mcp2515-can0,oscillator=12000000,interrupt=25,spimaxfrequency=2000000
    dtparam=spi=on

Next, install current versions of the bcm2835 code and WiringPi if your versions
are older.  The versions that come in Buster are not usable.  I didn't feel like
updating my base image to Bullseye, so I have no idea if these steps are needed
in that version.  To install them, type:

    wget http://www.airspayce.com/mikem/bcm2835/bcm2835-1.70.tar.gz
    tar zxvf bcm2835-1.70.tar.gz
    cd bcm2835-1.70/
    sudo ./configure
    sudo make && sudo make check && sudo make install

    wget https://project-downloads.drogon.net/wiringpi-latest.deb
    sudo dpkg -i wiringpi-latest.deb

Finally, the Panasonic support requires libcurl.  To install it, type:

    sudo apt-get install libcurl4-openssl-dev

Then just `make` and `./viscaptz`.
