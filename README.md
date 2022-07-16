VISCAPTZ
========

VISCAPTZ is an attempt to turn my Panasonic AG-CX350 into a full-blown PTZ camera,
complete with the ability to store and recall preset locations.

The design is intended to be modular, so that other cameras could theoretically
be supported, so long as they provide the ability to not only control the zoom
speed, but also obtain the current zoom position.  (Or, if you don't care about
being able to store and recall speeds, you could easily use an Arduino as a
LANC interpreter off of a serial connection.)

As currently configured, this code uses the Panasonic camera's PTZ CGI to support
zoom control, along with fairly simple custom hardware for interfacing with the
gimbal's motors and a pair of CAN-based encoders.  It also has support for RS485/
ModBUS encoders, though that code is entirely untested.

Because I'm testing this without the actual camcorder handy, this also includes
some emulator CGI scripts that mimic the zoom behavior of the Panasonic
camcorder (roughly), which the CGI portions are being tested against.

It also contains crude, completely untested support for Panasonic PTZ cameras,
just in case somebody wants to use this as a translator between VISCA and
Panasonic's CGI-based protocol for some reason.

It also provides support for getting tally information from various tally
sources.  The currently supported sources are:

    * VISCA
          The VISCA protocol allows you to set the tally state.
    * Panasonic
          If you're controlling a Panasonic camera via NDI, this software can
          read the tally state from the camera.
    * Newtek Tricaster
          By setting the tally source name to a specific source name, this
          software can read the active sources from a Tricaster video
          production workstation and control the tally state accordingly.
          This functionality has not yet been tested.
    * OBS
          By setting the tally source name to a specific source name, this
          software can read the active sources from OBS Studio and control
          the tally state accordingly.  This functionality requires
          installation of libgettally from here:

              https://github.com/dgatwood/v8-libwebsocket-obs-websocket

Beyond that, the only remaining missing piece of functionality is a thread
to obtain tally light information from OBS (via OBS-Websocket) or Tricaster
software (by polling) and push that tally light state to the camera.

Finally, I included a completely untested Arduino project that does the same
thing.  I gave up on using the Arduino because I didn't want to have to modify
that many shields that all use the same chip select pins, half of which don't
pass through the correct signals to other boards at all, etc.  You'll notice
that a lot of the function names are the same, and lots of code moved back
and forth between the two implementations because I couldn't decide which
hardware to use (at first).


Hardware:
---------

The following harware is currently supported:

  * Waveshare Motor Driver HAT for motor control
  * Waveshare RS485 CAN HAT for reading the encoders
  * BRT38-COM4069D24-RT1 encoders (CAN-based)
  * Panasonic AG-CX350 (zoom only)

I have this attached to a Vidpro MH-430 motorized gimbal, which provides the
actual motors and frame for moving the camera.  However, there is nothing
specific to that hardware in the code.  In theory, any motors that can be
controlled with the Waveshare Motor Driver HAT should work.

Because those encoders do not come with gears, you'll also need gears that
mesh with the gears on your gimbal.  For the Vidpro, you need a pair of
0.4M 60-tooth gears with a 6mm shaft.  You can find some here:

    https://www.aliexpress.com/item/2255800029508174.html

I didn't provide links for the other products, because they're easy to find,
but that particular search took *hours*.  :-)

If you are using the Vidpro hardware, you will need to replace the mounting
plates that hold the motor with a modified version that holds the encoders.
I have included the model files in the /hardware/ directory.  Mine are
3D-printed out of PETG.  Eventually, it would be nice to mill them out of
aluminum, but for now, the PETG plates are doing the job adequately.

If you are using other hardware, mounting the encoders is left as an
exercise for the reader.


Status:
-------

This software can now properly drive both motors, read the position
values from both CAN encoders, and move to an absolute position.  It
also provides support for reprogramming the CAN encoders to change
their IDs (which is necessary to do the first time you use them,
because they default to the same ID).

This code also takes advantage of motor speed calibration data collected
on first launch, which allows it to fairly precisely control the speed
of the motors in a nice s-curve.  This code has been tested only against
fake hardware, however, so its behavior is not guaranteed.  If for some
reason it does not work when run on actual hardware, you can go back to
the old behavior by setting the value of EXPERIMENTAL_TIME_PROGRESS to
zero (0) in config.h.

After I am able to test the code on the actual pan and tilt hardware
again, I'll do any final tuning on that.  Then, at some future date
when I have access to the Panasonic camera again, I need to verify
that the code works as expected against the real camera hardware.


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


Tally and Camera IP Configuration:
----------------------------------

OBS or
Tricaster:
    If you are using EITHER a Tricaster or OBS as your tally source, you must
    specify the source or scene name to monitor.  To do this, type:

    ./viscaptz --settallysourcename "sourceName"

    Because OBS scenes aren't always mapped 1:1 with sources, the OBS tally
    source lets you provide muliple scene names separated by a vertical pipe
    character (|).

OBS:
    If you are using OBS as your tally source, you must also configure the
    WebSocket URL and, if necessary, a password.  To do this, type:

        ./viscaptz --setobsurl "websocket URL"   # e.g. ws://127.0.0.1:4455/
        ./viscaptz --setobspass "thePassword"

Tricaster:
    If you are using a Tricaster switcher as your tally source, you must also
    provide its IP address.  To do this, type:

        ./viscaptz --settricasterip "IP address"

Panasonic camera:
    If you are using a Panasonic IP camera's pan, tilt, or zoom control or
    are using it as a tally source, you must provide its IP address.  To do
    this, type:

        ./viscaptz --setcameraip "IP address"


CANBus Configuration:
---------------------

Before you can use this tool, assuming you are using CAN-based encoders, you must
reprogram one of the encoders to have a different ID.  This also means that you
must appropriately mark the encoders so that you know which one is which.

Do do this, attach only one encoder initially, then type:

    ./viscaptz --reassign 1 2

This reassigns the encoder from ID 1 to 2.  Once you have done this, label that
encoder as the vertical (tilt) encoder.


Motor Configuration:
--------------------

Assuming you configure this software to use absolute positioning, the next thing
you must do is calibrate the motors and encoders.  This process takes most of an
hour under normal circumstances.  To do this, type:

    ./viscaptz --calibrate

    IMPORTANT:
       Once you start this operation, any existing
       calibration data is immediately discarded,
       because it would interfere with recalibration.

This software will ask you to move the gimbal first left, then right, then up, and
then down.  This defines the boundaries of motion for calibration purposes.  If
your boundaries are too small, it will not be able to get as accurate a measurement
of the motor speed (and will take a LOT longer to do so), so give it as much leeway
as you can.

This software automatically adapts to reverse-wired motors and encoders that count
in the wrong direction.  The way it does this is by learning which way the camera
moves when it applies positive values to the motor controller in response to you
moving the camera right or down.

    IMPORTANT:
       Regardless of how you pan or tilt the camera
       during the first part of calibration, the last
       horizontal motion must be to the right, and
       the last vertical motion must be downards.
       This is how the software learns which way is
       up (and left and right and down).

Ten seconds after you stop moving the camera, the software will store information
about the range of motion and whether the motors or encoders are reversed.  The
range of motion is used *only* for calibration, and this is for a good reason.
You may not end up lining up the encoders precisely.


Recentering:
------------

When you calibrate the motors, this software tells the encoders to use the current
position as their midpoint values.  This ensures that you don't run off either
end of the encoder.  If you find that recall tries to go the wrong direction,
your encoders have probably become severely decentered.  You can fix this by
typing:

    ./viscaptz --recenter

It might be worth always recentering every time you run the software, just to
minimize the risk of misbehavior.


Zoom Configuration:
-------------------

Because the zoom support also requires calibration to work properly, when you
change cameras, you need to recalibrate the zoom portion.  To do this, type:

    ./viscaptz --zoomcalibrate

This saves about forty minutes calibrating the pan and tilt motors.
