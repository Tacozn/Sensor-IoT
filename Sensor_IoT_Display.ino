#include <EEPROM.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Firebase_ESP_Client.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// Include Firebase helper files
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// OLED Display Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32  // Using 64 pixel height for more display space
#define OLED_RESET -1
#define I2C_SDA 21        // SDA pin for OLED
#define I2C_SCL 22        // SCL pin for OLED
#define SCREEN_ADDRESS 0x3C

// Firebase Configuration
#define API_KEY "AIzaSyB0ioUG57hMvo912MEsql3uYv2OOGfwDLc"
#define DATABASE_URL "https://sensor-test-e0209-default-rtdb.asia-southeast1.firebasedatabase.app/"

// GPIO Pins
#define BOOT_BUTTON 0     // Boot button for forcing AP mode
#define STATUS_LED 2      // LED to indicate AP mode

// Global variables
String ssid, password, deviceId;
String displayText = "Connecting...";
bool apMode = false;
bool displayUpdated = false;
unsigned long lastFirebaseCheck = 0;
const int firebaseCheckInterval = 5000;  // Check Firebase every 5 seconds

// Objects initialization
WebServer server(80);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Function prototypes
void readEEPROMData();
void writeEEPROMData(String ssid, String password, String deviceId);
void clearEEPROMData();
bool testWiFiConnection();
void startAPMode();
void setupWebServer();
void updateDisplay(String line1, String line2, String line3, String line4);
void setupFirebase();
void checkAndUpdateFromFirebase();

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n===== ESP32 IoT System Starting =====");
  
  // Initialize GPIO pins
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  
  // Initialize OLED
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    // Continue even if display fails, but log the error
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  updateDisplay("Starting...", "", "", "");
  
  // Read stored credentials from EEPROM
  readEEPROMData();
  
  // Check if boot button is pressed or no SSID is stored
  if (digitalRead(BOOT_BUTTON) == LOW || ssid.length() == 0) {
    Serial.println("Boot button pressed or no SSID found");
    updateDisplay("Config Mode", "Connect to WiFi:", "ESP32_IoT_Setup", "IP: 192.168.4.1");
    startAPMode();
    return;
  }
  
  // Try to connect to WiFi with stored credentials
  updateDisplay("Connecting to WiFi", ssid, "Please wait...", "");
  if (testWiFiConnection()) {
    Serial.println("Connected to WiFi");
    updateDisplay(String("WiFi Connected"), String("SSID: ") + ssid, String("IP: ") + WiFi.localIP().toString(), String("Loading data..."));
    
    // Setup NTP time
    configTime(28800, 0, "pool.ntp.org", "time.nist.gov");  // UTC+8
    
    // Setup Firebase
    setupFirebase();
  } else {
    Serial.println("Failed to connect to WiFi");
    updateDisplay(String("WiFi Failed"), String("SSID: ") + ssid, String("Entering setup mode"), String(""));
    startAPMode();
  }
}

void loop() {
  if (apMode) {
    // In AP mode, handle web server requests
    server.handleClient();
  } else {
    // In normal mode, check for Firebase updates
    if (millis() - lastFirebaseCheck >= firebaseCheckInterval) {
      lastFirebaseCheck = millis();
      checkAndUpdateFromFirebase();
    }
    
    // Check if boot button is pressed to enter config mode
    if (digitalRead(BOOT_BUTTON) == LOW) {
      delay(100); // Debounce
      if (digitalRead(BOOT_BUTTON) == LOW) {
        Serial.println("Boot button pressed during operation");
        updateDisplay("Config Mode", "Connect to WiFi:", "ESP32_IoT_Setup", "");
        startAPMode();
      }
    }
  }
}

// Read data from EEPROM
void readEEPROMData() {
  EEPROM.begin(512);
  Serial.println("Reading stored data from EEPROM...");
  
  ssid = password = deviceId = "";
  
  // Read SSID (bytes 0-19)
  for (int i = 0; i < 20; i++) {
    char c = EEPROM.read(i);
    if (isPrintable(c) && c != 0) ssid += c;
  }
  
  // Read password (bytes 20-39)
  for (int i = 20; i < 40; i++) {
    char c = EEPROM.read(i);
    if (isPrintable(c) && c != 0) password += c;
  }
  
  // Read device ID (bytes 40-59)
  for (int i = 40; i < 60; i++) {
    char c = EEPROM.read(i);
    if (isPrintable(c) && c != 0) deviceId += c;
  }
  
  Serial.println("Read from EEPROM:");
  Serial.println("SSID: " + ssid);
  Serial.println(String("Password: ") + (password.length() > 0 ? "********" : "Not set"));
  Serial.println("Device ID: " + deviceId);
  
  EEPROM.end();
}

// Write data to EEPROM
void writeEEPROMData(String ssid, String password, String deviceId) {
  EEPROM.begin(512);
  Serial.println("Writing data to EEPROM...");
  
  // Clear EEPROM first
  for (int i = 0; i < 60; i++) {
    EEPROM.write(i, 0);
  }
  
  // Write SSID (bytes 0-19)
  for (int i = 0; i < ssid.length() && i < 20; i++) {
    EEPROM.write(i, ssid[i]);
  }
  
  // Write password (bytes 20-39)
  for (int i = 0; i < password.length() && i < 20; i++) {
    EEPROM.write(i + 20, password[i]);
  }
  
  // Write device ID (bytes 40-59)
  for (int i = 0; i < deviceId.length() && i < 20; i++) {
    EEPROM.write(i + 40, deviceId[i]);
  }
  
  EEPROM.commit();
  EEPROM.end();
  
  Serial.println("Data written to EEPROM successfully");
}

// Clear all EEPROM data
void clearEEPROMData() {
  EEPROM.begin(512);
  for (int i = 0; i < 60; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  EEPROM.end();
  
  Serial.println("EEPROM data cleared");
}

// Test WiFi connection with stored credentials
bool testWiFiConnection() {
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  
  Serial.println("Connecting to: " + ssid);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nFailed to connect to WiFi.");
    return false;
  }
}

// Start Access Point mode for configuration
void startAPMode() {
  apMode = true;
  digitalWrite(STATUS_LED, HIGH);  // Turn on status LED
  
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_IoT_Setup", "");
  
  Serial.println("AP Mode started");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  // Setup web server
  setupWebServer();
  server.begin();
  Serial.println("Web server started");
}

// Setup the web server routes
void setupWebServer() {
  // Root page
  server.on("/", []() {
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <title>ESP32 IoT Setup</title>
      <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }
        .container { background-color: white; border-radius: 10px; padding: 20px; max-width: 600px; margin: 0 auto; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input[type="text"], input[type="password"] { width: 100%; padding: 8px; box-sizing: border-box; border: 1px solid #ddd; border-radius: 4px; }
        button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
        button:hover { background-color: #45a049; }
        .firebase-container { margin-top: 30px; border-top: 1px solid #ddd; padding-top: 20px; }
        .info-section { background-color: #f8f9fa; padding: 10px; border-radius: 4px; margin-bottom: 20px; }
      </style>
    </head>
    <body>
      <div class="container">
        <h1>ESP32 IoT Setup</h1>
        
        <div class="info-section">
          <h3>Current Configuration</h3>
          <p><strong>WiFi SSID:</strong> )rawliteral" + ssid + R"rawliteral(</p>
          <p><strong>Device ID:</strong> )rawliteral" + deviceId + R"rawliteral(</p>
        </div>
        
        <h3>WiFi & Device Settings</h3>
        <form action="/saveconfig" method="POST">
          <div class="form-group">
            <label for="ssid">WiFi SSID:</label>
            <input type="text" id="ssid" name="ssid" value=")rawliteral" + ssid + R"rawliteral(" required>
          </div>
          <div class="form-group">
            <label for="password">WiFi Password:</label>
            <input type="password" id="password" name="password" placeholder="Leave empty to keep current password">
          </div>
          <div class="form-group">
            <label for="deviceid">Device ID:</label>
            <input type="text" id="deviceid" name="deviceid" value=")rawliteral" + deviceId + R"rawliteral(" required>
          </div>
          <button type="submit">Save Configuration</button>
        </form>
        
        <div class="firebase-container">
          <h3>Firebase Display Text</h3>
          <form action="/updatetext" method="POST">
            <div class="form-group">
              <label for="displayText">Display Text:</label>
              <input type="text" id="displayText" name="displayText" placeholder="Enter text to display on OLED" required>
            </div>
            <button type="submit">Update Display Text</button>
          </form>
        </div>
        
        <p style="text-align: center; margin-top: 20px;">
          <a href="/reset" onclick="return confirm('Are you sure you want to reset all settings?');">Reset All Settings</a>
        </p>
      </div>
    </body>
    </html>
    )rawliteral";
    
    server.send(200, "text/html", html);
  });
  
  // Handle saving WiFi configuration
  server.on("/saveconfig", HTTP_POST, []() {
    String newSSID = server.arg("ssid");
    String newPassword = server.arg("password");
    String newDeviceId = server.arg("deviceid");
    
    // Update password only if a new one is provided
    if (newPassword.length() == 0) {
      newPassword = password;  // Keep existing password
    }
    
    writeEEPROMData(newSSID, newPassword, newDeviceId);
    
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <title>Configuration Saved</title>
      <style>
        body { font-family: Arial, sans-serif; margin: 20px; text-align: center; }
        .container { background-color: white; border-radius: 10px; padding: 20px; max-width: 600px; margin: 0 auto; }
        h1 { color: #4CAF50; }
      </style>
      <meta http-equiv="refresh" content="5;url=/" />
    </head>
    <body>
      <div class="container">
        <h1>Configuration Saved!</h1>
        <p>New settings have been saved. The device will restart in 5 seconds.</p>
        <p>If not redirected, <a href="/">click here</a>.</p>
      </div>
    </body>
    </html>
    )rawliteral";
    
    server.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
  });
  
  // Handle updating Firebase text
  server.on("/updatetext", HTTP_POST, []() {
    String newText = server.arg("displayText");
    
    if (Firebase.ready()) {
      FirebaseJson json;
      json.set("text", newText);
      json.set("timestamp", (int)time(nullptr));
      
      String path = "/devices/" + deviceId + "/display";
      if (Firebase.RTDB.setJSON(&fbdo, path, &json)) {
        Serial.println("Firebase text updated: " + newText);
        
        String html = R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
          <meta name="viewport" content="width=device-width, initial-scale=1">
          <title>Text Updated</title>
          <style>
            body { font-family: Arial, sans-serif; margin: 20px; text-align: center; }
            .container { background-color: white; border-radius: 10px; padding: 20px; max-width: 600px; margin: 0 auto; }
            h1 { color: #4CAF50; }
          </style>
          <meta http-equiv="refresh" content="3;url=/" />
        </head>
        <body>
          <div class="container">
            <h1>Display Text Updated!</h1>
            <p>New text has been sent to Firebase. OLED display will update shortly.</p>
            <p>If not redirected, <a href="/">click here</a>.</p>
          </div>
        </body>
        </html>
        )rawliteral";
        
        server.send(200, "text/html", html);
      } else {
        server.send(500, "text/plain", "Failed to update Firebase: " + fbdo.errorReason());
      }
    } else {
      server.send(500, "text/plain", "Firebase not connected");
    }
  });
  
  // Handle resetting all settings
  server.on("/reset", []() {
    clearEEPROMData();
    
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <title>Settings Reset</title>
      <style>
        body { font-family: Arial, sans-serif; margin: 20px; text-align: center; }
        .container { background-color: white; border-radius: 10px; padding: 20px; max-width: 600px; margin: 0 auto; }
        h1 { color: #f44336; }
      </style>
      <meta http-equiv="refresh" content="5;url=/" />
    </head>
    <body>
      <div class="container">
        <h1>All Settings Reset</h1>
        <p>All stored settings have been cleared. The device will restart in 5 seconds.</p>
        <p>If not redirected, <a href="/">click here</a>.</p>
      </div>
    </body>
    </html>
    )rawliteral";
    
    server.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
  });
  
  // Handle 404 Not Found
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });
}

// Update the OLED display with up to 4 lines of text
void updateDisplay(String line1, String line2, String line3, String line4) {
  display.clearDisplay();
  
  display.setCursor(0, 0);
  display.println(line1);
  
  display.setCursor(0, 16);
  display.println(line2);
  
  display.setCursor(0, 32);
  display.println(line3);
  
  display.setCursor(0, 48);
  display.println(line4);
  
  display.display();
}

// Setup Firebase connection
void setupFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  
  // Authentication credentials from dht11_firebase_v2.ino
  auth.user.email = "carrottan10@gmail.com";
  auth.user.password = "password123";
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  Serial.println("Connecting to Firebase...");
  updateDisplay(String("Device: ") + deviceId, String("Connecting to Firebase"), String("Please wait..."), String(""));
  
  // Wait for Firebase to connect
  int attempts = 0;
  while (!Firebase.ready() && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (Firebase.ready()) {
    Serial.println("\nFirebase connected!");
    
    // Create initial entry if it doesn't exist
    FirebaseJson json;
    json.set("text", String("Hello from ") + deviceId);
    json.set("timestamp", (int)time(nullptr));
    
    String path = String("/devices/") + deviceId + String("/display");
    Firebase.RTDB.setJSON(&fbdo, path, &json);
    
    // Get initial display text
    checkAndUpdateFromFirebase();
  } else {
    Serial.println("\nFailed to connect to Firebase.");
    updateDisplay(String("Device: ") + deviceId, String("Firebase Error"), String("Check credentials"), String(""));
  }
}

// Check for updates from Firebase and update display if necessary
void checkAndUpdateFromFirebase() {
  if (!Firebase.ready()) return;
  
  String path = String("/devices/") + deviceId + String("/display/text");
  
  if (Firebase.RTDB.getString(&fbdo, path)) {
    String newText = fbdo.stringData();
    
    if (newText != displayText) {
      displayText = newText;
      Serial.println("New display text from Firebase: " + displayText);
      
      // Split the display text into lines (max 64 chars per line)
      String line1 = String("Device: ") + deviceId;
      String line2 = displayText.substring(0, min((unsigned int)64, displayText.length()));
      String line3 = displayText.length() > 64 ? displayText.substring(64, min((unsigned int)128, displayText.length())) : "";
      String line4 = WiFi.localIP().toString();
      
      updateDisplay(line1, line2, line3, line4);
    }
  } else {
    Serial.println("Failed to get data from Firebase: " + fbdo.errorReason());
  }
}