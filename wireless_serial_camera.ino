/*
 * Copyright 2013 Barnaby Shearer <b@Zi.iS>, Gary Fletcher <garygfletcher@hotmail.com>
 * License GPLv2
 */

#include <Adafruit_VC0706.h>
#include <SD.h>
#include <avr/sleep.h>
#include <SoftwareSerial.h>
#include <avr/pgmspace.h>

#define NODE_ID 0
#define PICTURE_SIZE VC0706_160x120 // VC0706_640x480, VC0706_320x240, VC0706_160x120
#define PICTURE_COMPRESSION 250
#define SLEEP_RADIO
#define SLEEP_CAMERA
#define SLEEP
#define DEBUG
#define RADIO_BAUD 115200
#define RADIO_LIMIT 3
#define RADIO_SLOT 500
#define RADIO_GRACE 50
#define CAMERA_WARMUP 200

#define PIN_CAMERA_POWER A1
#define PIN_CAMERA_TX 5
#define PIN_CAMERA_RX 6
#define PIN_RADIO_POWER 4 //Radio must be programed ATSM 2 -- deep sleep mode
#define PIN_RADIO_ENABLE 8
#define PIN_PIR 2 //Must support Interupt
#define PIN_PIR_INT 0 //Must be the Interupt for PIN_PIR

static PROGMEM prog_uint32_t CRC[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};

unsigned long senduntil = 0;

void radio(const char[12], boolean = false);

void loop() {
  sleepTillPIR();
  takePicture();
}

void setup() {
  pinMode(PIN_CAMERA_POWER, OUTPUT);
  disableCamera();
  pinMode(PIN_RADIO_POWER, OUTPUT);
  pinMode(PIN_RADIO_ENABLE, OUTPUT);
  enableRadio();
  Serial.begin(RADIO_BAUD);
  pinMode(PIN_PIR, INPUT);
  delay(500); //Wait to allow OTA programming
  #ifdef DEBUG
    radio("\rPOWER ON   ");
  #endif
}

void sleepTillPIR() {
  disableRadio();
  if(digitalRead(PIN_PIR)==LOW) {
    #ifdef SLEEP
      sleep_enable();
      attachInterrupt(PIN_PIR_INT, pirDetect, HIGH);
      set_sleep_mode(SLEEP_MODE_PWR_DOWN);
      cli();
      sei();
      sleep_cpu();
      //Wait for PIR
      sleep_disable();
    #else
      //poll
      while(!digitalRead(PIN_PIR)) {
        delay(1000);
      }
    #endif
  }
  enableRadio();
}  

void pirDetect(void) {
  sleep_disable();
  detachInterrupt(PIN_PIR_INT);
}

void enableCamera() {
  #ifdef SLEEP_CAMERA
    digitalWrite(PIN_CAMERA_POWER, HIGH);
  #endif
}

void disableCamera() {
  #ifdef SLEEP_CAMERA
    digitalWrite(PIN_CAMERA_POWER, LOW);
  #endif
}

void enableRadio() {
  #ifdef SLEEP_RADIO
    digitalWrite(PIN_RADIO_POWER, LOW);
    while(Serial.available()) {
      Serial.read();
    }
  #endif
  digitalWrite(PIN_RADIO_ENABLE, HIGH);
}

void disableRadio() {
  radio("\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04", true);
  senduntil = 0;
  Serial.flush();
  digitalWrite(PIN_RADIO_ENABLE, LOW);
  #ifdef SLEEP_RADIO
    digitalWrite(PIN_RADIO_POWER, HIGH);
  #endif
}

void takePicture() {
  enableCamera();
  SoftwareSerial cameraSerial = SoftwareSerial(PIN_CAMERA_TX, PIN_CAMERA_RX);
  Adafruit_VC0706 camera = Adafruit_VC0706(&cameraSerial);
  if(!camera.begin()) {
    radio("\rCAMERA ERR ");
    return;
  } else {
    camera.getVersion(); //HACK: Needs getVersion and reset to apply changes
    camera.setImageSize(PICTURE_SIZE);
    camera.setCompression(PICTURE_COMPRESSION);
    camera.reset();
    camera.resumeVideo();
    unsigned long camera_warm = millis() + CAMERA_WARMUP;
    #ifdef DEBUG
      radio("\rCAMERA ON  ");
    #endif
    unsigned long iswarm = millis();
    if(iswarm < camera_warm) {
      delay(camera_warm - iswarm);
    }
  }
  
  if(!camera.takePicture()) {
    radio("\rCAMERA FAIL");
    return;
  }
  uint16_t length = camera.frameLength();
  char* buf = "            ";
  sprintf(buf,"\x01NEW_P%5u\x02", length);
  radio(buf);

  unsigned long crc = ~0L;
  uint8_t toread;
  uint8_t pointer = 32;
  char *cambuf;
  for(uint16_t i = 0; i < length; i++) {
    if(pointer == 32) {
      //HACK: Camera must be read in 32 byte chunks
      toread = min(32, length-i);
      cambuf = (char *)camera.readPicture(toread);
      pointer = 0;
    }
    buf[i%12] = cambuf[pointer];
    crc = crc_update(crc, buf[i%12]);
    if(i%12 == 11 || (toread < 32 && pointer == toread-1)) {
      radio(buf);
    }
    pointer += 1;
  }
  crc = ~crc;
  sprintf(buf,"CHK%08lx\x17", crc);
  radio(buf);
  disableCamera();
  radio("\rSLEEPING..."); //HACK: This last packet is unreliable so make it unimportant ;-)
}

void radio(const char packet[12], boolean drop) {
  uint8_t buf[] = {255,255,255,255,255,255,255,255,255,255,255,255};
  if(!drop) {
    while(millis() >= senduntil) {
      if(millis() <= senduntil + RADIO_GRACE) {
        Serial.write((const uint8_t*)"\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04", 12);
        senduntil = 0;
      }
      if(Serial.available()) {
        for(int i=0; i<11; i++) {
          buf[i] = buf[i+1];
        }
        buf[11] = Serial.read();
        if(
          buf[0]==5
        &&
          buf[2]==5
        &&
          buf[4]==5
        &&
          buf[6]==5
        &&
          buf[8]==5
        &&
          buf[10]==5
        &&
          buf[1]==NODE_ID
        &&
          buf[3]==NODE_ID
        &&
          buf[5]==NODE_ID
        &&
          buf[7]==NODE_ID
        &&
          buf[9]==NODE_ID
        &&
          buf[11]==NODE_ID
        ) {
          senduntil = millis() + RADIO_SLOT;
          Serial.write((const uint8_t*)"\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06", 12);
        }
      }
    }
  }
  if(!drop || millis() < senduntil) {
    Serial.write((const uint8_t*)packet, 12);
    delay(RADIO_LIMIT);
  }
  while(Serial.available()) {
    Serial.read();
  }
}

unsigned long crc_update(unsigned long crc, byte data) {
    byte tbl_idx;
    tbl_idx = crc ^ (data >> (0 * 4));
    crc = pgm_read_dword_near(CRC + (tbl_idx & 0x0f)) ^ (crc >> 4);
    tbl_idx = crc ^ (data >> (1 * 4));
    crc = pgm_read_dword_near(CRC + (tbl_idx & 0x0f)) ^ (crc >> 4);
    return crc;
}
