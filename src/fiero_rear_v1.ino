// Automaton - Version: Latest 
#include <Automaton.h>

// SoftwareSerial - Version: Latest 
#include <SoftwareSerial.h>

// Adafruit_NeoPixel - Version: Latest
#include <Adafruit_NeoPixel.h>

/*
 * First draft of Fiero NeoPixel rear controller
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
const uint32_t COLOR_BRAKES		= 0x00FF0000;	// red
const uint32_t COLOR_TURNS		= 0x00FFFF00;	// yellow
const uint32_t COLOR_REVERSE	= 0xFF000000;	// white
const uint32_t COLOR_RPARK		= 0x00330000;	// red 20%

// Configure pixel ranges within multi-purpose strands
const uint16_t REVERSE_START	= 0;	// first pixel to light for reverse gear (0 = innermost pixel)
const uint16_t REVERSE_LENGTH	= 2;	// number of pixels to light for reverse gear

// Configure patterns - more to come when the patterns get fancier
const int PULSE_MS  	= 100; 	// milliseconds between loop iterations
const int BLINK_RATE	= 5; 	// number of loops that a blinker stays on or off

// Specify input pins
static const uint8_t REVERSE_PIN	= A0;
static const uint8_t ACCENTPOT_PIN	= A1;	// potentiometer knob for selecting logo color
static const uint8_t RESET_PIN		= 9;

// Specify NeoPixel output pins
static const uint8_t LEFTTURN_PIX		= 2;	// turn signals/brakes/etc
static const uint8_t RIGHTTURN_PIX		= 3;	// turn signals/brakes/etc
static const uint8_t LEFTSIDE_PIX		= 4;	// side marker lights
static const uint8_t RIGHTSIDE_PIX		= 5;	// side marker lights
static const uint8_t LOGO_PIX			= 6;	// Pontiac logo lighting
// Specify NeoPixel strand lengths (in pixels)
static const uint16_t LEFTTURN_SIZE 	= 8;	// number of pixels in left turn signal/brake
static const uint16_t RIGHTTURN_SIZE	= 8;	// number of pixels in right turn signal/brake
static const uint16_t LEFTSIDE_SIZE 	= 8;	// number of pixels in side marker lights
static const uint16_t RIGHTSIDE_SIZE	= 8;	// number of pixels in side marker lights
static const uint16_t LOGO_SIZE 		= 1;	// number of pixels in Pontiac logo lighting
// Specify type of strands (usually NEO_GRB for 3-color pixels, or NEO_RGBW for 4-color pixels)
static const neoPixelType LEFTTURN_TYPE 	= NEO_RGBW;	// type of strand for left turn signal/brake
static const neoPixelType RIGHTTURN_TYPE	= NEO_RGBW;	// type of strand for right turn signal/brake
static const neoPixelType LEFTSIDE_TYPE 	= NEO_RGBW;	// type of strand for side marker lights
static const neoPixelType RIGHTSIDE_TYPE	= NEO_RGBW;	// type of strand for side marker lights
static const neoPixelType LOGO_TYPE 		= NEO_RGBW;	// type of strand for Pontiac logo lighting

// Comm lines
static const uint8_t LINK_TX	= 12;
static const uint8_t LINK_RX	= 11;


/****************************************\
 * NOTHING CUSTOMIZABLE BELOW THIS LINE *
\****************************************/


// Input Signals
Atm_digital reverseInput;
Atm_button resetButton;
Atm_analog colorKnob;
Atm_bit brakesOn; 
Atm_bit turningLeft;
Atm_bit turningRight;
Atm_bit lightsOn;
Atm_bit hazardOn;
Atm_bit inShowMode;
// Light strands
Adafruit_NeoPixel leftBrakeTurn = Adafruit_NeoPixel(LEFTTURN_SIZE, LEFTTURN_PIX, LEFTTURN_TYPE);
Adafruit_NeoPixel rightBrakeTurn = Adafruit_NeoPixel(RIGHTTURN_SIZE, RIGHTTURN_PIX, RIGHTTURN_TYPE);
Adafruit_NeoPixel leftSideMarker = Adafruit_NeoPixel(LEFTSIDE_SIZE, LEFTSIDE_PIX, LEFTSIDE_TYPE);
Adafruit_NeoPixel rightSideMarker = Adafruit_NeoPixel(RIGHTSIDE_SIZE, RIGHTSIDE_PIX, RIGHTSIDE_TYPE);
Adafruit_NeoPixel pontiacLogo = Adafruit_NeoPixel(LOGO_SIZE, LOGO_PIX, LOGO_TYPE);
// Serial controls
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

// static/global state variables
int blinkstarted; // value of up when blinking started
bool blinklit;

// Timer callback to push patterns out to the pixels
void pattern_callback( int idx, int v, int up ) {
	// idx is always 0 in our case since we omit it from the callback registration
	// v is the number of remaining loops, -1 for us since we loop indefinitely
	// up is the number of loops so far - the only potentially useful parameter
	int i;
	bool park, hazard, turnleft, turnright, brake, reverse;
	
	// FOR FUTURE USE - NOT YET IMPLEMENTED
	//if (inShowMode().state()) {
	//	showmode_callback(idx, v, up);
	//	return;
	//}
	
	park = lightsOn.state();
	hazard = hazardOn.state();
	turnleft = turningLeft.state();
	turnright = turningRight.state();
	brake = brakesOn.state();
	reverse = reverseInput.state();
	
	// paint the "base color" first - i.e. parking lights or off
	for (i=0; i<LEFTSIDE_SIZE; i++) 
		leftSideMarker.setPixelColor(i, park ? COLOR_RPARK : 0);
	for (i=0; i<RIGHTSIDE_SIZE; i++) 
		rightSideMarker.setPixelColor(i, park ? COLOR_RPARK : 0);
	// DRL is on when headlights are off - opposite of park
	for (i=0; i<LEFTTURN_SIZE; i++) 
		leftBrakeTurn.setPixelColor(i, park ? COLOR_RPARK : 0);
	for (i=0; i<RIGHTTURN_SIZE; i++) 
		rightBrakeTurn.setPixelColor(i, park ? COLOR_RPARK : 0);
	
	// if brakes on, override the brake lights
	if (brake) {
		for (i=0; i<LEFTTURN_SIZE; i++) 
			leftBrakeTurn.setPixelColor(i, COLOR_BRAKES);
		for (i=0; i<RIGHTTURN_SIZE; i++) 
			rightBrakeTurn.setPixelColor(i,COLOR_BRAKES);
	}
	
	// if turning or hazards, override the appropriate blinker
	if (hazard || turnleft || turnright) {
		// starting new (or counter rollover) - save the starting point
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
				leftBrakeTurn.setPixelColor(i, blinklit ? COLOR_TURNS : 0);
			}
		}
		if (hazard || turnright) {
			for (i=0; i<RIGHTTURN_SIZE; i++) {
				rightBrakeTurn.setPixelColor(i, blinklit ? COLOR_TURNS : 0);
			}
		}
	} else if (blinkstarted >= 0) {
		// we have a starting point saved but no blinkers are on - clear the starting point
		blinkstarted = -1;
		blinklit = false;
	}
	
	// finally, if in reverse gear, override the reverse section of the tails
	if (reverse) {
		for (i=REVERSE_START; i<REVERSE_START+REVERSE_LENGTH; i++) {
			leftBrakeTurn.setPixelColor(i, COLOR_REVERSE);
			rightBrakeTurn.setPixelColor(i, COLOR_REVERSE);
		}
	}
	
	// now push the color pattern out to the strips
	leftBrakeTurn.show();
	rightBrakeTurn.show();
	leftSideMarker.show();
	rightSideMarker.show();
}

// Send a command to our peer unit (frontduino)
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
	leftBrakeTurn.clear();
	rightBrakeTurn.clear();
	leftSideMarker.clear();
	rightSideMarker.clear();
	pontiacLogo.clear();
	leftBrakeTurn.show();
	rightBrakeTurn.show();
	leftSideMarker.show();
	rightSideMarker.show();
	pontiacLogo.show();
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
		case CMD_PRK:
			if(arg_true)
				lightsOn.on();
			else
				lightsOn.off();
			return;
		case CMD_BRK:
			if(arg_true)
				brakesOn.on();
			else
				brakesOn.off();
			return;
		case CMD_LTS:
			if(arg_true)
				turningLeft.on();
			else
				turningLeft.off();
			return;
		case CMD_RTS:
			if(arg_true)
				turningRight.on();
			else
				turningRight.off();
			return;
		case CMD_HAZ:
			if(arg_true)
				hazardOn.on();
			else
				hazardOn.off();
			return;
		case CMD_SHO:
			if(arg_true)
				inShowMode.on();
			else
				inShowMode.off();
			return;
	}
}

// Utility function adapted from the Adafruit NeoPattern example code
// Theirs took an 8-bit input - this uses the 10 bits available from an analog pin
uint32_t Wheel10(int index) {
	index %= 1024; // clip to 10 bits
    
    index = 1023 - index;
    if(index < 341) {
    	index = index * 3 / 4;
    	return Adafruit_NeoPixel::Color(255 - index, 0, index);
    } else if (index < 682) {
    	index = (index - 341) * 3 / 4;
    	return Adafruit_NeoPixel::Color(0, index, 255 - index);
    } else {
    	index = (index - 682) * 3 / 4;
    	return Adafruit_NeoPixel::Color(index, 255 - index, 0);
    }
}

// Analog callback for color potentiometer
// Atm_analog::onChange sends the current sample in v
void changeLogoColor(int idx, int v, int up) {
	// show the new color on the indicator LED, but don't sent to the Pontiac logo yet
	for (int i=0; i<LOGO_SIZE; i++) {
		pontiacLogo.setPixelColor(i, Wheel10(v));
	}
	pontiacLogo.show();
}

/*
 * Arduino standard functions
 */

// Arduino setup() - initialize everything here
void setup() {
	// Initialize the NeoPixel strands
	leftBrakeTurn.begin();
	rightBrakeTurn.begin();
	leftSideMarker.begin();
	rightSideMarker.begin();
	pontiacLogo.begin();

	// Initialize inter-duino serial connection
	serLink.begin(9600);
	//peerLink.begin();
	peerCmd.begin(serLink, buffer, sizeof(buffer))
		.list(cmdlist)
		.onCommand(cmd_callback);
		
	// Initialize input/bit state machines
	brakesOn.begin();
	turningLeft.begin();
	turningRight.begin();
	lightsOn.begin();
	hazardOn.begin();
	reverseInput.begin(REVERSE_PIN)
    	.onChange(HIGH, [](int idx, int v, int up) { sendPeer(CMD_REV, 1); })
    	.onChange(LOW, [](int idx, int v, int up) { sendPeer(CMD_REV, 0); });
	resetButton.begin(RESET_PIN)
    	.onPress([](int idx, int v, int up) { reset(false); });
    colorKnob.begin(ACCENTPOT_PIN)
    	.onChange(changeLogoColor);
	
	// grab the starting color from the knob position
	changeLogoColor(0, colorKnob.state(), 0);
	
    // Start the animation timer
    animator.begin(PULSE_MS)
    	.repeat(ATM_COUNTER_OFF)
    	.onTimer(pattern_callback)
    	.start();
}

// Arduino loop() - Automaton makes this a one-liner :)
void loop() {
	automaton.run();
}
