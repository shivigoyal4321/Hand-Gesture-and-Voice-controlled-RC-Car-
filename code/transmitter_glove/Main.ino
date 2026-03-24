#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Wire.h>
#include "MPU6050.h"

MPU6050 mpu;
// Receiver MAC Address
uint8_t receiverAddress[] = { 0xB4, 0xBF, 0xE9, 0x06, 0xAD, 0x4C };

// Data structure
typedef struct struct_message {
  int16_t x;
  int16_t y;
} struct_message;

struct_message myData;

// smoothing variables
float filteredX = 0;
float filteredY = 0;
float alpha = 0.7;

// callback when data sent
void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.print("Sent | X: ");
    Serial.print(myData.x);
    Serial.print("  Y: ");
    Serial.println(myData.y);
  } else {
    Serial.println("Send Failed");
  }
}

void setup() {

Serial.begin(115200);

  // 1. Initialize I2C and give it a moment to stabilize
  Wire.begin(21, 22); 
  delay(1000); 

  // 2. Initialize the MPU
  mpu.initialize();
  
  // 3. Force the MPU to wake up from sleep mode
  mpu.setSleepEnabled(false); 

  // 4. Verify we can actually read data instead of checking a specific chip ID
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  
  if (ax == 0 && ay == 0 && az == 0) {
    Serial.println("Warning: Sensor is not sending data!");
  } else {
    Serial.println("MPU6050 Initialized and Data Readable!");
  }

  // ESP-NOW setup
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (1)
      ;
  }

  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Peer add failed");
    while (1)
      ;
  }

  Serial.println("ESP-NOW Ready");
}

void loop() {

  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  Serial.printf("RAW -> AX: %d | AY: %d | AZ: %d\n", ax, ay, az);
  // calculate tilt angles
  float angleX = atan2(ay, az) * 180 / PI;
  float angleY = atan2(ax, az) * 180 / PI;

  // limit tilt range
  angleX = constrain(angleX, -45, 45);
  angleY = constrain(angleY, -45, 45);

  // map to joystick range
  int rawX = map(angleX, -45, 45, -1000, 1000);
  int rawY = map(angleY, -45, 45, -1000, 1000);

  // smoothing filter
  filteredX = alpha * filteredX + (1 - alpha) * rawX;
  filteredY = alpha * filteredY + (1 - alpha) * rawY;

  int x = filteredX;
  int y = filteredY;

  // curved response
  x = x * abs(x) / 1000;
  y = y * abs(y) / 1000;

  myData.x = x;
  myData.y = y;

  // send data
  esp_now_send(receiverAddress, (uint8_t *)&myData, sizeof(myData));

  Serial.print("X: ");
  Serial.print(myData.x);
  Serial.print(" Y: ");
  Serial.println(myData.y);

  delay(30);
}