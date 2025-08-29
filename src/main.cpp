#include <Arduino.h>
#include <SdFat.h>
#include "config.h"
#include "audio.h"
#include "ble.h"

static FsFile txFile;

// Button pin definitions
#define BTN_RECORD_PIN    D0    // Record/Stop toggle button
#define BTN_TRANSFER_PIN  D1    // BLE transfer button (hold 2s)
#define BTN_FILES_PIN     D2    // File management button (press=list, hold=delete)
#define LED_PIN           LED_BUILTIN  // Status LED

// Button state management
struct ButtonState {
  bool current_state;
  bool last_state;
  uint32_t press_start_time;
  uint32_t last_debounce_time;
  bool long_press_triggered;
};



static ButtonState btn_record = {HIGH, HIGH, 0, 0, false};
static ButtonState btn_transfer = {HIGH, HIGH, 0, 0, false};
static ButtonState btn_files = {HIGH, HIGH, 0, 0, false};

// Button timing constants
const uint32_t DEBOUNCE_DELAY_MS = 30;     // Faster debounce
const uint32_t LONG_PRESS_DELAY_MS = 3000; // 3 seconds for long press (more forgiving)

// LED status management
enum LedStatus {
  LED_IDLE,          // Slow blink (ready)
  LED_RECORDING,     // Solid on
  LED_TRANSFERRING,  // Fast blink
  LED_ERROR          // 3 quick flashes
};

static LedStatus current_led_status = LED_IDLE;
static uint32_t last_led_update = 0;
static bool led_state = false;
static uint8_t error_flash_count = 0;

// Button helper functions
void init_buttons() {
  pinMode(BTN_RECORD_PIN, INPUT_PULLUP);
  pinMode(BTN_TRANSFER_PIN, INPUT_PULLUP);
  pinMode(BTN_FILES_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("Buttons initialized on pins D0, D1, D2");
}



bool update_button_state(ButtonState* btn, int pin) {
  bool current_reading = digitalRead(pin);
  bool button_pressed = false;
  
  // Handle debouncing
  if (current_reading != btn->last_state) {
    btn->last_debounce_time = millis();
  }
  
  if ((millis() - btn->last_debounce_time) > DEBOUNCE_DELAY_MS) {
    // Only update current_state if it has actually changed
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
  uint32_t current_time = millis();
  
  // Update LED status based on system state
  if (audio_is_recording()) {
    current_led_status = LED_RECORDING;
  } else if (ble_is_connected()) {
    current_led_status = LED_TRANSFERRING;
  } else {
    current_led_status = LED_IDLE;
  }
  
  // Handle LED patterns
  switch (current_led_status) {
    case LED_IDLE:
      // Slow blink (1Hz)
      if (current_time - last_led_update >= 500) {
        led_state = !led_state;
        digitalWrite(LED_PIN, led_state);
        last_led_update = current_time;
      }
      break;
      
    case LED_RECORDING:
      // Solid on
      digitalWrite(LED_PIN, HIGH);
      break;
      
    case LED_TRANSFERRING:
      // Fast blink (5Hz)
      if (current_time - last_led_update >= 100) {
        led_state = !led_state;
        digitalWrite(LED_PIN, led_state);
        last_led_update = current_time;
      }
      break;
      
    case LED_ERROR:
      // 3 quick flashes then off
      if (current_time - last_led_update >= 150) {
        if (error_flash_count < 6) { // 3 on/off cycles
          led_state = !led_state;
          digitalWrite(LED_PIN, led_state);
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
  
  Serial.print("Initializing buttons... ");
  init_buttons();
  Serial.println("OK");

  Serial.println();
  Serial.println("Commands (Serial or Buttons):");
  Serial.println("  r / D0 short press = start recording");
  Serial.println("  s / D0 long press = stop recording + auto BLE");
  Serial.println("  c / D1 hold 3s = manual BLE transfer");
  Serial.println("  l / D2 press = list files");
  Serial.println("  d / D2 hold 3s = delete all WAV files");
  Serial.println("Ready for commands...");
  Serial.flush();
}

void loop(){
  // Update LED status indicators
  update_led_status();
  
  // Handle button presses
  // Handle recording button (D0)
  bool record_pressed = update_button_state(&btn_record, BTN_RECORD_PIN);
  
  if (record_pressed) {
    if (btn_record.long_press_triggered) {
      // Long press: Stop recording
      if (audio_is_recording()) {
        Serial.println("> Button: Stop recording (long press)");
        audio_stop_recording();
        Serial.print("✓ Recording stopped: "); 
        Serial.print(audio_get_last_filename());
        Serial.print(" ("); Serial.print(audio_get_bytes_recorded()); Serial.print(" bytes, ");
        Serial.print(audio_get_buffer_overruns()); Serial.println(" overruns)");
        
        // Auto-start BLE transfer after stopping recording
        const char* path = audio_get_last_filename();
        if (*path) {
          if (txFile.isOpen()) txFile.close();
          
          SdFs* sd = audio_get_sd_instance();
          if (txFile.open(sd, path, O_READ)){
            Serial.print("✓ Auto-starting BLE transfer: "); 
            Serial.print(path); Serial.print(" (");
            Serial.print(txFile.fileSize()); Serial.println(" bytes)");
            ble_set_transfer_file(&txFile, (uint32_t)txFile.fileSize(), path);
            ble_start_advertising();
            Serial.println("  Waiting for BLE connection...");
          } else {
            Serial.println("✗ Failed to open file for transfer");
            current_led_status = LED_ERROR;
          }
        } else {
          Serial.println("✗ No file to transfer");
          current_led_status = LED_ERROR;
        }
      } else {
        Serial.println("> Button: Not recording");
      }
    } else {
      // Short press: Start recording
      if (!audio_is_recording()) {
        Serial.println("> Button: Start recording (short press)");
        if (audio_start_recording()){
          Serial.print("✓ Recording started: "); 
          Serial.println(audio_get_last_filename());
        } else {
          Serial.println("✗ Recording start failed");
          current_led_status = LED_ERROR;
        }
      } else {
        Serial.println("> Button: Already recording (hold to stop)");
      }
    }
    Serial.println();
    Serial.flush();
  }
  
  // Handle BLE transfer button (D1 - hold 2s)
  bool transfer_pressed = update_button_state(&btn_transfer, BTN_TRANSFER_PIN);
  
  if (transfer_pressed) {
    if (btn_transfer.long_press_triggered) {
      // D1: Start BLE transfer (only works when not recording)
      if (audio_is_recording()) {
        Serial.println("> Button: Stop recording first! Cannot transfer while recording.");
      } else {
        Serial.println("> Button: Starting BLE transfer (2s hold)...");
        const char* path = audio_get_last_filename();
        if (!*path) { 
          Serial.println("✗ No file to transfer");
          current_led_status = LED_ERROR;
        } else {
          if (txFile.isOpen()) txFile.close();
          
          SdFs* sd = audio_get_sd_instance();
          if (txFile.open(sd, path, O_READ)){
            Serial.print("✓ BLE advertising: "); 
            Serial.print(path); Serial.print(" (");
            Serial.print(txFile.fileSize()); Serial.println(" bytes)");
            ble_set_transfer_file(&txFile, (uint32_t)txFile.fileSize(), path);
            ble_start_advertising();
            Serial.println("  Waiting for BLE connection...");
          } else {
            Serial.println("✗ Failed to open file for transfer");
            current_led_status = LED_ERROR;
          }
        }
      }
      Serial.println();
      Serial.flush();
    }
  }
  
  // Handle file management button
  bool files_pressed = update_button_state(&btn_files, BTN_FILES_PIN);
  
  if (files_pressed) {
    if (btn_files.long_press_triggered) {
      // D2 long press: Delete all WAV files
      Serial.println("> Button: Delete all WAV files (2s hold)...");
      SdFs* sd = audio_get_sd_instance();
      FsFile dir, ent;
      if (dir.open(sd, "/", O_RDONLY)) {
        int deleteCount = 0;
        while (ent.openNext(&dir, O_RDONLY)){
          char n[64]; 
          ent.getName(n, sizeof(n));
          if (strstr(n, ".WAV") || strstr(n, ".wav")){
            ent.close();
            if (sd->remove(n)) {
              Serial.print("  Deleted ");
              Serial.println(n);
              deleteCount++;
            } else {
              Serial.print("  Failed to delete ");
              Serial.println(n);
            }
          } else {
            ent.close();
          }
        }
        dir.close();
        Serial.print("Deleted ");
        Serial.print(deleteCount);
        Serial.println(" files");
      } else {
        Serial.println("✗ Failed to open SD card root directory");
        current_led_status = LED_ERROR;
      }
    } else {
      // D2 short press: List files
      Serial.println("> Button: List files");
      Serial.println("Files on SD card:");
      SdFs* sd = audio_get_sd_instance();
      FsFile dir, ent;
      if (dir.open(sd, "/", O_RDONLY)) {
        int fileCount = 0;
        while (ent.openNext(&dir, O_RDONLY)){
          char n[64]; ent.getName(n, sizeof(n));
          if (strstr(n, ".WAV") || strstr(n, ".wav")){
            Serial.print("  "); Serial.print(n);
            Serial.print(" ("); Serial.print(ent.fileSize()); Serial.println(" bytes)");
            fileCount++;
          }
          ent.close();
        }
        dir.close();
        if (fileCount == 0) {
          Serial.println("  No WAV files found");
        }
      } else {
        Serial.println("✗ Failed to open SD card root directory");
        current_led_status = LED_ERROR;
      }
    }
    Serial.println();
    Serial.flush();
  }
  
  // serial commands
  if (Serial.available()){
    char cmd = Serial.read();
    // Clear any remaining characters in buffer
    while (Serial.available()) Serial.read();
    
    Serial.print("> Command: ");
    Serial.println(cmd);
    Serial.flush();
    
    if (cmd=='r' && !audio_is_recording()){
      Serial.println("Starting recording...");
      if (audio_start_recording()){
        Serial.print("✓ Recording started: "); 
        Serial.println(audio_get_last_filename());
      } else {
        Serial.println("✗ Recording start failed");
      }
    } else if (cmd=='s' && audio_is_recording()){
      Serial.println("Stopping recording...");
      audio_stop_recording();
      Serial.print("✓ Recording stopped: "); 
      Serial.print(audio_get_last_filename());
      Serial.print(" ("); Serial.print(audio_get_bytes_recorded()); Serial.print(" bytes, ");
      Serial.print(audio_get_buffer_overruns()); Serial.println(" overruns)");
      
      // Auto-start BLE transfer after stopping recording
      const char* path = audio_get_last_filename();
      if (*path) {
        if (txFile.isOpen()) txFile.close();
        
        SdFs* sd = audio_get_sd_instance();
        if (txFile.open(sd, path, O_READ)){
          Serial.print("✓ Auto-starting BLE transfer: "); 
          Serial.print(path); Serial.print(" (");
          Serial.print(txFile.fileSize()); Serial.println(" bytes)");
          ble_set_transfer_file(&txFile, (uint32_t)txFile.fileSize(), path);
          ble_start_advertising();
          Serial.println("  Waiting for BLE connection...");
        } else {
          Serial.println("✗ Failed to open file for transfer");
        }
      } else {
        Serial.println("✗ No file to transfer");
      }
    } else if (cmd=='c' && !audio_is_recording()){
      Serial.println("Starting BLE transfer...");
      const char* path = audio_get_last_filename();
      if (!*path) { 
        Serial.println("✗ No file to transfer"); 
      } else {
        if (txFile.isOpen()) txFile.close();
        
        // Use the SD instance from audio module
        SdFs* sd = audio_get_sd_instance();
        if (txFile.open(sd, path, O_READ)){
          Serial.print("✓ BLE advertising: "); 
          Serial.print(path); Serial.print(" (");
          Serial.print(txFile.fileSize()); Serial.println(" bytes)");
          ble_set_transfer_file(&txFile, (uint32_t)txFile.fileSize(), path);
          ble_start_advertising();
          Serial.println("  Waiting for BLE connection...");
        } else {
          Serial.println("✗ Failed to open file");
        }
      }
    } else if (cmd=='l'){
      Serial.println("Files on SD card:");
      SdFs* sd = audio_get_sd_instance();
      FsFile dir, ent;
      if (dir.open(sd, "/", O_RDONLY)) {
        int fileCount = 0;
        while (ent.openNext(&dir, O_RDONLY)){
          char n[64]; ent.getName(n, sizeof(n));
          if (strstr(n, ".WAV") || strstr(n, ".wav")){
            Serial.print("  "); Serial.print(n);
            Serial.print(" ("); Serial.print(ent.fileSize()); Serial.println(" bytes)");
            fileCount++;
          }
          ent.close();
        }
        dir.close();
        if (fileCount == 0) {
          Serial.println("  No WAV files found");
        }
      } else {
        Serial.println("✗ Failed to open SD card root directory");
      }
    } else if (cmd == 'd') {
      Serial.println("Deleting all WAV files...");
      SdFs* sd = audio_get_sd_instance();
      FsFile dir, ent;
      if (dir.open(sd, "/", O_RDONLY)) {
        int deleteCount = 0;
        while (ent.openNext(&dir, O_RDONLY)){
          char n[64]; 
          ent.getName(n, sizeof(n));
          if (strstr(n, ".WAV") || strstr(n, ".wav")){
            ent.close();
            if (sd->remove(n)) {
              Serial.print("  Deleted ");
              Serial.println(n);
              deleteCount++;
            } else {
              Serial.print("  Failed to delete ");
              Serial.println(n);
            }
          } else {
            ent.close();
          }
        }
        dir.close();
        Serial.print("Deleted ");
        Serial.print(deleteCount);
        Serial.println(" files");
      } else {
        Serial.println("✗ Failed to open SD card root directory");
      }
    } else if (cmd == 'r' && audio_is_recording()) {
      Serial.println("✗ Already recording! Use 's' to stop first.");
    } else if (cmd == 's' && !audio_is_recording()) {
      Serial.println("✗ Not currently recording! Use 'r' to start.");
    } else if (cmd == 'c' && audio_is_recording()) {
      Serial.println("✗ Stop recording first! Use 's' to stop.");
    } else {
      Serial.println("✗ Unknown command or invalid state");
      Serial.println("Available commands: r=record, s=stop, c=BLE transfer, l=list files, d=delete all");
    }
    Serial.println();
    Serial.flush();
  }

  // Process audio data and BLE operations
  audio_process();
  ble_process_transfer();
}