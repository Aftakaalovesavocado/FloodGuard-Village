/*
 * FGV PROTOTIPE — PoC-3 : Gateway RX + OLED — ESP32-S3 #2 (unit PALD)
 * =====================================================================
 * poc3_gateway + OLED SSD1306 128x64 I2C.
 * Konfigurasi OLED diambil dari firmware PALD (receiver_gateway.ino:67-69):
 *   SDA=GPIO 4, SCL=GPIO 5, addr 0x3C, supply 3V3.
 *
 * ⚠️ CATATAN ARSITEKTUR: FGV ARSITEKTUR_DAN_PROTOKOL.md §2.3 menulis OLED
 * gateway di SDA=15/SCL=16. Unit PALD yang dipinjam memakai 4/5.
 * Sementara ini kita ikut unit PALD (4/5). Saat gateway FGV final dirakit
 * dengan OLED baru, pin mengikuti dokumen (15/16) — update di sini.
 * GPIO 4/5 tidak bentrok dengan LoRa (8-13).
 *
 * Pola RX = v2 tervalidasi: callback simpan ukuran, loop baca via
 * readBytes (tanpa parsePacket — jebakan LoRa.cpp 0.8.0, lihat HASIL_POC2).
 *
 * Paket 5 byte: node_id | seq | level_mm (0xFFFF = INVALID) | crc8
 * Radio: 433.0 MHz, SF9, BW125, CR4/5, sync 0xF1, CRC on.
 * Board: esp32:esp32:esp32s3 · Serial: 115200
 */

#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <stdint.h>

// ---------------- Pin map --------------------------------------------------
#define LORA_NSS   10
#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_RST   9
#define LORA_DIO0  8

#define OLED_SDA   4     // unit PALD (receiver_gateway.ino:67-68)
#define OLED_SCL   5
#define OLED_ADDR  0x3C

const long    RF_FREQ_HZ   = 433000000L;
const int     RF_SF        = 9;
const long    RF_BW        = 125E3;
const int     RF_CR        = 5;
const uint8_t RF_SYNCWORD = 0xF1;

const uint8_t  PKT_LEN        = 5;
const uint16_t LEVEL_INVALID  = 0xFFFF;
const uint32_t SUMMARY_MS     = 30000;
const uint32_t OLED_PERIOD_MS = 500;   // refresh layar 2x/detik (I2C lambat)

Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool oled_ok = false;

struct __attribute__((packed)) Poc3Tx {
  uint8_t  node_id;
  uint8_t  seq;
  uint16_t level_mm;
  uint8_t  crc8;
};

uint8_t crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0x00;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

// ---------------- Handoff ISR -> loop ---------------------------------------
volatile bool    pkt_flag = false;
volatile uint8_t pkt_len  = 0;

void onReceive(int packetSize) {
  pkt_len  = (uint8_t)packetSize;
  pkt_flag = true;
}

// ---------------- State (dipakai Serial & OLED) ------------------------------
uint32_t n_ok = 0, n_crc_bad = 0, n_lost = 0, n_foreign = 0;
int16_t  last_seq = -1;
uint32_t t_summary = 0, t_oled = 0;

// snapshot terakhir untuk layar
uint8_t  scr_node = 0;
uint8_t  scr_seq  = 0;
uint16_t scr_level = 0;      // 0xFFFF = INVALID
int8_t   scr_rssi = 0;
float    scr_snr = 0;
bool     scr_valid_data = false;

void drawOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("FGV GATEWAY (PoC-3)"));
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  if (!scr_valid_data) {
    display.setCursor(0, 20);
    display.println(F("Menunggu node..."));
    display.display();
    return;
  }

  display.setTextSize(2);
  display.setCursor(0, 14);
  if (scr_level == LEVEL_INVALID) {
    display.print(F("INVALID"));
  } else {
    display.print(scr_level);
    display.print(F("mm"));
  }

  display.setTextSize(1);
  display.setCursor(0, 34);
  display.print(F("NODE "));
  display.print(scr_node);
  display.print(F(" seq="));
  display.print(scr_seq);

  display.setCursor(0, 44);
  display.print(F("RSSI "));
  display.print(scr_rssi);
  display.print(F(" SNR "));
  display.print(scr_snr, 1);

  const uint32_t total = n_ok + n_crc_bad + n_lost + n_foreign;
  display.setCursor(0, 54);
  display.print(F("RX="));
  display.print(n_ok);
  display.print(F(" lost="));
  display.print(n_lost);
  if (total > 0) {
    display.print(F(" PDR="));
    display.print(n_ok * 100.0f / total, 1);
    display.print(F("%"));
  }
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println(F("FGV PoC-3 GATEWAY v2 | RX + OLED | decode level PoC-3"));

  // OLED (pin unit PALD: SDA=4 SCL=5, addr 0x3C)
  Wire.begin(OLED_SDA, OLED_SCL);
  oled_ok = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!oled_ok) {
    Serial.println(F("WARN: OLED tidak terdeteksi (cek wiring I2C). Lanjut tanpa layar."));
  } else {
    drawOLED();   // tampil "Menunggu node..."
  }

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(RF_FREQ_HZ)) {
    Serial.println(F("FATAL: LoRa.begin gagal."));
    while (true) delay(1000);
  }
  LoRa.setSpreadingFactor(RF_SF);
  LoRa.setSignalBandwidth(RF_BW);
  LoRa.setCodingRate4(RF_CR);
  LoRa.setSyncWord(RF_SYNCWORD);
  LoRa.enableCrc();
  LoRa.onReceive(onReceive);
  LoRa.receive();
  Serial.println(F("LoRa OK. Menunggu node PoC-3..."));
  t_summary = millis();
  t_oled    = millis();
}

void loop() {
  if (pkt_flag) {
    noInterrupts();
    pkt_flag = false;
    const uint8_t size = pkt_len;
    interrupts();

    const int8_t rssi = LoRa.packetRssi();
    const float  snr  = LoRa.packetSnr();

    if (size != PKT_LEN) {
      n_foreign++;
      Serial.print(F("RX asing len="));
      Serial.print(size);
      Serial.print(F(" RSSI="));
      Serial.print(rssi);
      Serial.print(F(" SNR="));
      Serial.print(snr, 1);
      Serial.print(F(" hex:"));
      while (LoRa.available()) { Serial.print(F(" ")); Serial.print(LoRa.read(), HEX); }
      Serial.println();
    } else {
      Poc3Tx p;
      LoRa.readBytes((uint8_t *)&p, sizeof(p));

      const bool crc_ok = (p.crc8 == crc8((uint8_t *)&p, sizeof(p) - 1));
      const bool id_ok  = (p.node_id >= 1 && p.node_id <= 3);

      if (crc_ok && id_ok) {
        if (last_seq >= 0) {
          const uint8_t gap = (uint8_t)((p.seq - (uint8_t)last_seq) & 0xFF);
          if (gap > 1 && gap < 128) n_lost += (gap - 1);
        }
        last_seq = p.seq;
        n_ok++;

        scr_node = p.node_id;
        scr_seq  = p.seq;
        scr_level = p.level_mm;
        scr_rssi  = rssi;
        scr_snr   = snr;
        scr_valid_data = true;

        Serial.print(F("NODE "));
        Serial.print(p.node_id);
        Serial.print(F(" seq="));
        Serial.print(p.seq);
        if (p.level_mm == LEVEL_INVALID) {
          Serial.print(F("  level=INVALID"));
        } else {
          Serial.print(F("  level_mm="));
          Serial.print(p.level_mm);
        }
        Serial.print(F("  RSSI="));
        Serial.print(rssi);
        Serial.print(F(" dBm  SNR="));
        Serial.print(snr, 1);
        Serial.println(F(" dB"));
      } else {
        n_crc_bad++;
        Serial.print(F("RX BROL -> dibuang  crc_ok="));
        Serial.print(crc_ok);
        Serial.print(F(" id_ok="));
        Serial.println(id_ok);
      }
    }
  }

  if (millis() - t_oled >= OLED_PERIOD_MS) {
    t_oled = millis();
    if (oled_ok) drawOLED();
  }

  if (millis() - t_summary >= SUMMARY_MS) {
    t_summary = millis();
    const uint32_t total = n_ok + n_crc_bad + n_lost + n_foreign;
    if (total == 0) {
      Serial.println(F("SUMMARY: belum ada paket sama sekali"));
    } else {
      const float pdr = n_ok * 100.0f / total;
      Serial.print(F("SUMMARY ok="));
      Serial.print(n_ok);
      Serial.print(F(" crc_bad="));
      Serial.print(n_crc_bad);
      Serial.print(F(" foreign="));
      Serial.print(n_foreign);
      Serial.print(F(" lost="));
      Serial.print(n_lost);
      Serial.print(F("  PDR="));
      Serial.print(pdr, 1);
      Serial.println(F("%"));
    }
  }
}
