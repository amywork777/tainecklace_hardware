#include <Arduino.h>
#include <SdFat.h>
#include "config.h"
#include "audio.h"
#include "ble.h"
#include "button.h"
#include "led.h"

// Application state
static FsFile txFile;

// Pin definitions
#define BTN_MAIN_PIN      D1              // Main button/switch pin
#define LED_PIN           LED_BUILTIN     // Status LED pin

// Initialize hardware modules
void init_hardware() {
  // Initialize button and LED modules
  button_init(BTN_MAIN_PIN);
  led_init(LED_PIN);
}


// Update system LED status based on current state
void update_system_status() {
  static LedStatus last_status = LED_IDLE;
  LedStatus current_status;
  
  // Determine current status
  if (audio_is_recording()) {
    current_status = LED_RECORDING;
  } else if (ble_is_connected()) {
    current_status = LED_TRANSFERRING;
  } else {
    current_status = LED_IDLE;
  }
  
  // Log state changes
  if (current_status != last_status) {
    Serial.print("Device mode: ");
    switch (current_status) {
      case LED_RECORDING:
        Serial.println("RECORDING");
        break;
      case LED_TRANSFERRING:
        Serial.println("BLE_TRANSFER");
        break;
      case LED_IDLE:
        Serial.println("IDLE");
        break;
      case LED_ERROR:
        Serial.println("ERROR");
        break;
    }
    last_status = current_status;
  }
  
  led_set_status(current_status);
  led_update();
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
  Serial.println("Production Mode - USB Powered");
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
  
  Serial.print("Initializing hardware modules... ");
  init_hardware();
  Serial.println("OK");

  Serial.println();
  Serial.println("=====================================");
  Serial.println("INTUITIVE SWITCH MODE");
  Serial.println("=====================================");
  Serial.println("Switch Position = Recording State:");
  Serial.println("  📍 HIGH (up) for >2s = START recording");
  Serial.println("  📍 Keep HIGH = KEEP recording");
  Serial.println("  📍 LOW (down) = STOP recording + auto BLE");
  Serial.println();
  Serial.println("💡 Simple: Switch position controls recording!");
  Serial.println("💡 Power efficient: <1% overhead during recording");
  Serial.println("=====================================");
  Serial.println("Serial commands available: r, s, c, l, d, h, x");
  Serial.println("Ready for intuitive switch control...");
  Serial.flush();
}

void loop(){
  // Update system status indicators
  update_system_status();
  
  // Handle single switch with state-change actions
  ButtonAction action = button_update();
  
  if (action == BTN_START_RECORD) {
    // Switch HIGH for >2s: Try to start recording (once only)
    Serial.println("=====================================");
    Serial.println("EXECUTING NEW RECORDING ACTION");
    Serial.println("=====================================");
    Serial.println("> Starting new recording...");
    if (audio_start_recording()){
      Serial.print("✓ Recording started: "); 
      Serial.println(audio_get_last_filename());
    } else {
      Serial.println("✗ Recording start failed - SD card may be full");
      Serial.println("  Try: Serial command 'd' to delete old files");
      Serial.println("  Or: Serial command 'h' to check hardware");
      button_mark_failed(); // Mark as failed - no more attempts
      led_set_status(LED_ERROR);
    }
    Serial.println("=====================================");
    Serial.println();
    Serial.flush();
  }
  
  else if (action == BTN_STOP_RECORD) {
    // Switch to LOW: Stop recording and start BLE transfer
    Serial.println("=====================================");
    Serial.println("EXECUTING STOP AND TRANSFER ACTION");
    Serial.println("=====================================");
    
    // Stop recording if active
    if (audio_is_recording()) {
      Serial.println("-> STOPPING recording (switch LOW)...");
      audio_stop_recording();
      Serial.print("✓ Recording stopped: "); 
      Serial.print(audio_get_last_filename());
      Serial.print(" ("); Serial.print(audio_get_bytes_recorded()); Serial.print(" bytes, ");
      Serial.print(audio_get_buffer_overruns()); Serial.println(" overruns)");
    }
    
    // Start BLE transfer
    Serial.println("-> Starting BLE transfer...");
    const char* path = audio_get_last_filename();
    if (!*path) { 
      Serial.println("✗ No file to transfer");
      led_set_status(LED_ERROR);
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
        Serial.println("✗ Failed to open file");
        led_set_status(LED_ERROR);
      }
    }
    Serial.println("=====================================");
    Serial.println();
    Serial.flush();
  }
  
  // Serial commands (preserved for debugging/testing)
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
    } else if (cmd == 'h') {
      Serial.println("Running hardware diagnostics...");
      audio_diagnose_hardware();
    } else if (cmd == 'x') {
      Serial.println("Cleaning up failed recording files...");
      SdFs* sd = audio_get_sd_instance();
      FsFile dir, ent;
      if (dir.open(sd, "/", O_RDONLY)) {
        int cleanCount = 0;
        while (ent.openNext(&dir, O_RDONLY)){
          char n[64]; 
          ent.getName(n, sizeof(n));
          if ((strstr(n, ".WAV") || strstr(n, ".wav")) && ent.fileSize() < 1000) {
            // Delete very small files (likely failed recordings)
            ent.close();
            if (sd->remove(n)) {
              Serial.print("  Cleaned up "); Serial.println(n);
              cleanCount++;
            }
          } else {
            ent.close();
          }
        }
        dir.close();
        Serial.print("Cleaned up "); Serial.print(cleanCount); Serial.println(" failed files");
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
      Serial.println("Available commands: r=record, s=stop, c=BLE transfer, l=list files, d=delete all, h=hardware test, x=cleanup");
    }
    Serial.println();
    Serial.flush();
  }

  // Process audio data and BLE operations
  audio_process();
  ble_process_transfer();
}