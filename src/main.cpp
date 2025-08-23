#include <Arduino.h>
#include <SdFat.h>
#include "config.h"
#include "audio.h"
#include "ble.h"

static FsFile txFile;

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

  Serial.println();
  Serial.println("Commands:");
  Serial.println("  r = start recording");
  Serial.println("  s = stop recording");
  Serial.println("  c = connect BLE transfer");
  Serial.println("  l = list files");
  Serial.println("  d = delete all WAV files");
  Serial.println("Ready for commands...");
  Serial.flush();
}

void loop(){
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