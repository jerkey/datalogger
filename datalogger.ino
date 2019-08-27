/* SD card datalogger
 * SD card attached to SPI bus as follows:
          ATMEGA328  32U4
 ** MOSI - pin 11    16
 ** MISO - pin 12    14
 ** CLK  - pin 13    15
 ** CS   - pin 4     10 (or whatever you want, set chipSelect
 */
#define POWER_PIN 2 // high when power supply is on (shutdown disk if this goes low)

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
uint32_t lastDiskAttempt = 0; // when was the last time we attempted to open the disk
#define INTERVAL_DISKATTEMPT 2000 // how often to attempt

#define address_ble 0x20 //actively sent data by BLE Module with gas/brake & mode values
#define address_x1 0x21 //actively sent data by ?BLE? with status like led on/off, normal/ecomode...
#define address_esc 0x23 //sent by motor controller with speed and other data
#define address_bms_request 0x22 //used by apps to read data from bms
#define address_bms 0x25 //data from bms sent with this address (only passive if requested via address_bms_request)
uint16_t BMSPacketsCollected = 0; // how many BMS packets we've picked up ever
uint16_t REQPacketsCollected = 0; // how many BMS Request packets we've picked up ever
uint16_t ESCPacketsCollected = 0; // how many ESC packets we've picked up ever
uint16_t BLEPacketsCollected = 0; // how many BLE packets we've picked up ever
int8_t packetProgress = 0; // how far along in a decoded packet are we
int8_t packetAddress = 0; // which type of packet is it?
uint8_t packetLength = 0; // received packet length
uint16_t remainingcapacity; //offset 0x62-0x63
uint16_t remainingpercent; //offset 0x64-0x65
int16_t current; //offset 0x66-67 - negative = charging; /100 = Ampere
uint16_t voltage; //offset 0x68-69 /10 = Volt
uint16_t speed; // uint16_t realspeed = ((int16_t)speed<=-10000?(uint16_t)((int32_t)(int16_t)speed+(int32_t)65536):(int16_t)abs((int16_t)speed))
uint8_t temperature[2]; //offset 0x6A-0x6B -20 = °C
uint8_t throttle = 0; // received in address_ble packets
uint8_t brake = 0; // received in address_ble packets

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
  // while (!Console); // wait for serial port to connect. Needed for native USB port only
#endif

  M365Serial.begin(115200);

}

uint8_t diskOpen = 0;
String logString = ""; // make a string for assembling the data to log:
File dataFile;

void loop() {
  handleM365Serial(); // if bytes are available, deal with them

  if (digitalRead(POWER_PIN)) {
    if ((diskOpen == 0) && (millis() - lastDiskAttempt > INTERVAL_DISKATTEMPT)){ // Console.print("Initializing SD card...");
      lastDiskAttempt = millis();
      if (SD.begin(chipSelect)) { // see if the card is present and can be initialized:
        Console.println("card initialized.");
        diskOpen = 1;
        lastLogWrite = millis(); // time starts now
        dataFile = SD.open("datalog.txt", FILE_WRITE);
      } else { Console.print("x"); }//Card failed, or not present"); }
    }
    if ((millis() - lastLogWrite > INTERVAL_LOGWRITE) && (diskOpen == 1)) {
      lastLogWrite += INTERVAL_LOGWRITE;
      logString = String(millis()/1000)+", "+String(voltage)+", ";
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

void handleM365Serial() {
  if (M365Serial.available()) {
    uint8_t inByte = M365Serial.read();
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
        if (packetAddress == address_ble) if ((inByte & 254) != 0x64) packetProgress = 0;
        break;
      case 5:
        packetProgress = 0; // restart unless the following
        if (packetAddress == address_bms) {
          //Console.print("BMS:0x"+String(inByte,HEX));
          if (inByte == 0x31) {packetProgress = 6; }// the packet aimed at bmsdata[0x62]  }
          if (inByte == 0x30) {M365Serial.read(); M365Serial.read(); packetProgress = 6;} // take up two bytes since we're used to 0x31 here
        }
        if (packetAddress == address_esc) {
          if (inByte != 0xB0) { } // Console.print("ESC:0x"+String(inByte,HEX)); }
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
  } // if (M365Serial.available())
} // handleM365Serial()

void printStatus() {
  Console.print("BMS:"+String(BMSPacketsCollected));
  Console.print(" REQ:"+String(REQPacketsCollected));
  Console.print(" ESC:"+String(ESCPacketsCollected));
  Console.print(" BLE:"+String(BLEPacketsCollected));
  Console.print("\tlastBMS: "+String(millis() - lastBMSPacket));
  Console.print("\tspeed: "+String((int16_t)speed));
  Console.print("\tVolt: "+String(voltage));
  Console.print("\tThrot: "+String(throttle));
  Console.print("\tBrake: "+String(brake));
  Console.print("\tAmps: "+String(current));
  Console.print("\tBatt: "+String(remainingpercent)+"%\n");
}
