/*
 * FGV — Gateway PoC-5 : RX paket Manning + OLED — ESP32-S3 #2 (unit PALD)
 * ========================================================================
 * Upgrade poc3_gateway: decode paket 11 byte PoC-5 (level + v + Q).
 * Pola RX = v2 tervalidasi (callback simpan size, loop readBytes, tanpa
 * parsePacket — lihat HASIL_POC2). Pin map & radio identik PoC-2/3.
 * OLED = unit PALD: SDA=4, SCL=5, 0x3C (catatan pin: FGV doc 15/16 —
 * ikut unit pinjaman dulu).
 * Board: esp32:esp32:esp32s3 · Serial: 115200
 */

#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <stdint.h>

#define LORA_NSS   10
#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_RST   9
#define LORA_DIO0  8

#define OLED_SDA   4
#define OLED_SCL   5
#define OLED_ADDR  0x3C

const long    RF_FREQ_HZ   = 433000000L;
const int     RF_SF        = 9;
const long    RF_BW        = 125E3;
const int     RF_CR        = 5;
const uint8_t RF_SYNCWORD = 0xF1;

const uint8_t  PKT_LEN        = 11;
const uint16_t LEVEL_INVALID  = 0xFFFF;
const uint16_t Q_INVALID_X10  = 0xFFFF;
const uint32_t SUMMARY_MS     = 30000;
const uint32_t OLED_PERIOD_MS = 500;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool oled_ok = false;

struct __attribute__((packed)) FgvPacketPoc5 {
  uint8_t  node_id;
  uint8_t  seq;
  uint16_t level_mm;
  uint16_t chan_mm;
  uint16_t v_cms;
  uint16_t q_lpm_x10;
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

volatile bool    pkt_flag = false;
volatile uint8_t pkt_len  = 0;

void onReceive(int packetSize) {
  pkt_len  = (uint8_t)packetSize;
  pkt_flag = true;
}

uint32_t n_ok = 0, n_crc_bad = 0, n_lost = 0, n_foreign = 0;
int16_t  last_seq = -1;
uint32_t t_summary = 0, t_oled = 0;

// snapshot layar
uint8_t  scr_node = 0, scr_seq = 0;
uint16_t scr_level = 0, scr_chan = 0, scr_qx10 = 0;
int8_t   scr_rssi = 0;
bool     scr_valid = false;

void drawOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("FGV GATEWAY PoC-5"));
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  if (!scr_valid) {
    display.setCursor(0, 20);
    display.println(F("Menunggu node..."));
    display.display();
    return;
  }

  display.setTextSize(2);
  display.setCursor(0, 14);
  if (scr_level == LEVEL_INVALID) display.print(F("INVALID"));
  else { display.print(scr_level); display.print(F("mm")); }

  display.setTextSize(1);
  display.setCursor(0, 34);
  display.print(F("NODE "));
  display.print(scr_node);
  display.print(F(" seq="));
  display.print(scr_seq);

  display.setCursor(0, 44);
  if (scr_level != LEVEL_INVALID) {
    display.print(F("Q="));
    display.print(scr_qx10 / 10.0f, 1);
    display.print(F(" L/m  "));
    display.print((int)(scr_level * 100 / max((uint16_t)1, scr_chan)));
    display.print(F("%"));
  }

  display.setCursor(0, 54);
  display.print(F("RSSI "));
  display.print(scr_rssi);
  display.print(F(" RX="));
  display.print(n_ok);
  display.print(F(" lost="));
  display.print(n_lost);
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println(F("FGV GATEWAY PoC-5 | RX + OLED | decode level+v+Q"));

  Wire.begin(OLED_SDA, OLED_SCL);
  oled_ok = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!oled_ok) {
    Serial.println(F("WARN: OLED tidak terdeteksi. Lanjut tanpa layar."));
  } else {
    drawOLED();
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
  Serial.println(F("LoRa OK. Menunggu node PoC-5..."));
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

    if (size != PKT_LEN) {
      n_foreign++;
      Serial.print(F("RX asing len="));
      Serial.println(size);
      while (LoRa.available()) LoRa.read();
    } else {
      FgvPacketPoc5 p;
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

        scr_node = p.node_id; scr_seq = p.seq;
        scr_level = p.level_mm; scr_chan = p.chan_mm;
        scr_qx10 = p.q_lpm_x10; scr_rssi = rssi;
        scr_valid = true;

        Serial.print(F("NODE "));
        Serial.print(p.node_id);
        Serial.print(F(" seq="));
        Serial.print(p.seq);
        if (p.level_mm == LEVEL_INVALID) {
          Serial.print(F("  level=INVALID"));
        } else {
          Serial.print(F("  lvl="));
          Serial.print(p.level_mm);
          Serial.print(F("/"));
          Serial.print(p.chan_mm);
          Serial.print(F("mm  v="));
          Serial.print(p.v_cms);
          Serial.print(F("cm/s  Q="));
          Serial.print(p.q_lpm_x10 / 10.0f, 1);
          Serial.print(F("L/m"));
        }
        Serial.print(F("  RSSI="));
        Serial.print(rssi);
        Serial.println(F(" dBm"));
      } else {
        n_crc_bad++;
        Serial.println(F("RX BROL -> dibuang"));
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
      Serial.println(F("SUMMARY: belum ada paket"));
    } else {
      Serial.print(F("SUMMARY ok="));
      Serial.print(n_ok);
      Serial.print(F(" bad="));
      Serial.print(n_crc_bad);
      Serial.print(F(" foreign="));
      Serial.print(n_foreign);
      Serial.print(F(" lost="));
      Serial.print(n_lost);
      Serial.print(F("  PDR="));
      Serial.print(n_ok * 100.0f / total, 1);
      Serial.println(F("%"));
    }
  }
}