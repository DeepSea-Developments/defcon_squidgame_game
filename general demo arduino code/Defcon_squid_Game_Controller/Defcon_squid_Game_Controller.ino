#include <lvgl.h>
#include <Wire.h>
#include <ADXL345.h>
#include <Adafruit_NeoPixel.h>


// uncomment a library for display driver
#define USE_TFT_ESPI_LIBRARY
// #define USE_ARDUINO_GFX_LIBRARY

#include "lv_xiao_round_screen.h"
#include "lv_hardware_test.h"


#define VIBRATION 1
#define RGB 43
#define BUTTON_L 41
#define BUTTON_R 42

ADXL345 adxl; //variable adxl is an instance of the ADXL345 library

Adafruit_NeoPixel strip = Adafruit_NeoPixel(60, RGB, NEO_GRB + NEO_KHZ800);


// the setup function runs once when you press reset or power the board
void setup() {
    pinMode(VIBRATION, OUTPUT);
    pinMode(BUTTON_L, INPUT_PULLUP); 
    pinMode(BUTTON_R, INPUT_PULLUP); 

    Serial.begin(9600);
    adxl.powerOn();

    strip.begin();
    strip.setBrightness(50);
    strip.show(); // Initialize all pixels to 'off'


    //set activity/ inactivity thresholds (0-255)
    // adxl.setActivityThreshold(75); //62.5mg per increment
    // adxl.setInactivityThreshold(75); //62.5mg per increment
    // adxl.setTimeInactivity(10); // how many seconds of no activity is inactive?
    
    //look of activity movement on this axes - 1 == on; 0 == off 
    // adxl.setActivityX(0);
    // adxl.setActivityY(0);
    // adxl.setActivityZ(0);
    
    //look of inactivity movement on this axes - 1 == on; 0 == off
    // adxl.setInactivityX(0);
    // adxl.setInactivityY(0);
    // adxl.setInactivityZ(0);
    
    //look of tap movement on this axes - 1 == on; 0 == off
    // adxl.setTapDetectionOnX(0);
    // adxl.setTapDetectionOnY(0);
    // adxl.setTapDetectionOnZ(0);
    
    // //set values for what is a tap, and what is a double tap (0-255)
    // adxl.setTapThreshold(50); //62.5mg per increment
    // adxl.setTapDuration(15); //625us per increment
    // adxl.setDoubleTapLatency(80); //1.25ms per increment
    // adxl.setDoubleTapWindow(200); //1.25ms per increment
    
    // //set values for what is considered freefall (0-255)
    // adxl.setFreeFallThreshold(7); //(5 - 9) recommended - 62.5mg per increment
    // adxl.setFreeFallDuration(45); //(20 - 70) recommended - 5ms per increment
    
    //setting all interrupts to take place on int pin 1
    //I had issues with int pin 2, was unable to reset it
    // adxl.setInterruptMapping( ADXL345_INT_SINGLE_TAP_BIT,   ADXL345_INT1_PIN );
    // adxl.setInterruptMapping( ADXL345_INT_DOUBLE_TAP_BIT,   ADXL345_INT1_PIN );
    // adxl.setInterruptMapping( ADXL345_INT_FREE_FALL_BIT,    ADXL345_INT1_PIN );
    // adxl.setInterruptMapping( ADXL345_INT_ACTIVITY_BIT,     ADXL345_INT1_PIN );
    // adxl.setInterruptMapping( ADXL345_INT_INACTIVITY_BIT,   ADXL345_INT1_PIN );
    
    //register interrupt actions - 1 == on; 0 == off  
    // adxl.setInterrupt( ADXL345_INT_SINGLE_TAP_BIT, 1);
    // adxl.setInterrupt( ADXL345_INT_DOUBLE_TAP_BIT, 1);
    // adxl.setInterrupt( ADXL345_INT_FREE_FALL_BIT,  1);
    // adxl.setInterrupt( ADXL345_INT_ACTIVITY_BIT,   1);
    // adxl.setInterrupt( ADXL345_INT_INACTIVITY_BIT, 1);


    lv_init();
    #if LVGL_VERSION_MAJOR == 9
    lv_tick_set_cb(millis);
    #endif
    
    lv_xiao_disp_init();
    lv_xiao_touch_init();

    lv_hardware_test();

}

// the loop function runs over and over again forever
// Variables for shake detection
const int SAMPLE_WINDOW = 50;  // Number of samples to consider for frequency calculation
float prevMagnitudes[SAMPLE_WINDOW] = {0};  // Array to store previous magnitudes
int sampleIndex = 0;
unsigned long lastShakeTime = 0;
int crossingCount = 0;
float currentScore = 0;
float baselineMagnitude = 0;  // Running average for still position
bool baselineInitialized = false;
const float MAGNITUDE_WEIGHT = 0.6;  // Weight for magnitude component in scoring
const float FREQUENCY_WEIGHT = 0.4;  // Weight for frequency component in scoring
const float SHAKE_THRESHOLD = 20;  // Minimum deviation from baseline to be considered a shake

// Score accumulation variables
float accumulatedScore = 0;
bool lastButtonState = HIGH;  // Assuming button is pulled high when not pressed

// Calculate shake score based on accelerometer data with baseline correction and frequency detection
float calculateShakeScore(int x, int y, int z) {
  // Calculate proper vector magnitude
  float magnitude = sqrt(x*x + y*y + z*z);
  
  // Initialize or update baseline (running average of still position)
  if (!baselineInitialized) {
    baselineMagnitude = magnitude;
    baselineInitialized = true;
  } else {
    // Slowly adjust baseline (0.01 = 1% per sample)
    baselineMagnitude = baselineMagnitude * 0.99 + magnitude * 0.01;
  }
  
  // Calculate deviation from baseline to remove static offset
  float deviation = abs(magnitude - baselineMagnitude);
  
  // Store current deviation in circular buffer
  prevMagnitudes[sampleIndex] = deviation;
  sampleIndex = (sampleIndex + 1) % SAMPLE_WINDOW;
  
  // Calculate frequency component by counting zero crossings
  crossingCount = 0;
  float avgDeviation = 0;
  for (int i = 0; i < SAMPLE_WINDOW - 1; i++) {
    avgDeviation += prevMagnitudes[i];
    // Count crossings above/below average
    if ((prevMagnitudes[i] > SHAKE_THRESHOLD && prevMagnitudes[i+1] < SHAKE_THRESHOLD) ||
        (prevMagnitudes[i] < SHAKE_THRESHOLD && prevMagnitudes[i+1] > SHAKE_THRESHOLD)) {
      crossingCount++;
    }
  }
  avgDeviation /= SAMPLE_WINDOW;
  
  // Calculate magnitude score (0-100 based on deviation strength)
  float magnitudeScore = constrain(map(deviation, 0, 200, 0, 100), 0, 100);
  
  // Calculate frequency score (0-100 based on shake frequency)
  float frequencyScore = constrain(map(crossingCount, 0, 25, 0, 100), 0, 100);
  
  // Combine magnitude and frequency with weights
  float combinedScore = (magnitudeScore * MAGNITUDE_WEIGHT) + (frequencyScore * FREQUENCY_WEIGHT);
  
  return combinedScore;
}

// Function to update accumulated score, reset on right button press
void updateAccumulatedScore() {
  // Read current button state
  bool currentButtonState = digitalRead(BUTTON_R);
  
  // Check for button press (transition from HIGH to LOW)
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    // Reset accumulated score on right button press
    accumulatedScore = 0;
  }
  
  // Update button state for next iteration
  lastButtonState = currentButtonState;
  
  // Add current shake score to accumulated total
  accumulatedScore += currentScore;
}

void loop() {
    // digitalWrite(PIN, HIGH);  // turn the LED on (HIGH is the voltage level)
    // delay(1000);                      // wait for a second
    // digitalWrite(PIN, LOW);   // turn the LED off by making the voltage LOW
    // delay(1000);                      // wait for a second

    lv_timer_handler();  //let the GUI do its work 
    // colorWipe(strip.Color(255, 0, 0), 50); // Red

    delay( 20 );

    //Boring accelerometer stuff   
	int x,y,z;  
	adxl.readXYZ(&x, &y, &z); //read the accelerometer values and store them in variables  x,y,z
	// Output x,y,z values 
	// Serial.print("values of X , Y , Z: ");
	// Serial.print(x);
	// Serial.print(" , ");
	// Serial.print(y);
	// Serial.print(" , ");
	// Serial.print(z);
	// Serial.print(" | Score: ");
	
	// Calculate current shake score
	currentScore = calculateShakeScore(x, y, z);
	
	// Update accumulated score and handle button press
	updateAccumulatedScore();
	
	// Print for Serial Plotter (tab-separated values)
	Serial.print(currentScore);
	// Serial.print("\t");
	// Serial.println(accumulatedScore);

}


// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
  for(uint16_t i=0; i<strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}
