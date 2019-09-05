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

#define NINEBOT_ADDR_ESC 0x20
#define NINEBOT_ADDR_BLE 0x21
#define NINEBOT_ADDR_BMS1 0x22
#define NINEBOT_ADDR_BMS2 0x23
#define NINEBOT_ADDR_APP2 0x3E
uint16_t ESCPacketsCollected = 0;
uint16_t BLEPacketsCollected = 0;
uint16_t BMS1PacketsCollected = 0;
uint16_t BMS2PacketsCollected  = 0;
int8_t packetProgress = 0; // how far along in a decoded packet are we
int8_t sender = 0; // who is sending this packet
int8_t receiver = 0; // who is it aimed at
int8_t command = 0;
int8_t cmdArg = 0;
uint8_t payloadLen = 0; // received packet length
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
        packetProgress = (inByte == 0x5A) ? 1 : 0; // we're looking for 0xA5 now
        break;
      case 1:
        packetProgress = (inByte == 0xA5) ? 2 : 0; // we're gonna grab packetlength next
        break;
      case 2:
        payloadLen = inByte;
        packetProgress = 3;
        break;
      case 3:
        packetProgress = 0; // restart unless it turns out one of the addresses is found
        sender = inByte;
        if (sender == NINEBOT_ADDR_BMS1) { packetProgress = 4; }
        if (sender == NINEBOT_ADDR_BMS2) { packetProgress = 4; }
        if (sender == NINEBOT_ADDR_ESC) { packetProgress = 4; }
        if (sender == NINEBOT_ADDR_BLE) { packetProgress = 4; }
        break;
      case 4:
        receiver = inByte;
        packetProgress = 5;
        break;
      case 5:
        command = inByte;
        packetProgress = 6;
        break;
      case 6:
        cmdArg = inByte;
        packetProgress = 7;
        break;
      case 7:
        packetProgress = 0; // restart collection cycle after the following
        if (sender==NINEBOT_ADDR_BLE && receiver==NINEBOT_ADDR_ESC && command==0x64 && cmdArg==0) {
          es4Serial.read();
          throttle = es4Serial.read();
          brake = es4Serial.read();
          BLEPacketsCollected++;
        }
        if (sender==NINEBOT_ADDR_ESC && receiver==NINEBOT_ADDR_BLE && command==0x64 && cmdArg==0) {
          for (int i=0; i<4; i++) es4Serial.read();
          speed = es4Serial.read();
          ESCPacketsCollected++;
          delay(1); // avoid finishing early
          while (es4Serial.available()) {
            delay(1); // this prevents floaters (incomplete flush)
            es4Serial.read();
          } // flush receive buffer
          sendes4Request(); // now we inject our BMS request packet
        }
        if (sender==NINEBOT_ADDR_BMS1 && receiver==NINEBOT_ADDR_APP2 && command==4 && cmdArg==0x31) {
          es4Serial.read(); // number of bytes coming (not incl. checksum) is 0x0A
          remainingcapacity[0] = es4Serial.read();
          remainingcapacity[0] += (uint16_t)es4Serial.read() << 8;
          remainingpercent[0] = es4Serial.read();
          remainingpercent[0] += (uint16_t)es4Serial.read() << 8;
          current[0] = es4Serial.read();
          current[0] += (uint16_t)es4Serial.read() << 8;
          voltage[0] = es4Serial.read();
          voltage[0] += (uint16_t)es4Serial.read() << 8;
          BMS1PacketsCollected++;
          lastBMSPacket = millis(); // store the time we got bms data
        }
        if (sender==NINEBOT_ADDR_BMS2 && receiver==NINEBOT_ADDR_APP2 && command==4 && cmdArg==0x31) {
          es4Serial.read(); // number of bytes coming (not incl. checksum) is 0x0A
          remainingcapacity[1] = es4Serial.read();
          remainingcapacity[1] += (uint16_t)es4Serial.read() << 8;
          remainingpercent[1] = es4Serial.read();
          remainingpercent[1] += (uint16_t)es4Serial.read() << 8;
          current[1] = es4Serial.read();
          current[1] += (uint16_t)es4Serial.read() << 8;
          voltage[1] = es4Serial.read();
          voltage[1] += (uint16_t)es4Serial.read() << 8;
          BMS2PacketsCollected++;
          lastBMSPacket = millis(); // store the time we got bms data
        }
        break;
    }
  } // if (es4Serial.available())
} // handleEs4Serial()

void printStatus() {
  Console.print("BMS1:"+String(BMS1PacketsCollected));
  Console.print(" BMS2:"+String(BMS2PacketsCollected));
  Console.print(" ESC:"+String(ESCPacketsCollected));
  Console.print(" BLE:"+String(BLEPacketsCollected));
  Console.print("\tlastBMS: "+String(millis() - lastBMSPacket));
  Console.print("\tVolt: "+String(voltage[0])+"/"+String(voltage[1]));
  Console.print("\tAmps: "+String(current[0])+"/"+String(current[1]));
  Console.print("\tspeed: "+String(speed));
  Console.print("\tThrot: "+String(throttle));
  Console.print("\tBrake: "+String(brake));
  Console.print("\tBatt: "+String(remainingpercent[0])+"%/"+String(remainingpercent[1])+"%\n");
}
