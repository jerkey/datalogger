/* SD card datalogger
 * SD card attached to SPI bus as follows:
          ATMEGA328  32U4
 ** MOSI - pin 11    16
 ** MISO - pin 12    14
 ** CLK  - pin 13    15
 ** CS   - pin 4     10 (or whatever you want, set chipSelect
 */
#define POWER_PIN 2 // high when power supply is on (shutdown disk if this goes low)
#define FILENAME "ninebot.txt"

#include <SPI.h>
#include <SD.h>

const int chipSelect = 10; // this is the SS/CS pin we choose to use

uint32_t lastLogEntry = 0; // when was the last time we saved a log entry
uint32_t lastBMSPacket = 0; // when was the last time we got a BMS data
uint32_t lastStatusPrint = 0; // when was the last time we printed our status
#define INTERVAL_STATUSPRINT 1000 // how often to print our status
uint32_t lastFilesystemFlush = 0; // when was the last time we flushed filesystem
#define INTERVAL_FILESYSTEMFLUSH 1000 // how often to flush filesystem
uint32_t lastLogWrite = 0; // when was the last time we wrote log
#define INTERVAL_LOGWRITE 1000 // how often to write log
uint32_t nextDiskAttempt = 0; // when is the next time we will attempt to open the disk
#define INTERVAL_DISKATTEMPT 5000 // how often to attempt to open disk

#define NINEBOT_ADDR_ESC = 0x20
#define NINEBOT_ADDR_BLE = 0x21
#define NINEBOT_ADDR_BMS1 = 0x22
#define NINEBOT_ADDR_BMS2 = 0x23
uint16_t ESCPacketsCollected = 0;
uint16_t BLEPacketsCollected = 0;
uint16_t BMS1PacketsCollected = 0;
uint16_t BMS2PacketsCollected  = 0;
int8_t packetProgress = 0; // how far along in a decoded packet are we
int8_t packetAddress = 0; // which type of packet is it?
uint8_t packetLength = 0; // received packet length
uint16_t remainingcapacity[2];
uint16_t remainingpercent[2];
int16_t current[2];
uint16_t voltage[2];
uint8_t speed;
uint8_t throttle = 0; // received in NINEBOT_ADDR_BLE packets
uint8_t brake = 0; // received in NINEBOT_ADDR_BLE packets

#define USBSERIAL // if we're using an ATMEGA32U4 board with native USB
#ifdef USBSERIAL
#define es4Serial Serial1
#define Console Serial
#elif
#define es4Serial Serial
#define Console Serial
#endif

void setup() {
#ifdef USBSERIAL
  Console.begin(115200); // native CDC USB serial
  // while (!Console); // wait for serial port to connect. Needed for native USB port only
#endif

  es4Serial.begin(115200);
}

uint8_t diskOpen = 0;
String logString = ""; // make a string for assembling the data to log:
File dataFile;

void loop() {
  handleEs4Serial(); // if bytes are available, deal with them

  if (digitalRead(POWER_PIN)) {
    if ((diskOpen == 0) && (millis() > nextDiskAttempt)){ // Console.print("Initializing SD card...");
      Console.print("[");
      if (SD.begin(chipSelect)) { // see if the card is present and can be initialized:
        Console.println("card initialized]");
        diskOpen = 1;
        lastLogWrite = millis(); // time starts now
        dataFile = SD.open(FILENAME, FILE_WRITE);
        dataFile.println("time, voltage[0], voltage[1], current[0], current[1], speed, throttle, brake, remainingpercent[0], remainingpercent[1]");
      } else {
        nextDiskAttempt = millis() + INTERVAL_DISKATTEMPT;
        Console.print("]");
      }//Card failed, or not present"); }
    }
    if ((millis() - lastLogWrite > INTERVAL_LOGWRITE) && (diskOpen == 1) && (millis() - lastBMSPacket < 750)) {
      lastLogWrite += INTERVAL_LOGWRITE;
      logString = String(millis()/1000)+", "+String(voltage[0])+", ";
      logString += String(voltage[1])+", "+String(current[0])+", ";
      logString += String(current[1])+", "+String(speed)+", ";
      logString += String(throttle)+", "+String(brake)+", ";
      logString += String(remainingpercent[0])+", "+String(remainingpercent[1]);
      dataFile.println(logString);
      Console.println(logString);
      dataFile.flush();
    }
  } else if (diskOpen == 1) { // digitalRead(POWER_PIN) is false, so power down disk access ASAP
    dataFile.close();
    Console.print("dataFile.close()");
    diskOpen = 0;
  }

  if (millis() - lastStatusPrint > INTERVAL_STATUSPRINT) {
    printStatus();
    lastStatusPrint = millis();
  }
}

void sendes4Request() {
  static uint8_t lastSent = 0; // rotate requests
  if (lastSent == 0) { // send BMS1 request
    es4Serial.write("\x5A\xA5\x01\x3E\x22\x01\x31\x0A\x62\xFF"); // BMS1 request
    lastSent = 1;
  } else { // send BMS2 request
    es4Serial.write("\x5A\xA5\x01\x3E\x23\x01\x31\x0A\x61\xFF"); // BMS2 request
    lastSent = 0;
  }
}

void handleEs4Serial() {
  if (es4Serial.available()) {
    uint8_t inByte = es4Serial.read();
    switch (packetProgress) {
      case 0:
        packetAddress = 0; // clear the address field
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
        packetProgress = 0; // restart unless it turns out one of the addresses is found
        packetAddress = inByte;
        if (packetAddress == address_bms) {
          BMSPacketsCollected++;
          packetProgress = 4;
        }
        if (packetAddress == address_bms_request) {
          REQPacketsCollected++;
          packetProgress = 0; // we're not interested in these
        }
        if (packetAddress == address_x1) {
          X1PacketsCollected++;
          packetProgress = 0; // we're not interested in these
          delay(1); // avoid finishing early
          while (es4Serial.available()) {
            es4Serial.read();
          } // flush receive buffer
          if (packetLength == 2) sendes4Request(); // let's see what this does
        }
        if (packetAddress == address_esc) {
          ESCPacketsCollected++;
          packetProgress = 4;
        }
        if (packetAddress == address_ble) {
          BLEPacketsCollected++;
          packetProgress = 4;
        }
        break;
      case 4:
        packetProgress = 5; // i don't know what that byte is so we skip it
        if (packetAddress == address_ble) {
          if ((inByte & 254) != 0x64) { // if inByte not 64 or 65
            //Console.print("BLE:"+String(inByte,HEX));
            //while (es4Serial.available()) Console.print(":"+String(es4Serial.read(),HEX));
            //Console.println();
            packetProgress = 0;
          }
        }
        break;
      case 5:
        packetProgress = 0; // restart unless the following
        if (packetAddress == address_bms) {
          //Console.print("BMS:0x"+String(inByte,HEX));
          if (inByte == 0x31) {packetProgress = 6; }// the packet aimed at bmsdata[0x62]  }
          if (inByte == 0x30) {es4Serial.read(); es4Serial.read(); packetProgress = 6;} // take up two bytes since we're used to 0x31 here
        }
        if (packetAddress == address_esc) {
          if (inByte != 0xB0) {
            Console.print("ESC:"+String(inByte,HEX));
            delay(1);
            while (es4Serial.available()) Console.print(":"+String(es4Serial.read(),HEX));
            Console.println();
          }
          else { packetProgress = 6; }// the packet aimed at escdata[0x160]
        }
        if (packetAddress == address_ble) packetProgress = 6;
        break;
      case 6:
        if (packetAddress == address_bms) remainingcapacity = inByte;
        packetProgress += 1;
        break;
      case 7:
        if (packetAddress == address_ble) throttle = inByte;
        if (packetAddress == address_bms) remainingcapacity += (uint16_t)inByte << 8;
        packetProgress += 1;
        break;
      case 8:
        if (packetAddress == address_ble) {
          brake = inByte;
          packetProgress = 0;
        }
        if (packetAddress == address_bms) remainingpercent = inByte;
        packetProgress += 1;
        break;
      case 9:
        if (packetAddress == address_bms) remainingpercent += (uint16_t)inByte << 8;
        packetProgress += 1;
        break;
      case 10:
        if (packetAddress == address_bms) current = inByte;
        packetProgress += 1;
        break;
      case 11:
        if (packetAddress == address_bms) current += (uint16_t)inByte << 8;
        packetProgress += 1;
        break;
      case 12:
        if (packetAddress == address_bms) voltage = inByte;
        packetProgress += 1;
        break;
      case 13:
        if (packetAddress == address_bms) voltage += (uint16_t)inByte << 8;
        packetProgress += 1;
        break;
      case 14:
        if (packetAddress == address_bms) temperature[0] = inByte;
        packetProgress += 1;
        break;
      case 15:
        if (packetAddress == address_bms) temperature[1] = inByte;
        packetProgress += 1;
        break;
      case 16:
        if (packetAddress == address_bms) {
          lastBMSPacket = millis(); // store the time we got bms data
          packetProgress = 0; // start looking for a new packet
        } else if (packetAddress == address_esc) {
          speed = inByte; // lower byte of speed
          packetProgress = 17;
        }
        break;
      case 17:
        if (packetAddress == address_esc) {
          speed += (uint16_t)inByte << 8;
          packetProgress = 0; // we're done
        }
    }
  } // if (es4Serial.available())
} // handleEs4Serial()

void printStatus() {
  //for(int i = 0; i < 10; i++) Console.print(String(ESCreq[i],HEX)+":");
  Console.print("BMS:"+String(BMSPacketsCollected));
  Console.print(" REQ:"+String(REQPacketsCollected));
  Console.print(" ESC:"+String(ESCPacketsCollected));
  Console.print(" BLE:"+String(BLEPacketsCollected));
  Console.print(" X1:"+String(X1PacketsCollected));
  Console.print("\tlastBMS: "+String(millis() - lastBMSPacket));
  Console.print("\tspeed: "+String((int16_t)speed));
  Console.print("\tVolt: "+String(voltage));
  Console.print("\tThrot: "+String(throttle));
  Console.print("\tBrake: "+String(brake));
  Console.print("\tAmps: "+String(current));
  Console.print("\tBatt: "+String(remainingpercent)+"%\n");
}
