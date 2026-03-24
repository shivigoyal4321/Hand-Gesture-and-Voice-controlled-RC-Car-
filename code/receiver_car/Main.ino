#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <esp_now.h>

// --- Mode Selector Pin ---
const int MODE_PIN = 4;  // Connect to GND for ESP-NOW, leave open for Wi-Fi
bool isWiFiMode = true;  // Keeps track of which mode we are in


// --- Pin Definitions ---
const int TRIG_PIN = 22; 
const int ECHO_PIN = 23;
const int LED_PIN = 2;
const int IN1 = 27;
const int IN2 = 26;
const int ENA = 14;
const int IN3 = 25;
const int IN4 = 33;
const int ENB = 32;

// --- PWM Settings (Hardware LEDC) ---
#define PWM_FREQ 1000
#define PWM_RES 8

// --- Ultrasonic & Movement State ---
unsigned long lastUltrasonicCheck = 0;
const unsigned long ultrasonicInterval = 250;  // Faster check for driving safely
const int OBSTACLE_DISTANCE = 25;
bool obstacleDetected = false;
bool isMoving = false;
bool isBackingUp = false;  // NEW: Tracks if we are in reverse!

// --- ESP-NOW Settings ---
#define SIGNAL_TIMEOUT 500  // 500ms failsafe
unsigned long lastReceiveTime = 0;
int deadzone = 130;

typedef struct struct_message {
  int16_t x;
  int16_t y;
} struct_message;
struct_message incomingData;

// --- Wi-Fi Credentials (AP Mode) ---
const char* ssid = "SG_ESP32_RobotCar";
const char* password = "Hello123";
WebServer server(80);

const char* htmlPage = R"rawliteral(
<!doctype html>
<html>
  <head>
    <meta
      name="viewport"
      content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no"
    />
    <meta name="theme-color" content="#222" />

    <style>
      html,
      body {
        margin: 0;
        padding: 0;
        min-height: 100vh;
        background-color: #222;
        font-family: Arial, sans-serif;
        color: white;
        text-align: center;
        -webkit-user-select: none;
        user-select: none;
      }

      h1 {
        font-size: 25px;
      }

      .container {
        padding-bottom: 20px;
      }

      .grid {
        display: grid;
        grid-template-columns: repeat(3, 1fr);
        gap: 10px;
        width: 95%;
        max-width: 380px;
        margin: auto;
      }

      .btn {
        padding: 20px 0;
        font-size: 18px;
        border-radius: 12px;
        background-color: #007bff;
        color: white;
        border: none;
        width: 100%;
        touch-action: manipulation;
        transition:
          background-color 0.2s ease,
          transform 0.06s ease;
      }

      /* NEW: Styling for the Voice Button */
      .btn-voice {
        background-color: #28a745;
        grid-column: span 3; /* Makes it stretch across the bottom */
        margin-top: 10px;
        font-weight: bold;
      }

      .btn.pressed {
        background-color: #0056b3;
        transform: scale(0.96);
      }

      .btn-stop {
        background-color: #dc3545;
        font-weight: bold;
      }

      .btn-stop.pressed {
        background-color: #a71d2a;
      }

      .empty {
        visibility: hidden;
      }

      /* Landscape improvement */
      @media (orientation: landscape) {
        .grid {
          max-width: 600px;
          gap: 15px;
        }

        .btn {
          padding: 25px 0;
          font-size: 20px;
        }
      }
    </style>
  </head>

  <body oncontextmenu="return false;">
    <div class="container">
      <h1>Dual Input RC</h1>

      <div class="grid">
        <div class="empty"></div>
        <button class="btn" data-endpoint="/forward">Forward</button>
        <div class="empty"></div>

        <button class="btn" data-endpoint="/left">Left</button>
        <button class="btn btn-stop" data-endpoint="/stop">STOP</button>
        <button class="btn" data-endpoint="/right">Right</button>

        <div class="empty"></div>
        <button class="btn" data-endpoint="/backward">Back</button>
        <div class="empty"></div>
      </div>
    </div>

    <script>
      let lastCommand = "";

      document.querySelectorAll(".btn").forEach((btn) => {
        btn.addEventListener("touchstart", (e) => {
          e.preventDefault();
          btn.classList.add("pressed");
          send(btn.dataset.endpoint);
        });

        btn.addEventListener("touchend", () => {
          btn.classList.remove("pressed");
          send("/stop");
        });

        btn.addEventListener("touchcancel", () => {
          btn.classList.remove("pressed");
          send("/stop");
        });

        btn.addEventListener("mousedown", () => {
          btn.classList.add("pressed");
          send(btn.dataset.endpoint);
        });

        btn.addEventListener("mouseup", () => {
          btn.classList.remove("pressed");
          send("/stop");
        });

        btn.addEventListener("mouseleave", () => {
          btn.classList.remove("pressed");
          send("/stop");
        });
      });

      function send(endpoint) {
        if (lastCommand === endpoint && endpoint !== "/stop") return;
        lastCommand = endpoint;
        fetch(endpoint).catch((err) => console.error(err));
      }
    </script>
  </body>
</html>

)rawliteral";

// ==========================================
// CORE MOTOR DRIVER (Hardware PWM)
// ==========================================
void setMotor(int pwmChannel, int dirPin1, int dirPin2, int speed) {
  // --- NEW: Minimum speed to overcome motor friction and stop whining ---
  int minSpeed = 110; 

  if (speed > 0) {
    if (speed < minSpeed) speed = minSpeed; // Bump up if too slow
    digitalWrite(dirPin1, HIGH);
    digitalWrite(dirPin2, LOW);
    ledcWrite(pwmChannel, speed);
  } else if (speed < 0) {
    if (speed > -minSpeed) speed = -minSpeed; // Bump up if too slow (reverse)
    digitalWrite(dirPin1, LOW);
    digitalWrite(dirPin2, HIGH);
    ledcWrite(pwmChannel, -speed);
  } else {
    digitalWrite(dirPin1, LOW);
    digitalWrite(dirPin2, LOW);
    ledcWrite(pwmChannel, 0);
  }
}

void stopMotors() {
  setMotor(ENA, IN1, IN2, 0);
  setMotor(ENB, IN3, IN4, 0);
  digitalWrite(LED_PIN, LOW);
  Serial.println("PHONE -> Action: STOPPED");
  isMoving = false;
  isBackingUp = false;  // Always reset reverse tracker when we stop!
}

// ==========================================
// WI-FI MODE CONTROLS (Max Speed: 255)
// ==========================================
void moveForward() {
  if (obstacleDetected) return;  // Block forward if wall is near
  setMotor(ENA, IN1, IN2, 255);
  setMotor(ENB, IN3, IN4, 255);
  digitalWrite(LED_PIN, HIGH);
  isMoving = true;
  isBackingUp = false;
  Serial.println("PHONE -> Action: FORWARD | Speed: 255");
  if (isWiFiMode) server.send(200, "text/plain", "Forward");
}
void moveBackward() {
  setMotor(ENA, IN1, IN2, -255);
  setMotor(ENB, IN3, IN4, -255);
  digitalWrite(LED_PIN, HIGH);
  isMoving = true;
  isBackingUp = true;
  Serial.println("PHONE -> Action: BACKWARD | Speed: -255");
  if (isWiFiMode) server.send(200, "text/plain", "Backward");
}
void turnLeft() {
  if (obstacleDetected) return;  // Block if wall is near
  setMotor(ENA, IN1, IN2, -255);
  setMotor(ENB, IN3, IN4, 255);
  digitalWrite(LED_PIN, HIGH);
  isMoving = true;
  isBackingUp = false;
  Serial.println("PHONE -> Action: LEFT | Left: -255, Right: 255");
  if (isWiFiMode) server.send(200, "text/plain", "Left");
}
void turnRight() {
  if (obstacleDetected) return;  // Block if wall is near
  setMotor(ENA, IN1, IN2, 255);
  setMotor(ENB, IN3, IN4, -255);
  digitalWrite(LED_PIN, HIGH);
  isMoving = true;
  isBackingUp = false;
  Serial.println("PHONE -> Action: RIGHT | Left: 255, Right: -255");
  if (isWiFiMode) server.send(200, "text/plain", "Right");
}
void stopWiFiMotors() {
  // Check if we are ALREADY stopped to ignore the phone's ghost click!
  if (!isMoving) {
    if (isWiFiMode) server.send(200, "text/plain", "Already Stopped");
    return;
  }

  // Apply brakes and print the PHONE message HERE
  stopMotors();
  if (isWiFiMode) server.send(200, "text/plain", "Stopped");
}

// ==========================================
// ESP-NOW TANK MIXING (Glove Controls)
// ==========================================
void driveMotors(int16_t x, int16_t y) {
  if (abs(x) < deadzone) x = 0;
  if (abs(y) < deadzone) y = 0;

  // Track state for the safety sensor
  isBackingUp = (y < 0);

  // Obstacle avoidance: Stop forward Pitch (y) if wall is too close
  if (obstacleDetected && y > 0) { y = 0; }

  int16_t motorA = y + x;
  int16_t motorB = y - x;

  // motorA = constrain(motorA, -1000, 1000);
  // motorB = constrain(motorB, -1000, 1000);

  // motorA = map(motorA, -1000, 1000, -255, 255);
  // motorB = map(motorB, -1000, 1000, -255, 255);

  // Lowering the 1000 cap to 500 means you hit max speed with half the tilt!
  motorA = constrain(motorA, -750, 750);
  motorB = constrain(motorB, -750, 750);

  motorA = map(motorA, -750, 750, -255, 255);
  motorB = map(motorB, -750, 750, -255, 255);

  setMotor(ENA, IN1, IN2, motorA);
  setMotor(ENB, IN3, IN4, motorB);

  if (motorA != 0 || motorB != 0) {
    isMoving = true;
    digitalWrite(LED_PIN, HIGH);
    // CHECK GLOVE SPEED
    // --- ADDED: Direction Logging ---
    if (y > 0) Serial.println("GLOVE -> Moving: FORWARD");
    else if (y < 0) Serial.println("GLOVE -> Moving: BACKWARD");
    else if (x > 0) Serial.println("GLOVE -> Moving: RIGHT");
    else if (x < 0) Serial.println("GLOVE -> Moving: LEFT");
    Serial.printf("GLOVE -> Left Speed: %d | Right Speed: %d\n", motorA, motorB);
  } else {
    isMoving = false;
    digitalWrite(LED_PIN, LOW);
  }
}
// --- NEW ESP-NOW V3 CALLBACK SIGNATURE ---
void onReceive(const esp_now_recv_info* info, const uint8_t* incomingDataRaw, int len) {
  if (len != sizeof(struct_message)) return;
  memcpy(&incomingData, incomingDataRaw, sizeof(incomingData));
  lastReceiveTime = millis();
  driveMotors(incomingData.y, incomingData.x);
}


// ==========================================
// ULTRASONIC SENSOR LOGIC
// ==========================================
long getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  return (pulseIn(ECHO_PIN, HIGH, 30000) * 0.034 / 2);
}

void setup() {
  delay(500);
  Serial.begin(115200);

  pinMode(MODE_PIN, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // --- NEW Hardware PWM Setup (Core V3) ---
  ledcAttach(ENA, PWM_FREQ, PWM_RES);
  ledcAttach(ENB, PWM_FREQ, PWM_RES);

  stopMotors();

  if (digitalRead(MODE_PIN) == HIGH) {
    // BOOT INTO WI-FI MODE
    isWiFiMode = true;
    Serial.println("Booting Wi-Fi Mode");
    WiFi.softAP(ssid, password);
    server.on("/", []() {
      server.send(200, "text/html", htmlPage);
    });
    server.on("/forward", moveForward);
    server.on("/backward", moveBackward);
    server.on("/left", turnLeft);
    server.on("/right", turnRight);
    server.on("/stop", stopWiFiMotors);
    server.begin();
  } else {
    // BOOT INTO ESP-NOW MODE
    isWiFiMode = false;
    Serial.println("Booting ESP-NOW Mode");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Force ESP-NOW to Channel 1 to match your Glove Transmitter
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() == ESP_OK) {
      esp_now_register_recv_cb(onReceive);
      Serial.println("ESP-NOW Ready");
    } else {
      Serial.println("ESP-NOW Init Failed");
    }
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Handle Wi-Fi Clients (if active)
  if (isWiFiMode) {
    server.handleClient();
    // Safety: Stop if client disconnects
    if (WiFi.softAPgetStationNum() == 0 && isMoving) stopMotors();
  }

  // 2. Handle ESP-NOW Failsafe (if active)
  else {
    if (currentMillis - lastReceiveTime > SIGNAL_TIMEOUT && isMoving) {
      Serial.println("Signal Lost - Brakes Applied!");
      stopMotors();
    }
  }

  // 3. Handle Ultrasonic Safety Sensor
  if (currentMillis - lastUltrasonicCheck >= ultrasonicInterval) {
    lastUltrasonicCheck = currentMillis;
    long distance = getDistance();

    if (distance > 0 && distance < OBSTACLE_DISTANCE) {
      // --- NEW: Print warning ONLY when the obstacle first appears ---
      if (!obstacleDetected) {
        Serial.printf("OBSTACLE DETECTED! Distance: %ld cm. Applying Brakes!\n", distance);
        obstacleDetected = true;
      }
      // If we are currently moving forward in Wi-Fi mode, stop.
      // (ESP-NOW handles its own stopping inside driveMotors)
      if (isMoving && !isBackingUp) stopMotors();
    } else {
      // --- NEW: Print clearance ONLY when the obstacle is removed ---
      if (obstacleDetected) {
        Serial.println("Path Clear!");
      }
      obstacleDetected = false;
    }
  }
}