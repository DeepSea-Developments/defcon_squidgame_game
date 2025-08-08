#include <lvgl.h>
#include <Wire.h>
#include <ADXL345.h>
#include <Adafruit_NeoPixel.h>
#include "arduinoFFT.h"


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
// Variables for FFT-based shake detection
const uint16_t FFT_SAMPLES = 64;  // Must be power of 2 for FFT
double vReal[FFT_SAMPLES];
double vImag[FFT_SAMPLES];
int sampleIndex = 0;
float currentScore = 0;
float baselineMagnitude = 0;  // Running average for still position
bool baselineInitialized = false;
const float SAMPLING_FREQUENCY = 50;  // Hz (based on 20ms delay in main loop)

// FFT object
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SAMPLES, SAMPLING_FREQUENCY);

// Score accumulation variables
float accumulatedScore = 0;
bool lastButtonState = HIGH;  // Assuming button is pulled high when not pressed




// Calculate shake score based on FFT frequency analysis
int calculateShakeScore(int x, int y, int z) 
{
  int gravity_offset = 250;
  
  currentScore = abs(x) + abs(y) + abs(z) - gravity_offset;
  currentScore =  abs(currentScore);
  // Return previous score if buffer not full yet

  if(currentScore < 200)
    return 0;
  else if (currentScore < 400)
    return 1;
  else if ( currentScore < 600)
    return 3;
  else if (currentScore < 900)
    return 4;
  else if (currentScore < 1200)
    return 5;
  
  return 6;

  return currentScore;
}



void loop() 
{
    int x, y, z;
    adxl.readXYZ(&x, &y, &z);

    float currentScore = calculateShakeScore(x, y, z);

    // Throttle serial output
    static unsigned long lastPrintTime = 0;
    if (millis() - lastPrintTime > 100) {
      lastPrintTime = millis();
      // Only print score on "green light", otherwise print 0
      if (1) {//isGreenLight
        float x = currentScore / 10;
        if (x < 1.6)
          x = 0;
        Serial.println(currentScore);
      } else {
        Serial.println(currentScore);
      }
    }
    delay(10);
}


// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
  for(uint16_t i=0; i<strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}
