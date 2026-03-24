#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Wire.h>
#include "MPU6050.h"
MPU6050 mpu;

void setup() {
  Serial.begin(115200);

  // 1. Initialize I2C and give it a moment
  Wire.begin(21, 22); 
  delay(1000); 

  // 2. Initialize the MPU manually
  mpu.initialize();
  
  // 3. Force the MPU to wake up from sleep mode
  mpu.setSleepEnabled(false); 

  // 4. Instead of testing the ID, let's test if we can read an acceleration value
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  
  if (ax == 0 && ay == 0 && az == 0) {
    Serial.println("Warning: Sensor is initialized but returning zero data. Check wiring!");
  } else {
    Serial.println("MPU6050 Initialized and Data Readable!");
  }

  // ... (rest of your ESP-NOW setup)
}
void loop() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);

  // Print raw values directly from the sensor
  Serial.printf("RAW -> AX: %d | AY: %d | AZ: %d\n", ax, ay, az);

  // Send "dummy" data to keep the link alive, but we only care about the print
  myData.x = 0; 
  myData.y = 0;
  esp_now_send(receiverAddress, (uint8_t *)&myData, sizeof(myData));

  delay(500); // Slowed down so you can read the screen
}