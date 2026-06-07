#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>

// ----------------------------- WiFi Credentials ----------------------------------------
#define WIFI_SSID   "dlink- TT"
#define WIFI_PASS   "70528424++@"

// ----------------------------- Pin Definitions ----------------------------------------
#define RXD2         16
#define TXD2         17
#define SD_CS         5    // SD card CS (SPI: MOSI=23, MISO=19, SCK=18)

#define MEM_WRITE_CHUNK  128

// ----------------------------- Flash HAL Statuses ------------------------------------
#define FLASH_HAL_OK         0x00
#define FLASH_HAL_ERROR      0x01
#define FLASH_HAL_BUSY       0x02
#define FLASH_HAL_TIMEOUT    0x03
#define FLASH_HAL_INV_ADDR   0x04

// ----------------------------- BL Commands -------------------------------------------
#define COMMAND_BL_GET_VER           0x51
#define COMMAND_BL_GET_HELP          0x52
#define COMMAND_BL_GET_CID           0x53
#define COMMAND_BL_GET_RDP_STATUS    0x54
#define COMMAND_BL_GO_TO_ADDR        0x55
#define COMMAND_BL_FLASH_ERASE       0x56
#define COMMAND_BL_MEM_WRITE         0x57

// ----------------------------- Command Lengths ----------------------------------------
#define COMMAND_BL_GET_VER_LEN           6
#define COMMAND_BL_GET_HELP_LEN          6
#define COMMAND_BL_GET_CID_LEN           6
#define COMMAND_BL_GET_RDP_STATUS_LEN    6
#define COMMAND_BL_GO_TO_ADDR_LEN        10
#define COMMAND_BL_FLASH_ERASE_LEN       8
#define COMMAND_BL_MEM_WRITE_LEN         11

// ----------------------------- Web Server & SSE ---------------------------------------
WebServer server(80);

// SSE client tracking
WiFiClient sseClient;
bool       sseConnected = false;

// busy flag — prevent two commands running at once
volatile bool bl_busy = false;

// ----------------------------- Terminal Log -------------------------------------------
// All output goes through here: printed to Serial AND streamed to browser via SSE
void terminalLog(String msg) {
    Serial.println(msg);
    if (sseConnected && sseClient.connected()) {
        // Escape newlines for SSE data field
        msg.replace("\n", "<br>");
        msg.replace("\r", "");
        sseClient.print("data: " + msg + "\n\n");
    }
}

// ----------------------------- Utilities ----------------------------------------------
uint8_t word_to_byte(uint32_t addr, int index) {
    return (addr >> (8 * (index - 1))) & 0x000000FF;
}

uint32_t get_crc(uint8_t *buff, uint32_t length) {
    uint32_t Crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < length; i++) {
        Crc ^= buff[i];
        for (int j = 0; j < 32; j++) {
            Crc = (Crc & 0x80000000) ? (Crc << 1) ^ 0x04C11DB7 : (Crc << 1);
        }
    }
    return Crc;
}

void purge_serial_port() {
    while (Serial2.available()) Serial2.read();
}

void Write_to_serial_port(uint8_t value) {
    char buf[8];
    snprintf(buf, sizeof(buf), " 0x%02X", value);
    terminalLog(String(buf));
    Serial2.write(value);
}

// ----------------------------- Command Response Processors ----------------------------
void process_COMMAND_BL_GET_VER(int length) {
    unsigned long t = millis();
    while (Serial2.available() < length) { if (millis()-t>5000) return; delay(5); }
    uint8_t ver = Serial2.read();
    char buf[40]; snprintf(buf, sizeof(buf), "Bootloader Ver. : 0x%02X", ver);
    terminalLog(String(buf));
}

void process_COMMAND_BL_GET_HELP(int length) {
    unsigned long t = millis();
    while (Serial2.available() < length) { if (millis()-t>5000) return; delay(5); }
    String out = "Supported Commands :";
    for (int i = 0; i < length; i++) {
        char b[8]; snprintf(b, sizeof(b), " 0x%02X", Serial2.read());
        out += b;
    }
    terminalLog(out);
}

void process_COMMAND_BL_GET_CID(int length) {
    unsigned long t = millis();
    while (Serial2.available() < length) { if (millis()-t>5000) return; delay(5); }
    uint8_t b0 = Serial2.read(), b1 = Serial2.read();
    uint16_t cid = ((uint16_t)b1 << 8) | b0;
    char buf[30]; snprintf(buf, sizeof(buf), "Chip Id. : 0x%04X", cid);
    terminalLog(String(buf));
}

void process_COMMAND_BL_GET_RDP_STATUS(int length) {
    unsigned long t = millis();
    while (Serial2.available() < length) { if (millis()-t>5000) return; delay(5); }
    uint8_t rdp = Serial2.read();
    char buf[30]; snprintf(buf, sizeof(buf), "RDP Status : 0x%02X", rdp);
    terminalLog(String(buf));
}

void process_COMMAND_BL_GO_TO_ADDR(int length) {
    unsigned long t = millis();
    while (Serial2.available() < length) { if (millis()-t>5000) return; delay(5); }
    uint8_t s = Serial2.read();
    char buf[30]; snprintf(buf, sizeof(buf), "Address Status : 0x%02X", s);
    terminalLog(String(buf));
}

void process_COMMAND_BL_FLASH_ERASE(int length) {
    unsigned long t = millis();
    while (Serial2.available() < length) { if (millis()-t>8000) return; delay(5); }
    uint8_t s = Serial2.read();
    switch (s) {
        case FLASH_HAL_OK:       terminalLog("Erase Status: FLASH_HAL_OK");      break;
        case FLASH_HAL_ERROR:    terminalLog("Erase Status: FLASH_HAL_ERROR");   break;
        case FLASH_HAL_BUSY:     terminalLog("Erase Status: FLASH_HAL_BUSY");    break;
        case FLASH_HAL_TIMEOUT:  terminalLog("Erase Status: FLASH_HAL_TIMEOUT"); break;
        case FLASH_HAL_INV_ADDR: terminalLog("Erase Status: FLASH_HAL_INV_SECTOR"); break;
        default: { char b[40]; snprintf(b,sizeof(b),"Erase Status: UNKNOWN 0x%02X",s); terminalLog(b); }
    }
}

void process_COMMAND_BL_MEM_WRITE(int length) {
    unsigned long t = millis();
    while (Serial2.available() < length) {
        if (millis()-t > 10000) { terminalLog("MEM_WRITE Timeout: no status byte received"); return; }
        delay(5);
    }
    uint8_t s = Serial2.read();
    switch (s) {
        case FLASH_HAL_OK:       terminalLog("Write Status: FLASH_HAL_OK");      break;
        case FLASH_HAL_ERROR:    terminalLog("Write Status: FLASH_HAL_ERROR");   break;
        case FLASH_HAL_BUSY:     terminalLog("Write Status: FLASH_HAL_BUSY");    break;
        case FLASH_HAL_TIMEOUT:  terminalLog("Write Status: FLASH_HAL_TIMEOUT"); break;
        case FLASH_HAL_INV_ADDR: terminalLog("Write Status: FLASH_HAL_INV_ADDR"); break;
        default: { char b[40]; snprintf(b,sizeof(b),"Write Status: UNKNOWN 0x%02X",s); terminalLog(b); }
    }
}

// ----------------------------- Bootloader Reply Reader --------------------------------
int read_bootloader_reply(uint8_t command_code) {
    unsigned long timeout = millis();
    while (Serial2.available() < 2) {
        if (millis() - timeout > 5000) { terminalLog("Timeout: Bootloader not responding"); return -2; }
        delay(5);
    }
    uint8_t ack0 = Serial2.read(), ack1 = Serial2.read();

    if (ack0 == 0xA5) {
        char buf[40]; snprintf(buf, sizeof(buf), "CRC: SUCCESS  Len: %d", ack1);
        terminalLog(String(buf));
        switch (command_code) {
            case COMMAND_BL_GET_VER:        process_COMMAND_BL_GET_VER(ack1);        break;
            case COMMAND_BL_GET_HELP:       process_COMMAND_BL_GET_HELP(ack1);       break;
            case COMMAND_BL_GET_CID:        process_COMMAND_BL_GET_CID(ack1);        break;
            case COMMAND_BL_GET_RDP_STATUS: process_COMMAND_BL_GET_RDP_STATUS(ack1); break;
            case COMMAND_BL_GO_TO_ADDR:     process_COMMAND_BL_GO_TO_ADDR(ack1);     break;
            case COMMAND_BL_FLASH_ERASE:    process_COMMAND_BL_FLASH_ERASE(ack1);    break;
            case COMMAND_BL_MEM_WRITE:      process_COMMAND_BL_MEM_WRITE(ack1);      break;
            default: terminalLog("Response received (no processor for this command)");
                     while (Serial2.available()) Serial2.read(); break;
        }
        return 0;
    } else if (ack0 == 0x7F) {
        terminalLog("CRC: FAIL");
        return -1;
    } else {
        char buf[40]; snprintf(buf, sizeof(buf), "Unknown ACK byte: 0x%02X", ack0);
        terminalLog(String(buf));
        return -1;
    }
}

// ----------------------------- Command Executor ---------------------------------------
// Called from web handlers (params already parsed) or directly
// param1 / param2 meaning depends on command:
//   cmd 5 (GO_TO_ADDR)  : param1 = hex address string
//   cmd 7 (FLASH_ERASE) : param1 = sector hex string, param2 = nsec decimal string
//   cmd 8 (MEM_WRITE)   : param1 = hex base address string
void execute_command(int command, String param1 = "", String param2 = "") {
    uint8_t  data_buf[255] = {0};
    uint32_t crc32 = 0;

    terminalLog("----------------------------------------");

    if (command == 1) {
        terminalLog("Command ==> BL_GET_VER");
        data_buf[0] = COMMAND_BL_GET_VER_LEN - 1;
        data_buf[1] = COMMAND_BL_GET_VER;
        crc32 = get_crc(data_buf, COMMAND_BL_GET_VER_LEN - 4);
        data_buf[2] = word_to_byte(crc32,1); data_buf[3] = word_to_byte(crc32,2);
        data_buf[4] = word_to_byte(crc32,3); data_buf[5] = word_to_byte(crc32,4);
        for (int i = 0; i < COMMAND_BL_GET_VER_LEN; i++) { Serial2.write(data_buf[i]); }
        read_bootloader_reply(data_buf[1]);
    }
    else if (command == 2) {
        terminalLog("Command ==> BL_GET_HELP");
        data_buf[0] = COMMAND_BL_GET_HELP_LEN - 1;
        data_buf[1] = COMMAND_BL_GET_HELP;
        crc32 = get_crc(data_buf, COMMAND_BL_GET_HELP_LEN - 4);
        data_buf[2] = word_to_byte(crc32,1); data_buf[3] = word_to_byte(crc32,2);
        data_buf[4] = word_to_byte(crc32,3); data_buf[5] = word_to_byte(crc32,4);
        for (int i = 0; i < COMMAND_BL_GET_HELP_LEN; i++) { Serial2.write(data_buf[i]); }
        read_bootloader_reply(data_buf[1]);
    }
    else if (command == 3) {
        terminalLog("Command ==> BL_GET_CID");
        data_buf[0] = COMMAND_BL_GET_CID_LEN - 1;
        data_buf[1] = COMMAND_BL_GET_CID;
        crc32 = get_crc(data_buf, COMMAND_BL_GET_CID_LEN - 4);
        data_buf[2] = word_to_byte(crc32,1); data_buf[3] = word_to_byte(crc32,2);
        data_buf[4] = word_to_byte(crc32,3); data_buf[5] = word_to_byte(crc32,4);
        for (int i = 0; i < COMMAND_BL_GET_CID_LEN; i++) { Serial2.write(data_buf[i]); }
        read_bootloader_reply(data_buf[1]);
    }
    else if (command == 4) {
        terminalLog("Command ==> BL_GET_RDP_STATUS");
        data_buf[0] = COMMAND_BL_GET_RDP_STATUS_LEN - 1;
        data_buf[1] = COMMAND_BL_GET_RDP_STATUS;
        crc32 = get_crc(data_buf, COMMAND_BL_GET_RDP_STATUS_LEN - 4);
        data_buf[2] = word_to_byte(crc32,1); data_buf[3] = word_to_byte(crc32,2);
        data_buf[4] = word_to_byte(crc32,3); data_buf[5] = word_to_byte(crc32,4);
        for (int i = 0; i < COMMAND_BL_GET_RDP_STATUS_LEN; i++) { Serial2.write(data_buf[i]); }
        read_bootloader_reply(data_buf[1]);
    }
    else if (command == 5) {
        terminalLog("Command ==> BL_GO_TO_ADDR");
        if (param1.length() == 0) { terminalLog("[ERROR] Address parameter missing"); return; }
        uint32_t go_address = strtoul(param1.c_str(), NULL, 16);
        char buf[40]; snprintf(buf, sizeof(buf), "Go address: 0x%08X", go_address);
        terminalLog(String(buf));
        data_buf[0] = COMMAND_BL_GO_TO_ADDR_LEN - 1;
        data_buf[1] = COMMAND_BL_GO_TO_ADDR;
        data_buf[2] = word_to_byte(go_address,1); data_buf[3] = word_to_byte(go_address,2);
        data_buf[4] = word_to_byte(go_address,3); data_buf[5] = word_to_byte(go_address,4);
        crc32 = get_crc(data_buf, COMMAND_BL_GO_TO_ADDR_LEN - 4);
        data_buf[6] = word_to_byte(crc32,1); data_buf[7] = word_to_byte(crc32,2);
        data_buf[8] = word_to_byte(crc32,3); data_buf[9] = word_to_byte(crc32,4);
        for (int i = 0; i < COMMAND_BL_GO_TO_ADDR_LEN; i++) { Serial2.write(data_buf[i]); }
        read_bootloader_reply(data_buf[1]);
    }
    else if (command == 6) {
        terminalLog("Command ==> BL_FLASH_MASS_ERASE");
        data_buf[0] = COMMAND_BL_FLASH_ERASE_LEN - 1;
        data_buf[1] = COMMAND_BL_FLASH_ERASE;
        data_buf[2] = 0xFF;
        data_buf[3] = 0x00;
        crc32 = get_crc(data_buf, COMMAND_BL_FLASH_ERASE_LEN - 4);
        data_buf[4] = word_to_byte(crc32,1); data_buf[5] = word_to_byte(crc32,2);
        data_buf[6] = word_to_byte(crc32,3); data_buf[7] = word_to_byte(crc32,4);
        for (int i = 0; i < COMMAND_BL_FLASH_ERASE_LEN; i++) { Serial2.write(data_buf[i]); }
        read_bootloader_reply(data_buf[1]);
    }
    else if (command == 7) {
        terminalLog("Command ==> BL_FLASH_ERASE");
        if (param1.length() == 0) { terminalLog("[ERROR] Sector parameter missing"); return; }
        uint8_t sector_num = (uint8_t)strtoul(param1.c_str(), NULL, 16);
        uint8_t nsec       = (sector_num == 0xFF) ? 0 : (uint8_t)param2.toInt();
        char buf[60];
        snprintf(buf, sizeof(buf), "Sector: 0x%02X  Count: %d", sector_num, nsec);
        terminalLog(String(buf));
        data_buf[0] = COMMAND_BL_FLASH_ERASE_LEN - 1;
        data_buf[1] = COMMAND_BL_FLASH_ERASE;
        data_buf[2] = sector_num;
        data_buf[3] = nsec;
        crc32 = get_crc(data_buf, COMMAND_BL_FLASH_ERASE_LEN - 4);
        data_buf[4] = word_to_byte(crc32,1); data_buf[5] = word_to_byte(crc32,2);
        data_buf[6] = word_to_byte(crc32,3); data_buf[7] = word_to_byte(crc32,4);
        for (int i = 0; i < COMMAND_BL_FLASH_ERASE_LEN; i++) { Serial2.write(data_buf[i]); }
        read_bootloader_reply(data_buf[1]);
    }
    else if (command == 8) {
        terminalLog("Command ==> BL_MEM_WRITE (SD card)");
        if (param1.length() == 0) { terminalLog("[ERROR] Base address parameter missing"); return; }

        if (!SD.begin(SD_CS)) { terminalLog("[ERROR] SD card mount failed"); return; }
        terminalLog("SD card mounted OK");

        File bin_file = SD.open("/user_app.bin", FILE_READ);
        if (!bin_file) { terminalLog("[ERROR] Cannot open /user_app.bin"); SD.end(); return; }

        uint32_t t_len_of_file = bin_file.size();
        if (t_len_of_file == 0) { terminalLog("[ERROR] user_app.bin is empty"); bin_file.close(); SD.end(); return; }

        char buf[60]; snprintf(buf, sizeof(buf), "File size: %lu bytes", t_len_of_file);
        terminalLog(String(buf));

        uint32_t base_mem_address = strtoul(param1.c_str(), NULL, 16);
        snprintf(buf, sizeof(buf), "Writing to: 0x%08X", base_mem_address);
        terminalLog(String(buf));

        uint32_t bytes_remaining   = t_len_of_file;
        uint32_t bytes_so_far_sent = 0;
        int      ret_value         = 0;

        while (bytes_remaining > 0) {
            uint8_t  chunk_buf[255] = {0};
            uint32_t len_to_read    = (bytes_remaining >= MEM_WRITE_CHUNK) ? MEM_WRITE_CHUNK : bytes_remaining;
            uint32_t actually_read  = bin_file.read(&chunk_buf[7], len_to_read);
            if (actually_read == 0) { terminalLog("[ERROR] Unexpected end of file"); break; }

            uint32_t total_len = COMMAND_BL_MEM_WRITE_LEN + actually_read;
            chunk_buf[0] = (uint8_t)(total_len - 1);
            chunk_buf[1] = COMMAND_BL_MEM_WRITE;
            chunk_buf[2] = word_to_byte(base_mem_address,1);
            chunk_buf[3] = word_to_byte(base_mem_address,2);
            chunk_buf[4] = word_to_byte(base_mem_address,3);
            chunk_buf[5] = word_to_byte(base_mem_address,4);
            chunk_buf[6] = (uint8_t)actually_read;

            crc32 = get_crc(chunk_buf, total_len - 4);
            chunk_buf[7+actually_read]  = word_to_byte(crc32,1);
            chunk_buf[8+actually_read]  = word_to_byte(crc32,2);
            chunk_buf[9+actually_read]  = word_to_byte(crc32,3);
            chunk_buf[10+actually_read] = word_to_byte(crc32,4);

            for (uint32_t b = 0; b < total_len; b++) Serial2.write(chunk_buf[b]);

            base_mem_address  += actually_read;
            bytes_so_far_sent += actually_read;
            bytes_remaining    = t_len_of_file - bytes_so_far_sent;

            ret_value = read_bootloader_reply(COMMAND_BL_MEM_WRITE);
            if (ret_value != 0) {
                snprintf(buf, sizeof(buf), "[ERROR] Chunk failed at offset %lu. Aborting.", bytes_so_far_sent - actually_read);
                terminalLog(String(buf)); break;
            }
            snprintf(buf, sizeof(buf), "Progress: %lu / %lu bytes", bytes_so_far_sent, t_len_of_file);
            terminalLog(String(buf));

            // Yield to web server between chunks so SSE stream stays alive
            server.handleClient();
        }

        bin_file.close();
        SD.end();
        if (bytes_remaining == 0) terminalLog("BL_MEM_WRITE complete. All bytes written successfully.");
    }
    else {
        terminalLog("[ERROR] Unknown command: " + String(command));
    }

    terminalLog("----------------------------------------");
    bl_busy = false;
}

// ----------------------------- HTML Page ----------------------------------------------
const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>STM32 Bootloader</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:monospace;background:#1a1a2e;color:#e0e0e0;min-height:100vh;padding:20px}
  h1{color:#00d4ff;text-align:center;margin-bottom:6px;font-size:1.4rem;letter-spacing:2px}
  .subtitle{text-align:center;color:#888;font-size:0.8rem;margin-bottom:24px}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;margin-bottom:24px}
  .card{background:#16213e;border:1px solid #0f3460;border-radius:8px;padding:14px}
  .card h3{color:#00d4ff;font-size:0.8rem;margin-bottom:10px;text-transform:uppercase;letter-spacing:1px}
  button{width:100%;padding:10px;border:none;border-radius:6px;background:#0f3460;color:#e0e0e0;
         font-family:monospace;font-size:0.85rem;cursor:pointer;transition:background 0.2s}
  button:hover{background:#00d4ff;color:#1a1a2e}
  button:disabled{background:#333;color:#666;cursor:not-allowed}
  input[type=text]{width:100%;padding:7px;background:#0a0a1a;border:1px solid #0f3460;
                   border-radius:4px;color:#e0e0e0;font-family:monospace;font-size:0.8rem;margin-bottom:8px}
  input[type=text]:focus{outline:none;border-color:#00d4ff}
  label{font-size:0.75rem;color:#888;display:block;margin-bottom:3px}
  #terminal{background:#0a0a1a;border:1px solid #0f3460;border-radius:8px;padding:14px;
            height:320px;overflow-y:auto;font-size:0.8rem;line-height:1.6}
  #terminal .line{color:#00ff88;border-bottom:1px solid #111;padding:2px 0}
  #terminal .line.err{color:#ff4444}
  #terminal .line.info{color:#00d4ff}
  .status{text-align:right;font-size:0.75rem;color:#888;margin-bottom:6px}
  .busy{color:#ffaa00}
  .idle{color:#00ff88}
  .clear-btn{float:right;padding:4px 10px;font-size:0.75rem;background:#333;width:auto;margin-top:-2px}
</style>
</head><body>
<h1>STM32F4 Bootloader</h1>
<div class="subtitle">ESP32 Wireless Host &nbsp;|&nbsp; SD card: user_app.bin</div>

<div class="grid">

  <!-- Info Commands -->
  <div class="card">
    <h3>Read Info</h3>
    <button onclick="send(1)">Get Version</button><br><br>
    <button onclick="send(2)">Get Help</button><br><br>
    <button onclick="send(3)">Get Chip ID</button><br><br>
    <button onclick="send(4)">Get RDP Status</button>
  </div>

  <!-- Go To Address -->
  <div class="card">
    <h3>Jump</h3>
    <label>Go-to Address (hex)</label>
    <input type="text" id="go_addr" placeholder="e.g. 08008000">
    <button onclick="send(5)">BL_GO_TO_ADDR</button>
  </div>

  <!-- Flash Erase -->
  <div class="card">
    <h3>Flash Erase</h3>
    <button onclick="send(6)">Mass Erase (all sectors)</button><br><br>
    <label>Sector (0–7 or FF for mass)</label>
    <input type="text" id="sector_num" placeholder="e.g. 2">
    <label>Number of sectors</label>
    <input type="text" id="sector_cnt" placeholder="e.g. 6">
    <button onclick="send(7)">Sector Erase</button>
  </div>

  <!-- Mem Write -->
  <div class="card">
    <h3>Flash Write</h3>
    <label>Base Address (hex)</label>
    <input type="text" id="mem_addr" placeholder="e.g. 08008000">
    <button onclick="send(8)">Write user_app.bin</button>
  </div>

</div>

<div class="status">Status: <span id="st" class="idle">Idle</span>
  <button class="clear-btn" onclick="clearTerm()">Clear</button>
</div>
<div id="terminal"></div>

<script>
var es = null;

function connectSSE(){
  es = new EventSource('/events');
  es.onmessage = function(e){
    var t = document.getElementById('terminal');
    var d = document.createElement('div');
    d.className = 'line' + (e.data.indexOf('[ERROR]')>=0?' err':(e.data.indexOf('CRC')>=0||e.data.indexOf('Ver')>=0||e.data.indexOf('Chip')>=0?' info':''));
    d.innerHTML = e.data;
    t.appendChild(d);
    t.scrollTop = t.scrollHeight;
    if(e.data.indexOf('---')>=0){ document.getElementById('st').textContent='Idle'; document.getElementById('st').className='idle'; setButtons(false); }
  };
  es.onerror = function(){ setTimeout(connectSSE, 2000); };
}
connectSSE();

function setButtons(disabled){
  document.querySelectorAll('button:not(.clear-btn)').forEach(function(b){ b.disabled=disabled; });
}

function send(cmd){
  var p1='', p2='';
  if(cmd==5){ p1=document.getElementById('go_addr').value.trim(); if(!p1){alert('Enter a go-to address');return;} }
  if(cmd==7){ p1=document.getElementById('sector_num').value.trim(); p2=document.getElementById('sector_cnt').value.trim(); if(!p1){alert('Enter sector number');return;} }
  if(cmd==8){ p1=document.getElementById('mem_addr').value.trim(); if(!p1){alert('Enter base address');return;} }
  document.getElementById('st').textContent='Busy...';
  document.getElementById('st').className='busy';
  setButtons(true);
  fetch('/cmd?c='+cmd+'&p1='+encodeURIComponent(p1)+'&p2='+encodeURIComponent(p2));
}

function clearTerm(){ document.getElementById('terminal').innerHTML=''; }
</script>
</body></html>
)rawhtml";

// ----------------------------- Web Handlers -------------------------------------------
void handleRoot() {
    server.send_P(200, "text/html", HTML_PAGE);
}

void handleCommand() {
    if (bl_busy) { server.send(200, "text/plain", "busy"); return; }
    int    cmd  = server.arg("c").toInt();
    String p1   = server.arg("p1");
    String p2   = server.arg("p2");
    server.send(200, "text/plain", "ok");
    bl_busy = true;
    execute_command(cmd, p1, p2);
}

void handleSSE() {
    sseClient    = server.client();
    sseConnected = true;
    sseClient.print("HTTP/1.1 200 OK\r\n");
    sseClient.print("Content-Type: text/event-stream\r\n");
    sseClient.print("Cache-Control: no-cache\r\n");
    sseClient.print("Connection: keep-alive\r\n");
    sseClient.print("Access-Control-Allow-Origin: *\r\n\r\n");
    sseClient.flush();
    // Send a hello so the browser knows the stream is live
    sseClient.print("data: Connected to ESP32 Bootloader Host\n\n");
    sseClient.flush();
}

// ----------------------------- Setup & Loop -------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
    delay(500);

    Serial.println("\n=== STM32 Bootloader Host ===");

    // Connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println();
    Serial.print("Connected! IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("Open the above IP in your browser.");

    // Register routes
    server.on("/",       handleRoot);
    server.on("/cmd",    handleCommand);
    server.on("/events", handleSSE);
    server.begin();
    Serial.println("Web server started.");
}

void loop() {
    server.handleClient();
}