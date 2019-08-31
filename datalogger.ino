/* SD card datalogger
 * SD card attached to SPI bus as follows:
          ATMEGA328  32U4
 ** MOSI - pin 11    16
 ** MISO - pin 12    14
 ** CLK  - pin 13    15
 ** CS   - pin 4     10 (or whatever you want, set chipSelect
 */
#define POWER_PIN 2 // high when power supply is on (shutdown disk if this goes low)
#define THROTTLE_PIN A0 // 
#define BRAKE_PIN A1 // 
#define VOLTAGE_PIN A2 // 
#define CURRENT_PIN A3 // 
#define ENCODER_PIN 7 // triggers an interrupt to count movement

#include <SPI.h>
#include <SD.h>

const int chipSelect = 10; // this is the SS/CS pin we choose to use

uint32_t lastLogEntry = 0; // when was the last time we saved a log entry
uint32_t lastSpeedCalc = 0; // when was the last time we calculated speed
#define INTERVAL_SPEEDCALC 1000 // how often to calculate speed
uint32_t lastStatusPrint = 0; // when was the last time we printed our status
#define INTERVAL_STATUSPRINT 1000 // how often to print our status
uint32_t lastFilesystemFlush = 0; // when was the last time we flushed filesystem
#define INTERVAL_FILESYSTEMFLUSH 1000 // how often to flush filesystem
uint32_t lastLogWrite = 0; // when was the last time we wrote log
#define INTERVAL_LOGWRITE 1000 // how often to write log
uint32_t nextDiskAttempt = 0; // when is the next time we will attempt to open the disk
#define INTERVAL_DISKATTEMPT 5000 // how often to attempt to open disk

int16_t current; // 100 = 1 Ampere (negative = charging)
uint16_t voltage; // 100 = 1 volt
uint16_t throttle = 0;
uint16_t brake = 0;

uint16_t analogAdds = 0; // how many times we've oversampled into the below values
int32_t currentAdder = 0;
uint32_t voltageAdder = 0;
uint32_t throttleAdder = 0;
uint32_t brakeAdder = 0;
uint32_t encoderCount = 0; // counts number of motor pulses since last cleared
uint32_t speed;

#define Console Serial

void setup() {
  Console.begin(115200); // native CDC USB serial

  attachInterrupt(4,encoder,RISING); // digital pin 7 on Leonardo is interrupt 4 https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
}

uint8_t diskOpen = 0;
String logString = ""; // make a string for assembling the data to log:
File dataFile;

void loop() {
  readAnalogs(); // sample analog values continuously into adders
  updateSpeedAndAnalogs(); // calculate speed from interrupt-driven counter and divide analog oversamplers to get precise values

  if (digitalRead(POWER_PIN)) {
    if ((diskOpen == 0) && (millis() > nextDiskAttempt)){ // Console.print("Initializing SD card...");
      Console.print("[");
      if (SD.begin(chipSelect)) { // see if the card is present and can be initialized:
        Console.println("card initialized]");
        diskOpen = 1;
        lastLogWrite = millis(); // time starts now
        dataFile = SD.open("analogs.txt", FILE_WRITE);
        dataFile.println("time, voltage, current, speed, throttle, brake");
      } else {
        nextDiskAttempt = millis() + INTERVAL_DISKATTEMPT;
        Console.print("]");
      }//Card failed, or not present"); }
    }
    if ((millis() - lastLogWrite > INTERVAL_LOGWRITE) && (diskOpen == 1)) {
      lastLogWrite += INTERVAL_LOGWRITE;
      logString = String(millis()/1000)+", "+String(voltage)+", ";
      logString += String(current)+", "+String(speed)+", ";
      logString += String(throttle)+", "+String(brake);
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

void encoder() {
  encoderCount++;
}

void updateSpeedAndAnalogs() {
  uint32_t interval = millis() - lastSpeedCalc;
  if (interval > INTERVAL_SPEEDCALC) {
    lastSpeedCalc = millis();
    speed = (encoderCount * 1000) / interval; // speed is pulses per second
    encoderCount = 0; // reset the counter
    updateAnalogs(); // divide analog oversamplers to get precise values
  }
}

void readAnalogs() {
  voltageAdder += analogRead(VOLTAGE_PIN);
  currentAdder += analogRead(CURRENT_PIN) - 512; // current sensor centers at Vcc/2 for 0 current
  throttleAdder += analogRead(THROTTLE_PIN);
  brakeAdder += analogRead(BRAKE_PIN);
  analogAdds++;
}

void updateAnalogs() { // divide analog oversamplers to get precise values
  voltage = voltageAdder / analogAdds;
  current = currentAdder / analogAdds; // this is an integer because current can be negative
  throttle = throttleAdder / analogAdds;
  brake = brakeAdder / analogAdds;
  currentAdder = 0;
  voltageAdder = 0;
  throttleAdder = 0;
  brakeAdder = 0;
  analogAdds = 0;
}

void printStatus() {
  Console.print("time: "+String(millis() / 1000));
  Console.print("\tspeed: "+String(speed));
  Console.print("\tVolt: "+String(voltage));
  Console.print("\tAmps: "+String(current));
  Console.print("\tThrot: "+String(throttle));
  Console.print("\tBrake: "+String(brake)+"\n");
}
