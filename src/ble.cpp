#include "config.h"
#include "ble.h"
#include <ArduinoBLE.h>

// File transfer service (existing)
static BLEService svc(BLE_SERVICE_UUID);
static BLECharacteristic txData(BLE_TX_DATA_UUID, BLENotify, 244);
static BLECharacteristic rxCred(BLE_RX_CREDITS_UUID, BLEWriteWithoutResponse, 1);
static BLECharacteristic fileInfo(BLE_FILE_INFO_UUID, BLERead, 64);

// Live audio streaming service (new)
static BLEService audioSvc(BLE_AUDIO_SERVICE_UUID);
static BLECharacteristic audioStream(BLE_AUDIO_STREAM_UUID, BLENotify, 244);
static BLECharacteristic audioControl(BLE_AUDIO_CONTROL_UUID, BLEWriteWithoutResponse, 1);
static BLECharacteristic audioStatus(BLE_AUDIO_STATUS_UUID, BLERead, 16);

// Transfer state
static volatile int g_credits = 0;
static uint32_t g_seq = 0;
static uint32_t g_file_pos = 0;  // Track position for resume
static uint32_t g_bytes_sent = 0;  // Total bytes sent this session

static FsFile* g_file = nullptr;
static uint32_t g_fileSize = 0;
static char g_fname[20] = {0};

// Performance tracking
static uint32_t g_last_report_ms = 0;
static uint32_t g_last_report_bytes = 0;

// Buffer management - smaller buffer to reduce memory pressure
static uint8_t io_buffer[2048];  // Reduced from 4096
static size_t buffer_pos = 0;
static size_t buffer_size = 0;

// Live streaming state
static volatile bool g_streaming_connected = false;
static volatile bool g_streaming_active = false;
static uint32_t g_stream_seq = 0;
static uint32_t g_stream_chunks_sent = 0;

static uint16_t crc16_ccitt(const uint8_t* p, size_t n){
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++){ 
    c ^= (uint16_t)p[i] << 8;
    for (int b = 0; b < 8; b++) {
      c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
  } 
  return c;
}

static void onCred(BLEDevice, BLECharacteristic chr){
  uint8_t v = 0; 
  chr.readValue(v);
  g_credits += (int)v;
  if (g_credits > 64) g_credits = 64; // clamp to prevent overflow
}

static void onAudioControl(BLEDevice device, BLECharacteristic chr) {
  (void)device;
  uint8_t command = 0;
  chr.readValue(command);
  
  switch (command) {
    case 1: // Start streaming
      if (!g_streaming_active) {
        g_streaming_active = true;
        g_stream_seq = 0;
        g_stream_chunks_sent = 0;
        Serial.println("BLE Audio: Streaming started by client");
      }
      break;
      
    case 0: // Stop streaming
      if (g_streaming_active) {
        g_streaming_active = false;
        Serial.println("BLE Audio: Streaming stopped by client");
      }
      break;
  }
}

static void onConnect(BLEDevice central){
  (void)central; 
  g_seq = 0; 
  g_credits = 0;
  g_bytes_sent = 0;
  g_last_report_ms = millis();
  g_last_report_bytes = 0;
  
  // Reset file position to beginning for fresh connection
  // (Could be enhanced to support resume by having client send last received seq)
  g_file_pos = 0;
  if (g_file && g_file->isOpen()) {
    g_file->seekSet(0);
  }
  
  // Handle streaming connection
  g_streaming_connected = true;
  g_streaming_active = false;  // Wait for explicit start command
  
  Serial.println("BLE: Client connected (file transfer and streaming available)");
}

static void onDisconnect(BLEDevice central){
  (void)central; 
  g_seq = 0; 
  g_credits = 0;
  
  // Report final statistics
  if (g_bytes_sent > 0) {
    Serial.print("BLE: Disconnected after sending ");
    Serial.print(g_bytes_sent);
    Serial.print(" of ");
    Serial.print(g_fileSize);
    Serial.println(" bytes");
    
    if (g_bytes_sent < g_fileSize) {
      Serial.println("BLE: Transfer incomplete - client can reconnect to retry");
    }
  }
  
  // Handle streaming disconnection
  if (g_streaming_connected) {
    g_streaming_connected = false;
    g_streaming_active = false;
    
    if (g_stream_chunks_sent > 0) {
      Serial.print("BLE Audio: Disconnected after streaming ");
      Serial.print(g_stream_chunks_sent);
      Serial.println(" chunks");
    }
  }
}

bool ble_init(){
  if (!BLE.begin()) return false;
  
  // Set connection parameters for better stability with large transfers
  BLE.setConnectionInterval(6, 12);  // 7.5ms - 15ms for good throughput
  BLE.setDeviceName("XIAO-REC");
  BLE.setLocalName("XIAO-REC");
  
  // File transfer service
  svc.addCharacteristic(txData);
  svc.addCharacteristic(rxCred);
  svc.addCharacteristic(fileInfo);
  BLE.addService(svc);

  rxCred.setEventHandler(BLEWritten, onCred);
  BLE.setEventHandler(BLEConnected, onConnect);
  BLE.setEventHandler(BLEDisconnected, onDisconnect);
  
  // Initialize streaming service if enabled
  if (ENABLE_LIVE_STREAMING) {
    // Add streaming service
    audioSvc.addCharacteristic(audioStream);
    audioSvc.addCharacteristic(audioControl);
    audioSvc.addCharacteristic(audioStatus);
    BLE.addService(audioSvc);
    
    // Set up event handlers
    audioControl.setEventHandler(BLEWritten, onAudioControl);
    
    // Initialize status characteristic
    uint8_t status[16] = {0};
    memcpy(status, &SAMPLE_RATE, 4);      // Sample rate
    memcpy(status + 4, &CHANNELS, 2);     // Channels
    memcpy(status + 6, &ADPCM_BITS_PER_SAMPLE, 2);  // Bits per sample
    memcpy(status + 8, &STREAM_CHUNK_SIZE, 4);  // Chunk size
    audioStatus.setValue(status, sizeof(status));
    
    Serial.println("BLE Audio: Streaming service initialized");
  }
  
  return true;
}

void ble_set_transfer_file(FsFile* f, uint32_t size, const char* name){
  g_file = f; 
  g_fileSize = size;
  g_file_pos = 0;
  buffer_pos = 0;
  buffer_size = 0;
  
  strncpy(g_fname, name, sizeof(g_fname) - 1);
  
  // Prepare FILE_INFO payload: [u32 size][name (null-terminated)]
  uint8_t info[64] = {0};
  memcpy(info, &g_fileSize, 4);
  strncpy((char*)info + 4, g_fname, sizeof(info) - 5);
  fileInfo.setValue(info, sizeof(info));
  
  Serial.print("BLE: Ready to transfer ");
  Serial.print(size);
  Serial.println(" bytes");
}

void ble_start_advertising(){
  BLE.advertise();
  Serial.println("BLE: Advertising started");
}

void ble_stop_advertising(){
  BLE.stopAdvertise();
  if (BLE.connected()) {
    BLE.disconnect();
  }
}

bool ble_is_connected(){ 
  return BLE.connected(); 
}

// Send packet format: [seq32|len16|crc16|payload<=236]
void ble_process_transfer(){
  if (!BLE.connected() || g_file == nullptr || !g_file->isOpen()) { 
    BLE.poll(); 
    return; 
  }

  // Refill buffer if needed
  if (buffer_size == 0 && g_file_pos < g_fileSize) {
    // Seek to current position (in case of reconnection)
    g_file->seekSet(g_file_pos);
    
    size_t to_read = min(sizeof(io_buffer), (size_t)(g_fileSize - g_file_pos));
    buffer_size = g_file->read(io_buffer, to_read);
    buffer_pos = 0;
    
    if (buffer_size == 0 && g_file_pos < g_fileSize) {
      Serial.println("BLE: Read error!");
      return;
    }
  }

  // Send chunks while we have credits and data
  while (g_credits > 0 && buffer_pos < buffer_size) {
    uint16_t chunk_size = (uint16_t)min((size_t)236, buffer_size - buffer_pos);
    uint8_t pkt[244];
    
    // Build packet
    memcpy(pkt + 0, &g_seq, 4);
    memcpy(pkt + 4, &chunk_size, 2);
    uint16_t crc = crc16_ccitt(io_buffer + buffer_pos, chunk_size);
    memcpy(pkt + 6, &crc, 2);
    memcpy(pkt + 8, io_buffer + buffer_pos, chunk_size);
    
    if (txData.setValue(pkt, 8 + chunk_size)) {
      buffer_pos += chunk_size;
      g_file_pos += chunk_size;
      g_bytes_sent += chunk_size;
      g_seq++;
      g_credits--;
      
      // Progress reporting every second
      uint32_t now = millis();
      if (now - g_last_report_ms >= 1000) {
        uint32_t bytes_per_sec = (g_bytes_sent - g_last_report_bytes) * 1000 / (now - g_last_report_ms);
        uint32_t percent = (uint32_t)((uint64_t)g_file_pos * 100 / g_fileSize);
        
        Serial.print("BLE: ");
        Serial.print(percent);
        Serial.print("% (");
        Serial.print(g_file_pos);
        Serial.print("/");
        Serial.print(g_fileSize);
        Serial.print(" bytes) at ");
        Serial.print(bytes_per_sec);
        Serial.println(" B/s");
        
        // Estimate time remaining
        if (bytes_per_sec > 0) {
          uint32_t remaining_bytes = g_fileSize - g_file_pos;
          uint32_t eta_seconds = remaining_bytes / bytes_per_sec;
          Serial.print("BLE: ETA ");
          Serial.print(eta_seconds / 60);
          Serial.print("m ");
          Serial.print(eta_seconds % 60);
          Serial.println("s");
        }
        
        g_last_report_ms = now;
        g_last_report_bytes = g_bytes_sent;
      }
    } else {
      break; // BLE stack needs time
    }
    
    BLE.poll();
  }

  // Clear buffer if consumed
  if (buffer_pos >= buffer_size) {
    buffer_size = 0;
  }
  
  // Send EOF packet when done
  if (g_file_pos >= g_fileSize && g_credits > 0 && buffer_size == 0) {
    static bool eof_sent = false;
    if (!eof_sent) {
      uint8_t eof_pkt[8];
      memcpy(eof_pkt + 0, &g_seq, 4);
      uint16_t zero_len = 0;
      memcpy(eof_pkt + 4, &zero_len, 2);
      uint16_t zero_crc = 0;
      memcpy(eof_pkt + 6, &zero_crc, 2);
      
      if (txData.setValue(eof_pkt, 8)) {
        eof_sent = true;
        Serial.println("BLE: Transfer complete!");
      }
    }
  }
  
  BLE.poll();
}

// ========================================
// Live Audio Streaming Implementation
// ========================================

bool ble_streaming_is_connected() {
  return g_streaming_connected;
}

void ble_streaming_send_chunk(const uint8_t* adpcm_data, size_t chunk_size) {
  if (!g_streaming_active || !g_streaming_connected) {
    return;  // Not ready to stream
  }
  
  // Limit chunk size to BLE packet size minus header
  if (chunk_size > 236) {
    chunk_size = 236;
  }
  
  // Build streaming packet: [seq32|timestamp32|chunk_size16|adpcm_data]
  uint8_t packet[244];
  uint32_t timestamp = millis();
  uint16_t size16 = (uint16_t)chunk_size;
  
  memcpy(packet, &g_stream_seq, 4);
  memcpy(packet + 4, &timestamp, 4);
  memcpy(packet + 8, &size16, 2);
  memcpy(packet + 10, adpcm_data, chunk_size);
  
  if (audioStream.setValue(packet, 10 + chunk_size)) {
    g_stream_seq++;
    g_stream_chunks_sent++;
  }
}