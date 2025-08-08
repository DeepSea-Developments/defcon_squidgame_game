/**
 * @file firmware_controls.ino
 * @author Gemini
 * @brief Refactored Arduino firmware for a shake-detecting game controller with JSON support and a flicker-free TFT screen.
 * @version 4.6
 * @date 2025-08-08
 *
 * @details This firmware controls a device with an ADXL345 accelerometer, a vibration motor,
 * an Adafruit NeoPixel LED strip, and a TFT screen. It uses FreeRTOS to handle serial commands,
 * accelerometer readings, LED animations, and TFT screen updates concurrently. It can parse
 * JSON commands to control a "red light/green light" state. The TFT screen now displays a
 * colored circle representing the player.
 *
 * This version uses a TFT_eSprite (double buffering) to eliminate screen flicker during updates.
 *
 * Core functionalities:
 * 1.  **Accelerometer-based Shake Detection:** Continuously reads from the ADXL345
 * to calculate a real-time shake score.
 * 2.  **Haptic Feedback:** Provides specific vibration patterns for win and lose events.
 * 3.  **JSON Command Parsing:** The serial task can parse JSON strings to get game
 * state information like light color and game status.
 * 4.  **Flicker-Free TFT Screen Control:** A dedicated RTOS task updates the screen using a
 * sprite (in-memory buffer) to prevent flickering. It displays a red/green background
 * indicating the game state, with a central circle representing the player's color.
 * 5.  **LED Progress Bar:** The entire LED strip shows player progress, with the color indicating the current light state (red/green).
 * 6.  **RTOS-based Operation:** Uses three RTOS tasks for concurrent operation of serial
 * command listening, LED animation, and TFT screen management.
 */

// --- Included Libraries ---
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <ADXL345.h>
#include <string.h>      // For strcmp
#include <ArduinoJson.h> // For parsing JSON commands

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240
#define LVGL_BUFF_SIZE 10 // Number of rows

#define CHSC6X_I2C_ID 0x2e
#define CHSC6X_MAX_POINTS_NUM 1
#define CHSC6X_READ_POINT_LEN 5
#define TOUCH_INT D7

#ifndef XIAO_BL
#define XIAO_BL D6
#endif
#define XIAO_DC D3
#define XIAO_CS D1

uint8_t screen_rotation;

#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI(SCREEN_WIDTH, SCREEN_HEIGHT);
TFT_eSprite spr = TFT_eSprite(&tft); // Sprite for flicker-free drawing

// --- Image assets and definitions ---
// Note: These are no longer used by the TFT task but are kept for potential future use.
#include "player_images.h"

// --- Pin and LED Configuration ---
#define LED_PIN 43
#define LED_COUNT 24
#define VIBRATION_PIN 1

// --- Global Objects ---
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
ADXL345 adxl;

// --- Shake Detection Constants and Variables ---
const int SAMPLE_WINDOW = 10;
const float SHAKE_THRESHOLD = 1.5;
const float MAGNITUDE_WEIGHT = 0.8;
const float FREQUENCY_WEIGHT = 0.2;

// --- RTOS Handles ---
QueueHandle_t commandQueue;
TaskHandle_t serialTaskHandle;
TaskHandle_t animationTaskHandle;
TaskHandle_t tftTaskHandle; // Handle for the new TFT task

// --- Game State Variables (volatile for thread-safety) ---
volatile bool isGreenLight = false;
volatile bool isPlayerAlive = true;                     // Player status, true by default
volatile int playerProgress = 0;                        // Player progress (0-100)
volatile uint32_t playerProgressColor = 0;              // Stores the packed color for NeoPixels
volatile uint8_t playerR = 0, playerG = 0, playerB = 0; // Stores color components for TFT

// --- Animation State Machine ---
enum AnimationState
{
  ANIM_IDLE,
  ANIM_DEFAULT,
  ANIM_WIN,
  ANIM_LOSE,
  ANIM_GAME_OVER
};
volatile AnimationState currentAnimation = ANIM_DEFAULT;
volatile bool winnerReceived = false;

// --- Function Prototypes ---
void serialTask(void *pvParameters);
void animationTask(void *pvParameters);
void tftTask(void *pvParameters); // Prototype for the new TFT task
int calculateShakeScore(int x, int y, int z);
void runAnimations();
void updateProgressBar();
void winAnimation();
void loseAnimation();
void drawImageWithTransparency(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t *img, uint16_t transparentColor);
uint32_t Wheel(byte WheelPos);

// --- Setup Function ---
void setup()
{
  Serial.begin(115200);
  pinMode(VIBRATION_PIN, OUTPUT);
  digitalWrite(VIBRATION_PIN, LOW); // Ensure vibration is off at start

  // Initialize NeoPixel Strip
  strip.begin();
  strip.setBrightness(150);
  strip.show(); // Initialize all pixels to 'off'

  // Initialize TFT Screen and Sprite
  tft.begin();
  tft.setRotation(screen_rotation);
  spr.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT); // Create the sprite buffer in memory
  spr.setSwapBytes(true);                        // Swap byte order for 16-bit image data from arrays
  tft.fillScreen(TFT_RED);                       // Start with red screen, as isGreenLight is initially false

  // Initialize Accelerometer
  adxl.powerOn();

  // Create RTOS command queue for win/lose events
  commandQueue = xQueueCreate(2, sizeof(char[32]));

  // Create and pin serial task to Core 0
  xTaskCreatePinnedToCore(serialTask, "Serial Task", 4096, NULL, 1, &serialTaskHandle, 0);

  // Create and pin animation task to Core 1
  xTaskCreatePinnedToCore(animationTask, "Animation Task", 4096, NULL, 2, &animationTaskHandle, 1);

  // Create and pin the new TFT task to Core 1
  xTaskCreatePinnedToCore(tftTask, "TFT Task", 4096, NULL, 1, &tftTaskHandle, 1);
}

// --- Main Loop (Arduino Core) ---
// Dedicated to reading the accelerometer and printing the score based on game state.
void loop()
{
  int x, y, z;
  adxl.readXYZ(&x, &y, &z);

  int currentScore = calculateShakeScore(x, y, z);

  // Throttle serial output
  static unsigned long lastPrintTime = 0;
  if (millis() - lastPrintTime > 100)
  {
    lastPrintTime = millis();
    // Always print the current score
    Serial.println((float)currentScore);
  }
  delay(10);
}

/**
 * @brief RTOS task to handle incoming serial commands (JSON and plain text).
 */
void serialTask(void *pvParameters)
{
  char command[256]; // Increased buffer for JSON
  int index = 0;

  for (;;)
  {
    if (Serial.available())
    {
      char c = Serial.read();
      if (c == '\n' || index >= sizeof(command) - 1)
      {
        command[index] = '\0';

        // Attempt to parse the command as JSON first
        StaticJsonDocument<256> doc; // Increased size for new format
        DeserializationError error = deserializeJson(doc, command);

        if (!error)
        {
          // JSON parsing successful
          // Parse "light" state ("red" or "green")
          if (doc.containsKey("light"))
          {
            const char *light = doc["light"];
            isGreenLight = (strcmp(light, "green") == 0);
          }

          // Parse "progress"
          if (doc.containsKey("progress"))
          {
            playerProgress = doc["progress"];
          }

          // Parse and darken "player_color" array
          if (doc.containsKey("player_color"))
          {
            JsonArray colorArray = doc["player_color"].as<JsonArray>();
            if (colorArray.size() == 3)
            {
              uint8_t r = colorArray[0];
              uint8_t g = colorArray[1];
              uint8_t b = colorArray[2];
              float factor = 1;
              // Store darkened components for TFT
              playerR = (uint8_t)(r * factor);
              playerG = (uint8_t)(g * factor);
              playerB = (uint8_t)(b * factor);
              // Get packed color for NeoPixel
              playerProgressColor = strip.Color(playerR, playerG, playerB);
            }
          }

          // Parse "status" to trigger animations
          if (doc.containsKey("status"))
          {
            const char *status = doc["status"];
            if (strcmp(status, "winner") == 0)
            {
              char cmd[] = "WINNER";
              xQueueSend(commandQueue, &cmd, portMAX_DELAY);
            }
            else if (strcmp(status, "eliminated") == 0)
            {
              char cmd[] = "ELIMINATED";
              xQueueSend(commandQueue, &cmd, portMAX_DELAY);
            }
            else if (strcmp(status, "game_over") == 0)
            {
              char cmd[] = "GAME_OVER";
              xQueueSend(commandQueue, &cmd, portMAX_DELAY);
            }
            else if (strcmp(status, "playing") == 0)
            {
              char cmd[] = "PLAYING";
              xQueueSend(commandQueue, &cmd, portMAX_DELAY);
            }
            else if (strcmp(status, "game_started") == 0)
            {
              char cmd[] = "GAME_STARTED";
              xQueueSend(commandQueue, &cmd, portMAX_DELAY);
            }
            else if (strcmp(status, "game_stopped") == 0)
            {
              char cmd[] = "GAME_STOPPED";
              xQueueSend(commandQueue, &cmd, portMAX_DELAY);
            }
          }
        }
        else
        {
          // JSON parsing failed, fall back to simple commands
          if (strcmp(command, "WINNER") == 0 || strcmp(command, "ELIMINATED") == 0 || strcmp(command, "GAME_OVER") == 0 || strcmp(command, "NEW_GAME") == 0)
          {
            xQueueSend(commandQueue, &command, portMAX_DELAY);
          }
        }
        index = 0; // Reset for next command
      }
      else if (isprint(c))
      {
        command[index++] = c;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

/**
 * @brief RTOS task to manage the TFT screen. It displays a red/green background
 * based on the game state and a central circle with the player's color.
 */
void tftTask(void *pvParameters)
{
  bool lastKnownLightState = !isGreenLight;

  bool lastPlayerAliveState = !isPlayerAlive;

  uint8_t lastR = 0, lastG = 0, lastB = 0; // Track last player color

  for (;;)
  {
    // Redraw if light state or player color changes
    bool needsRedraw = (isGreenLight != lastKnownLightState) ||
                       (playerR != lastR) || (playerG != lastG) || (playerB != lastB) || (isPlayerAlive != lastPlayerAliveState);

    if (needsRedraw)
    {
      // 1. Fill sprite background based on light state (red/green)
      uint16_t bgColor = isGreenLight ? TFT_GREEN : TFT_RED;
      spr.fillSprite(bgColor);

      // 2. Calculate circle properties
      int16_t centerX = SCREEN_WIDTH / 2;
      int16_t centerY = SCREEN_HEIGHT / 2;
      int16_t radius = (SCREEN_WIDTH * 0.7) / 2; // Diameter is 70% of screen width

      // 3. Draw the filled circle with the player's color
      uint16_t playerColor565 = tft.color565(playerR, playerG, playerB);
      spr.fillCircle(centerX, centerY, radius, playerColor565);

      // 4. Draw alive/dead image in the center of the sprite using a custom function
      int16_t x_pos = (SCREEN_WIDTH - (IMG_WIDTH-10)) / 2;
      int16_t y_pos = (SCREEN_HEIGHT - (IMG_HEIGHT-10)) / 2;

      if (isPlayerAlive)
      {
        drawImageWithTransparency(x_pos, y_pos, IMG_WIDTH, IMG_HEIGHT, alive_img, TRANSPARENT_COLOR);
      }
      else
      {
        drawImageWithTransparency(x_pos, y_pos, IMG_WIDTH, IMG_HEIGHT, dead_img, TRANSPARENT_COLOR);
      }

      // 4. Push the completed sprite to the screen in one go
      spr.pushSprite(0, 0);

      // Update state trackers
      lastKnownLightState = isGreenLight;
      lastPlayerAliveState = isPlayerAlive;
      lastR = playerR;
      lastG = playerG;
      lastB = playerB;
    }

    // Wait for a short period before checking again.
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/**
 * @brief RTOS task to manage and display LED animations and player status.
 */
void animationTask(void *pvParameters)
{
  char receivedCommand[32];

  for (;;)
  {
    // Check for a command from the queue
    if (xQueueReceive(commandQueue, &receivedCommand, 0) == pdPASS)
    {
      if (strcmp(receivedCommand, "WINNER") == 0)
      {
        currentAnimation = ANIM_WIN;
        winnerReceived = true;
      }
      else if (strcmp(receivedCommand, "ELIMINATED") == 0)
      {
        currentAnimation = ANIM_LOSE;
        isPlayerAlive = false; // Player is eliminated
      }
      else if (strcmp(receivedCommand, "GAME_OVER") == 0)
      {
        //currentAnimation = ANIM_GAME_OVER;
      }
      else if (strcmp(receivedCommand, "NEW_GAME") == 0 || strcmp(receivedCommand, "GAME_STARTED") == 0)
      {
        winnerReceived = false;
        isPlayerAlive = true;    // Player is alive for new game
        playerProgress = 0;      // Reset progress for new game
        playerProgressColor = 0; // Reset the progress color
        currentAnimation = ANIM_DEFAULT;
      }
      else if (strcmp(receivedCommand, "PLAYING") == 0)
      {
        currentAnimation = ANIM_DEFAULT;
      }
      else if (strcmp(receivedCommand, "GAME_STOPPED") == 0)
      {
        playerProgress = 0;
        playerProgressColor = 0;
        isGreenLight = false; // Set light to red to indicate stopped state
        currentAnimation = ANIM_DEFAULT;
      }
    }

    runAnimations();
    vTaskDelay(pdMS_TO_TICKS(50)); // Update progress bar at ~20fps
  }
}

/**
 * @brief Main animation state machine runner.
 */
void runAnimations()
{
  switch (currentAnimation)
  {
  case ANIM_IDLE:
    break;
  case ANIM_WIN:
    winAnimation();
    currentAnimation = ANIM_IDLE; // Revert after one run
    break;
  case ANIM_LOSE:
    loseAnimation();
    currentAnimation = ANIM_IDLE; // Revert after one run
    break;
  case ANIM_GAME_OVER:
    if (!winnerReceived)
    {
      loseAnimation();
    }
    currentAnimation = ANIM_DEFAULT;
    break;
  case ANIM_DEFAULT:
  default:
    updateProgressBar(); // Default animation is now the progress bar
    break;
  }
}

/**
 * @brief Displays progress on the entire LED strip, colored according to the light state.
 */
void updateProgressBar()
{
  strip.clear();

  // Calculate the number of LEDs for the progress bar
  int ledsToShow = map(playerProgress, 0, 100, 0, LED_COUNT);

  // Determine the color of the progress bar based on the light state
  uint32_t progressColor = isGreenLight ? strip.Color(0, 80, 0) : strip.Color(80, 0, 0);

  // Draw the progress bar on all LEDs with the light state color
  for (int i = 0; i < ledsToShow; i++)
  {
    strip.setPixelColor(i, progressColor);
  }

  strip.show();
}

/**
 * @brief Calculates a shake score from accelerometer data.
 */
int calculateShakeScore(int x, int y, int z)
{
  int gravity_offset = 250;
  int currentScore = abs(x) + abs(y) + abs(z) - gravity_offset;
  currentScore = abs(currentScore);

  if (currentScore < 200)
    return 0;
  else if (currentScore < 400)
    return 1;
  else if (currentScore < 600)
    return 3;
  else if (currentScore < 900)
    return 4;
  else if (currentScore < 1200)
    return 5;

  return 6;
}

/**
 * @brief Animation for winning. Includes rainbow cycle, flashing lights, and alternating vibration.
 */
void winAnimation()
{
  digitalWrite(VIBRATION_PIN, HIGH); // Start vibration
  // Rainbow cycle part
  for (int j = 0; j < 256 * 2; j++)
  {
    for (int i = 0; i < strip.numPixels(); i++)
    {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
    }
    strip.show();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  digitalWrite(VIBRATION_PIN, LOW); // Stop vibration
}

/**
 * @brief Animation for losing. Includes red fade effect and a solid vibration.
 */
void loseAnimation()
{
  digitalWrite(VIBRATION_PIN, HIGH); // Start vibration
  // Red fade animation
  for (int j = 0; j < 2; j++)
  {
    // Fade in
    for (int k = 0; k < 255; k++)
    {
      strip.fill(strip.Color(k, 0, 0));
      strip.show();
      vTaskDelay(pdMS_TO_TICKS(3));
    }
    // Fade out
    for (int k = 255; k >= 0; k--)
    {
      strip.fill(strip.Color(k, 0, 0));
      strip.show();
      vTaskDelay(pdMS_TO_TICKS(3));
    }
  }
  digitalWrite(VIBRATION_PIN, LOW); // Stop vibration
}

/**
 * @brief Generates rainbow colors across 0-255 positions.
 */
uint32_t Wheel(byte WheelPos)
{
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85)
  {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if (WheelPos < 170)
  {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

/**
 * @brief Draws a 16-bit image from an array onto the sprite, skipping a specified transparent color.
 * @param x The x-coordinate of the top-left corner.
 * @param y The y-coordinate of the top-left corner.
 * @param w The width of the image.
 * @param h The height of the image.
 * @param img Pointer to the image data array (16-bit colors).
 * @param transparentColor The color to treat as transparent.
 */
void drawImageWithTransparency(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t *img, uint16_t transparentColor)
{
  for (int16_t j = 0; j < h; j++)
  {
    for (int16_t i = 0; i < w; i++)
    {
      uint16_t color = img[j * w + i];
      if (color != transparentColor)
      {
        spr.drawPixel(x + i, y + j, color);
      }
    }
  }
}
