/**
 * ESP32 STM32 Bootloader – Serial Bridge (No WiFi)
 * -------------------------------------------------
 * PlatformIO project: place this file in src/main.cpp
 *
 * Open Serial Monitor at 115200 baud.
 * Type a command number (1–8) and press Enter.
 * The ESP32 builds the bootloader packet, sends it to the STM32
 * over UART2, reads the reply, and prints it.
 *
 * Wiring:
 *   ESP32 GPIO17 (TX2) ──► STM32 UART2 RX  (PA3)
 *   ESP32 GPIO16 (RX2) ◄── STM32 UART2 TX  (PA2)
 *   Common GND
 *
 * platformio.ini:
 *   [env:esp32dev]
 *   platform  = espressif32
 *   board     = esp32dev
 *   framework = arduino
 */

#include <Arduino.h>   // ← Required in PlatformIO (Arduino IDE adds it silently)
#include <stdint.h>
#include <stddef.h>    // size_t
#include <string.h>    // memcpy, strtok
#include <stdlib.h>    // strtoul

// ── UART config ───────────────────────────────────────────────────────────────
#define STM32_SERIAL    Serial2
#define STM32_BAUD      115200
#define STM32_RX_PIN    16
#define STM32_TX_PIN    17
#define STM32_TIMEOUT   2000    // ms to wait for STM32 reply

// ── Command codes ─────────────────────────────────────────────────────────────
#define CMD_GET_VER         0x51
#define CMD_GET_HELP        0x52
#define CMD_GET_CID         0x53
#define CMD_GET_RDP_STATUS  0x54
#define CMD_GO_TO_ADDR      0x55
#define CMD_FLASH_ERASE     0x56
#define CMD_MEM_WRITE       0x57

// ── CRC32 — must match the Python bootloader host exactly:
//
//   Crc = 0xFFFFFFFF
//   for data in buff:
//       Crc = Crc ^ data          <-- XOR into LSB (NOT shifted to MSB)
//       for i in range(32):
//           if Crc & 0x80000000:  Crc = (Crc << 1) ^ 0x04C11DB7
//           else:                 Crc = (Crc << 1)
//
uint32_t crc32_stm32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc ^ (uint32_t)data[i];   // XOR byte into the LOW bits
        for (int b = 0; b < 32; b++) {
            if (crc & 0x80000000)
                crc = (crc << 1) ^ 0x04C11DB7;
            else
                crc = (crc << 1);
        }
    }
    return crc;
}

// ── Append CRC32 (little-endian) to buf[], return new total length ────────────
size_t appendCRC(uint8_t* buf, size_t data_len) {
    uint32_t crc = crc32_stm32(buf, data_len);
    buf[data_len + 0] = (uint8_t)((crc >>  0) & 0xFF);
    buf[data_len + 1] = (uint8_t)((crc >>  8) & 0xFF);
    buf[data_len + 2] = (uint8_t)((crc >> 16) & 0xFF);
    buf[data_len + 3] = (uint8_t)((crc >> 24) & 0xFF);
    return data_len + 4;
}

// ── Send packet to STM32 and read reply ──────────────────────────────────────
void sendAndReceive(uint8_t* buf, size_t pkt_len, size_t expected) {
    // Flush stale RX bytes
    while (STM32_SERIAL.available()) STM32_SERIAL.read();

    // Print packet being sent
    Serial.print("  -> Sending [");
    for (size_t i = 0; i < pkt_len; i++) {
        Serial.printf("%02X", buf[i]);
        if (i < pkt_len - 1) Serial.print(' ');
    }
    Serial.println("]");

    STM32_SERIAL.write(buf, pkt_len);
    STM32_SERIAL.flush();

    // Read reply
    uint8_t  reply[64];
    memset(reply, 0, sizeof(reply));
    size_t   rlen  = 0;
    uint32_t start = millis();

    if (expected > 0) {
        while (rlen < expected && (millis() - start) < STM32_TIMEOUT) {
            if (STM32_SERIAL.available()) {
                reply[rlen++] = (uint8_t)STM32_SERIAL.read();
                start = millis();   // reset timeout on each byte
            }
        }
    } else {
        // Timed drain for variable-length replies
        while ((millis() - start) < STM32_TIMEOUT && rlen < sizeof(reply)) {
            if (STM32_SERIAL.available()) {
                reply[rlen++] = (uint8_t)STM32_SERIAL.read();
                start = millis();
            }
        }
    }

    if (rlen == 0) {
        Serial.println("  <- No reply from STM32 (timeout)");
        return;
    }

    Serial.printf("  <- Reply (%u byte%s): ", (unsigned)rlen, rlen == 1 ? "" : "s");
    for (size_t i = 0; i < rlen; i++) {
        Serial.printf("0x%02X ", reply[i]);
    }
    Serial.println();
}

// ── Build simple 6-byte packet (no arguments) ────────────────────────────────
// Structure: [len_to_follow=5] [cmd] [CRC: 4 bytes]
size_t buildSimplePacket(uint8_t* buf, uint8_t cmd) {
    buf[0] = 0x05;   // length_to_follow = 5
    buf[1] = cmd;
    return appendCRC(buf, 2);
}

// ── Wait until at least one byte is available in Serial ──────────────────────
static void waitForSerial() {
    while (Serial.available() == 0) { delay(10); }
}

// ─── Command handlers ────────────────────────────────────────────────────────

void cmd_BL_GET_VER() {
    uint8_t buf[16];
    size_t  len = buildSimplePacket(buf, CMD_GET_VER);
    Serial.println("[BL_GET_VER]");
    sendAndReceive(buf, len, 1);   // 1 byte: version number
}

void cmd_BL_GET_HELP() {
    uint8_t buf[16];
    size_t  len = buildSimplePacket(buf, CMD_GET_HELP);
    Serial.println("[BL_GET_HELP]");
    sendAndReceive(buf, len, 10);  // 10 bytes: supported command codes
}

void cmd_BL_GET_CID() {
    uint8_t buf[16];
    size_t  len = buildSimplePacket(buf, CMD_GET_CID);
    Serial.println("[BL_GET_CID]");
    sendAndReceive(buf, len, 2);   // 2 bytes: chip ID LSB + MSB
}

void cmd_BL_GET_RDP_STATUS() {
    uint8_t buf[16];
    size_t  len = buildSimplePacket(buf, CMD_GET_RDP_STATUS);
    Serial.println("[BL_GET_RDP_STATUS]");
    sendAndReceive(buf, len, 1);   // 1 byte: RDP level
}

void cmd_BL_GO_TO_ADDR() {
    // Packet: [len=9] [0x55] [addr: 4 bytes LE] [CRC: 4 bytes]  → 10 bytes
    Serial.println("[BL_GO_TO_ADDR]");
    Serial.print("  Enter 8-hex-digit address (e.g. 08000000): ");
    waitForSerial();
    String input = Serial.readStringUntil('\n');
    input.trim();
    uint32_t addr = (uint32_t)strtoul(input.c_str(), NULL, 16);
    Serial.printf("  Address: 0x%08X\n", (unsigned int)addr);

    uint8_t buf[16];
    buf[0] = 0x09;
    buf[1] = CMD_GO_TO_ADDR;
    buf[2] = (uint8_t)((addr >>  0) & 0xFF);
    buf[3] = (uint8_t)((addr >>  8) & 0xFF);
    buf[4] = (uint8_t)((addr >> 16) & 0xFF);
    buf[5] = (uint8_t)((addr >> 24) & 0xFF);
    size_t len = appendCRC(buf, 6);

    sendAndReceive(buf, len, 1);   // 1 byte: status
}

void cmd_BL_FLASH_MASS_ERASE() {
    // Packet: [len=7] [0x56] [0xFF] [0x00] [CRC: 4 bytes]  → 8 bytes
    // 0xFF in sector field signals mass erase to the bootloader
    Serial.println("[BL_FLASH_MASS_ERASE]");
    Serial.println("  WARNING: This will erase ALL user flash sectors!");
    Serial.print("  Type YES to confirm: ");
    waitForSerial();
    String confirm = Serial.readStringUntil('\n');
    confirm.trim();
    if (!confirm.equalsIgnoreCase("YES")) {
        Serial.println("  Aborted.");
        return;
    }

    uint8_t buf[16];
    buf[0] = 0x07;
    buf[1] = CMD_FLASH_ERASE;
    buf[2] = 0xFF;   // mass erase trigger
    buf[3] = 0x00;
    size_t len = appendCRC(buf, 4);

    sendAndReceive(buf, len, 1);
}

void cmd_BL_FLASH_ERASE() {
    // Packet: [len=7] [0x56] [sector_num] [num_sectors] [CRC: 4 bytes]
    Serial.println("[BL_FLASH_ERASE]");

    Serial.print("  Starting sector (0-7): ");
    waitForSerial();
    String s1 = Serial.readStringUntil('\n');
    s1.trim();
    uint8_t sector = (uint8_t)s1.toInt();
    Serial.println(sector);

    Serial.print("  Number of sectors (1-8): ");
    waitForSerial();
    String s2 = Serial.readStringUntil('\n');
    s2.trim();
    uint8_t num = (uint8_t)s2.toInt();
    Serial.println(num);

    uint8_t buf[16];
    buf[0] = 0x07;
    buf[1] = CMD_FLASH_ERASE;
    buf[2] = sector;
    buf[3] = num;
    size_t len = appendCRC(buf, 4);

    sendAndReceive(buf, len, 1);
}

void cmd_BL_MEM_WRITE() {
    // Packet: [len=10+X] [0x57] [addr:4 LE] [payload_len:1] [payload:X] [CRC:4]
    Serial.println("[BL_MEM_WRITE]");

    Serial.print("  Base address (hex, e.g. 08008000): ");
    waitForSerial();
    String sa = Serial.readStringUntil('\n');
    sa.trim();
    uint32_t addr = (uint32_t)strtoul(sa.c_str(), NULL, 16);
    Serial.printf("  0x%08X\n", (unsigned int)addr);

    Serial.print("  Payload bytes as hex pairs (e.g. DE AD BE EF): ");
    waitForSerial();
    String sp = Serial.readStringUntil('\n');
    sp.trim();

    // Parse space-separated hex bytes into payload[]
    uint8_t payload[128];
    uint8_t plen = 0;
    // strtok needs a mutable char buffer
    char spbuf[256];
    strncpy(spbuf, sp.c_str(), sizeof(spbuf) - 1);
    spbuf[sizeof(spbuf) - 1] = '\0';
    char* tok = strtok(spbuf, " ");
    while (tok != NULL && plen < 128) {
        payload[plen++] = (uint8_t)strtoul(tok, NULL, 16);
        tok = strtok(NULL, " ");
    }
    Serial.printf("  %u byte(s) of payload\n", (unsigned)plen);

    // Build packet
    uint8_t buf[256];
    buf[0] = (uint8_t)(10 + plen - 1);   // length_to_follow
    buf[1] = CMD_MEM_WRITE;
    buf[2] = (uint8_t)((addr >>  0) & 0xFF);
    buf[3] = (uint8_t)((addr >>  8) & 0xFF);
    buf[4] = (uint8_t)((addr >> 16) & 0xFF);
    buf[5] = (uint8_t)((addr >> 24) & 0xFF);
    buf[6] = plen;
    memcpy(&buf[7], payload, plen);
    size_t len = appendCRC(buf, 7 + plen);

    sendAndReceive(buf, len, 1);
}

// ── Menu ─────────────────────────────────────────────────────────────────────

void printMenu() {
    Serial.println();
    Serial.println("+============================================+");
    Serial.println("|        STM32F4 BootLoader Bridge           |");
    Serial.println("+============================================+");
    Serial.println("  Which BL command do you want to send ??");
    Serial.println();
    Serial.println("  BL_GET_VER              --> 1");
    Serial.println("  BL_GET_HELP             --> 2");
    Serial.println("  BL_GET_CID              --> 3");
    Serial.println("  BL_GET_RDP_STATUS       --> 4");
    Serial.println("  BL_GO_TO_ADDR           --> 5");
    Serial.println("  BL_FLASH_MASS_ERASE     --> 6");
    Serial.println("  BL_FLASH_ERASE          --> 7");
    Serial.println("  BL_MEM_WRITE            --> 8");
    Serial.println();
    Serial.print("  > ");
}

// ── Setup / Loop ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(1000);   // give USB-Serial time to enumerate before first prints

    // ── Init UART2 ───────────────────────────────────────────────────────
    // Call .end() first to guarantee a clean pin-mux state on all ESP32
    // board variants / Arduino core versions under PlatformIO.
    STM32_SERIAL.end();
    delay(100);
    STM32_SERIAL.begin(STM32_BAUD, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);
    delay(200);

    // ── TX sanity check (probe GPIO17 with logic analyser) ───────────────
    // At startup the ESP32 will blast 0xAA 0x55 four times on GPIO17.
    // If your analyser sees nothing here the issue is wiring / pin-mux,
    // not the bootloader protocol.
    Serial.println("[DEBUG] Sending TX test pattern 0xAA 0x55 on GPIO17...");
    for (int i = 0; i < 4; i++) {
        STM32_SERIAL.write((uint8_t)0xAA);
        STM32_SERIAL.write((uint8_t)0x55);
    }
    STM32_SERIAL.flush();
    Serial.printf("[DEBUG] Done.  UART2  baud=%d  RX=GPIO%d  TX=GPIO%d\n",
                  STM32_BAUD, STM32_RX_PIN, STM32_TX_PIN);

    printMenu();
}

void loop() {
    if (!Serial.available()) return;

    String input = Serial.readStringUntil('\n');
    input.trim();
    int choice = input.toInt();
    Serial.println(choice);   // echo

    switch (choice) {
        case 1: cmd_BL_GET_VER();          break;
        case 2: cmd_BL_GET_HELP();         break;
        case 3: cmd_BL_GET_CID();          break;
        case 4: cmd_BL_GET_RDP_STATUS();   break;
        case 5: cmd_BL_GO_TO_ADDR();       break;
        case 6: cmd_BL_FLASH_MASS_ERASE(); break;
        case 7: cmd_BL_FLASH_ERASE();      break;
        case 8: cmd_BL_MEM_WRITE();        break;
        default:
            Serial.println("  Invalid choice. Enter 1-8.");
    }

    printMenu();
}