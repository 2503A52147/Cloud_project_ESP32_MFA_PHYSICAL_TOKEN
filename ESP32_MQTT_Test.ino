
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>
#include "secrets.h"

// ========== Wi-Fi ==========
const char* WIFI_SSID = "Your wifi name";
const char* WIFI_PASSWORD = "Your wifi password";

// ========== AWS IoT Core ==========
const char* AWS_IOT_ENDPOINT =
  "replace your aws end point here";

const char* MQTT_CLIENT_ID = "ESP32_MFA_Token";
const char* MQTT_TOPIC = "esp32/mfa/otp";

// ========== Hardware ==========
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define BUTTON_PIN 4

Adafruit_SSD1306 display(
  SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET
);

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

bool oledReady = false;

// ========== OTP settings ==========
char currentOTP[7] = "";
unsigned long otpGeneratedAt = 0;
bool otpActive = false;

const unsigned long OTP_VALIDITY_MS = 60000UL;

// ========== OLED display ==========
void showMessage(const char* line1, const char* line2 = "") {
  if (!oledReady) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println(line1);
  display.println();
  display.println(line2);
  display.display();
}

// ========== Wi-Fi connection ==========
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  showMessage("Connecting Wi-Fi", "Please wait...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < 20000UL) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWIFI CONNECTED");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("\nWIFI CONNECTION FAILED");
  showMessage("Wi-Fi FAILED", "Check credentials");
  return false;
}

// ========== AWS MQTT connection ==========
bool connectMQTT() {
  if (mqttClient.connected()) {
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  showMessage("Connecting AWS", "MQTT...");
  Serial.println("Connecting to AWS IoT Core...");

  if (mqttClient.connect(MQTT_CLIENT_ID)) {
    Serial.println("MQTT CONNECTED SUCCESSFULLY!");
    showMessage("AWS MQTT", "Connected");
    return true;
  }

  Serial.print("MQTT CONNECTION FAILED. State: ");
  Serial.println(mqttClient.state());
  showMessage("MQTT FAILED", "Check Serial");
  return false;
}

// ========== Generate and publish OTP ==========
void generateAndPublishOTP() {
  if (!connectWiFi() || !connectMQTT()) {
    Serial.println("OTP NOT PUBLISHED: connection unavailable.");
    showMessage("Connection error", "OTP not sent");
    return;
  }

  // Generate a six-digit demonstration OTP.
  uint32_t otp = 100000UL + (esp_random() % 900000UL);

  snprintf(
    currentOTP,
    sizeof(currentOTP),
    "%06lu",
    (unsigned long)otp
  );

  otpGeneratedAt = millis();
  otpActive = true;

  Serial.println();
  Serial.print("Generated OTP: ");
  Serial.println(currentOTP);

  showMessage("Your OTP:", currentOTP);

  // Publish the OTP to AWS IoT Core.
  char payload[100];

  snprintf(
    payload,
    sizeof(payload),
    "{\"device\":\"ESP32_MFA_Token\",\"otp\":\"%s\"}",
    currentOTP
  );

  if (mqttClient.publish(MQTT_TOPIC, payload)) {
    Serial.println("OTP PUBLISHED SUCCESSFULLY!");
    Serial.print("Topic: ");
    Serial.println(MQTT_TOPIC);
  } else {
    Serial.println("OTP PUBLISH FAILED!");
    showMessage("Publish failed", "Check connection");
  }

  Serial.println("OTP validity: 60 seconds");
  Serial.println("Enter the OTP in Serial Monitor and press Enter.");
}

// ========== Verify entered OTP ==========
void verifyEnteredOTP(const char* enteredOTP) {
  // Check that an OTP exists and has not expired.
  if (!otpActive ||
      millis() - otpGeneratedAt >= OTP_VALIDITY_MS) {
    otpActive = false;
    currentOTP[0] = '\0';

    Serial.println("OTP INVALID OR EXPIRED");
    showMessage("OTP expired", "Generate a new one");
    return;
  }

  // Accept exactly six numeric digits.
  if (strlen(enteredOTP) != 6) {
    Serial.println("Enter exactly six digits.");
    return;
  }

  for (int i = 0; i < 6; i++) {
    if (enteredOTP[i] < '0' || enteredOTP[i] > '9') {
      Serial.println("OTP must contain digits only.");
      return;
    }
  }

  if (strcmp(enteredOTP, currentOTP) == 0) {
    Serial.println("OTP VERIFIED SUCCESSFULLY!");
    Serial.println("DEMONSTRATION ACCESS GRANTED.");

    // A successful OTP cannot be reused.
    otpActive = false;
    currentOTP[0] = '\0';

    showMessage("OTP VERIFIED", "Access granted");
  } else {
    Serial.println("INCORRECT OTP. TRY AGAIN.");
    showMessage("Incorrect OTP", "Try again");
  }
}

// ========== Read Serial Monitor input ==========
void checkOTPInput() {
  static char input[16];
  static size_t index = 0;

  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (index > 0) {
        input[index] = '\0';
        verifyEnteredOTP(input);
        index = 0;
      }
    } else if (c >= '0' && c <= '9') {
      if (index < sizeof(input) - 1) {
        input[index++] = c;
      } else {
        index = 0;
        Serial.println("Input too long. Enter six digits.");
      }
    } else {
      index = 0;
      Serial.println("Enter digits only.");
    }
  }
}

// ========== Setup ==========
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin();  // Default ESP32 SDA=21, SCL=22

  oledReady = display.begin(
    SSD1306_SWITCHCAPVCC,
    0x3C
  );

  if (oledReady) {
    showMessage("ESP32 MFA Token", "Starting...");
  } else {
    Serial.println("OLED not detected. Check wiring/address.");
  }

  // Configure AWS TLS certificates from secrets.h.
  secureClient.setCACert(AWS_ROOT_CA);
  secureClient.setCertificate(DEVICE_CERT);
  secureClient.setPrivateKey(DEVICE_PRIVATE_KEY);

  mqttClient.setServer(AWS_IOT_ENDPOINT, 8883);
  mqttClient.setSocketTimeout(15);

  if (connectWiFi() && connectMQTT()) {
    showMessage("System Ready", "Press button");
    Serial.println("System ready. Press the physical button.");
  }
}

// ========== Main loop ==========
void loop() {
  // Maintain the Wi-Fi and MQTT connection.
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      connectMQTT();
    }

    if (mqttClient.connected()) {
      mqttClient.loop();
    }
  }

  // Detect a physical button press on GPIO 4.
  static bool lastButtonState = HIGH;
  static unsigned long lastPressTime = 0;

  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH &&
      currentButtonState == LOW &&
      millis() - lastPressTime > 300UL) {
    lastPressTime = millis();
    generateAndPublishOTP();
  }

  lastButtonState = currentButtonState;

  // Expire the OTP after 60 seconds.
  if (otpActive &&
      millis() - otpGeneratedAt >= OTP_VALIDITY_MS) {
    otpActive = false;
    currentOTP[0] = '\0';

    Serial.println("OTP EXPIRED. Generate a new OTP.");
    showMessage("OTP expired", "Press button again");
  }

  // Read the OTP entered through Serial Monitor.
  checkOTPInput();

  delay(10);
}
