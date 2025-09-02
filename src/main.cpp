#include "config.h"
#include "audio.h"
#include "ble.h"
#include <SdFat.h>

// ========================================
// LED Status Management
// ========================================

typedef enum {
  LED_IDLE,
  LED_RECORDING,
  LED_TRANSFERRING,
  LED_STREAMING,
  LED_ERROR
} LedStatus;

static LedStatus current_led_status = LED_IDLE;

// Button state management
struct ButtonState {
  bool current_state;
  bool last_state;
  uint32_t press_start_time;
  uint32_t last_debounce_time;
  bool long_press_triggered;
};

static ButtonState btn_record = {HIGH, HIGH, 0, 0, false};

// Pin assignments
constexpr int LED_PIN = LED_BUILTIN;
constexpr int BTN_RECORD_PIN = D0;

// Button timing constants
constexpr uint32_t DEBOUNCE_DELAY_MS = 50;
constexpr uint32_t LONG_PRESS_DELAY_MS = 2000;

// File transfer
static FsFile txFile;

void init_buttons() {
  pinMode(BTN_RECORD_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

bool update_button_state(ButtonState* btn, int pin) {
  bool current_reading = digitalRead(pin);
  bool button_pressed = false;
  
  // Debounce logic
  if (current_reading != btn->last_state) {
    btn->last_debounce_time = millis();
  }
  
  if ((millis() - btn->last_debounce_time) > DEBOUNCE_DELAY_MS) {
    if (current_reading != btn->current_state) {
      btn->current_state = current_reading;
      
      // Button was just pressed (HIGH to LOW transition)
      if (btn->current_state == LOW) {
        btn->press_start_time = millis();
        btn->long_press_triggered = false;
      }
      // Button was just released (LOW to HIGH transition)  
      else if (btn->current_state == HIGH) {
        uint32_t press_duration = millis() - btn->press_start_time;
        if (press_duration < LONG_PRESS_DELAY_MS && !btn->long_press_triggered) {
          button_pressed = true; // Short press detected
        }
        // Reset the long press flag when button is released
        btn->long_press_triggered = false;
      }
    }
    
    // Check for long press while button is still held
    if (btn->current_state == LOW && !btn->long_press_triggered) {
      uint32_t press_duration = millis() - btn->press_start_time;
      if (press_duration >= LONG_PRESS_DELAY_MS) {
        btn->long_press_triggered = true;
        button_pressed = true; // Long press detected
      }
    }
  }
  
  btn->last_state = current_reading;
  return button_pressed;
}

void update_led_status() {
  static uint32_t last_led_update = 0;
  static bool led_state = false;
  static int flash_count = 0;
  static int error_flash_count = 0;
  
  uint32_t current_time = millis();
  
  // Determine current status priority (Recording > Streaming > Transferring > Idle)
  if (audio_is_recording()) {
    current_led_status = LED_RECORDING;
  } else if (ENABLE_LIVE_STREAMING && audio_is_streaming()) {
    current_led_status = LED_STREAMING;  
  } else if (ble_is_connected()) {
    current_led_status = LED_TRANSFERRING;
  } else {
    current_led_status = LED_IDLE;
  }
  
  switch (current_led_status) {
    case LED_IDLE:
      if (current_time - last_led_update >= 2000) {
        led_state = !led_state;
        digitalWrite(LED_PIN, led_state ? HIGH : LOW);
        last_led_update = current_time;
      }
      break;
      
    case LED_RECORDING:
      // Flashing red pattern for recording (fast blink)
      if (current_time - last_led_update >= 300) {
        led_state = !led_state;
        digitalWrite(LED_PIN, led_state ? HIGH : LOW);
        last_led_update = current_time;
      }
      break;
      
    case LED_STREAMING:
      // Solid ON during streaming
      digitalWrite(LED_PIN, HIGH);
      break;
      
    case LED_TRANSFERRING:
      // Fast blink during transfer
      if (current_time - last_led_update >= 200) {
        led_state = !led_state;
        digitalWrite(LED_PIN, led_state ? HIGH : LOW);
        last_led_update = current_time;
      }
      break;
      
    case LED_ERROR:
      // Triple flash pattern for errors
      if (current_time - last_led_update >= 150) {
        if (error_flash_count < 6) {
          led_state = !led_state;
          digitalWrite(LED_PIN, led_state ? HIGH : LOW);
          error_flash_count++;
        } else {
          digitalWrite(LED_PIN, LOW);
          if (current_time - last_led_update >= 1000) {
            error_flash_count = 0; // Reset for next error
          }
        }
        last_led_update = current_time;
      }
      break;
  }
}

void setup(){
  // Initialize serial with longer wait time for USB serial
  Serial.begin(115200);
  delay(1000);  // Give USB time to enumerate
  while (!Serial && millis() < 5000) { 
    delay(100); 
  }

  Serial.println();
  Serial.println("=================================");
  Serial.println("XIAO Voice Logger (PCM+BLE)");
  Serial.println("=================================");
  Serial.flush();
  
  Serial.print("Initializing audio (includes SD)... ");
  if (!audio_init()) { 
    Serial.println("FAILED"); 
    Serial.flush();
    while(1) delay(1000); 
  }
  Serial.println("OK");
  
  Serial.print("Initializing BLE... ");
  if (!ble_init()) { 
    Serial.println("FAILED"); 
    Serial.flush();
    while(1) delay(1000); 
  }
  Serial.println("OK");
  
  // Always start advertising for instant connection
  Serial.print("Starting BLE advertising... ");
  ble_start_advertising();
  Serial.println("OK");
  
  Serial.print("Initializing buttons... ");
  init_buttons();
  Serial.println("OK");

  Serial.println();
  Serial.println("=== SMART VOICE RECORDER ===");
  Serial.println("🔵 BLE advertising active - ready for app connection");
  Serial.println();
  Serial.println("Button Behavior:");
  Serial.println("  📱 App connected: D0 = Live streaming + real-time transcription");
  Serial.println("  💾 App offline:   D0 = Record to SD card for later sync");
  Serial.println();
  Serial.println("Device automatically detects mode based on app connection.");
  Serial.println("Ready to stream or record...");
  Serial.flush();
}

void loop(){
  // Update LED status indicators
  update_led_status();
  
  // Handle button presses
  // Handle recording button (D0)
  bool record_pressed = update_button_state(&btn_record, BTN_RECORD_PIN);
  
  if (record_pressed) {
    if (record_pressed && !btn_record.long_press_triggered) {
      // Smart mode detection: streaming if connected, recording if not
      if (ble_streaming_is_connected()) {
        // Live mode - toggle streaming
        if (audio_is_streaming()) {
          Serial.println("> Button: Stop live streaming");
          audio_stop_streaming();
          Serial.println("✓ Live streaming stopped");
        } else {
          Serial.println("> Button: Start live streaming");
          if (audio_start_streaming()) {
            Serial.println("✓ Live streaming started - real-time transcription active");
          } else {
            Serial.println("✗ Failed to start streaming");
          }
        }
      } else {
        // Offline mode - toggle recording
        if (audio_is_recording()) {
          Serial.println("> Button: Stop offline recording");
          audio_stop_recording();
          Serial.print("✓ Offline recording stopped: "); 
          Serial.print(audio_get_last_filename());
          Serial.print(" ("); Serial.print(audio_get_bytes_recorded()); Serial.println(" bytes)");
        } else {
          Serial.println("> Button: Start offline recording");
          if (audio_start_recording()) {
            Serial.print("✓ Offline recording started: "); 
            Serial.println(audio_get_last_filename());
          } else {
            Serial.println("✗ Recording start failed");
          }
        }
      }
    }
    Serial.println();
    Serial.flush();
  }
  
  // Single-button operation - other buttons disabled for simplicity
  // (File management handled through the app)
  
  // Process audio data and BLE operations
  audio_process();
  ble_process_transfer();
}
