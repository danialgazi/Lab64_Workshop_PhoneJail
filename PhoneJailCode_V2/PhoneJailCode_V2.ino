// -------------------- Imports --------------------

// OLED Imports
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>
#include <EEPROM.h>

// -------------------- Module Definitions --------------------

// Phone jail states
typedef enum {
  STATE_IDLE,
  STATE_LOCKED
} State_t;
State_t currentLockState = STATE_LOCKED;  // Set initial state to locked (prevents accidental unlocking)

// Encoder definitions (TODO: CONFIRM THESE ARE CORRECT)
#define PIN_ENCODER_A 2     // A
#define PIN_ENCODER_B 3     // B
#define PIN_BUTTON 5        // S2
#define PIN_SERVO 10        // M1

// OLED definitions
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library. 
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Servo definitions
Servo servo;
int closedAngle = 90;    // [deg], servo closed angle
int openAngle = 0;       // [deg], servo open angle
bool servoState = 0;     // 0 = closed (default), 1 = open

// Timer definitions
volatile unsigned long timerDuration = 0;  // [ms], to update with rotary encoder
unsigned long timeRemaining = 0;           // [ms], time left in timer
unsigned long startTime = 0;               // [ms], global time at start
bool isTimerRunning = false;      // is the timer running?
const unsigned long timerIncrement = 60000;      // [ms], how much timer increases/decreases by each scroll

// EEPROM stuff
const uint32_t signature = 0x12345678;        // signature value
const uint8_t signatureAddr = 0x00;           // address of signature stored in EEPROM
const uint8_t timeRemainingAddr = 0x10;       // address of timeRemaing variable
const uint8_t timerDurationAddr = 0x20;       // address of timerDuration variable
unsigned long lastWriteTime = 0;              // time stamp of last write to EEPROM
const unsigned long interval = 60000;         // write to EEPROM only every minute
const unsigned long punishment = 60000 * 60;  // added time for turning it off while running

// -------------------- Function Definitions --------------------
// State machines and services
void RunLockingSM();            // Main state machine, handles locking
void RunScrollingService();     // Service, handles rotary encoder scrolling
void RunDisplayUpdateService(); // Service, updates display every second

// Event checkers
bool CheckButtonPressed();      // Checks rotary encoder button pressed
bool CheckTimerExpired();       // Checks if the timer has expired

// EEPROM helper functions
bool EEPROMInitialized();

// -------------------- MAIN CODE --------------------

// SETUP: runs once
void setup() {
  // put your setup code here, to run once:

  Serial.begin(9600);

  if (!EEPROMInitialized()) {
    // first time boards powered up
    // erase eeprom
    for (int i = 0; i < EEPROM.length(); i++) {
      EEPROM.write(i, 0x00);
    }
    // write unique signature to eeprom
    EEPROM.put(signatureAddr, signature);
  } else {
    EEPROM.get(timeRemainingAddr, timeRemaining);
    timeRemaining += punishment;
    EEPROM.get(timerDurationAddr, timerDuration);
    currentLockState = STATE_LOCKED;
  }

  // ------ Encoder Setup -------
  // set pins as input with internal pull-up resistors enabled
  pinMode(PIN_ENCODER_A, INPUT);
  pinMode(PIN_ENCODER_B, INPUT);
  pinMode(PIN_BUTTON, INPUT);
  digitalWrite(PIN_ENCODER_A, HIGH);
  digitalWrite(PIN_ENCODER_B, HIGH);
  digitalWrite(PIN_BUTTON, LOW);

  // Attach interrupt for scrolling service
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), RunScrollingService, CHANGE);

  // ------- Servo Setup -------
  servo.attach(PIN_SERVO);

  // ------- OLED Display Setup -------
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(75); // Pause for 2 seconds

  // Clear the buffer
  display.clearDisplay();

  // Make timer text
  display.setTextSize(2);             
  display.setTextColor(SSD1306_WHITE);       // Draw white text
}

// LOOP: runs continuously
void loop() {
  // put your main code here, to run repeatedly:

  // No need to add scrolling service here--it's an interrupt in setup()
  RunDisplayUpdateService();
  RunLockingSM();

}


// -------------------- Service Functions --------------------

/*
Name: RunScrollingService
Purpose: Checks rotary encoder rising and falling edges and updates timer duration
Inputs: None
Outputs: None
*/
void RunScrollingService() {

  // Check current encoder pin states
  bool A_state = digitalRead(PIN_ENCODER_A);
  bool B_state = digitalRead(PIN_ENCODER_B);

  // Timer duration can only change when device is in idle state
  if (currentLockState == STATE_IDLE) {
    if (A_state == B_state) {
      // Make sure timer doesn't go negative
      if (timerDuration > 0) {
        timerDuration -= timerIncrement;    // Reduce time
      }
    } else {
      timerDuration += timerIncrement;    // Increase time
    }
  }
}


/*
Name: RunDisplayUpdateService
Purpose: Updates display when counting down or scrolling
Inputs: None
Outputs: None
*/
void RunDisplayUpdateService() {

  if (isTimerRunning == true) { // Update display when counting down

    // Declare current and previous display times
    static unsigned long prevDisplaySecond = 0;
    static unsigned long currentDisplaySecond = timeRemaining / 1000;

    // We only want to update the display once per sec
    if (currentDisplaySecond != prevDisplaySecond) {

      // Calculate minutes and seconds to display
      int minRemaining = timeRemaining / 60000;
      int secRemaining = (timeRemaining % 60000) / 1000;

      // Format display
      display.clearDisplay();
      display.setTextSize(2);

      // Print pre-time message
      display.setCursor(0, 0);
      display.println(F("Remaining:"));

      // Print time remaining in MM:SS
      display.setCursor(0, 25);
      display.print(minRemaining);
      display.print(":");
      if (secRemaining < 10) display.print("0");  // Add leading 0 for seconds if < 10 sec
      display.print(secRemaining);

      // Display the time
      display.display();

      // Update previous display second to equal current
      prevDisplaySecond = currentDisplaySecond;
    }

  } else {                  // Update display when scrolling

    int timerMin = timerDuration / 60000;
    int timerSec = (timerDuration % 60000) / 1000;

    // Format display
    display.clearDisplay();
    display.setTextSize(2);

    // Print pre-time message
    display.setCursor(0, 0);
    display.println(F("Set Time:"));

    // Print timer duration
    display.setCursor(0, 25);
    display.print(timerMin);
    display.print("m ");
    display.print(timerSec);
    display.print("s");

    // Display the time
    display.display();
    
  }

}


/*
Name: RunLockingSM
Purpose: Main run function for State Machine. Handles state switching and runs event checkers.
Inputs: None
Outputs: None
*/
void RunLockingSM() {

  // Run event checkers
  bool isButtonPressed = CheckButtonPressed();
  bool isTimerRunning = CheckTimerExpired();

  // State switch case
  switch (currentLockState) {
    case STATE_IDLE:
      // Only care about button if timer is set to a nonzero value
      if ((isButtonPressed == true) && (timerDuration > 0)) {
        // 1. Lock servo
        servo.write(closedAngle);

        // 2. Set start time for timer and write time duration to EEPROM
        startTime = millis();
        isTimerRunning = true;
        EEPROM.put(timerDurationAddr, timerDuration);

        // 3. Move to next state
        currentLockState = STATE_LOCKED;
      }
      break;
    
    case STATE_LOCKED:
      if (!isTimerRunning) {
        // 1. Unlock servo
        servo.write(openAngle);

        // // 2. Update display 
        // // 2a. Format display
        // display.clearDisplay();
        // display.setTextSize(2);

        // // 2b. Print message
        // display.setCursor(0, 0);
        // display.println(F("Done!"));

        // // 2c. Display the message
        // display.display();

        // 3. Reset values
        isTimerRunning = false;   // Timer has stopped running
        timerDuration = 0;        // Reset timer duration

        // 4. Move to next state (unlocked)
        currentLockState = STATE_IDLE;
      }
      break;
  }
}


// -------------------- Event Checker Functions --------------------

/*
Name: CheckButtonPressed
Purpose: Checks if rotary encoder button is pressed. Returns true if so.
Inputs: None
Outputs: bool
*/
bool CheckButtonPressed() {
  bool returnVal = false;

  // Initialize button states
  static bool prevButtonState = 0;
  bool currentButtonState = digitalRead(PIN_BUTTON);

  // Check if state has changed since last run and button is pressed
  if ((prevButtonState != currentButtonState) && currentButtonState == HIGH) {
    returnVal = true;
  }

  // Set previous state to current state for next loop
  prevButtonState = currentButtonState;

  return returnVal;
}


/*
Name: CheckTimerExpired
Purpose: Checks if locking timer has expired. Returns true if so.
Inputs: None
Outputs: bool
*/
bool CheckTimerExpired() {
  bool returnVal = false;

  unsigned long currentTime = millis();
  long elapsedTime = currentTime - startTime;

  // Make sure time remaining is never negative
  if (elapsedTime >= timeRemaining) {
    timeRemaining = 0; // Lower limit is 0
    returnVal = true;
  } else {
    timeRemaining = timerDuration - elapsedTime;
  }

  // If a minute as elapsed from last write then write to EEPROM
  if (currentTime - lastWriteTime > interval) {
    EEPROM.put(timeRemainingAddr, timeRemaining);
    lastWriteTime = millis();
  }

  return returnVal;
}

/*
Name: EEPROMInitialized
Purpose: Checks if EEPROM has been written to before. Returns true if yes, otherwise false.
Inputs: None
Outputs: bool
*/
bool EEPROMInitialized() {
  uint32_t val;
  EEPROM.get(signatureAddr, val);
  return val == signature;
}