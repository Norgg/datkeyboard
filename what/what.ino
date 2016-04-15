#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

#include "KitBounce.h"
#include <stdio.h>

// N_PINS must be at most half the bit-width of an int (so can't be >16 right now, AFAIK)
#define N_PINS 10

#define IMU_RESET_PIN 20

#define LED_PIN 13
                      //lllllrrrrr
#define RIGHT_SQUEEZE 0b0000011110
#define LEFT_SQUEEZE  0b1111000000
#define ALL_SQUEEZE   0b0111101111

#define HOLD_WAIT_PERIOD 500

#define DEBUG true

#ifdef DEBUG
#define BUF_SIZE 128
char buffer[BUF_SIZE];
#define PRINT(stuff) Serial.print(stuff)
#define PRINTLN(stuff) Serial.println(stuff)
#define PRINTF(...) do { snprintf(buffer, BUF_SIZE, __VA_ARGS__); PRINT(buffer); } while(0)
#else
#define PRINT(stuff)
#define PRINTLN(stuff)
#define PRINTF(...)
#endif

const int mask = ~((~0) << N_PINS);

Adafruit_BNO055 bno = Adafruit_BNO055(55);

// For figuring out when to consider a real button (the actual physical ones) pressed, or when a chord has been pressed
enum ButtonsState {
    B_ERROR, // We should really never enter this state
    START,
    PRESSING,
    HOLDING,
    CHORD_1,
    CHORD_2,
    CHORD_HOLDING
};

// For figuring out when to emit a keyboard button press to the computer
enum KeyboardState {
    K_START,
    AWAIT_SECOND
};


// Here come the three letter variable names (so's I can use them in the array down below without messing with the
// alignment)
#define CTL KEY_LEFT_CTRL
#define SFT KEY_LEFT_SHIFT
#define ALT KEY_LEFT_ALT
#define WIN KEY_LEFT_GUI
#define  UP KEY_UP_ARROW
#define DWN KEY_DOWN_ARROW
#define LFT KEY_LEFT_ARROW
#define RIT KEY_RIGHT_ARROW
#define TAB KEY_TAB
#define RET KEY_RETURN
#define ESC KEY_ESC
#define INS KEY_INSERT
#define DEL KEY_DELETE
#define PGU KEY_PAGE_UP
#define PGD KEY_PAGE_DOWN
#define HOM KEY_HOME
#define END KEY_END
#define CPS KEY_CAPS_LOCK

#define  F1 KEY_F1
#define  F2 KEY_F2
#define  F3 KEY_F3
#define  F4 KEY_F4
#define  F5 KEY_F5
#define  F6 KEY_F6
#define  F7 KEY_F7
#define  F8 KEY_F8
#define  F9 KEY_F9
#define F10 KEY_F10
#define F11 KEY_F11
#define F12 KEY_F12
#define APS '\''

// English letter frequency order:
// ETAOINSHRDULCMFY WGPVBKXQJZ

const int mapping[N_PINS][N_PINS] = {
   // r1   r2   r3   r4   r5     l1   l2   l3   l4   l5
    {HOM, ' ', TAB, ' ', ' ',   '0', ' ', ' ', 'z', ' '}, // r1
    {LFT, ' ', RET, ' ', PGU,   '1', 'g', 'p', 'j', ' '}, // r2
    {DWN, DEL, ESC, ' ', PGD,   '2', 'v', 'w', 'q', ' '}, // r3
    { UP, ' ', ' ', END, ' ',   '3', 'b', 'k', 'x', F11}, // r4
    {RIT, ' ', ' ', INS, CPS,   '4', ' ', ' ', ' ', F12}, // r5

    {'d', 'c', 'm', 'f', ' ',   '5', CTL, ' ',  F1,  F6}, // l1
    {'l', 't', 'i', 'r', '`',   '6', ' ', SFT,  F2,  F7}, // l2
    {'u', 'a', 'e', 'h', '=',   '7', ']', ALT,  F3,  F8}, // l3
    {'y', 's', 'n', 'o', '-',   '8', '[', ' ',  F4,  F9}, // l4
    {',', '.', '/', APS, ';',   '9', ' ', ' ',  F5, F10}, // l5
};

typedef struct {
    int what;
    unsigned long when;
} TimeButton;

int timeButtonCmp(const void * x, const void * y){
    TimeButton a = *(const TimeButton *)x, b = *(const TimeButton *)y;
    if(a.when > b.when){
        return -1;
    }else if(b.when > a.when){
        return 1;
    }
    return 0;
}

// button_states:
//  Each entry is a byte, we treat each bit in that byte as whether the button was pressed at that moment in time (with
//  the rightmost bit being the current state)
uint8_t button_states[N_PINS];

// buttons_now:
//  A full int of history of the buttons, the rightmost N_PINS bits are the buttons right now
uint buttons_now = 0;

// buttons:
//  Array of debouncer objects
Bounce buttons[N_PINS];

// The two state machines we use.
ButtonsState buttons_state = START;
KeyboardState keyboard_state = K_START;


int count(){
    // How many buttons are currently pressed
    return __builtin_popcount(buttons_now & mask);
}

int which(){
    // Which button is currently pressed (lowest index).
    // -1 if nothing pressed
    return __builtin_ffs(buttons_now & mask) - 1;
}

void emit_1(int pin, bool release){
    if(pin < 0 || pin >= N_PINS){
        return;
    }
    // A real button has been considered pressed
    PRINTF("EMIT %d ", pin);

    static int first;
    switch(keyboard_state){
        case K_START:
            first = pin;
            keyboard_state = AWAIT_SECOND;
            break;

        case AWAIT_SECOND:
            keyboard_state = K_START;
            int pressing = lookup(first, pin);
            Keyboard.press(pressing);
            if(release && immediate_release(pressing)){
                Keyboard.releaseAll();
            }
            break;
    }
}

int immediate_release(int c){
    switch(c){
        case CTL:
        case SFT:
        case ALT:
        case WIN:
            return false;
            break;
        default:
            return true;
            break;
    }
}

int lookup(int first, int second){
    // Get the virtual key to be pressed (the thing to send to the computer) from a pair of real button presses
    if(first >= N_PINS || first < 0){
        PRINTF("\r\nFirst pressed is wrong: %d\n", first);
    }
    if(second >= N_PINS || second < 0){
        PRINTF("\r\nSecond pressed is wrong: %d\n", second);
    }
    return mapping[second][first];
    // Second comes first because we want the `mapping` matrix to be across-then-down (which is intuitive)
}

void clearSerial(){
    while(Serial.available() > 0){
        char c = Serial.read();
        switch(c){
            case 'r':
                _reboot_Teensyduino_();
                break;
        }
    }
}

void displaySensorDetails(void){
    sensors_event_t sensor;
    bno.getEvent(&sensor);
    Serial.println("------------------------------------");
    Serial.print  ("Orientation:  "); Serial.print(sensor.orientation.x); Serial.print(","); Serial.print(sensor.orientation.y); Serial.print(","); Serial.println(sensor.orientation.z);
    Serial.println("------------------------------------");
    Serial.println("");
    delay(500);
}

void setup(){
#ifdef DEBUG
    Serial.begin(9600);
    while(!Serial){
        ;
    }
    delay(500);
#endif

    for(int i = 0; i < N_PINS; i++){
        // Initialize the debouncers
        buttons[i] = Bounce();
        buttons[i].attach(i, INPUT_PULLUP);
        buttons[i].interval(20);

        // Set all the button states up
        button_states[i] = 0;
    }

    buttons_state = START;
    keyboard_state = K_START;

    // If we ever want to use it
    pinMode(LED_PIN, OUTPUT);
    pinMode(IMU_RESET_PIN, OUTPUT);
    digitalWrite(IMU_RESET_PIN, LOW);
    delay(10);
    digitalWrite(IMU_RESET_PIN, HIGH);

    Keyboard.begin();

    /* Initialise the sensor */
    if(!bno.begin()){
        /* There was a problem detecting the BNO055 ... check your connections */
        Serial.print("Ooops, no BNO055 detected ... Check your wiring or I2C ADDR!");
        while(1); // Really?
    }
    delay(1000);

    displaySensorDetails();

    PRINTF("|%20s|%20s| %6s | %6s | %6s | \n", "Buttons State", "Keyboard State", "pin", "nbits", "bnm");
}

ButtonsState transduce(ButtonsState previous){
    int nbits, bnm;
    static int trying_chord, last_bnm = 0;
    ButtonsState next_state = B_ERROR;  // The function *needs* to set this, we want to throw errors otherwise

    nbits = count();
    bnm = buttons_now & mask;

    switch(buttons_state){
        case START:
            if(nbits == 0){
                next_state = START;
            }else if(nbits > 0){
                next_state = PRESSING;
            }
            break;
        case PRESSING:
            // This state when 3 > buttons pressed
            // if nbits goes down, press all released buttons in order of first pressed
            // if we're left with only one, start counting for it to start being held
            if(last_bnm == bnm){ // No change
                if(nbits == 1 &&
                        (millis() - buttons[which()].getDownAt()) > HOLD_WAIT_PERIOD){
                    // One button has been pressed for long enough
                    next_state = HOLDING;
                    emit_1(which(), false);
                }else{
                    next_state = PRESSING;
                }

            }else{  // Now we need to figure out what kind of change
                TimeButton released[N_PINS];
                int where = 0;

                for(int i = 0; i < N_PINS; i++){  // Find the released ones so we can deal with them
                    if((button_states[i] & 0b11) == 0b10){
                        released[where++] = {i, buttons[i].getDownAt()};
                    }
                }

                switch(where){ // It also acts as a count of how many were released
                    case 0:
                        trying_chord = bnm;
                        switch(bnm){
                            case RIGHT_SQUEEZE:
                            case LEFT_SQUEEZE:
                            case ALL_SQUEEZE:
                                next_state = CHORD_1;
                                break;
                            default:
                                next_state = PRESSING;
                                break;
                        }
                        break;
                    case 1:
                        emit_1(released[0].what, true);
                        next_state = PRESSING;
                        break;
                    default: // 2 or more released
                        PRINT("Many released ");
                        if(nbits == 0){
                            next_state = START;
                        }else{
                            next_state = PRESSING;
                        }
                        qsort(released, where, sizeof(TimeButton), timeButtonCmp);
                        for(int i = 0; i < where; i++){
                            emit_1(released[i].what, true);
                        }
                        break;
                }
            }
            break;
        case HOLDING:
            // This state when one button pressed and held (e.g. repeating letter)
            if(nbits == 0){
                Keyboard.releaseAll();
                next_state = START;
            }else{
                next_state = HOLDING;
            }
            break;
        case CHORD_1:
            if(bnm == trying_chord){
                next_state = CHORD_2;
            }else{
                next_state = PRESSING;
            }
            break;
        case CHORD_2:
            if(bnm == trying_chord){
                next_state = CHORD_HOLDING;
                switch(bnm){
                    case RIGHT_SQUEEZE:
                        PRINTF("Chord Spacebar ");
                        Keyboard.press(' ');
                        break;
                    case LEFT_SQUEEZE:
                        PRINTF("Chord Backspace ");
                        Keyboard.press(KEY_BACKSPACE);
                        keyboard_state = K_START;
                        break;
                    case ALL_SQUEEZE:
                        PRINTF("Chord Cancel ");
                        keyboard_state = K_START;
                        break;
                }
            }else{
                next_state = PRESSING;
            }
            break;
        case CHORD_HOLDING:
            if(nbits == 0){
                next_state = START;
                Keyboard.releaseAll();
            }else{
                next_state = CHORD_HOLDING;
            }
            break;

        case B_ERROR:
            // Just leave the next state as B_ERROR
            break;
    }
    last_bnm = bnm;
    return next_state;
}

void loop(){
    // Housekeeping
    clearSerial();
    digitalWrite(LED_PIN, keyboard_state == AWAIT_SECOND);

    // Update button states
    for(int i = N_PINS - 1; i >= 0; i--){ // Downwards because we're pushing backwards onto buttons_now
        buttons[i].update();

        // We use the button states as an 8-iteration-long history of the button
        button_states[i] <<= 1;
        button_states[i] |= !buttons[i].read(); // ! because pullup

        buttons_now <<= 1;
        buttons_now |= !buttons[i].read();

    }

    // Use the button states
    run_debug_print();
    buttons_state = transduce(buttons_state);

    displaySensorDetails();

    // 200Hz because it seems to work fine
    delay(50);
}

void run_debug_print(){
#ifdef DEBUG
    int nbits = count(), bnm = buttons_now & mask;

    // So we can tell if we've changed state
    static ButtonsState last_bs = START;
    bool state_changed = last_bs == buttons_state;
    last_bs = buttons_state;

    char bs_buf[20];
    switch(buttons_state){
        case START:
            strcpy(bs_buf, "START");
            break;
        case PRESSING:
            strcpy(bs_buf, "PRESSING");
            break;
        case HOLDING:
            strcpy(bs_buf, "HOLDING");
            break;
        case CHORD_1:
            strcpy(bs_buf, "CHORD_1");
            break;
        case CHORD_2:
            strcpy(bs_buf, "CHORD_2");
            break;
        case CHORD_HOLDING:
            strcpy(bs_buf, "CHORD_HOLDING");
            break;
        case B_ERROR:
            strcpy(bs_buf, "B_ERROR");
            break;
    }

    char ks_buf[20];
    switch(keyboard_state){
        case K_START:
            strcpy(ks_buf, "K_START");
            break;
        case AWAIT_SECOND:
            strcpy(ks_buf, "AWAIT_SECOND");
            break;
    }

    static char last_buffer[BUF_SIZE] = "";
    static char current_buffer[BUF_SIZE] = "";

    if(strcmp(last_buffer, current_buffer) == 0){
        PRINT("\r");
    }else{
        PRINT("\r\n");
    }

    strncpy(last_buffer, current_buffer, BUF_SIZE);
    PRINTF("|%20s|%20s| % 6d | %6u | %6u | ", bs_buf, ks_buf, which(), nbits, bnm);
    strncpy(current_buffer, buffer, BUF_SIZE);

    switch(buttons_state){
        case START:
            if(state_changed){
                PRINTF("Clean slate");
            }
            break;
        default:
            break;
    }

#endif
}
