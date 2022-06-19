/*
 * This Arduino sketch is the software for a pan/tilt/zoom controller.  It uses wired motor control
 * for panning and tilting, and uses LANC for sending zoom commands to compatible cameras.  It
 * supports both the Bescor MP-101 (with direct wiring) and the Vidpro MH-430 (with a Pololu
 * Dual MC33926 Motor Driver Shield).  The LANC circuit can be found on the web at:
 *
 * http://controlyourcamera.blogspot.com/2011/02/arduino-controlled-video-recording-over.html
 *
 * Note that the record button in that circuit is unused, and does not need to be included.
 * The LANC portions of this code are verified to work with the Canon XH-A1, Panasonic AG-CX350,
 * and Sony DCR-TRV9.  It is likely that it will work with any LANC-compatible (a.k.a. Control-L)
 * camera.
 */
/*
 * PTZ Controller
 * © 2020 David A. Gatwood.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions
 *    and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions
 * and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Constants.  Do not change
#define PTZ_MODE_LANC 0
#define PTZ_MODE_P2 1
#define PTZ_MODE_PANASONIC_PTZ 2

#pragma mark - Configuration settings

#undef TEMPORARY_NO_LANC

// For MP-101 hardware configuration, set to true (different outputs).
// For Vidpro MH-430 hardware configuration with a motor controller, set to false.
#define USE_MP_101_MODE 0

// Enables VISCA remote control support.
// 
// This code can be used in conjunction with an Ethernet shield and a CAN bus shield
// to provide support for remote control.
//
// In this mode, this software listens for VISCA UDP messages on port 17479 (DG in ASCII)
// and sends back a response to the sending port/IP.
//
// It then uses LANC to control the pan, tilt, and zoom speed based on those
// commands unless the control switch pin (NETWORK_MODE_SWITCH_PIN) is grounded,
// in which case it provides local manual control.  Thus, the same control box
// can be used either within a few feet of the device by a nearby operator or
// remotely over the network by an operator far away.
#define ENABLE_VISCA 1

// Enables absolute position storage and retrieval of pan and tilt.
#define ENABLE_ABSOLUTE_POSITIONING 1

// Enables absolute zoom positioning and tally light status (Panasonic cameras only).
#define ENABLE_ABSOLUTE_ZOOM 1

// Sets the PTZ mode
//
// PTZ_MODE_LANC:          Normal LANC-only mode.
// PTZ_MODE_P2:            P2 protocol mode (experimental).
// PTZ_MODE_PANASONIC_PTZ: PTZ protocol mode (experimental).
//
#define ACTIVE_PTZ_MODE PTZ_MODE_PANASONIC_PTZ

// Uses the current tally state (Panasonic cameras only) to determine the maximum
// pan, tilt, and zoom speed.  Supported only if ACTIVE_PTZ_MODE == PTZ_MODE_P2.
#define USE_TALLY_FOR_SPEED_CONTROL 1

// Speed settings used for preset recall when speed is determined using tally lights.
#define LIVE_MAX_PAN_SPEED 8       // Max is 24
#define LIVE_MAX_TILT_SPEED 8      // Max is 24
#define LIVE_MAX_ZOOM_SPEED 3      // Max is 8
#define OFFLINE_MAX_PAN_SPEED 24   // Max is 24
#define OFFLINE_MAX_TILT_SPEED 24  // Max is 24
#define OFFLINE_MAX_ZOOM_SPEED 8   // Max is 8

// Set to 1 to make (manual) zooming faster when controlled over VISCA.  The VISCA
// protocol supports zoom speeds from -7 to 7.  The LANC protocol supports zoom
// speeds from 0 to 7 in each direction, plus a separate command for stopping,
// i.e. effectively the equivalent of -8 to 8.  By default, this software maps the
// VISCA speeds 1:1, leaving the fastest LANC zoom speed unused.  Setting this to
// 1 changes the mapping so that the slowest LANC zoom speed is unused instead.
#define ENABLE_FASTER_VISCA_ZOOM 0

#define USE_S_CURVE 1

// Enabling this causes it to send a CAN-bus command that tells a rotary encoder
// to change its device ID.
#undef reassignCANBusID
#define oldCANBusID 1
#define newCANBusID 2

#define panCANBusID 1
#define tiltCANBusID 2


int current_pan_position = 0;
int current_tilt_position = 0;

const double e_constant = 2.71828;

#if ENABLE_VISCA
    #define NETWORK_MODE_SWITCH_PIN 22
    #define SD_CS_PIN 6
    #define ETHERNET_CS_PIN 10
    #define CAN_BUS_CS_PIN 41

    #include <SD.h>
    
    // Configure the Ethernet shield here.
    const byte arduino_mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE };
    const byte arduino_ip[] = { 192, 168, 100, 252 };
    const byte arduino_dns[] = { 8, 8, 8, 8 };  // Unused.
    const byte arduino_gateway[] = { 192, 168, 100, 1 };
    const byte arduino_subnet[] = { 255, 255, 255, 0 };
    EthernetUDP listen_udp;
    const unsigned short listen_port = 17479;

    bool pan_recall_in_progress = false;  // True if a pan recall is in progress, else false.
    int current_pan_recall_start = -1;    // The starting position for the current pan recall.
    int current_pan_recall_target = -1;   // The target position for the current pan recall.
    int current_pan_speed = 0;            // The current pan speed provided by either
                                          // a pan/tilt speed command or by an active
                                          // preset recall.
    int current_pan_recall_max_speed = 0;  // The speed for the current motion, whether provided
                                           // in a VISCA absolute move command or loaded from
                                           // default_pan_recall_max_speed during a preset recall.
    int default_pan_recall_max_speed = 10; // The speed used for tilting during a preset recall.

    bool tilt_recall_in_progress = false;  // True if a tilt recall is in progress, else false.
    int current_tilt_recall_start = -1;    // The starting position for the current tilt recall.
    int current_tilt_recall_target = -1;   // The target position for the current tilt recall.
    int current_tilt_speed = 0;            // The current tilt speed provided by either
                                           // a pan/tilt speed command or by an active
                                           // preset recall.
    int current_tilt_recall_max_speed = 0;  // The speed for the current motion, whether provided
                                            // in a VISCA absolute move command or loaded from
                                            // default_pan_recall_max_speed during a preset recall.
    int default_tilt_recall_max_speed = 10;  // The speed used for tilting during a preset recall.

    int current_tally_state;              // 0 if off, 5 if on program, 6 if on preview.

    typedef struct {
      uint8_t cmd[2];
      uint16_t len;
      uint32_t sequence_number;
      uint8_t data[30];  // In theory, if IPv6, could be up to 65519, but not enough RAM for that.
    } visca_cmd_t;
    
    typedef struct {
      uint8_t cmd[2];
      uint16_t len;  // Big endian!
      uint32_t sequence_number;
      uint8_t data[30];  // In theory, if IPv6, could be up to 65527, but not enough RAM for that.
    } visca_response_t;

    typedef struct {
      int pan_position;
      int tilt_position;
      int zoom_position;
    } position_data_blob_t;

    #define bcopy(src, dest, len) memcpy(dest, src, len)
    #define htons(a) ((a & 0x00ff) << 8) | ((a & 0xff00) >> 8)

    int current_zoom_position = 0;        // Absolute position provided by P2 data, or 0.

    #if ENABLE_ABSOLUTE_POSITIONING
      MCP2515 mcp2515(CAN_BUS_CS_PIN);
    #endif

    #if ENABLE_ABSOLUTE_ZOOM
        // The IP address of the Panasonic camera, if applicable.
        const byte camera_ip[] = { 192, 168, 100, 14 };
        IPAddress panasonic_address = IPAddress(camera_ip);

        EthernetUDP panasonic_udp;
        const unsigned short panasonic_p2_port = 49153;

        bool zoom_recall_in_progress = false;  // True if a zoom recall is in progress, else false.
        int current_zoom_recall_start = -1;    // The starting position for the current zoom recall.
        int current_zoom_recall_target = -1;   // The target position for the current zoom recall.
        int current_zoom_speed = 0;            // The current zoom speed provided by either
                                               // a zoom speed command or by an active
                                               // preset recall.
        int current_zoom_recall_max_speed = 0;  // The speed for the current motion, whether provided
                                                // in a VISCA absolute move command or loaded from
                                                // default_pan_recall_max_speed during a preset recall.
        int default_zoom_recall_max_speed = 10;  // The speed used for tilting during a preset recall.

        typedef struct p2_optical_data {
            uint8_t type;
            uint8_t size;
            uint8_t reserved_1;
            uint8_t reserved_2;
            uint8_t reserved_3;
            uint8_t focus_ft_high;
            uint8_t focus_ft_low;
            uint8_t focus_in;
            uint8_t iris_high;
            uint8_t iris_low;
            uint8_t focus_exp : 4;
            uint8_t focus_sig_high : 4;
            uint8_t focus_sig_low;
            uint8_t zoom_exp : 4;
            uint8_t zoom_sig_high : 4;
            uint8_t zoom_sig_low;
            uint8_t lens_model_name[30];
            uint8_t master_gain_high;
            uint8_t master_gain_low;
            uint8_t shutter_speed_high;
            uint8_t shutter_speed_low;
            uint8_t reserved_4 : 4;
            uint8_t shutter_mode : 4;
            uint8_t shutter_speed_decimal;
            uint8_t gamma_mode;
            uint8_t reserved_5;
            uint8_t atw_wb : 2;
            uint8_t under_over : 2;
            uint8_t color_temp_high : 4;
            uint8_t color_temp_low;
            uint8_t nd_filter;
            uint8_t cc_filter;
            uint8_t iris_info : 2;
            uint8_t focus_info : 2;
            uint8_t zoom_info : 2;
            uint8_t model_info : 2;
            uint8_t agc_enabled : 1;
            uint8_t gain_mode : 1;
            uint8_t iA_zoom_info : 2;
            uint8_t rb_gain_info : 1;
            uint8_t iris_type : 1;
            uint8_t iZoom_sm : 1;
            uint8_t nd_disp_type : 1;
            uint8_t vfr_mode : 4;
            uint8_t vfr_frame_rate_high : 4;
            uint8_t vfr_frame_rate_low;
            uint8_t iA_zoom_exp : 4;
            uint8_t iA_zoom_high : 4;
            uint8_t iA_zoom_low;
            uint8_t iso_select : 4;
            uint8_t e1_gain_mode : 1;
            uint8_t awb_color_temp : 1;
            uint8_t color_temp_mag : 1;
            uint8_t awb_channel : 1;
            uint8_t color_temp_GMg_high;
            uint8_t color_temp_GMg_low;
        } p2_optical_data_t;

        typedef struct p2_camera_data {
          uint8_t type;
          uint8_t size;
          uint8_t junk1;
          uint8_t junk2;
          uint8_t tally_and_thumbnail;
          uint8_t junk3[30];
        } p2_camera_data_t;
    #endif
#endif

bool useNetwork = false;  // Updated each loop from NETWORK_MODE_SWITCH_PIN if networking is enabled, else false.

const int kMotorMax = 400;
const double kMotorSpeedPower = 2.0;
const int kMinimumMotorThreshold = 50;  // Ensure that values close to the center are treated as stopped.

#if ACTIVE_PTZ_MODE == PTZ_MODE_PANASONIC_PTZ
  const int kZoomMax = 49;
#else
  const int kZoomMax = 9;
#endif
const double kZoomSpeedPower = 2.0;
const int kMinimumZoomThreshold = 50;  // Ensure that values close to the center are treated as stopped.

bool networkMode = false;

#if !USE_MP_101_MODE
DualMC33926MotorShield md;
#endif

// The analog pins are the same in all configurations.
#define MP101AnalogPowerPin A0 
#define MP101AnalogGroundPin A1
#define horizontalSensePin A3
#define verticalSensePin A2
#define zoomSensePin A4

#if ENABLE_VISCA && USE_MP_101_MODE
// VISCA requires a Mega 2560, because there aren't enough pins on a normal Arduino.
// By using the same pin order, you can just move the connector over.
// One connector is on the odd pins from one end.  The other is on the even pins
// near the other end, leaving the last four pins clear because they are needed for SPI.

#if ACTIVE_PTZ_MODE == PTZ_MODE_LANC || ACTIVE_PTZ_MODE == PTZ_MODE_P2
#define LANC_ENABLED 1
#endif

#if LANC_ENABLED
#define lancOutputPin 33
#define lancInputPin 42
#endif

#define MP101UpPin 25
#define MP101DownPin 27
#define MP101LeftPin 29
#define MP101RightPin 31

#elif ENABLE_VISCA  // && ! USE_MP_101_MODE
#if LANC_ENABLED
#define lancOutputPin 48
#define lancInputPin 42
#endif

#elif USE_MP_101_MODE
// The MP101 setup doesn't have the motor control on pin 8, so keep LANC on a single connector.
#if LANC_ENABLED
#define lancOutputPin 8
#define lancInputPin 11
#endif
#define MP101UpPin 2
#define MP101DownPin 3
#define MP101LeftPin 4
#define MP101RightPin 5

#else  // !ENABLE_VISCA && ! USE_MP_101_MODE
// Use the only free pin we have on a non-Mega Arduino.
#if LANC_ENABLED
#define lancOutputPin 6
#define lancInputPin 11
#endif
#endif


typedef enum {
  kDebugModePan = 0x1,
  kDebugModeTilt = 0x2,
  kDebugModeZoom = 0x4,
  kDebugScaling = 0x8,
  kDebugLANC = 0x16,
} debugMode;

int debugPanAndTilt = 0; // kDebugModePan;// kDebugModeZoom;  // Bitmap from debugMode.

void setup() {
  // Initialize the motor controller
  Serial.begin(9600);

#if USE_MP_101_MODE
  pinMode(MP101AnalogPowerPin, OUTPUT);
  digitalWrite(MP101AnalogPowerPin, HIGH);
  pinMode(MP101AnalogGroundPin, OUTPUT);
  digitalWrite(MP101AnalogGroundPin, LOW);
  pinMode(MP101UpPin, OUTPUT);
  pinMode(MP101DownPin, OUTPUT);
  pinMode(MP101LeftPin, OUTPUT);
  pinMode(MP101RightPin, OUTPUT);
#else  // !USE_MP_101_MODE
  Serial.print(F("Initializing motor controller."));
  md.init();
  Serial.print(F("Done."));
#endif

#if ENABLE_VISCA
  pinMode(NETWORK_MODE_SWITCH_PIN, INPUT_PULLUP);

  pinMode(11,INPUT);
  pinMode(12,INPUT);
  pinMode(13,INPUT);
  pinMode(53,OUTPUT);
  digitalWrite(53,HIGH);

  #if ENABLE_ABSOLUTE_POSITIONING
    configureCANBus();
  #endif
#endif

#ifndef TEMPORARY_NO_LANC
#if LANC_ENABLED
  Serial.print(F("Initializing LANC."));
  lancSetup();
  Serial.print(F("Done."));
#endif
#endif  // TEMPORARY_NO_LANC

#if ENABLE_VISCA
  pinMode(SD_CS_PIN, OUTPUT);
  pinMode(ETHERNET_CS_PIN, OUTPUT);

  SD.begin(SD_CS_PIN);

  Ethernet.begin(arduino_mac, arduino_ip, arduino_dns, arduino_gateway, arduino_subnet);
  disableSDCardReader();

  listen_udp.begin(listen_port);
#if ENABLE_ABSOLUTE_ZOOM
  Serial.println("Enabling Panasonic Networking.");
  panasonic_udp.begin(panasonic_p2_port);
  Serial.println("Done.");
#endif  // ENABLE_ABSOLUTE_ZOOM
#endif  // ENABLE_VISCA

}

void stopIfFault()
{
#if !USE_MP_101_MODE
  if (md.getFault()) {
    md.setM1Speed(0);
    md.setM2Speed(0);
    Serial.println(F("fault"));
    while(1);
  }
#endif
}

void loop() {
#if LANC_ENABLED
  #if defined(TEMPORARY_NO_LANC) || !ENABLE_VISCA
    useNetwork = true;
  #else  // !TEMPORARY_NO_LANC
    useNetwork = digitalRead(NETWORK_MODE_SWITCH_PIN);
  #endif  // TEMPORARY_NO_LANC

  if (!useNetwork) {
    handleZoom();
  }
#endif
  handleNonLANCOperations();
}

void handleNonLANCOperations() {
#if ENABLE_VISCA
#if ENABLE_ABSOLUTE_POSITIONING
  updatePanAndTiltPosition();
#if ENABLE_ABSOLUTE_ZOOM
#if ACTIVE_PTZ_MODE == PTZ_MODE_P2
  updateZoomPositionFromP2Data();
#elif ACTIVE_PTZ_MODE == PTZ_MODE_PANASONIC_PTZ
  updateZoomPositionByHTTP();
#else
  #error ENABLE_ABSOLUTE_ZOOM requires ACTIVE_PTZ_MODE == (PTZ_MODE_P2 | PTZ_MODE_PANASONIC_PTZ)
#endif
#endif  // updatePanAndTiltPosition
#endif  // ENABLE_ABSOLUTE_POSITIONING
  handleNetworkControlRequests();
#endif  // ENABLE_VISCA
  if (useNetwork) {
#if (ENABLE_VISCA && ENABLE_ABSOLUTE_POSITIONING)
    handleRecallUpdates();
#endif
  } else {
    handleMotorControl();
  }
}

#pragma mark - Motor control

void handleMotorControl() {
  int rawHorizontalValue = analogRead(horizontalSensePin);
  setHorizontalSpeedWithScaling(rawHorizontalValue);

  int rawVerticalValue = analogRead(verticalSensePin);
  setVerticalSpeedWithScaling(rawVerticalValue);
}

void setHorizontalSpeedWithScaling(int rawHorizontalValue) {
  int scaledHorizontalSpeed = scaleHorizontalSpeed(rawHorizontalValue);
  setHorizontalSpeed(scaledHorizontalSpeed);
}

int maxPanTiltValue(void) {
#if USE_MP_101_MODE
  return 255
#else  // !USE_MP_101_MODE
  return kMotorMax;
#endif  // USE_MP_101_MODE
}

int scaleHorizontalSpeed(int rawHorizontalValue) {
  int horizontalValue = computeScaledSpeedValue(1023 - rawHorizontalValue, kMotorSpeedPower, kMinimumMotorThreshold,
                                                maxPanTiltValue());  // Reversed.
  logValues("horizontal: ", rawHorizontalValue, horizontalValue, kDebugModePan);
  return horizontalValue;
}

void setHorizontalSpeed(int horizontalValue) {
#if USE_MP_101_MODE
    setMP101Horizontal(horizontalValue);
#else
    md.setM2Speed(horizontalValue);
#endif
}

void setVerticalSpeedWithScaling(int rawHorizontalValue) {
  int scaledVerticalSpeed = scaleHorizontalSpeed(rawHorizontalValue);
  setVerticalSpeed(scaledVerticalSpeed);
}

int scaleVerticalSpeed(int rawVerticalValue) {
  int verticalValue = computeScaledSpeedValue(rawVerticalValue, kMotorSpeedPower, kMinimumMotorThreshold,
                                              maxPanTiltValue());
  logValues("vertical: ", rawVerticalValue, verticalValue, kDebugModeTilt);
  return verticalValue;
}

void setVerticalSpeed(int verticalValue) {
#if 0
    setMP101Vertical(verticalValue);
#else
    md.setM1Speed(verticalValue);
#endif
}

// By stripping the sign and scaling the value to the range 0..1, we can raise the input value to
// an arbitrary power to change the curve. We then multiply that by maxScale and provide the
// sign to get a value in the range [-maxScale .. maxScale].  (Positive and negative input values
// are not exactly balanced numerically, so we throw away the largest possible value on the positive
// side.)
int computeScaledSpeedValue(int bareValue, int power, int threshold, int maxScale) {
  int rawSpeed = abs(bareValue - 512);  // Range -512 .. 511
  bool direction = (bareValue >= 512);  // Equal number of positive and negative values.

  if (debugPanAndTilt & kDebugScaling) {
    Serial.print(F("bareValue: "));
    Serial.print(rawSpeed);
    Serial.print(F(" rawSpeed: "));
    Serial.print(rawSpeed);
    Serial.print(F(" direction: "));
    Serial.println(direction ? F("true") : F("false"));
  }

  if (rawSpeed <= threshold) {
    return 0;
  }
  int maxSpeed = 511 - threshold;
  double speedPercentage = ((double)rawSpeed / (double)maxSpeed);
  if (debugPanAndTilt & kDebugScaling) {
    Serial.print(F("Percent: "));
    Serial.println(speedPercentage);
  }

  speedPercentage = pow(speedPercentage, power);
  
  float scaledSpeed = (speedPercentage * maxScale);
  int scaledSpeedInt = (int)scaledSpeed;
  if (scaledSpeed > maxScale) {
    scaledSpeedInt = maxScale;
  }
  return direction ? scaledSpeedInt : (0 - scaledSpeedInt);
}

#pragma mark - MP101 motor control

#if USE_MP_101_MODE
void setMP101Horizontal(int horizontalValue) {
  int leftPinValue = (horizontalValue < 0) ? -horizontalValue : 0;
  int rightPinValue = (horizontalValue > 0) ? horizontalValue : 0;
  analogWrite(MP101LeftPin, leftPinValue);
  analogWrite(MP101RightPin, rightPinValue);
  if (0) {
    Serial.print(F("Left: "));
    Serial.print(leftPinValue);
    Serial.print(F(" Right: "));
    Serial.println(rightPinValue);
  }
}

void setMP101Vertical(int verticalValue) {
  int upPinValue = (verticalValue > 0) ? verticalValue : 0;
  int downPinValue = (verticalValue < 0) ? -verticalValue : 0;

  analogWrite(MP101UpPin, upPinValue);
  analogWrite(MP101DownPin, downPinValue);
  if (0) {
    Serial.print(F("Up: "));
    Serial.print(upPinValue);
    Serial.print(F(" Down: "));
    Serial.println(downPinValue);
  }
}
#endif

#pragma mark - Logging

void logValues(char *label, int rawValue, int scaledValue, debugMode mode) {
  if (debugPanAndTilt & mode) {
    Serial.write(label);
    Serial.print(rawValue);
    Serial.print(F(" -> "));
    Serial.print(scaledValue);
    Serial.println(F(""));
  }
}

#pragma mark - Zoom control (generic)

int scaleZoomSpeed(int rawZoomValue) {
#if !USE_MP_101_MODE
    rawZoomValue = 1023 - rawZoomValue;
#endif
  int zoomSpeed = computeScaledSpeedValue(rawZoomValue, kZoomSpeedPower, kMinimumZoomThreshold, kZoomMax);  // Reversed.
  logValues("zoom: ", rawZoomValue, zoomSpeed, kDebugModeZoom);
  return zoomSpeed;
}

#pragma mark - LANC

#if LANC_ENABLED
extern boolean ZOOM_IN_0[];
extern boolean ZOOM_IN_1[];
extern boolean ZOOM_IN_2[];
extern boolean ZOOM_IN_3[];
extern boolean ZOOM_IN_4[];
extern boolean ZOOM_IN_5[];
extern boolean ZOOM_IN_6[];
extern boolean ZOOM_IN_7[];
extern boolean ZOOM_OUT_0[];
extern boolean ZOOM_OUT_1[];
extern boolean ZOOM_OUT_2[];
extern boolean ZOOM_OUT_3[];
extern boolean ZOOM_OUT_4[];
extern boolean ZOOM_OUT_5[];
extern boolean ZOOM_OUT_6[];
extern boolean ZOOM_OUT_7[];
extern boolean IDLE_COMMAND[];

void handleZoom() {
  int rawZoomValue = analogRead(zoomSensePin);
  int scaledZoomSpeed = scaleZoomSpeed(rawZoomValue);
  sendZoomSpeedViaLANC(scaledZoomSpeed);
}

void sendZoomSpeedViaLANC(int zoomSpeed) {
  switch (zoomSpeed) {
    case -9:  // Extremely unlikely
    case -8:
      lancCommand(ZOOM_OUT_7);
      break;
    case -7:
      lancCommand(ZOOM_OUT_6);
      break;
    case -6:
      lancCommand(ZOOM_OUT_5);
      break;
    case -5:
      lancCommand(ZOOM_OUT_4);
      break;
    case -4:
      lancCommand(ZOOM_OUT_3);
      break;
    case -3:
      lancCommand(ZOOM_OUT_2);
      break;
    case -2:
      lancCommand(ZOOM_OUT_1);
      break;
    case -1:
      lancCommand(ZOOM_OUT_0);
      break;
    case 0:
      lancCommand(IDLE_COMMAND);
      break;
    case 1:
      lancCommand(ZOOM_IN_0);
      break;
    case 2:
      lancCommand(ZOOM_IN_1);
      break;
    case 3:
      lancCommand(ZOOM_IN_2);
      break;
    case 4:
      lancCommand(ZOOM_IN_3);
      break;
    case 5:
      lancCommand(ZOOM_IN_4);
      break;
    case 6:
      lancCommand(ZOOM_IN_5);
      break;
    case 7:
      lancCommand(ZOOM_IN_6);
      break;
    case 8:
    case 9:  // Extremely unlikely
      lancCommand(ZOOM_IN_7);
      break;
    default:
      break;
  }
}

#pragma mark - LANC Library

/*******************************************************************************************
 * All code below this point is derived from an existing LANC example, with the loop()     *
 * method stripped out, the setup() method renamed to lancSetup(), and shorter timeouts    *
 * added so that a failed LANC connection won't prevent pan and tilt control from working. *
 * The copyright notice and licensing terms are included below.                            *
 *******************************************************************************************/

/*
 SIMPLE LANC REMOTE
 Version 1.0
 Sends LANC commands to the LANC port of a video camera.
 Tested with a Canon XF300 camcorder
 For the interface circuit interface see 
 http://controlyourcamera.blogspot.com/2011/02/arduino-controlled-video-recording-over.html
 Feel free to use this code in any way you want.
 2011, Martin Koch

 "LANC" is a registered trademark of SONY.
 CANON calls their LANC compatible port "REMOTE".
*/
int cmdRepeatCount;
int bitDuration = 104; //Duration of one LANC bit in microseconds. 


//LANC commands byte 0 + byte 1
//Tested with Canon XF300

//Start-stop video recording
boolean REC[] = {LOW,LOW,LOW,HIGH,HIGH,LOW,LOW,LOW,   LOW,LOW,HIGH,HIGH,LOW,LOW,HIGH,HIGH};          // 18 33

boolean IDLE_COMMAND[] = {LOW,LOW,LOW,LOW,LOW,LOW,LOW,LOW,   LOW,LOW,LOW,LOW,LOW,LOW,LOW,LOW};               // 00 00

//Zoom in from slowest to fastest speed
boolean ZOOM_IN_0[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,LOW,LOW,LOW,LOW,LOW};        // 28 00
boolean ZOOM_IN_1[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,LOW,LOW,LOW,HIGH,LOW};       // 28 02
boolean ZOOM_IN_2[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,LOW,LOW,HIGH,LOW,LOW};       // 28 04
boolean ZOOM_IN_3[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,LOW,LOW,HIGH,HIGH,LOW};      // 28 06
boolean ZOOM_IN_4[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,LOW,HIGH,LOW,LOW,LOW};       // 28 08
boolean ZOOM_IN_5[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,LOW,HIGH,LOW,HIGH,LOW};      // 28 0A
boolean ZOOM_IN_6[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,LOW,HIGH,HIGH,LOW,LOW};      // 28 0C
boolean ZOOM_IN_7[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,LOW,HIGH,HIGH,HIGH,LOW};     // 28 0E

//Zoom out from slowest to fastest speed
boolean ZOOM_OUT_0[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,HIGH,LOW,LOW,LOW,LOW};      // 28 10
boolean ZOOM_OUT_1[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,HIGH,LOW,LOW,HIGH,LOW};     // 28 12
boolean ZOOM_OUT_2[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,HIGH,LOW,HIGH,LOW,LOW};     // 28 14
boolean ZOOM_OUT_3[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,HIGH,LOW,HIGH,HIGH,LOW};    // 28 16
boolean ZOOM_OUT_4[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,HIGH,HIGH,LOW,LOW,LOW};     // 28 18
boolean ZOOM_OUT_5[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,HIGH,HIGH,LOW,HIGH,LOW};    // 28 1A
boolean ZOOM_OUT_6[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,HIGH,HIGH,HIGH,LOW,LOW};    // 28 1C
boolean ZOOM_OUT_7[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,LOW,LOW,HIGH,HIGH,HIGH,HIGH,LOW};   // 28 1E

//Focus control. Camera must be switched to manual focus
boolean FOCUS_NEAR[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,HIGH,LOW,LOW,LOW,HIGH,HIGH,HIGH};   // 28 47
boolean FOCUS_FAR[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,HIGH,LOW,LOW,LOW,HIGH,LOW,HIGH};     // 28 45

boolean FOCUS_AUTO[] = {LOW,LOW,HIGH,LOW,HIGH,LOW,LOW,LOW,   LOW,HIGH,LOW,LOW,LOW,LOW,LOW,HIGH};     // 28 41

//boolean POWER_OFF[] = {LOW,LOW,LOW,HIGH,HIGH,LOW,LOW,LOW,   LOW,HIGH,LOW,HIGH,HIGH,HIGH,HIGH,LOW}; // 18 5E
//boolean POWER_ON[] = {LOW,LOW,LOW,HIGH,HIGH,LOW,LOW,LOW,   LOW,HIGH,LOW,HIGH,HIGH,HIGH,LOW,LOW};   // 18 5C
       // Doesn't work because there's no power supply from the LANC port when the camera is off
//boolean POWER_OFF2[] = {LOW,LOW,LOW,HIGH,HIGH,LOW,LOW,LOW,   LOW,LOW,HIGH,LOW,HIGH,LOW,HIGH,LOW};  // 18 2A
       // Turns the XF300 off and then on again
//boolean POWER_SAVE[] = {LOW,LOW,LOW,HIGH,HIGH,LOW,LOW,LOW,   LOW,HIGH,HIGH,LOW,HIGH,HIGH,LOW,LOW}; // 18 6C
       // Didn't work


void lancSetup() {
  pinMode(lancInputPin, INPUT); // listens to the LANC line
  pinMode(lancOutputPin, OUTPUT); // writes to the LANC line
  digitalWrite(lancOutputPin, LOW); // set LANC line to +5V
  delay(5000); // Wait for camera to power up completly
  bitDuration = bitDuration - 8; // Writing to the digital port takes about 8 microseconds,
                                 // so only 96 microseconds are left for each bit

  pinMode(zoomSensePin, INPUT); // listens to the LANC line
}

void lancCommand(boolean lancBit[]) {
#ifndef TEMPORARY_NO_LANC
  cmdRepeatCount = 0;

while (cmdRepeatCount < 5) {  // repeat 5 times to make sure the camera accepts the command
  // If this breaks, change the timeout (which didn't exist before, and thus defaulted to one second).
  // This gives us 40 adjustments per second, and is 5000 msec longer than a full LANC cycle, which
  // means this should kick in only if the LANC hardware is not attached.
  int missed = 0;
  unsigned long duration = 0;
  while (((duration = pulseIn(lancInputPin, HIGH, 25000))) < 5000) {   
    // "pulseIn, HIGH" catches any 0V TO +5V TRANSITION and waits until the LANC line goes back to 0V 
    // "pulseIn" also returns the pulse duration so we can check if the previous +5V duration was long enough (>5ms)
    // to be the pause before a new 8 byte data packet.  Loop till pulse duration is >5ms

    // Run the motor at least 100 times per second anyway while we're waiting.
    if (duration == 0) {
      if (debugPanAndTilt & kDebugLANC) {
        Serial.println(F("No pulse"));
      }
      handleNonLANCOperations();
    }
  }

  // LOW after long pause means the START bit of Byte 0 is here
  delayMicroseconds(bitDuration);  // wait START bit duration

  // Write the 8 bits of byte 0 
  // Note that the command bits have to be put out in reverse order with the least significant, right-most bit (bit 0) first
  for (int i=7; i>-1; i--) {
    digitalWrite(lancOutputPin, lancBit[i]);  //Write bits. 
    delayMicroseconds(bitDuration); 
  }

  // Byte 0 is written now put LANC line back to +5V
  digitalWrite(lancOutputPin, LOW);
  delayMicroseconds(10); //make sure to be in the stop bit before byte 1

  while (digitalRead(lancInputPin)) { 
    // Loop as long as the LANC line is +5V during the stop bit
  }

  // 0V after the previous stop bit means the START bit of Byte 1 is here
  delayMicroseconds(bitDuration);  // wait START bit duration

  // Write the 8 bits of Byte 1
  // Note that the command bits have to be put out in reverse order with the least significant, right-most bit (bit 0) first
  for (int i=15; i>7; i--) {
    digitalWrite(lancOutputPin,lancBit[i]);  // Write bits 
    delayMicroseconds(bitDuration);
  }

  // Byte 1 is written now put LANC line back to +5V
  digitalWrite(lancOutputPin, LOW); 

  cmdRepeatCount++;  // increase repeat count by 1

  /* Control bytes 0 and 1 are written, now don’t care what happens in Bytes 2 to 7
     and just wait for the next start bit after a long pause to send the first two command bytes again. */
  } // While cmdRepeatCount < 5
#endif
}

#endif  // LANC_ENABLED

#if ENABLE_VISCA
#if ENABLE_ABSOLUTE_POSITIONING

struct can_frame canBusFrameMake(uint32_t can_id, uint8_t can_dlc, uint8_t *data) {
  struct can_frame message;
  message.can_id = can_id;
  message.can_dlc = can_dlc;
  memcpy(message.data, data, sizeof(message.data));
  return message;
}

void configureCANBus(void) {
  // Reset the CAN bus bridge and configure it for 500 kbps operation.
  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS);
  mcp2515.setNormalMode();

  #ifdef reassignCANBusID
  // When programming them:
  // 
  // 0.  Configure for 500 kbps.
  // 1.  To reassign device XX to be device YY
  //         Sent:     0x04 0xXX 0x02 0xYY
  //         Response: 0x04 0xYY 0x02 0x00
  //     Last byte is an error code.  If you get back this:
  //         Response: 0x04 0xXX 0x02 0xZZ
  //     with some error code ZZ from the original device id (XX), 
  //     report the error.
  uint8_t data[8] = { 0x04, oldCANBusID, 0x02, newCANBusID, 0, 0, 0, 0 };
  struct can_frame message = canBusFrameMake(oldCANBusID, 4, data);

  if (mcp2515.sendMessage(&message) == MCP2515::ERROR_OK) {
    struct can_frame response;
    if (mcp2515.readMessage(&response) == MCP2515::ERROR_OK) {
      if (response.data[0] == 0x4 && response.data[1] == oldCANBusID &&
          response.data[2] == 0x2 && response.data[3] == 0 &&
          response.can_id == newCANBusID) {
        Serial.println("Reassignment successful.");
      } else {
        Serial.print("Reassignment failed with error ");
        Serial.println(response.data[3]);
      }
    }
  }
  #endif  // reassignCANBusID
}

// Stores data into current_pan_position and current_tilt_position.
void updatePanAndTiltPosition(void) {
// Information about the BR38-COM series of multi-turn CAN-bus encoders:
//
// 1.  Send broadcast (destination ID 0).
//     Command: 0x1
//     For device XX:
//         Sent:     0x04 0xXX 0x01 0x00
//     For value 0xAABBCCDD from device XX:
//         Response: 0x07 0xXX 0x01 0xDD 0xCC 0xBB 0xAA


  uint8_t data[8] = { 0x04, 0x00, 0x01, 0, 0, 0, 0, 0 };
  struct can_frame message = canBusFrameMake(0x00, 4, data);

  if (mcp2515.sendMessage(&message) == MCP2515::ERROR_OK) {
    struct can_frame response;
    for (int i = 0; i < 2; i++) {
      if (mcp2515.readMessage(&response) == MCP2515::ERROR_OK) {
        if (response.data[0] == 0x7 && response.data[2] == 0x1) {
          long value = response.data[3] | (response.data[4] << 8) |
              (response.data[5] << 16) | (response.data[6] << 24);
          if (response.data[1] == panCANBusID) {
            current_pan_position = value;
          } else if (response.data[1] == tiltCANBusID) {
            current_tilt_position = value;
          } else {
            Serial.print("Received message from unknown CAN bus ID ");
            Serial.println(response.data[1]);
          }
        } else {
          char buf[120];
          sprintf(buf, "Unknown response: %02x %02x %02x %02x %02x %02x %02x %02x from device %ld with length code %d",
                       response.data[0], response.data[1], response.data[2], response.data[3],
                       response.data[4], response.data[5], response.data[6], response.data[7],
                       response.can_id, response.can_dlc);
          Serial.println(buf);
        }
      }
    }
  }
}

#if ENABLE_ABSOLUTE_ZOOM

#if ACTIVE_PTZ_MODE == PTZ_MODE_P2

// Helper function that computes the zoom position from a P2 data packet.
int zoomPositionFromData(p2_optical_data_t *optical_data) {
    int bits = optical_data->zoom_sig_high << 8 | optical_data->zoom_sig_low;
    int exponent = optical_data->zoom_exp;

    int exp = optical_data->zoom_exp;  // 1..15 repeating, but starts at 3.
    int high = optical_data->zoom_sig_high - 11;  // 0 or 1.
    int rough_value = ((high * 15) + exp) - 3;
    int low = optical_data->zoom_sig_low;  // 0..255.  Starts at 112.

    return ((rough_value << 8) + low) - 112;
}

// Helper function that tells the Panasonic camera to start providing its zoom position.
// This should be called about once every 5 seconds.  If you do not call it for 15
// seconds, the camera stops providing zoom position data.
bool enableZoomPositionupdates(void) {
  char requestBuf[3] = { 0xff, 0x01, 0xff };

  if (!panasonic_udp.beginPacket(panasonic_address, panasonic_p2_port)) {
    return false;
  }
  panasonic_udp.write(requestBuf, 3);
  return (panasonic_udp.endPacket() == 1);
}

bool timeToRequestZoomPosition(void) {
  static long lastChecked = 0;
  static bool hasChecked = false;

  // Check once per 5 seconds.
  if (!hasChecked || (millis() > lastChecked + 5000)) {
    hasChecked = true;
    return true;
  }
  return false;
}

void updateZoomPositionFromP2Data(void) {
  uint8_t packetBuffer[UDP_TX_PACKET_MAX_SIZE];  // buffer to hold incoming packet,
  int packetSize;

  if (timeToRequestZoomPosition()) {
    enableZoomPositionupdates();
  }
  
  while ((packetSize = panasonic_udp.parsePacket()) > 0) {
    panasonic_udp.read((unsigned char *)packetBuffer, UDP_TX_PACKET_MAX_SIZE);
    uint8_t message_type = packetBuffer[0];
    if (message_type == 6) {
      p2_optical_data_t *optical_data = (p2_optical_data_t *)packetBuffer;
      current_zoom_position = zoomPositionFromData(optical_data);
      Serial.print("@@@ Zoom position: ");
      Serial.println(current_zoom_position);
    } else if (message_type == 0x0A) {
      p2_camera_data_t *camera_data = (p2_camera_data_t *)packetBuffer;
      // Red tally is bit 0 (value & 0x1).
      // Green tally is bit 1. (value & 0x2).
      // Return 5 for red (active).
      // Return 6 for green (preview).
      int raw_tally_state = (camera_data->tally_and_thumbnail & 0x03);
      current_tally_state = raw_tally_state ? (raw_tally_state | 0x4) : 0;
#if USE_TALLY_FOR_SPEED_CONTROL
      if (current_tally_state & 0x1) {
        current_pan_recall_max_speed = LIVE_MAX_PAN_SPEED;
        current_tilt_recall_max_speed = LIVE_MAX_TILT_SPEED;
        current_zoom_recall_max_speed = LIVE_MAX_ZOOM_SPEED;
      } else {
        current_pan_recall_max_speed = OFFLINE_MAX_PAN_SPEED;
        current_tilt_recall_max_speed = OFFLINE_MAX_TILT_SPEED;
        current_zoom_recall_max_speed = OFFLINE_MAX_ZOOM_SPEED;        
      }
#endif
    }
  }
}

#elif ACTIVE_PTZ_MODE == PTZ_MODE_PANASONIC_PTZ

// Range -49 to 49.
void sendZoomSpeedViaPTZ(int zoomSpeed) {
  // Update command: http://IPADDR/cgi-bin/aw_ptz?cmd=%23Z*&res=1 where * is the speed.
  zoomSpeed = (zoomSpeed < -49) ? -49 : (zoomSpeed > 49) ? 49 : zoomSpeed;
  char queryPath[100];
  sprintf(queryPath, "/cgi-bin/aw_ptz?cmd=%%23Z%d&res=1", zoomSpeed + 50);
  EthernetClient ethernetClient;
  HttpClient client(ethernetClient);
  client.get(panasonic_address, queryPath);
  if (client.available()) {
    char buf[100];
    int len = client.read(buf, 99);
    buf[len] = '\0';
    if (len < 3) {
      return;
    }
    int value = atoi(&buf[2]);
    current_zoom_position = value;
  }
  client.stop();
}

void updateZoomPositionByHTTP(void) {
  // Update command: http://IPADDR/cgi-bin/aw_ptz?cmd=%23GZ&res=1
  const char *const queryPath = "/cgi-bin/aw_ptz?cmd=%23GZ&res=1";
  EthernetClient ethernetClient;
  HttpClient client(ethernetClient);
  client.get(panasonic_address, queryPath);
  if (client.available()) {
    char buf[100];
    int len = client.read(buf, 99);
    buf[len] = '\0';
    if (len < 3) {
      return;
    }
    int value = atoi(&buf[2]);
    current_zoom_position = value;
  }
  client.stop();
}

#endif  // ACTIVE_PTZ_MODE == PTZ_MODE_P2/PTZ_MODE_PANASONIC_PTZ

#endif  // ENABLE_ABSOLUTE_ZOOM

#endif  // ENABLE_ABSOLUTE_POSITIONING

void cancel_active_recall() {
#if ENABLE_ABSOLUTE_POSITIONING
  pan_recall_in_progress = false;
  tilt_recall_in_progress = false;
#if ENABLE_ABSOLUTE_ZOOM
  zoom_recall_in_progress = false;
#endif  // ENABLE_ABSOLUTE_ZOOM
#endif  // ENABLE_ABSOLUTE_POSITIONING
}

// Handles network requests from the main controller.
void handleNetworkControlRequests(void) {
  int packetSize;
  while ((packetSize = listen_udp.parsePacket()) > 0) {
    visca_cmd_t visca_command;
    listen_udp.read((unsigned char *)&visca_command, sizeof(visca_command));
    handleVISCAPacket(visca_command);
  }
}

bool handleVISCAPacket(visca_cmd_t command) {
  bool retval = true;
  if (command.cmd[0] != 0x1) {
    fprintf(stderr, "INVALID[0 = %02x]\n", command.cmd[0]);
    retval = false;
  } else if (command.cmd[1] == 0x10) {
    retval = handleVISCAInquiry(command.data, htons(command.len), command.sequence_number);
  } else {
    retval = handleVISCACommand(command.data, htons(command.len), command.sequence_number);
  }
  if (!retval) {
    sendVISCAResponse(failedVISCAResponse(), command.sequence_number);
  }
}

#define SET_RESPONSE(response, array) setResponseArray(response, array, (uint8_t)(sizeof(array) / sizeof(array[0])))
void setResponseArray(visca_response_t *response, uint8_t *array, uint8_t count);

visca_response_t *failedVISCAResponse(void) {
  static visca_response_t response;
  uint8_t data[] = { 0x10, 0x60, 0x20, 0xff };
  SET_RESPONSE(&response, data);
  return &response;
}

visca_response_t *enqueuedVISCAResponse(void) {
  static visca_response_t response;
  uint8_t data[] = { 0x10, 0x41, 0xff };
  SET_RESPONSE(&response, data);
  return &response;
}

visca_response_t *completedVISCAResponse(void) {
  static visca_response_t response;
  uint8_t data[] = { 0x10, 0x41, 0xff };
  SET_RESPONSE(&response, data);
  return &response;
}

visca_response_t *tallyEnabledResponse(int tallyState) {
  static visca_response_t response;
  uint8_t data[] = { 0x10, 0x50, 0x00, 0xff };
  data[2] = (tallyState == 0) ? 2 : 3;
  SET_RESPONSE(&response, data);
  return &response;
}

visca_response_t *tallyModeResponse(int tallyState) {
  static visca_response_t response;
  uint8_t data[] = { 0x10, 0x50, 0x00, 0xff };
  data[2] = tallyState;
  SET_RESPONSE(&response, data);
  return &response;
}

//    Response: y0 50 0w 0w 0w 0w
//              0z 0z 0z 0z FF
//        wwww: Pan Position
//        zzzz: Tilt Position
visca_response_t *panTiltPositionResponse(int panPosition, int tiltPosition) {
  static visca_response_t response;
  uint8_t data[] = { 0x10, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff };
  data[2] = ( panPosition >> 24) & 0xf;
  data[3] = ( panPosition >> 16) & 0xf;
  data[4] = ( panPosition >>  8) & 0xf;
  data[5] = ( panPosition >>  0) & 0xf;
  data[6] = (tiltPosition >> 24) & 0xf;
  data[7] = (tiltPosition >> 16) & 0xf;
  data[8] = (tiltPosition >>  8) & 0xf;
  data[9] = (tiltPosition >>  0) & 0xf;
  SET_RESPONSE(&response, data);
  return &response;
}

//    Response: y0 50 0p 0q 0r 0s FF
//        pqrs: Zoom Position
visca_response_t *zoomPositionResponse(int zoomPosition) {
  static visca_response_t response;
  uint8_t data[] = { 0x10, 0x50, 0x00, 0x00, 0x00, 0x00, 0xff };
  data[2] = (zoomPosition >> 24) & 0xf;
  data[3] = (zoomPosition >> 16) & 0xf;
  data[4] = (zoomPosition >>  8) & 0xf;
  data[5] = (zoomPosition >>  0) & 0xf;
  SET_RESPONSE(&response, data);
  return &response;
}

bool handleVISCAInquiry(uint8_t *command, uint8_t len, uint32_t sequenceNumber) {
  // All VISCA inquiries start with 0x90.
  // fprintf(stderr, F("INQUIRY\n"));
  if(command[0] != 0x81) return false;

// Also implement:
// Pan-tiltMaxSpeedInq  8x 09 06 11 FF
//    Response: y0 50 ww zz FF  ww: Pan Max Speed
//          zz: Tilt Max Speed
// Pan-tiltPosInq  8x 09 06 12 FF
  switch(command[1]) {
    case 0x09:
      switch(command[2]) {
        case 0x04:
          if (command[3] == 0x47 && command[4] == 0xff) {
            // Zoom position inquiry.
            visca_response_t *response = NULL;
            response = zoomPositionResponse(current_zoom_position);
            sendVISCAResponse(response, sequenceNumber);
            return true;
          }
          break;
        case 0x06:
          if (command[3] == 0x11 && command[4] == 0xff) {
            // Pan/tilt position inquiry.
            visca_response_t *response = NULL;
#if ENABLE_ABSOLUTE_POSITIONING
            updatePanAndTiltPosition();
#endif
            response = panTiltPositionResponse(current_pan_position, current_tilt_position);
            sendVISCAResponse(response, sequenceNumber);
            return true;
          }
          break;
        case 0x7e:
          if (command[3] == 0x01 && command[4] == 0x0a && command[6] == 0xff) {
            sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber);
            visca_response_t *response = NULL;
            if (command[5] == 0x00) {
              // 8x 09 7E 01 0A 00 FF -> y0 50 0p FF
              response = tallyEnabledResponse(current_tally_state);
            } else if (command[5] == 0x01) {
              // 8x 09 7E 01 0A 01 FF -> y0 50 0p FF
              response = tallyModeResponse(current_tally_state);
            } else {
              break;
            }
            sendVISCAResponse(response, sequenceNumber);
            return true;
          }
          break;
        default:
          break;
      }
    default:
      break;
  }

  // Zoom position inquiry: 8x 09 04 47 FF -> y0 50 0p 0q 0r 0s FF ; pqrs -> 0x0000.0x40000

  return false;
}

void setResponseArray(visca_response_t *response, uint8_t *array, uint8_t count) {
  response->cmd[0] = 0x01;
  response->cmd[1] = 0x11;
  response->len = htons(count);
  bcopy(array, response->data, count);
}

bool handleVISCACommand(uint8_t *command, uint8_t len, uint32_t sequenceNumber) {

  // fprintf(stderr, F("COMMAND\n"));
  // All VISCA commands start with 0x8x, where x is the camera number.  For IP, always 1.
  if(command[0] != 0x81) return false;

  switch(command[1]) {
    case 0x01:
      switch(command[2]) {
        case 0x04:
            switch(command[3]) {
              case 0x00: // Power on (2) / off (3): Not implemented.
                break;
              case 0x03: // Manual red gain: Not implemented.  02=reset, 02=up, 03=down,
                         // 00-00-0p-0q = set to pq (0x00..0x80)
                break;
              case 0x07: // Zoom stop (00), tele (02 or 20-27), wide (03 or 30-37)
{
                sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber);
                uint8_t zoomCmd = command[4];
                if (zoomCmd == 2) zoomCmd = 0x23;
                if (zoomCmd == 3) zoomCmd = 0x33;
                int8_t zoomRawSpeed = command[4] & 0xf;
                int8_t zoomSpeed = ((zoomCmd & 0xf0) == 0x20) ? zoomRawSpeed : -zoomRawSpeed;
                setZoomSpeedFromVISCA(zoomSpeed);
                sendVISCAResponse(completedVISCAResponse(), sequenceNumber);
                return true;
}
              case 0x08: // Focus: Not implemented
                break;
              case 0x0B: // Iris settings.
                break;
              case 0x10: // One-push white balance trigger: Not implemented.  Next byte 05.
                break;
              case 0x19: // Lens initialization start: Not implemented.
                break;
              case 0x2B: // Iris settings.
                break;
              case 0x2F: // Iris settings.
                break;
              case 0x35: // White balance set: Not implemented.
                         // cmd4=00: auto, 01=in, 02=out, 03=one-push, 04=auto tracing,
                         // 05=manual, 0C=sodium.
                break;
              case 0x38: // Focus or Curve Tracking: Not implemented.
                break;
              case 0x39: // AE settings.
                break;
              case 0x3C: // Flickerless settings.
                break;
              case 0x3F: // Preset saving and retrieving
                // Maybe also implement:
                // Preset reset 81 01 04 3F 00 pp FF
                //     pp: Preset number (0 to 127)
                if (command[6] == 0xFF) {
                  int presetNumber = command[5];
                  switch(command[4]) {
                    case 0:
                      // Reset: not implemented;
                      break;
                    case 1:
                      savePreset(presetNumber);
                      sendVISCAResponse(completedVISCAResponse(), sequenceNumber);
                      return true;
                    case 2:
                      recallPreset(presetNumber);
                      sendVISCAResponse(completedVISCAResponse(), sequenceNumber);
                      return true;
                  }
                }
                break;
              case 0x47: // Absolute zoom.
{
                sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber);
                uint32_t position = ((command[4] & 0xf) << 12) | ((command[5] & 0xf) << 8) |
                                    ((command[6] & 0xf) << 4) | (command[7] & 0xf);
                uint8_t speed = ((command[len - 2] & 0xf0) == 0) ? (command[8] & 0xf) : 3;
                setZoomPosition(position, scaleZoomSpeed(speed));

                // If (command[9] & 0xf0) == 0, then the low bytes of 9-12 are focus position,
                // and speed is at position 13, shared with focus.  If we ever add support for
                // focusing, handle that case here.

                sendVISCAResponse(completedVISCAResponse(), sequenceNumber);
                return true;
}
              case 0x58: // Autofocus sensitivity: Not implemented.
                break;
              case 0x5c: // Autofocus frame: Not implemented.
                break;
            }
          break;
        case 0x06:
            switch(command[3]) {
              case 0x01: // Pan/tilt drive
                sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber);
                if (len == 6) {
                  // Preset recall speed.
                  int rawMaxSpeed = command[4];
                  default_pan_recall_max_speed = rawMaxSpeed;
                  default_tilt_recall_max_speed = rawMaxSpeed;
                  default_zoom_recall_max_speed = ((rawMaxSpeed + 2) * 8) / 24;  // 3 zoom values per pan/tilt value; 0 = 0.
                } else {
                  int16_t panSpeed = command[4];
                  int16_t tiltSpeed = command[5];
                  uint8_t panCommand = command[6];
                  uint8_t tiltCommand = command[7];
                  if (panCommand == 2) panSpeed = -panSpeed;
                  else if (panCommand == 3) panSpeed = 0;
                  if (tiltCommand == 2) tiltSpeed = -tiltSpeed;
                  else if (tiltCommand == 3) tiltSpeed = 0;
  
                  setTiltSpeedFromVISCA(tiltSpeed);
                  setPanSpeedFromVISCA(panSpeed);
                }
                sendVISCAResponse(completedVISCAResponse(), sequenceNumber);
                return true;
              case 0x02: // Pan/tilt absolute
{
                sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber);
                int16_t panSpeed = command[4];
                int16_t tiltSpeed = command[5];
                uint16_t rawPanValue = ((command[6] & 0xf) << 12) | ((command[7] & 0xf) << 8) |
                                       ((command[8] & 0xf) << 4) | (command[9] & 0xf);
                uint16_t rawTiltValue = ((command[10] & 0xf) << 12) | ((command[11] & 0xf) << 8) |
                                        ((command[12] & 0xf) << 4) | (command[13] & 0xf);
                int16_t panValue = (int16_t)rawPanValue;
                int16_t tiltValue = (int16_t)rawTiltValue;

                if (!setTiltPosition(tiltValue, scaleVerticalSpeed(tiltSpeed))) return false;
                if (!setPanPosition(tiltValue, scaleHorizontalSpeed(panSpeed))) return false;

                sendVISCAResponse(completedVISCAResponse(), sequenceNumber);
                return true;
}
              case 0x03: // Pan/tilt relative
{
                sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber);
                int16_t panSpeed = command[4];
                int16_t tiltSpeed = command[5];
                uint16_t rawPanValue = ((command[6] & 0xf) << 12) | ((command[7] & 0xf) << 8) |
                                       ((command[8] & 0xf) << 4) | (command[9] & 0xf);
                uint16_t rawTiltValue = ((command[10] & 0xf) << 12) | ((command[11] & 0xf) << 8) |
                                        ((command[12] & 0xf) << 4) | (command[13] & 0xf);
                int16_t panValue = (int16_t)rawPanValue;
                int16_t tiltValue = (int16_t)rawTiltValue;

#if ENABLE_ABSOLUTE_POSITIONING
                updatePanAndTiltPosition();
#endif
                double panPosition = current_pan_position + panValue;
                double tiltPosition = current_tilt_position + tiltValue;
                if (!setTiltPosition(tiltPosition, scaleVerticalSpeed(tiltSpeed))) return false;
                if (!setPanPosition(tiltPosition, scaleHorizontalSpeed(panSpeed))) return false;

                sendVISCAResponse(completedVISCAResponse(), sequenceNumber);
                return true;
}
              case 0x07: // Pan/tilt limit set: Not implemented.
                break;
              case 0x35: // Select resolution: Not implemented.
                break;
              case 0x37: // HDMI output range (???)
                break;
            }
          break;
        default:
          break;
      }
    case 0x0A: // Unimplemented
      // 0x81 0a 01 03 10 ff : AF calibration.
      // 0x81 0a 02 02 0p ff : Tally: p=1: flashing; p=2: solid; p=3: normal.
      break;
    case 0x0B: // Unimplemented
      // 0x81 0b 01 xx ff : Tally: 01 = high; 02 = medium; 03 = low; 04 = off.
      break;
    case 0x2A: // Unimplemented
      // 0x81 2a 02 a0 04 0p ff: p=2: USB/UAC audio on; p=3: off.
      break;
    default:
      break;
  }
  return false;
}

bool sendVISCAResponse(visca_response_t *response, uint32_t sequenceNumber) {
  uint16_t length = htons(response->len);
  response->sequence_number = sequenceNumber;
  if (length > 0) {
    length += 8;  // 8-byte UDP header.
    if (!listen_udp.beginPacket(listen_udp.remoteIP(), listen_udp.remotePort())) {
      return false;
    }
    listen_udp.write((char *)response, length);
    return (listen_udp.endPacket() == 1);
  }
}

int actionProgress(int startPosition, int endPosition, int curPosition) {
  int progress = curPosition - startPosition;
  int total = endPosition - startPosition;

  int tenth_percent = abs((1000 * progress) / total);
  return (tenth_percent < 0) ? 0 : (tenth_percent > 1000) ? 1000 : tenth_percent;
}

/// Computes the speed for pan, tilt, and zoom motors on a scale of -maxSpeed to maxSpeed.
///
///     @param progress         How much progress has been made (in tenths of a percent
///                             of the total move length).
///     @param maxSpeed         The maximum speed (as specified in the VISCA command or recall settings).
int computeSpeed(int progress, int maxSpeed) {
  int speedFromProgress = maxSpeed;
  if (progress < 100 || progress > 900) {
#if USE_S_CURVE
      // For now, we evenly ramp up to 10% and down from 90%.  This is *NOT* ideal,
      // particularly given that we get zoom position updates only once per second.
      int distance_to_nearest_endpoint = (progress >= 900) ? (1000 - progress) : progress;

      // With vertical acccuracy of ± .1% at the two endpoints (i.e. 0.001 at the bottom,
      //     0.999 at the top):
      // 1 / (1 + e^-x) would give us a curve of length 14 from -7 to 7.
      // 1 / (1 + e^-(x*7)) would give us a curve of length 14 from -1 to 1.
      // 1 / (1 + e^-((x*7 / 50)) would give us a curve of length 100 from -50 to 50.
      // 1 / (1 + e^-(((x - 50)*7 / 50)) gives us a curve from 0 to 100.
      // 1 / (1 + e^-(7x/50 - 7)) is simplified.
      // 1 / (1 + e^(-7x/50 + 7) is fully simplified.
      double exponent = 7.0 - ((7.0 * distance_to_nearest_endpoint) / 50.0);
      speedFromProgress = 1 / (1 + pow(e_constant, exponent));
#else
      // For now, we evenly ramp up to 10% and down from 90%.  This is *NOT* ideal,
      // particularly given that we get zoom position updates only once per second.
      int distance_to_nearest_endpoint = (progress >= 900) ? (1000 - progress) : progress;
      int interval = 100 / maxSpeed;
      speedFromProgress = ((distance_to_nearest_endpoint  + interval - 1) / interval);
#endif
  }

  // Now adjust the speed based on estimated time to completion.  Under no circumstances
  // should the speed be predicted to hit the target in under about 2 seconds to avoid
  // massive overshoot (particularly with zoom, which gets a position only once per
  // second, and this is not adjustable).

  return speedFromProgress;
}

// Update motor speed based on current position of motors and zoom.
void handleRecallUpdates(void) {
  updatePanAndTiltPosition();

  if (pan_recall_in_progress) {
    // Compute how far into the motion we are (with a range of 0 to 1,000).
    int panProgress = actionProgress(current_pan_recall_start, current_pan_position, current_pan_recall_target);

    if (panProgress == 1000) {
      pan_recall_in_progress = false;
      current_pan_speed = 0;
    } else {      
      // Scale based on max speed out of 24 (VISCA percentage) and scale to a range of -511 to 511.
      current_pan_speed = ((long)computeSpeed(panProgress, 511) * current_pan_recall_max_speed) / 24;
    }

    // Set the motor speed.
    setHorizontalSpeedWithScaling(current_pan_speed);
  }

  if (tilt_recall_in_progress) {
    // Compute how far into the motion we are (with a range of 0 to 1,000).
    int tiltProgress = actionProgress(current_tilt_recall_start, current_tilt_position, current_tilt_recall_target);

    if (tiltProgress == 1000) {
      tilt_recall_in_progress = false;
      current_tilt_speed = 0;
    } else {  
      // Scale based on max speed out of 24 (VISCA percentage) and scale to a range of -511 to 511.
      current_tilt_speed = ((long)computeSpeed(tiltProgress, 511) * current_tilt_recall_max_speed) / 24;
    }

    // Set the motor speed.
    setVerticalSpeedWithScaling(current_tilt_speed);
  }

  if (zoom_recall_in_progress) {
    // Compute how far into the motion we are (with a range of 0 to 1,000).
    int zoomProgress = actionProgress(current_zoom_recall_start, current_zoom_position, current_zoom_recall_target);

    if (zoomProgress == 1000) {
      zoom_recall_in_progress = false;
      current_zoom_speed = 0;
    } else {
      // Scale based on max speed out of 8 (VISCA percentage) and scale to a range of -49 to 49.
      current_zoom_speed = ((long)computeSpeed(zoomProgress, 49) * current_zoom_recall_max_speed) / 8;
    }
  
    // Set the zoom speed.
    sendZoomSpeedViaPTZ(current_zoom_speed);
  }
}

bool setPanPosition(int position, int speed) {
#if ENABLE_ABSOLUTE_POSITIONING
  pan_recall_in_progress = true;
  current_pan_recall_target = position;
  current_pan_recall_max_speed = speed;

  updatePanAndTiltPosition();
  current_pan_recall_start = current_pan_position;
#else
  return 0;
#endif
}

bool setTiltPosition(int position, int speed) {
#if ENABLE_ABSOLUTE_POSITIONING
  tilt_recall_in_progress = true;
  current_tilt_recall_target = position;
  current_tilt_recall_max_speed = speed;

  updatePanAndTiltPosition();
  current_pan_recall_start = current_tilt_position;
#else
  return 0;
#endif
}

bool setZoomPosition(int position, int speed) {
#if (ENABLE_ABSOLUTE_POSITIONING && ENABLE_ABSOLUTE_ZOOM)
  zoom_recall_in_progress = true;
  current_zoom_recall_target = position;
  current_zoom_recall_max_speed = speed;

  current_pan_recall_start = current_zoom_position;
#else
  return 0;
#endif
}

void setPanSpeedFromVISCA(int speed) {
  // Incoming range is -24 to 24 (+/- 0x18).
  int16_t scaledHorizontalSpeed = scaleHorizontalSpeed(speed);
  if (scaledHorizontalSpeed != 0) {
    cancel_active_recall();
  }
  setHorizontalSpeed(scaledHorizontalSpeed);
}

void setTiltSpeedFromVISCA(int speed) {
  // Incoming range is -24 to 24 (+/- 0x18).
  int16_t scaledVerticalSpeed = scaleVerticalSpeed(speed);
  if (scaledVerticalSpeed != 0) {
    cancel_active_recall();
  }
  setVerticalSpeed(scaledVerticalSpeed);
}

void setZoomSpeedFromVISCA(int speed) {
  #if ACTIVE_PTZ_MODE == PTZ_MODE_PANASONIC_PTZ
    // Incoming range is -7 to 7.  Map this onto zoom speeds 1 through 49.
    sendZoomSpeedViaPTZ(speed * 7);
  #else  // !ACTIVE_PTZ_MODE == PTZ_MODE_PANASONIC_PTZ
    // Incoming range is -7 to 7.  Map this onto zoom speeds 0 through 6 (drop the fastest speed).
  #if ENABLE_FASTER_VISCA_ZOOM
    int16_t scaledZoomSpeed = (speed > 0) ? (speed + 1) : (speed < 0) ? (speed - 1) : speed
  #else
    int16_t scaledZoomSpeed = speed;
  #endif
    if (scaledZoomSpeed != 0) {
      cancel_active_recall();
    }
    #if LANC_ENABLED
      sendZoomSpeedViaLANC(scaledZoomSpeed);
    #endif
  #endif  // ACTIVE_PTZ_MODE == PTZ_MODE_PANASONIC_PTZ
}

char *presetFilename(int positionNumber) {
  char buffer[20];
  if (positionNumber < 0 || positionNumber > 127) {
    positionNumber = 0;
  }
  sprintf(buffer, "preset-%d", positionNumber);
  return buffer;
}

void recallPreset(int positionNumber) {
#if ENABLE_ABSOLUTE_POSITIONING
  position_data_blob_t data;

  File presetFile = SD.open(presetFilename(positionNumber), FILE_READ);
  if (!presetFile) {
    // File not found.
    return;
  }
  if (presetFile.read((void *)&data, sizeof(data)) < 0) {
    // Failed to read file.
    return;
  }
  presetFile.close();

  current_pan_position = data.pan_position;
  current_pan_recall_max_speed = default_pan_recall_max_speed;
  pan_recall_in_progress = true;

  current_tilt_position = data.tilt_position;
  current_tilt_recall_max_speed = default_tilt_recall_max_speed;
  tilt_recall_in_progress = true;

#if ENABLE_ABSOLUTE_ZOOM
  current_zoom_speed = data.zoom_position;
  current_zoom_recall_max_speed = default_zoom_recall_max_speed;
  zoom_recall_in_progress = true;
#endif  // ENABLE_ABSOLUTE_ZOOM
#endif  // ENABLE_ABSOLUTE_POSITIONING
}

void savePreset(int positionNumber) {
#if ENABLE_ABSOLUTE_POSITIONING
  updatePanAndTiltPosition();
  position_data_blob_t data;

  data.pan_position = current_pan_position;
  data.tilt_position = current_tilt_position;

#if ENABLE_ABSOLUTE_ZOOM
  data.zoom_position = current_zoom_speed;
#else  // !ENABLE_ABSOLUTE_ZOOM
  data.zoom_position = 0;
#endif  // ENABLE_ABSOLUTE_ZOOM

  char *filename = presetFilename(positionNumber);
  File presetFile = SD.open(filename, FILE_WRITE);
  if (!presetFile) {
    // File not found.
    return;
  }
  bool success = true;
  if (presetFile.write((char *)&data, sizeof(data)) < sizeof(data)) {
    // Failed to read file.
    success = false;
  }
  presetFile.close();
  if (!success) {
    SD.remove(filename);
  }
#endif  // ENABLE_ABSOLUTE_POSITIONING
}

#if ENABLE_VISCA
void disableSDCardReader(void) {
  digitalWrite(SD_CS_PIN, HIGH);
  digitalWrite(ETHERNET_CS_PIN, LOW);
}

void enableSDCardReader(void) {
  digitalWrite(SD_CS_PIN, LOW);
  digitalWrite(ETHERNET_CS_PIN, HIGH);
}
#endif

#endif
