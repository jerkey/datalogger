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

uint32_t lastBMSPacket = 0; // when was the last time we got a BMS data
int8_t packetProgress = 0; // how far along in a decoded packet are we
uint8_t packetLength = 0; // received packet length
uint16_t remainingcapacity; //offset 0x62-0x63
uint16_t remainingpercent; //offset 0x64-0x65
int16_t current; //offset 0x66-67 - negative = charging; /100 = Ampere
uint16_t voltage; //offset 0x68-69 /10 = Volt
uint8_t temperature[2]; //offset 0x6A-0x6B -20 = °C


void setup() {
  Serial.begin(115200); //while (!Serial); // wait for serial port to connect. Needed for native USB port only

  Serial.print("Initializing SD card...");

  if (!SD.begin(chipSelect)) { // see if the card is present and can be initialized:
    Serial.println("Card failed, or not present");
    //while (1); // don't do anything more:
  } else {
    Serial.println("card initialized.");
  }
}

void loop() {
  // make a string for assembling the data to log:
  String dataString = "";

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
    Serial.println(logString);
  }
  // if the file isn't open, pop up an error:
  else {
    Serial.println("error opening datalog.txt");
  }
}









