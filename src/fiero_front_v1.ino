// Automaton - Version: Latest 
#include <Automaton.h>

// SoftwareSerial - Version: Latest 
#include <SoftwareSerial.h>

// Adafruit_NeoPixel - Version: Latest
#include <Adafruit_NeoPixel.h>

/*
 * First draft of Fiero NeoPixel front controller
 */

/*************************\ 
 * CONFIGURATION SECTION *
\*************************/

// Configure colors
//	NeoPixel unified color format: 32 bit value comprised of 8 bits each for white, red, green, blue
//  The format is very similar to HTML colors, except replace the # sign with 0x, and the
//  extra first two digits go to the white LED if equipped (ignored if not). For each 
//  8-bit pair, 00 is completely off, and FF (255 in decimal) is on at maximum intensity.
//  There are a number of existing utilities available to generate HTML color codes.
const uint32_t COLOR_BRAKES	= 0x00FF0000;	// red
const uint32_t COLOR_TURNS	= 0x00FFFF00;	// yellow
const uint32_t COLOR_DRL	= 0xBF000000;	// white 75%
const uint32_t COLOR_FPARK	= 0x00333300;	// yellow 20%
const uint32_t COLOR_RPARK	= 0x00330000;	// red 20%

// Configure patterns - more to come when the patterns get fancier
const int PULSE_MS  	= 100; 	// milliseconds between loop iterations
const int BLINK_RATE	= 5; 	// number of loops that a blinker stays on or off

// Specify input pins
static const uint8_t BRAKE_PIN		= A0;
static const uint8_t TURNLEFT_PIN 	= A1;
static const uint8_t TURNRIGHT_PIN	= A2;
static const uint8_t HAZARD_PIN 	= A3;
static const uint8_t PARK_PIN		= A4;
static const uint8_t SHOWMODE_PIN	= A5;
static const uint8_t RESET_PIN		= 9;

// Specify NeoPixel output pins
static const uint8_t LEFTTURN_PIX		= 2;	// turn signals
static const uint8_t RIGHTTURN_PIX		= 3;	// turn signals
static const uint8_t LEFTSIDE_PIX		= 4;	// side marker lights
static const uint8_t RIGHTSIDE_PIX		= 5;	// side marker lights
static const uint8_t CENTER_PIX 		= 6;	// center brake light
//static const uint8_t DASH_PIX			= 7;	// dashboard indicators (future)
// Specify NeoPixel strand lengths (in pixels)
static const uint16_t LEFTTURN_SIZE 	= 8;	// number of pixels in left turn signal
static const uint16_t RIGHTTURN_SIZE	= 8;	// number of pixels in right turn signal
static const uint16_t LEFTSIDE_SIZE 	= 8;	// number of pixels in side marker lights
static const uint16_t RIGHTSIDE_SIZE	= 8;	// number of pixels in side marker lights
static const uint16_t CENTER_SIZE		= 8;	// number of pixels in center brake light
//static const uint16_t DASH_SIZE 		= 8;	// number of pixels in dashboard indicators (future)
// Specify type of strands (usually NEO_GRB for 3-color pixels, or NEO_RGBW for 4-color pixels)
static const neoPixelType LEFTTURN_TYPE 	= NEO_RGBW;	// type of strand for left turn signal/brake
static const neoPixelType RIGHTTURN_TYPE	= NEO_RGBW;	// type of strand for right turn signal/brake
static const neoPixelType LEFTSIDE_TYPE 	= NEO_RGBW;	// type of strand for side marker lights
static const neoPixelType RIGHTSIDE_TYPE	= NEO_RGBW;	// type of strand for side marker lights
static const neoPixelType CENTER_TYPE 		= NEO_RGBW;	// type of strand for center brake light
//static const neoPixelType DASH_TYPE		= NEO_RGBW;	// type of strand for dashboard indicators (future)

// Comm lines
static const uint8_t LINK_TX	= 11;
static const uint8_t LINK_RX	= 12;


/****************************************\
 * NOTHING CUSTOMIZABLE BELOW THIS LINE *
\****************************************/


// Input Signals
Atm_digital brakeInput;
Atm_digital turnleftInput;
Atm_digital turnrightInput;
Atm_digital headlightInput;
Atm_digital hazardInput;
Atm_digital showModeInput;
Atm_digital resetButton;
Atm_bit inReverse;
Atm_bit inShowMode;
// Light strands
Adafruit_NeoPixel leftBlinker = Adafruit_NeoPixel(LEFTTURN_SIZE, LEFTTURN_PIX, LEFTTURN_TYPE);
Adafruit_NeoPixel rightBlinker = Adafruit_NeoPixel(RIGHTTURN_SIZE, RIGHTTURN_PIX, RIGHTTURN_TYPE);
Adafruit_NeoPixel leftSideMarker = Adafruit_NeoPixel(LEFTSIDE_SIZE, LEFTSIDE_PIX, LEFTSIDE_TYPE);
Adafruit_NeoPixel rightSideMarker = Adafruit_NeoPixel(RIGHTSIDE_SIZE, RIGHTSIDE_PIX, RIGHTSIDE_TYPE);
Adafruit_NeoPixel centerBrake = Adafruit_NeoPixel(CENTER_SIZE, CENTER_PIX, CENTER_TYPE);
// Serial command parser
Atm_command peerCmd;
// Animation timer
Atm_timer animator;

// SoftwareSerial link to other end
SoftwareSerial serLink(LINK_RX, LINK_TX); // define RX, TX pins (reverse for other side)
// Command definitions - these next 3 lines *MUST* use the same command order
enum { CMD_NOP, CMD_RST, CMD_PRK, CMD_BRK, CMD_LTS, CMD_RTS, CMD_HAZ, CMD_REV, CMD_SHO }; 
const char cmdlist[] = "NOP RST PRK BRK LTS RTS HAZ REV SHO";
const char* cmdarray[] = {"NOP", "RST", "PRK", "BRK", "LTS", "RTS", "HAZ", "REV", "SHO"};
char buffer[32];

// static/global state variables used by pattern_callback
int blinkstarted; // value of up when blinking started
bool blinklit;

// Timer callback to push patterns out to the pixels
void pattern_callback( int idx, int v, int up ) {
	// idx is always 0 in our case since we omit it from the callback registration
	// v is the number of remaining loops, -1 for us since we loop indefinitely
	// up is the number of loops so far - the only potentially useful parameter
	int i;
	bool park, hazard, turnleft, turnright, brake;
	
	// FOR FUTURE USE - NOT YET IMPLEMENTED
	//if (inShowMode().state()) {
	//	showmode_callback(idx, v, up);
	//	return;
	//}
	
	park = headlightInput.state();
	hazard = hazardInput.state();
	turnleft = turnleftInput.state();
	turnright = turnrightInput.state();
	brake = brakeInput.state();
	
	// paint the "base color" first - i.e. parking lights, DRL, or off
	for (i=0; i<LEFTSIDE_SIZE; i++) 
		leftSideMarker.setPixelColor(i, park ? COLOR_FPARK : 0);
	for (i=0; i<RIGHTSIDE_SIZE; i++) 
		rightSideMarker.setPixelColor(i, park ? COLOR_FPARK : 0);
	// DRL is on when headlights are off - opposite of park
	for (i=0; i<LEFTTURN_SIZE; i++) 
		leftBlinker.setPixelColor(i, park ? 0 : COLOR_DRL);
	for (i=0; i<RIGHTTURN_SIZE; i++) 
		rightBlinker.setPixelColor(i, park ? 0 : COLOR_DRL);
	
	// center brake has one function in this design
	for (i=0; i<CENTER_SIZE; i++) 
		centerBrake.setPixelColor(i, brake ? COLOR_BRAKES : 0);
		
	// if turning or hazards, paint the appropriate blinker(s)
	if (hazard || turnleft || turnright) {
		// if starting new (or counter rollover), save the starting point
		if (blinkstarted < 0 || up < blinkstarted) {
			blinkstarted = up;
		} 
		
		// check if it's time to toggle on or off
		if ((up - blinkstarted) % BLINK_RATE == 0) {
			blinklit = !blinklit;
		}
		
		// push the appropriate pixel colors
		if (hazard || turnleft) {
			for (i=0; i<LEFTTURN_SIZE; i++) {
				leftBlinker.setPixelColor(i, blinklit ? COLOR_TURNS : 0);
			}
		}
		if (hazard || turnright) {
		for (i=0; i<RIGHTTURN_SIZE; i++) {
			rightBlinker.setPixelColor(i, blinklit ? COLOR_TURNS : 0);
			}
		}
	} else if (blinkstarted >= 0) {
		// we have a starting point saved but no blinkers are on - clear the starting point
		blinkstarted = -1;
		blinklit = false;
	}
	
	// now push the color pattern out to the strips
	leftBlinker.show();
	rightBlinker.show();
	leftSideMarker.show();
	rightSideMarker.show();
	centerBrake.show();
}

// Send a command to our peer unit (rearduino)
void sendPeer(int cmd, int arg=-1) {
	serLink.print(cmdarray[cmd]);
	if(arg >= 0) {
		serLink.print(' ');
		serLink.print(arg);
	}
	serLink.print('\n');
}

// Reset everything possible
void reset(bool fromPeer) {
	if(!fromPeer) {
		sendPeer(CMD_RST);
	}
	animator.stop();
	leftBlinker.clear();
	rightBlinker.clear();
	leftSideMarker.clear();
	rightSideMarker.clear();
	centerBrake.clear();
	leftBlinker.show();
	rightBlinker.show();
	leftSideMarker.show();
	rightSideMarker.show();
	centerBrake.show();
	blinkstarted = -1;
	blinklit = false;
	animator.start();
}

// Command-processing callback for peerLink
void cmd_callback( int idx, int v, int up ) {
  bool arg_true = false;
  if(v > CMD_RST) {
      arg_true = peerCmd.arg(1)[0] == '1';
  }
  switch ( v ) {
    case CMD_NOP: 
      // FUTURE: flag peer as alive
      return;
    case CMD_RST:
      reset(true);
      return;
    case CMD_REV:
      if(arg_true)
        inReverse.on();
      else
        inReverse.off();
      return;
  }
}

/*
 * Arduino standard functions
 */

// Arduino setup() - initialize everything here
void setup() {
	// Initialize the NeoPixel strands
	leftBlinker.begin();
	rightBlinker.begin();
	centerBrake.begin();
	leftSideMarker.begin();
	rightSideMarker.begin();

	// Initialize inter-duino serial connection
    serLink.begin(9600);
    //peerLink.begin();
    peerCmd.begin(serLink, buffer, sizeof(buffer))
        .list(cmdlist)
        .onCommand(cmd_callback);
        
    // Initialize input/bit state machines
    brakeInput.begin(BRAKE_PIN)
    	.onChange(HIGH, [](int idx, int v, int up) { sendPeer(CMD_BRK, 1); })
    	.onChange(LOW, [](int idx, int v, int up) { sendPeer(CMD_BRK, 0); });
    turnleftInput.begin(TURNLEFT_PIN)    	
    	.onChange(HIGH, [](int idx, int v, int up) { sendPeer(CMD_LTS, 1); })
    	.onChange(LOW, [](int idx, int v, int up) { sendPeer(CMD_LTS, 0); });
    turnrightInput.begin(TURNRIGHT_PIN)    	
    	.onChange(HIGH, [](int idx, int v, int up) { sendPeer(CMD_RTS, 1); })
    	.onChange(LOW, [](int idx, int v, int up) { sendPeer(CMD_RTS, 0); });
    headlightInput.begin(PARK_PIN)    	
    	.onChange(HIGH, [](int idx, int v, int up) { sendPeer(CMD_PRK, 1); })
    	.onChange(LOW, [](int idx, int v, int up) { sendPeer(CMD_PRK, 0); });
    hazardInput.begin(HAZARD_PIN)    	
    	.onChange(HIGH, [](int idx, int v, int up) { sendPeer(CMD_HAZ, 1); })
    	.onChange(LOW, [](int idx, int v, int up) { sendPeer(CMD_HAZ, 0); });
    showModeInput.begin(SHOWMODE_PIN)
    	.onChange(HIGH, [](int idx, int v, int up) { inShowMode.on(); sendPeer(CMD_SHO, 1); })
    	.onChange(LOW, [](int idx, int v, int up) { inShowMode.off(); sendPeer(CMD_SHO, 0); });
    resetButton.begin(RESET_PIN)
    	.onChange(HIGH, [](int idx, int v, int up) { reset(false); });
    inShowMode.begin();
    inReverse.begin();
	
    // Start the animation timer
    animator.begin(100)
    	.repeat(ATM_COUNTER_OFF)
    	.onTimer(pattern_callback)
    	.start();
}

// Arduino loop() - Automaton makes this a one-liner :)
void loop() {
    automaton.run();
}
