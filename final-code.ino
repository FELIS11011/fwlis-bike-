#include <Wire.h>
#include <MPU6050.h>
#include <TinyGPSPlus.h>
#include <SD.h>
#include <SPI.h>

#define SDA_PIN     21
#define SCL_PIN     22
#define GPS_RX_PIN  16
#define GPS_TX_PIN  17
#define GPS_BAUD    9600
#define SD_CS_PIN   5
#define SD_SCK      18
#define SD_MOSI     23
#define SD_MISO     19
#define LED_PIN     2

MPU6050 mpu;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

File   logFile;
bool   fileOpen  = false;
bool   sdReady   = false;
int    pointCount = 0;
String filename  = "/ride.csv";

unsigned long startTime   = 0;
unsigned long lastLog     = 0;
unsigned long lastBlink   = 0;
bool ledState = false;

void openLogFile() {
  if (gps.date.isValid() && gps.time.isValid()) {
    char buf[40];
    sprintf(buf, "/ride_%04u%02u%02u_%02u%02u%02u.csv",
      gps.date.year(), gps.date.month(), gps.date.day(),
      gps.time.hour(), gps.time.minute(), gps.time.second());
    filename = String(buf);
  }
  File f = SD.open(filename, FILE_WRITE);
  if (!f) {
    Serial.println("ERROR: Could not create log file!");
    return;
  }
  // full raw header
  f.println("point,utc_time,"
            "latitude,longitude,altitude_m,"
            "speed_kmh,course_deg,"
            "satellites,hdop,pdop,"
            "ax_raw,ay_raw,az_raw,"
            "gx_raw,gy_raw,gz_raw,"
            "accel_mag_g,"
            "temp_c");
  f.close();
  fileOpen = true;
  Serial.printf("Logging to: %s\n", filename.c_str());
}

void writeDataPoint() {
  if (!fileOpen) return;

  // raw IMU data
  int16_t ax, ay, az, gx, gy, gz;
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
  float accelMag = sqrt((float)ax*ax + (float)ay*ay + (float)az*az) / 16384.0;
  float tempC = mpu.getTemperature() / 340.0 + 36.53;

  char utc[25];
  sprintf(utc, "%04u-%02u-%02uT%02u:%02u:%02uZ",
    gps.date.year(), gps.date.month(), gps.date.day(),
    gps.time.hour(), gps.time.minute(), gps.time.second());

  char row[300];
  sprintf(row, "%d,%s,"
               "%.7f,%.7f,%.2f,"
               "%.3f,%.2f,"
               "%u,%.2f,%.2f,"
               "%d,%d,%d,"
               "%d,%d,%d,"
               "%.5f,"
               "%.2f",
    pointCount, utc,
    gps.location.lat(), gps.location.lng(), gps.altitude.meters(),
    gps.speed.kmph(), gps.course.deg(),
    gps.satellites.value(), gps.hdop.hdop(), gps.hdop.hdop(),
    ax, ay, az,
    gx, gy, gz,
    accelMag,
    tempC
  );

  File f = SD.open(filename, FILE_APPEND);
  if (f) {
    f.println(row);
    f.close();
    pointCount++;
    Serial.printf("Point #%d | Sats:%d | HDOP:%.1f | %.7f,%.7f | %.1fkm/h | ax:%d ay:%d az:%d | gx:%d gy:%d gz:%d | Mag:%.3f\n",
      pointCount,
      gps.satellites.value(), gps.hdop.hdop(),
      gps.location.lat(), gps.location.lng(),
      gps.speed.kmph(),
      ax, ay, az, gx, gy, gz, accelMag);
  } else {
    Serial.println("ERROR: append failed!");
  }
}

void listFiles() {
  File root = SD.open("/");
  File f = root.openNextFile();
  bool any = false;
  Serial.println("--- Files on SD ---");
  while (f) {
    Serial.printf("  %s  (%u bytes)\n", f.name(), f.size());
    f = root.openNextFile();
    any = true;
  }
  if (!any) Serial.println("  No files found.");
  Serial.println("-------------------");
}

void deleteFile() {
  fileOpen = false;
  pointCount = 0;
  SD.remove(filename);
  Serial.println("File deleted. Ready for new ride.");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("\n===== ESP32 Bike Module =====");

  Serial.print("SD card... ");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("FAILED");
  } else {
    Serial.printf("OK  |  %llu MB\n", SD.cardSize() / (1024 * 1024));
    sdReady = true;
    listFiles();
  }

  Serial.print("IMU... ");
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  mpu.initialize();
  mpu.setSleepEnabled(false);
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_4);  // ±4g range for bike
  mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_500);  // ±500deg/s range
  if (mpu.testConnection()) Serial.println("OK");
  else Serial.println("FAILED");

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("GPS started.");
  Serial.println("Slow blink = searching | Fast blink = fix, logging started");
  Serial.println("Commands: l=list files  x=delete file");

  startTime = millis();
}

void loop() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  // LED
  unsigned long blinkRate = gps.location.isValid() ? 200 : 1000;
  if (millis() - lastBlink >= blinkRate) {
    lastBlink = millis();
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }

  // commands
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'l') listFiles();
    if (c == 'x') deleteFile();
  }

  // log every 1 second (more data points than before)
  if (millis() - lastLog >= 1000) {
    lastLog = millis();
    if (millis() - startTime < 3000) return;

    if (gps.location.isValid() && gps.satellites.value() >= 4) {
      if (!fileOpen && sdReady) openLogFile();
      writeDataPoint();
    } else {
      Serial.printf("No fix | Sats:%d | Chars:%lu\n",
        gps.satellites.value(), gps.charsProcessed());
    }
  }
}