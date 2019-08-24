/* SD card datalogger
 * SD card attached to SPI bus as follows:
 ** MOSI - pin 11
 ** MISO - pin 12
 ** CLK - pin 13
 ** CS - pin 4 (for MKRZero SD: SDCARD_SS_PIN)
 */

#include <SPI.h>
#include <SD.h>

const int chipSelect = 4;

uint32_t lastLogEntry = 0; // when was the last time we saved a log entry
uint32_t lastBMSPacket = 0; // when was the last time we got a BMS data
uint32_t lastStatusPrint = 0; // when was the last time we printed our status
#define INTERVAL_STATUSPRINT 1000 // how often to print our status
uint32_t lastFilesystemFlush = 0; // when was the last time we flushed filesystem
#define INTERVAL_FILESYSTEMFLUSH 1000 // how often to flush filesystem

uint16_t BMSPacketsCollected = 0; // how many BMS packets we've picked up ever
uint16_t REQPacketsCollected = 0; // how many BMS Request packets we've picked up ever
uint16_t ESCPacketsCollected = 0; // how many ESC packets we've picked up ever
uint16_t BLEPacketsCollected = 0; // how many BLE packets we've picked up ever
int8_t packetProgress = 0; // how far along in a decoded packet are we
uint8_t packetLength = 0; // received packet length
uint16_t remainingcapacity; //offset 0x62-0x63
uint16_t remainingpercent; //offset 0x64-0x65
int16_t current; //offset 0x66-67 - negative = charging; /100 = Ampere
uint16_t voltage; //offset 0x68-69 /10 = Volt
uint8_t temperature[2]; //offset 0x6A-0x6B -20 = °C

#define USBSERIAL // if we're using an ATMEGA32U4 board with native USB
#ifdef USBSERIAL
#define M365Serial Serial1
#define Console Serial
#elif
#define M365Serial Serial
#define Console Serial
#endif

void setup() {
#ifdef USBSERIAL
  Console.begin(115200); // native CDC USB serial
  while (!Console); // wait for serial port to connect. Needed for native USB port only
#endif

  M365Serial.begin(115200);

  Console.print("Initializing SD card...");

  if (!SD.begin(chipSelect)) { // see if the card is present and can be initialized:
    Console.println("Card failed, or not present");
    //while (1); // don't do anything more:
  } else {
    Console.println("card initialized.");
  }
}

// TODO: flush and reopen file regularly

void loop() {
  String logString = ""; // make a string for assembling the data to log:

  handleM365Serial(); // if bytes are available, deal with them

  for (int analogPin = 0; analogPin < 3; analogPin++) {
    int sensor = analogRead(analogPin);
    logString += String(sensor);
    if (analogPin < 2) {
      logString += ",";
    }
  }

  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  File dataFile = SD.open("datalog.txt", FILE_WRITE);

  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(logString);
    dataFile.close();
    // print to the serial port too:
    Console.println(logString);
  }
  if (millis() - lastStatusPrint > INTERVAL_STATUSPRINT) {
    printStatus();
    lastStatusPrint = millis();
  }
}

void handleM365Serial() {
  if (M365Serial.available()) {
    uint8_t inByte = M365Serial.read();
    switch (packetProgress) {
      case 0:
        packetProgress = (inByte == 0x55) ? 1 : 0; // we're looking for 0xAA now
        break;
      case 1:
        packetProgress = (inByte == 0xAA) ? 2 : 0; // we're gonna grab packetlength next
        break;
      case 2:
        packetLength = inByte;
        packetProgress = 3;
        break;
      case 3:
        packetProgress = (inByte == 0x25) ? 4 : 0; // 0x25 means address_bms
        if (inByte == 0x25) BMSPacketsCollected++;
        if (inByte == 0x22) REQPacketsCollected++;
        if (inByte == 0x23) ESCPacketsCollected++;
        if (inByte == 0x20) BLEPacketsCollected++;
        break;
      case 4:
        packetProgress = 5; // i don't know what that byte is so we skip it
        break;
      case 5:
        packetProgress = (inByte == 0x31) ? 6 : 0; // we need the packet aimed at bmsdata[0x62]
        break;
      case 6:
        remainingcapacity = inByte;
        packetProgress += 1;
        break;
      case 7:
        remainingcapacity += inByte << 8;
        packetProgress += 1;
        break;
      case 8:
        remainingpercent = inByte;
        packetProgress += 1;
        break;
      case 9:
        remainingpercent += inByte << 8;
        packetProgress += 1;
        break;
      case 10:
        current = inByte;
        packetProgress += 1;
        break;
      case 11:
        current += inByte << 8;
        packetProgress += 1;
        break;
      case 12:
        voltage = inByte;
        packetProgress += 1;
        break;
      case 13:
        voltage += inByte << 8;
        packetProgress += 1;
        break;
      case 14:
        temperature[0] = inByte;
        packetProgress += 1;
        break;
      case 15:
        temperature[1] = inByte;
        packetProgress += 1;
        break;
      case 16:
        lastBMSPacket = millis(); // store the time we got bms data
        packetProgress = 0; // start looking for a new packet
        break;
    }
  } // if (M365Serial.available())
} // handleM365Serial1()

void printStatus() {
  Console.print("BMS:"+String(BMSPacketsCollected));
  Console.print(" REQ:"+String(REQPacketsCollected));
  Console.print(" ESC:"+String(ESCPacketsCollected));
  Console.print(" BLE:"+String(BLEPacketsCollected));
  Console.print("\tlastBMSPacket: "+String(millis() - lastBMSPacket));
  Console.print("\tVolt: "+String(voltage));
  Console.print("\tAmps: "+String(current));
  Console.print("\tBatt: "+String(remainingpercent)+"%\n");
}
