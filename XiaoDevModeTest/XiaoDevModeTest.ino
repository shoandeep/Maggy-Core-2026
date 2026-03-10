#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h> 
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <WiFiClientSecure.h> 
#include <WebServer.h>
#include <WiFiManager.h> 
#include <ElegantOTA.h> 
#include <FirebaseESP32.h>
#include <time.h> 
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "secrets.h" 

#define SCREEN_ADDRESS 0x3C 
#define SHAKE_THRESHOLD 15.0  
#define GRAVITY_DEADZONE 1.5  
#define TOUCH_PIN 5             
#define MODE_BUTTON_PIN 21      
#define TOUCH_ACTIVE_STATE HIGH 

Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);
Adafruit_MPU6050 mpu;
WebServer server(80);
WiFiManager wm; 

FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;
String firebaseEmotion = "AUTO"; 

unsigned long lastUpdateID = 0;      
unsigned long lastFirebaseCheck = 0; 
unsigned long lastTelemetryPush = 0; 
int touchCounter = 0; 
String lastPomoCmd = ""; 
String lastDevCmd = ""; 
int firebaseFetchState = 0; 

int currentModeIndex = 0;
String modeNames[] = {"AUTO", "HAPPY", "ANGRY", "SURPRISED", "GIDDY", "DANCE", "GAME", "LOVE", "SLEEPY", "CLOCK", "POMODORO"};
const int numModes = 11;
bool lastButtonState = HIGH;
unsigned long modeTextTimer = 0;
bool showingModeText = false;
String modeText = "";
unsigned long buttonPressStartTime = 0;
bool buttonIsPressed = false;
bool longPressTriggered = false;
bool isDevMode = false; 
bool wifiPortalActive = false;
unsigned long devModeStartTime = 0;
const unsigned long WIFI_PORTAL_TIMEOUT = 120000; // 2 min timeout

float accelOffsetX = 0.0; float accelOffsetY = 0.0; float accelOffsetZ = 0.0;
enum PomoState { POMO_SETTING, POMO_RUNNING, POMO_PAUSED, POMO_DONE };
PomoState pomoState = POMO_SETTING;
int pomoSelectedMins = 0; long pomoRemainingSecs = 0; unsigned long lastPomoTick = 0;
bool lastTouchState = false; bool webTimeSelected = false; 

unsigned long lastWeatherUpdate = 0;
const unsigned long weatherInterval = 3600000; 
String weatherTemp = "--C"; String weatherDesc = "Syncing...";
bool firstWeatherFetch = true;

uint8_t customBuffer[1024]; bool customBitmapLoaded = false; unsigned long lastDownloadAttempt = 0; 
struct Eye { float x, y, targetX, targetY, defaultX, defaultY; float width, height, targetWidth, targetHeight; float topEyelid, bottomEyelid, targetTopEyelid, targetBottomEyelid; };
Eye leftEye = {35, 32, 35, 32, 35, 32, 28, 38, 28, 38, 0, 0, 0, 0}; Eye rightEye = {93, 32, 93, 32, 93, 32, 28, 38, 28, 38, 0, 0, 0, 0};
struct Mouth { float y, targetY, width, targetWidth; }; Mouth mouth = {55, 55, 16, 16};
struct Particle { float x, y; bool active; int type; }; Particle particles[8]; 
enum Emotion { HAPPY, LOOKING, GIDDY, SLEEPY, RECOVERING, OTA_MODE, ANGRY, LOVE, SURPRISED, DANCE, GAME_MODE, CUSTOM, CLOCK_MODE, POMODORO_MODE }; 
Emotion currentEmotion = HAPPY;

unsigned long lastUpdateTime = 0; const int frameDelay = 16; float smoothingFactor = 0.20; 
unsigned long recoveryStartTime = 0; const int recoveryDuration = 1000; unsigned long lastShakeTime = 0; const int giddyHoldTime = 500;     
unsigned long lastBlinkTime = 0; unsigned long blinkInterval = 3000; bool isBlinking = false; unsigned long lastYawnTime = 0; bool isYawning = false;
float blinkSavedTargetHeight = 35.0;

String decodeWeatherCode(int code) {
  if (code == 0) return "Clear"; if (code == 1 || code == 2 || code == 3) return "Cloudy";
  if (code >= 45 && code <= 48) return "Foggy"; if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 65) return "Rain"; if (code >= 80 && code <= 82) return "Showers";
  if (code >= 95 && code <= 99) return "Storms"; return "Unknown";
}
void fetchWeather() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure *client = new WiFiClientSecure;
    if(client) {
      client->setInsecure(); HTTPClient http;
      String url = "https://api.open-meteo.com/v1/forecast?latitude=5.4333&longitude=100.5333&current_weather=true";
      http.begin(*client, url); int httpCode = http.GET();
      if (httpCode == 200) {
        String payload = http.getString(); DynamicJsonDocument doc(1024); deserializeJson(doc, payload);
        float temp = doc["current_weather"]["temperature"]; int wmoCode = doc["current_weather"]["weathercode"]; 
        weatherTemp = String((int)temp) + "C"; weatherDesc = decodeWeatherCode(wmoCode);
      } else weatherDesc = "Net Error";
      http.end(); delete client;
    }
  }
}

void loadCustomBitmap() {
  if(millis() - lastDownloadAttempt < 3000) return;
  lastDownloadAttempt = millis();
  memset(customBuffer, 0, 1024);
  int bufferIndex = 0;
  display.clearDisplay(); display.setCursor(20, 25); display.setTextSize(1);
  display.setTextColor(SH110X_WHITE); display.print("DOWNLOADING..."); display.display();
  for (int part = 0; part < 4; part++) {
    String path = "/shoans_secret_vault_7788/img_parts/" + String(part);
    if (Firebase.getString(firebaseData, path)) {
      String hexStr = firebaseData.stringData();
      int len = hexStr.length();
      for (int i = 0; i < len; i += 2) {
        char byteChars[3] = { hexStr[i], hexStr[i+1], '\0' };
        if (bufferIndex < 1024) customBuffer[bufferIndex++] = (uint8_t) strtol(byteChars, NULL, 16);
      }
    } else return; 
  }
  customBitmapLoaded = true;
}

void renderFace(); void renderDevMode(); void renderClock(); void renderPomodoro(); 
void drawEye(Eye &e, bool isLeft); void drawMouth(); void setEmotion(Emotion e);

void performAction(int durationMs, int logicType, float p1, float p2) {
  unsigned long start = millis();
  while(millis() - start < durationMs) {
    long elapsed = millis() - start;
    if (logicType == 1) { float wave = sin(((float)elapsed / durationMs) * 3.14159); mouth.targetWidth = 8 + (wave * (p1 - 8)); leftEye.targetHeight = 3 - (wave * (3 - p2)); rightEye.targetHeight = 3 - (wave * (3 - p2)); }
    else if (logicType == 2) { float wave = sin(elapsed / 40.0) * p1; leftEye.targetX = leftEye.defaultX + wave; rightEye.targetX = rightEye.defaultX + wave; }
    float smooth = 0.15; 
    leftEye.x += (leftEye.targetX - leftEye.x) * smooth; leftEye.y += (leftEye.targetY - leftEye.y) * smooth; leftEye.width += (leftEye.targetWidth - leftEye.width) * smooth; leftEye.height += (leftEye.targetHeight - leftEye.height) * smooth; leftEye.topEyelid += (leftEye.targetTopEyelid - leftEye.topEyelid) * smooth;
    rightEye.x += (rightEye.targetX - rightEye.x) * smooth; rightEye.y += (rightEye.targetY - rightEye.y) * smooth; rightEye.width += (rightEye.targetWidth - rightEye.width) * smooth; rightEye.height += (rightEye.targetHeight - rightEye.height) * smooth; rightEye.topEyelid += (rightEye.targetTopEyelid - leftEye.topEyelid) * smooth; mouth.width += (mouth.targetWidth - mouth.width) * smooth;
    display.clearDisplay(); drawEye(leftEye, true); drawEye(rightEye, false); drawMouth(); display.display(); delay(10); 
  }
}

void playWakeUpSequence() {
  currentEmotion = SLEEPY; leftEye.height = 3; rightEye.height = 3; leftEye.topEyelid = 0; rightEye.topEyelid = 0; mouth.width = 8;
  leftEye.x = leftEye.defaultX; leftEye.y = leftEye.defaultY; rightEye.x = rightEye.defaultX; rightEye.y = rightEye.defaultY;
  performAction(2500, 1, 30, 1); leftEye.targetHeight = 15; rightEye.targetHeight = 15; leftEye.targetTopEyelid = 8; rightEye.targetTopEyelid = 8;
  for(int i=0; i<3; i++) { mouth.targetWidth = 14; performAction(200, 0, 0, 0); mouth.targetWidth = 8; performAction(200, 0, 0, 0); }
  leftEye.targetHeight = 2; rightEye.targetHeight = 2; leftEye.targetTopEyelid = 0; rightEye.targetTopEyelid = 0; performAction(300, 0, 0, 0);
  leftEye.targetHeight = 15; rightEye.targetHeight = 15; leftEye.targetTopEyelid = 5; rightEye.targetTopEyelid = 5; performAction(400, 0, 0, 0);
  performAction(1000, 2, 4.0, 0);
  leftEye.targetX = leftEye.defaultX; rightEye.targetX = rightEye.defaultX; leftEye.targetHeight = 2; rightEye.targetHeight = 2; leftEye.targetTopEyelid = 0; rightEye.targetTopEyelid = 0; performAction(200, 0, 0, 0);
  leftEye.targetHeight = 12; rightEye.targetHeight = 12; leftEye.targetTopEyelid = 4; rightEye.targetTopEyelid = 4; performAction(500, 0, 0, 0); 
  setEmotion(LOOKING); 
}

void drawBootScreen(String status, int progress) {
  display.clearDisplay(); display.fillRect(leftEye.defaultX - 14, leftEye.defaultY, 28, 3, SH110X_WHITE); display.fillRect(rightEye.defaultX - 14, rightEye.defaultY, 28, 3, SH110X_WHITE); display.fillRect(60, 50, 8, 2, SH110X_WHITE); 
  int16_t x1, y1; uint16_t w, h; display.setTextSize(1); display.getTextBounds(status, 0, 0, &x1, &y1, &w, &h); display.setCursor((128 - w) / 2, 15); display.print(status);
  display.drawRect(14, 58, 100, 6, SH110X_WHITE); display.fillRect(16, 60, progress, 2, SH110X_WHITE); display.display();
}

void smoothLoad(String status, int start, int end) { for(int i = start; i <= end; i++) { drawBootScreen(status, i); delay(5); } }

void setup() {
  Serial.begin(115200); Wire.begin(); Wire.setClock(400000); 
  pinMode(TOUCH_PIN, INPUT); pinMode(MODE_BUTTON_PIN, INPUT_PULLUP); 
  if(!display.begin(0x3C, true)) { Serial.println("OLED Failed"); for(;;); }
  display.setTextColor(SH110X_WHITE);
  
  smoothLoad("Booting...", 0, 20);
  if (!mpu.begin()) { display.clearDisplay(); display.setCursor(0,0); display.print("MPU ERROR"); display.display(); delay(2000); } 
  else { mpu.setAccelerometerRange(MPU6050_RANGE_8_G); mpu.setGyroRange(MPU6050_RANGE_500_DEG); mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); }
  
  smoothLoad("Sensors OK", 20, 30); smoothLoad("Searching WiFi...", 30, 40);
  wm.setConfigPortalBlocking(false); 
  
  if (!wm.autoConnect("RobotBuddy_Setup")) {
      unsigned long portalTimer = millis();
      while (WiFi.status() != WL_CONNECTED && (millis() - portalTimer < 60000)) {
          wm.process(); 
          display.clearDisplay(); display.setTextSize(1); display.setTextColor(SH110X_WHITE);
          display.setCursor(0, 0); display.println("NO WIFI DETECTED"); display.drawLine(0, 10, 128, 10, SH110X_WHITE);
          display.setCursor(0, 20); display.println("Connect Phone to:"); display.setCursor(10, 30); display.println("RobotBuddy_Setup"); 
          if ((millis() / 500) % 2 == 0) { display.setCursor(0, 50); display.println("> PRESS BTN TO SKIP"); }
          display.display();
          if (digitalRead(MODE_BUTTON_PIN) == LOW) { delay(300); break; }
      }
  }

  if (WiFi.status() == WL_CONNECTED) {
      smoothLoad("Connected!", 40, 60); 
      configTime(28800, 0, "pool.ntp.org", "time.nist.gov"); smoothLoad("Time Synced!", 60, 80); delay(500);
      server.on("/", []() { server.send(200, "text/plain", "Maggy Online"); });
      ElegantOTA.begin(&server); ElegantOTA.onStart([]() { setEmotion(OTA_MODE); }); server.begin();
      config.host = FIREBASE_HOST; config.signer.tokens.legacy_token = FIREBASE_AUTH;
      Firebase.begin(&config, &auth); Firebase.reconnectWiFi(true); firebaseData.setBSSLBufferSize(1024, 512); firebaseData.setResponseSize(2560); 
      Firebase.setBool(firebaseData, "/shoans_secret_vault_7788/dev_mode_active", false); 
      smoothLoad("Ready!", 80, 100);
  } else {
      WiFi.disconnect(true); WiFi.mode(WIFI_OFF); smoothLoad("Offline Mode", 40, 100); delay(1000);
  }
  
  for(int i=0;i<=100;i++){ drawBootScreen("Place upright...",i); delay(15); }
  drawBootScreen("Calibrating...", 100);
  { float sX=0,sY=0,sZ=0; for(int i=0;i<50;i++){ sensors_event_t a,g,t; mpu.getEvent(&a,&g,&t); sX+=a.acceleration.x; sY+=a.acceleration.y; sZ+=a.acceleration.z; delay(10); } accelOffsetX=sX/50; accelOffsetY=sY/50; accelOffsetZ=sZ/50; }
  for(int i=0;i<=100;i++){ drawBootScreen("Ready!",i); delay(5); }
  delay(300);

  playWakeUpSequence();
  for(int i=0; i<8; i++) particles[i].active = false; lastYawnTime = millis();
}

void setEmotion(Emotion e) {
  if (currentEmotion == e && e != LOOKING && e != GIDDY) return;
  if (e == LOVE) { leftEye.width = 5; leftEye.height = 5; rightEye.width = 5; rightEye.height = 5; }
  if (e == POMODORO_MODE) { pomoState = POMO_SETTING; pomoSelectedMins = 0; webTimeSelected = false; } 
  currentEmotion = e;
  leftEye.targetTopEyelid = 0; rightEye.targetTopEyelid = 0; leftEye.targetBottomEyelid = 0; rightEye.targetBottomEyelid = 0;
  leftEye.targetWidth = 28; leftEye.targetHeight = 35; rightEye.targetWidth = 28; rightEye.targetHeight = 35; mouth.targetWidth = 16;
  switch(e) {
    case HAPPY: leftEye.targetBottomEyelid = 18; rightEye.targetBottomEyelid = 18; break;
    case SLEEPY: leftEye.targetTopEyelid = 28; rightEye.targetTopEyelid = 28; mouth.targetWidth = 6; break;
    case LOOKING: leftEye.targetHeight = 38; rightEye.targetHeight = 38; mouth.targetWidth = 10; break;
    case GIDDY: leftEye.targetWidth = 30; leftEye.targetHeight = 30; rightEye.targetWidth = 30; rightEye.targetHeight = 30; mouth.targetWidth = 20; break;
    case RECOVERING: leftEye.targetWidth = 28; leftEye.targetHeight = 10; rightEye.targetWidth = 28; rightEye.targetHeight = 10; mouth.targetWidth = 18; break;
    case ANGRY: leftEye.targetTopEyelid = 15; rightEye.targetTopEyelid = 15; mouth.targetWidth = 20; break;
    case LOVE: leftEye.targetHeight = 30; rightEye.targetHeight = 30; mouth.targetWidth = 10; break;
    case SURPRISED: leftEye.targetWidth = 15; leftEye.targetHeight = 15; rightEye.targetWidth = 15; rightEye.targetHeight = 15; mouth.targetWidth = 12; break;
    case DANCE: leftEye.targetWidth = 28; leftEye.targetHeight = 28; rightEye.targetWidth = 28; rightEye.targetHeight = 28; mouth.targetWidth = 20; break;
    case GAME_MODE: case CUSTOM: case CLOCK_MODE: case POMODORO_MODE: break; 
    case OTA_MODE: leftEye.targetWidth = 28; leftEye.targetHeight = 28; rightEye.targetWidth = 28; rightEye.targetHeight = 28; mouth.targetWidth = 10; break;
  }
}

void calibrateMPU() {
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SH110X_WHITE);
  display.setCursor(20, 20); display.print("CALIBRATING..."); display.setCursor(15, 40); display.print("REMOVE HAND NOW"); display.display();
  delay(1500); 
  float sumX = 0, sumY = 0, sumZ = 0; int samples = 50;
  for(int i = 0; i < samples; i++) {
     sensors_event_t a, g, temp; mpu.getEvent(&a, &g, &temp);
     sumX += a.acceleration.x; sumY += a.acceleration.y; sumZ += a.acceleration.z; delay(10);
  }
  accelOffsetX = sumX / samples; accelOffsetY = sumY / samples; accelOffsetZ = sumZ / samples;
  display.clearDisplay(); display.setCursor(45, 30); display.print("DONE!"); display.display(); delay(1000);
}

void startWifiPortal() {
  server.stop();                      // MUST stop first — prevents "Maggy Online" intercepting the config page
  wifiPortalActive = true;
  devModeStartTime = millis();
  WiFi.mode(WIFI_AP_STA);             // keep existing WiFi + open hotspot simultaneously
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(0);       // we manage timeout ourselves
  wm.startConfigPortal("RobotBuddy_Setup");
}

void stopWifiPortal() {
  wifiPortalActive = false;
  wm.stopConfigPortal();
  WiFi.mode(WIFI_STA);
  server.begin();                     // restart WebServer after portal closes
}

void checkFirebase() {
  if (millis() - lastFirebaseCheck > 200) {
    lastFirebaseCheck = millis();
    
    if (firebaseFetchState == 0) {
        if (Firebase.getBool(firebaseData, "/shoans_secret_vault_7788/dev_mode_active")) {
            bool webReq = firebaseData.boolData();
            if (webReq && !isDevMode) {
                isDevMode = true; startWifiPortal();
            } else if (!webReq && isDevMode) {
                isDevMode = false; stopWifiPortal();
            }
        }
        
        if (Firebase.getString(firebaseData, "/shoans_secret_vault_7788/dev_cmd")) {
            String cmd = firebaseData.stringData();
            if (cmd != "" && cmd != lastDevCmd) {
                lastDevCmd = cmd;
                if (cmd == "CALIBRATE") { calibrateMPU(); }
                else if (cmd == "CHANGE_WIFI") { startWifiPortal(); }
                else if (cmd == "KILL_WIFI") { 
                    isDevMode = false; Firebase.setBool(firebaseData, "/shoans_secret_vault_7788/dev_mode_active", false);
                    delay(500); WiFi.disconnect(true); WiFi.mode(WIFI_OFF); 
                }
            }
            if (cmd == "") lastDevCmd = ""; 
        }
        firebaseFetchState = 1; 
    } 
    else if (firebaseFetchState == 1 && !isDevMode) {
        if (Firebase.getString(firebaseData, "/shoans_secret_vault_7788/emotion")) {
            String newVal = firebaseData.stringData();
            if (newVal != firebaseEmotion) {
                firebaseEmotion = newVal;
                for(int i=0; i<numModes; i++) { if(modeNames[i] == firebaseEmotion) currentModeIndex = i; }
            }
        }
        firebaseFetchState = 2;
    }
    else if (firebaseFetchState == 2 && !isDevMode) {
        if (Firebase.getString(firebaseData, "/shoans_secret_vault_7788/pomo_cmd")) {
           String cmd = firebaseData.stringData();
           if (cmd != "" && cmd != lastPomoCmd) {
              lastPomoCmd = cmd;
              if (cmd == "SET_5") { pomoSelectedMins = 5; pomoState = POMO_SETTING; webTimeSelected = true; }
              else if (cmd == "SET_15") { pomoSelectedMins = 15; pomoState = POMO_SETTING; webTimeSelected = true; }
              else if (cmd == "SET_30") { pomoSelectedMins = 30; pomoState = POMO_SETTING; webTimeSelected = true; }
              else if (cmd == "SET_45") { pomoSelectedMins = 45; pomoState = POMO_SETTING; webTimeSelected = true; }
              else if (cmd == "SET_60") { pomoSelectedMins = 60; pomoState = POMO_SETTING; webTimeSelected = true; }
              else if (cmd == "START" && pomoSelectedMins > 0) { pomoRemainingSecs = pomoSelectedMins * 60; pomoState = POMO_RUNNING; lastPomoTick = millis(); }
              else if (cmd == "PAUSE" && pomoState == POMO_RUNNING) { pomoState = POMO_PAUSED; }
              else if (cmd == "RESET") { pomoState = POMO_SETTING; pomoRemainingSecs = 0; webTimeSelected = false; }
           }
           if (cmd == "") lastPomoCmd = ""; 
        }
        firebaseFetchState = 3; 
    } 
    else if (firebaseFetchState == 3 && !isDevMode) {
        if (Firebase.getInt(firebaseData, "/shoans_secret_vault_7788/updateID")) {
          unsigned long serverID = firebaseData.intData(); 
          if (serverID != lastUpdateID) { 
              lastUpdateID = serverID; 
              if (currentEmotion == CUSTOM) loadCustomBitmap(); 
          }
        }
        firebaseFetchState = 0;
    }
    else {
        firebaseFetchState = 0; 
    }
  }
}

void handleModeButton() {
  bool currentButtonState = digitalRead(MODE_BUTTON_PIN);
  if (currentButtonState == LOW && lastButtonState == HIGH) { delay(50); if (digitalRead(MODE_BUTTON_PIN) == LOW) { buttonIsPressed = true; buttonPressStartTime = millis(); longPressTriggered = false; } }
  if (currentButtonState == LOW && buttonIsPressed) {
    if (!longPressTriggered && (millis() - buttonPressStartTime >= 5000)) { 
        longPressTriggered = true; 
        if (!isDevMode) { 
           isDevMode = true; startWifiPortal();
           if (WiFi.status() == WL_CONNECTED) { Firebase.setBool(firebaseData, "/shoans_secret_vault_7788/dev_mode_active", true); }
        } 
    }
  }
  if (currentButtonState == HIGH && lastButtonState == LOW) {
    delay(50); 
    if (digitalRead(MODE_BUTTON_PIN) == HIGH) {
      buttonIsPressed = false;
      if (!longPressTriggered) {
         if (isDevMode) {
             isDevMode = false; stopWifiPortal();
             if (WiFi.status() == WL_CONNECTED) { Firebase.setBool(firebaseData, "/shoans_secret_vault_7788/dev_mode_active", false); }
         } else {
            currentModeIndex = (currentModeIndex + 1) % numModes;
            firebaseEmotion = modeNames[currentModeIndex]; modeText = firebaseEmotion; showingModeText = true; modeTextTimer = millis();
            if (WiFi.status() == WL_CONNECTED) { Firebase.setString(firebaseData, "/shoans_secret_vault_7788/emotion", firebaseEmotion); }
         }
      }
    }
  }
  lastButtonState = currentButtonState;
}

void updatePomodoro(sensors_event_t &a) {
  if (currentEmotion != POMODORO_MODE) return; unsigned long now = millis();
  float totalAccel = sqrt(a.acceleration.x*a.acceleration.x + a.acceleration.y*a.acceleration.y + a.acceleration.z*a.acceleration.z);
  if (totalAccel > SHAKE_THRESHOLD) { pomoState = POMO_SETTING; pomoSelectedMins = 0; pomoRemainingSecs = 0; webTimeSelected = false; return; }
  bool currentTouch = (digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_STATE);
  if (currentTouch && !lastTouchState) {
     if (pomoState == POMO_SETTING && pomoSelectedMins > 0) { pomoRemainingSecs = pomoSelectedMins * 60; pomoState = POMO_RUNNING; lastPomoTick = now; } 
     else if (pomoState == POMO_RUNNING) pomoState = POMO_PAUSED; else if (pomoState == POMO_PAUSED) { pomoState = POMO_RUNNING; lastPomoTick = now; } 
     else if (pomoState == POMO_DONE) { pomoState = POMO_SETTING; pomoSelectedMins = 0; webTimeSelected = false; }
  }
  lastTouchState = currentTouch;
  if (pomoState == POMO_SETTING) {
     float tiltThreshold = 4.0; bool isTilted = (abs(a.acceleration.y) > tiltThreshold) || (abs(a.acceleration.z) > tiltThreshold);
     if (isTilted) {
         webTimeSelected = false; 
         if (a.acceleration.y > tiltThreshold) pomoSelectedMins = 15; else if (a.acceleration.y < -tiltThreshold) pomoSelectedMins = 5; 
         else if (a.acceleration.z > tiltThreshold) pomoSelectedMins = 30; else if (a.acceleration.z < -tiltThreshold) pomoSelectedMins = 45;
     } else { if (!webTimeSelected) pomoSelectedMins = 60; }
  }
  if (pomoState == POMO_RUNNING) { if (now - lastPomoTick >= 1000) { lastPomoTick = now; pomoRemainingSecs--; if (pomoRemainingSecs <= 0) { pomoRemainingSecs = 0; pomoState = POMO_DONE; } } }
}

// FIX 4: Corrected Auto Mode Orientation Logic
void determineBehavior(sensors_event_t &a, sensors_event_t &g) {
  if (currentEmotion == OTA_MODE) return; 
  if (firebaseEmotion != "AUTO" && firebaseEmotion != "") {
     if (currentEmotion == GIDDY && firebaseEmotion != "GIDDY") { setEmotion(RECOVERING); recoveryStartTime = millis(); return; }
     if (currentEmotion == RECOVERING) { if (millis() - recoveryStartTime < recoveryDuration) return; }
     if (firebaseEmotion == "CUSTOM") { setEmotion(CUSTOM); return; }
     if (firebaseEmotion == "HAPPY") setEmotion(HAPPY); else if (firebaseEmotion == "ANGRY") setEmotion(ANGRY);
     else if (firebaseEmotion == "SLEEPY") setEmotion(SLEEPY); else if (firebaseEmotion == "LOVE") setEmotion(LOVE);
     else if (firebaseEmotion == "SURPRISED") setEmotion(SURPRISED); else if (firebaseEmotion == "DANCE") setEmotion(DANCE);
     else if (firebaseEmotion == "GAME") setEmotion(GAME_MODE); else if (firebaseEmotion == "CLOCK") setEmotion(CLOCK_MODE); 
     else if (firebaseEmotion == "POMODORO") { setEmotion(POMODORO_MODE); return; } else if (firebaseEmotion == "GIDDY") { setEmotion(GIDDY); lastShakeTime = millis(); }
     return;
  }
  if (currentEmotion == CUSTOM || currentEmotion == CLOCK_MODE || currentEmotion == POMODORO_MODE) { setEmotion(LOOKING); return; } 
  
  float totalAccel = sqrt(a.acceleration.x * a.acceleration.x + a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z);
  if (totalAccel > SHAKE_THRESHOLD) { setEmotion(GIDDY); lastShakeTime = millis(); return; }
  if (currentEmotion == GIDDY) { if (millis() - lastShakeTime > giddyHoldTime) { setEmotion(RECOVERING); recoveryStartTime = millis(); } return; }
  if (currentEmotion == RECOVERING) { if (millis() - recoveryStartTime < recoveryDuration) return; }
  
  if (digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_STATE) { touchCounter++; } else { touchCounter = 0; }
  if (touchCounter > 5) { setEmotion(LOVE); return; }

  // Z axis: face pointing down → SLEEPY
  if (a.acceleration.z < -5.0) { setEmotion(SLEEPY); return; }
  // X axis with Y guard: forward/back tilt → HAPPY. Y large = left/right = ignored.
  if (abs(a.acceleration.x) > GRAVITY_DEADZONE && abs(a.acceleration.y) < 3.0) { setEmotion(HAPPY); return; }
  // Left/right tilt or upright at rest → LOOKING
  setEmotion(LOOKING);
}

void updatePhysics(sensors_event_t &a) {
  if (currentEmotion == GAME_MODE || currentEmotion == CUSTOM || currentEmotion == CLOCK_MODE || currentEmotion == POMODORO_MODE) return; 
  unsigned long now = millis(); float offsetX = 0; float offsetY = 0;
  if (currentEmotion == RECOVERING) {
      long elapsed = millis() - recoveryStartTime; float shakeOffset = sin(millis() / 40.0) * 3.0; 
      leftEye.targetX = leftEye.defaultX + shakeOffset; rightEye.targetX = rightEye.defaultX + shakeOffset;
      leftEye.targetY = leftEye.defaultY; rightEye.targetY = rightEye.defaultY;
      float breatheProgress = (float)elapsed / recoveryDuration; mouth.targetWidth = 18 - (breatheProgress * 14); 
  } else if (currentEmotion == GIDDY) {
      float t = millis() / 80.0; float radius = 6.0;         
      leftEye.targetX = leftEye.defaultX + cos(t) * radius; leftEye.targetY = leftEye.defaultY + sin(t) * radius;
      rightEye.targetX = rightEye.defaultX + cos(t) * radius; rightEye.targetY = rightEye.defaultY + sin(t) * radius;
  } else {
      if (currentEmotion == HAPPY) { offsetX = sin(now / 150.0) * 3.0; offsetY = abs(sin(now / 150.0)) * -3.0; }
      else if (currentEmotion == DANCE) {
         int dancePhase = (now / 4000) % 3; 
         if (dancePhase == 0) { offsetX = sin(now / 100.0) * 5.0; offsetY = cos(now / 50.0) * 3.0; } else if (dancePhase == 1) { offsetX = 0; offsetY = sin(now / 40.0) * 6.0; }
         else { leftEye.targetY = leftEye.defaultY + sin(now / 100.0) * 5.0; rightEye.targetY = rightEye.defaultY - sin(now / 100.0) * 5.0; offsetX = 0; offsetY = 0; }
         float beat = abs(sin(now / 100.0)) * 5.0; leftEye.targetHeight = 20 + beat; rightEye.targetHeight = 20 + beat;
      } else if (currentEmotion == ANGRY) { offsetX = random(-2, 3); offsetY = random(-2, 3); }
      else if (currentEmotion == SLEEPY) {
         if (!isYawning && now - lastYawnTime > 6000 + random(5000)) { isYawning = true; lastYawnTime = now; }
         if (isYawning) {
            long yawnProg = now - lastYawnTime;
            if (yawnProg < 1000) { mouth.targetWidth = 20; leftEye.targetHeight = 2; rightEye.targetHeight = 2; leftEye.targetTopEyelid = 0; rightEye.targetTopEyelid = 0; } 
            else if (yawnProg < 2500) { mouth.targetWidth = 20; } else if (yawnProg < 3500) { mouth.targetWidth = 6; leftEye.targetHeight = 35; rightEye.targetHeight = 35; leftEye.targetTopEyelid = 28; rightEye.targetTopEyelid = 28; } 
            else { isYawning = false; lastYawnTime = now; }
         } else { mouth.targetWidth = 6; leftEye.targetHeight = 35; rightEye.targetHeight = 35; leftEye.targetTopEyelid = 28; rightEye.targetTopEyelid = 28; }
         offsetY = sin(now / 1000.0) * 1.0; 
      } else if (currentEmotion == LOVE) {
         float pulse = sin(now / 200.0) * 4.0; leftEye.targetWidth = 30 + pulse;  leftEye.targetHeight = 30 + pulse; rightEye.targetWidth = 30 + pulse; rightEye.targetHeight = 30 + pulse;
         offsetX = cos(now / 500.0) * 1.0; offsetY = sin(now / 500.0) * 1.0;
      }
      if (currentEmotion == LOOKING || (firebaseEmotion == "AUTO" && currentEmotion == HAPPY)) { offsetX += constrain(a.acceleration.y * 2.0, -15, 15); offsetY += constrain(-a.acceleration.x * 2.0, -10, 10); }
      if (currentEmotion != DANCE || ((now / 4000) % 3 != 2)) { leftEye.targetY = leftEye.defaultY + offsetY; rightEye.targetY = rightEye.defaultY + offsetY; }
      leftEye.targetX = leftEye.defaultX + offsetX; rightEye.targetX = rightEye.defaultX + offsetX; 
  }
  leftEye.x += (leftEye.targetX - leftEye.x) * smoothingFactor; leftEye.y += (leftEye.targetY - leftEye.y) * smoothingFactor; leftEye.width += (leftEye.targetWidth - leftEye.width) * smoothingFactor; leftEye.height += (leftEye.targetHeight - leftEye.height) * smoothingFactor; leftEye.topEyelid += (leftEye.targetTopEyelid - leftEye.topEyelid) * smoothingFactor; leftEye.bottomEyelid += (leftEye.targetBottomEyelid - leftEye.bottomEyelid) * smoothingFactor;
  rightEye.x += (rightEye.targetX - rightEye.x) * smoothingFactor; rightEye.y += (rightEye.targetY - rightEye.y) * smoothingFactor; rightEye.width += (rightEye.targetWidth - rightEye.width) * smoothingFactor; rightEye.height += (rightEye.targetHeight - rightEye.height) * smoothingFactor; rightEye.topEyelid += (rightEye.targetTopEyelid - rightEye.topEyelid) * smoothingFactor; rightEye.bottomEyelid += (rightEye.targetBottomEyelid - rightEye.bottomEyelid) * smoothingFactor; mouth.width += (mouth.targetWidth - mouth.width) * smoothingFactor;
}

void handleParticles() {
  int typeToSpawn = -1;
  if(currentEmotion == LOVE && random(100) < 10) typeToSpawn = 0; if(currentEmotion == SLEEPY && random(100) < 10) typeToSpawn = 1; 
  if(typeToSpawn != -1) { for(int i=0; i<8; i++) { if(!particles[i].active) { particles[i].active = true; particles[i].type = typeToSpawn; particles[i].x = random(20, 108); particles[i].y = 64; break; } } }
  for(int i=0; i<8; i++) { if(particles[i].active) { particles[i].y -= 1.0; particles[i].x += sin((millis() + i*100) / 200.0) * 0.5; if(particles[i].y < -10) particles[i].active = false; } }
}

void handleBlinking(unsigned long currentTime) {
  if (currentEmotion == SLEEPY || currentEmotion == GIDDY || currentEmotion == RECOVERING || currentEmotion == GAME_MODE || currentEmotion == CUSTOM || currentEmotion == CLOCK_MODE || currentEmotion == POMODORO_MODE) return;
  if (!isBlinking && currentTime - lastBlinkTime > blinkInterval) {
    isBlinking = true; lastBlinkTime = currentTime;
    blinkInterval = 1000 + random(2500);
    blinkSavedTargetHeight = leftEye.targetHeight;
  }
  if (isBlinking) {
    if (currentTime - lastBlinkTime < 80) {
      leftEye.height = 2; rightEye.height = 2;         // snap shut — bypass smoothing
      leftEye.targetHeight = 2; rightEye.targetHeight = 2;
    } else if (currentTime - lastBlinkTime < 220) {
      leftEye.targetHeight = blinkSavedTargetHeight; rightEye.targetHeight = blinkSavedTargetHeight;
    } else { isBlinking = false; }
  }
}

void drawPacman(long t) {
  long cycleTime = t % 8000; int pacX = (cycleTime / 5) % 180 - 20; int mouthOpen = abs(sin(t / 50.0)) * 10;
  display.fillCircle(pacX, 32, 10, SH110X_WHITE); display.fillTriangle(pacX, 32, pacX + 12, 32 - mouthOpen, pacX + 12, 32 + mouthOpen, SH110X_BLACK);
  for(int i=0; i<6; i++) { int dotX = (i * 30) + 10; if(dotX > pacX + 5) display.fillCircle(dotX, 32, 2, SH110X_WHITE); }
}
void drawDino(long t) {
  long cycleTime = t % 8000; int dinoY = 40; int cactusX = 140 - ((cycleTime / 5) % 180);
  if(cactusX > 10 && cactusX < 30) dinoY -= 20; display.fillRect(20, dinoY, 10, 10, SH110X_WHITE); display.fillRect(25, dinoY-5, 8, 5, SH110X_WHITE); 
  if(cactusX > -10) { display.fillRect(cactusX, 42, 6, 8, SH110X_WHITE); display.fillRect(cactusX-2, 44, 10, 2, SH110X_WHITE); }
  display.drawLine(0, 50, 128, 50, SH110X_WHITE);
}
void drawPong(long t) {
   long cycleTime = t % 8000; int ballX = abs(((cycleTime / 3) % 240) - 120); int ballY = abs(((cycleTime / 2) % 110) - 55) + 5; int paddleY = ballY - 5;
   display.fillRect(0, paddleY, 4, 15, SH110X_WHITE); display.fillRect(124, 64 - paddleY - 15, 4, 15, SH110X_WHITE); display.fillCircle(ballX, ballY, 3, SH110X_WHITE); display.drawRect(0,0,128,64,SH110X_WHITE); 
}
void drawSnake(long t) {
  long cycleTime = t % 8000; int segs = map(cycleTime, 0, 7500, 5, 128); if(segs > 128) segs = 128;
  int x = 0; int y = 0; int dir = 1; for(int i=0; i<segs; i++) { 
    display.fillRect(x, y, 7, 7, SH110X_WHITE); if(i == segs-1) { display.fillRect(x+2, y+2, 1, 1, SH110X_BLACK); display.fillRect(x+5, y+2, 1, 1, SH110X_BLACK); }
    x += (8 * dir); if(x > 120 || x < 0) { x = (x > 120) ? 120 : 0; y += 8; dir *= -1; } 
  }
  int appleIndices[] = {12, 28, 40, 55, 75, 90, 110, 125}; 
  for(int k=0; k<8; k++) { if(segs < appleIndices[k]) { int idx = appleIndices[k]; int r = idx / 16; int c = idx % 16; if (r % 2 == 1) c = 15 - c; display.fillCircle((c*8)+3, (r*8)+3, 2, SH110X_WHITE); display.drawPixel((c*8)+3, (r*8), SH110X_WHITE); } }
}
void drawTetrisMatch(long t) {
  long cycle = t % 8000; int baseX = 50; int baseY = 60; int blk = 8; int sp = 1; 
  int yRed = (cycle < 2000) ? map(cycle, 0, 2000, -20, baseY-(blk*2)) : baseY-(blk*2); if(yRed > baseY - (blk*2)) yRed = baseY - (blk*2);
  display.fillRect(baseX + blk+sp, yRed, blk, blk, SH110X_WHITE); display.fillRect(baseX + (blk*2)+sp*2, yRed, blk, blk, SH110X_WHITE); display.fillRect(baseX + blk+sp, yRed+blk+sp, blk, blk, SH110X_WHITE); display.fillRect(baseX + (blk*2)+sp*2, yRed+blk+sp, blk, blk, SH110X_WHITE);
  if(cycle > 2000) { int yBlue = (cycle < 4000) ? map(cycle, 2000, 4000, -20, baseY-(blk*3)-sp) : baseY-(blk*3)-sp; if(yBlue > baseY - (blk*3) - sp) yBlue = baseY - (blk*3) - sp; display.fillRect(baseX, yBlue, blk, blk, SH110X_WHITE); display.fillRect(baseX, yBlue+blk+sp, blk, blk, SH110X_WHITE); display.fillRect(baseX, yBlue+(blk*2)+sp*2, blk, blk, SH110X_WHITE); display.fillRect(baseX+blk+sp, yBlue, blk, blk, SH110X_WHITE); }
  if(cycle > 4000) { int yGreen = (cycle < 6000) ? map(cycle, 4000, 6000, -20, baseY-(blk*4)-sp*2) : baseY-(blk*4)-sp*2; if(yGreen > baseY - (blk*4) - sp*2) yGreen = baseY - (blk*4) - sp*2; display.fillRect(baseX, yGreen, blk, blk, SH110X_WHITE); display.fillRect(baseX+blk+sp, yGreen, blk, blk, SH110X_WHITE); display.fillRect(baseX+(blk*2)+sp*2, yGreen, blk, blk, SH110X_WHITE); display.fillRect(baseX+(blk*2)+sp*2, yGreen+blk+sp, blk, blk, SH110X_WHITE); }
  if(cycle > 6000) { if((cycle / 100) % 2 == 0) display.drawRect(baseX-2, baseY-(blk*4)-4, (blk*3)+6, (blk*4)+6, SH110X_WHITE); }
}

void renderGame() {
   display.clearDisplay(); unsigned long now = millis(); int game = (now / 8000) % 5;
   if(game == 0) drawPacman(now); else if(game == 1) drawDino(now); else if(game == 2) drawPong(now); else if(game == 3) drawSnake(now); else drawTetrisMatch(now);
}

void renderClock() {
  display.clearDisplay(); display.setTextColor(SH110X_WHITE);
  if (WiFi.status() == WL_CONNECTED) {
      struct tm timeinfo;
      if(!getLocalTime(&timeinfo)){ display.setCursor(20, 25); display.setTextSize(1); display.print("Syncing Time..."); display.display(); return; }
      char timeStringBuff[15]; char dateStringBuff[20]; strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo); strftime(dateStringBuff, sizeof(dateStringBuff), "%a, %b %d", &timeinfo);
      display.setTextSize(2); int16_t x1, y1; uint16_t w, h; display.getTextBounds(timeStringBuff, 0, 0, &x1, &y1, &w, &h); display.setCursor((128 - w) / 2, 10); display.print(timeStringBuff);
      display.setTextSize(1); display.getTextBounds(dateStringBuff, 0, 0, &x1, &y1, &w, &h); display.setCursor((128 - w) / 2, 34); display.print(dateStringBuff);
      String weatherStr = weatherTemp + " | " + weatherDesc; display.getTextBounds(weatherStr, 0, 0, &x1, &y1, &w, &h); display.setCursor((128 - w) / 2, 50); display.print(weatherStr);
  } else {
      unsigned long s = millis() / 1000; int hrs = (s / 3600) % 24; int mins = (s / 60) % 60; int secs = s % 60;
      char timeStr[15]; sprintf(timeStr, "%02d:%02d:%02d", hrs, mins, secs);
      display.setTextSize(2); int16_t x1, y1; uint16_t w, h; display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h); display.setCursor((128 - w) / 2, 20); display.print(timeStr);
      String upText = "UPTIME (OFFLINE)"; display.setTextSize(1); display.getTextBounds(upText, 0, 0, &x1, &y1, &w, &h); display.setCursor((128 - w) / 2, 45); display.print(upText);
  }
}

void renderPomodoro() {
  display.clearDisplay(); display.setTextColor(SH110X_WHITE);
  if (pomoState == POMO_SETTING) {
     display.setTextSize(1); display.setCursor(30, 0); display.print("TILT TO SET");
     display.setTextSize(4); if (pomoSelectedMins == 0) { display.setCursor(40, 20); display.print("--"); } 
     else { int offset = (pomoSelectedMins < 10) ? 52 : 40; display.setCursor(offset, 20); display.print(pomoSelectedMins); }
     display.setTextSize(1); display.setCursor(52, 55); display.print("MIN");
  } else if (pomoState == POMO_RUNNING || pomoState == POMO_PAUSED) {
     int m = pomoRemainingSecs / 60; int s = pomoRemainingSecs % 60; char timeStr[6]; sprintf(timeStr, "%02d:%02d", m, s);
     if (pomoState == POMO_PAUSED && (millis() / 500) % 2 == 0) { /* Blinking */ } 
     else { display.setTextSize(3); display.setCursor(20, 25); display.print(timeStr); }
     if (pomoState == POMO_PAUSED) { display.setTextSize(1); display.setCursor(45, 5); display.print("PAUSED"); } 
     else { if ((millis() / 1000) % 2 == 0) display.fillCircle(120, 10, 2, SH110X_WHITE); }
  } else if (pomoState == POMO_DONE) {
     if ((millis() / 300) % 2 == 0) { display.setTextSize(3); display.setCursor(20, 25); display.print("00:00"); }
     display.setTextSize(1); display.setCursor(40, 5); display.print("TIME UP!");
  }
}

void drawAngerSymbol() {
  display.drawLine(100, 15, 105, 10, SH110X_WHITE); display.drawLine(105, 10, 110, 15, SH110X_WHITE);
  display.drawLine(100, 22, 105, 17, SH110X_WHITE); display.drawLine(105, 17, 110, 22, SH110X_WHITE);
  display.drawLine(90, 20, 95, 10, SH110X_WHITE); display.drawLine(115, 20, 110, 10, SH110X_WHITE);
}

void drawEye(Eye &e, bool isLeft) {
  if(currentEmotion == LOVE) { int w = e.width; int h = e.height; int hX = e.x; int hY = e.y; display.fillCircle(hX - w/4, hY - h/4, w/4, SH110X_WHITE); display.fillCircle(hX + w/4, hY - h/4, w/4, SH110X_WHITE); display.fillTriangle(hX - w/2, hY - h/4, hX + w/2, hY - h/4, hX, hY + h/2, SH110X_WHITE); return; }
  float radius = e.width / 2.0; if(radius > e.height/2.0) radius = e.height/2.0;
  display.fillRoundRect(e.x - e.width/2, e.y - e.height/2, e.width, e.height, radius, SH110X_WHITE);
  if(e.topEyelid > 0) display.fillRect(e.x - e.width/2, e.y - e.height/2, e.width, e.topEyelid, SH110X_BLACK);
  if(e.bottomEyelid > 0) display.fillRect(e.x - e.width/2, (e.y + e.height/2) - e.bottomEyelid, e.width, e.bottomEyelid, SH110X_BLACK);
  if (currentEmotion == ANGRY || currentEmotion == RECOVERING) { int slant = 10; if(isLeft) display.fillTriangle(e.x + e.width/2, e.y - e.height/2, e.x, e.y - e.height/2, e.x + e.width/2, e.y - e.height/2 + slant, SH110X_BLACK); else display.fillTriangle(e.x - e.width/2, e.y - e.height/2, e.x, e.y - e.height/2, e.x - e.width/2, e.y - e.height/2 + slant, SH110X_BLACK); }
}

void drawMouth() {
  if (currentEmotion == HAPPY || currentEmotion == LOVE || currentEmotion == DANCE) { display.fillCircle(64, mouth.y, mouth.width/2, SH110X_WHITE); display.fillRect(50, mouth.y - mouth.width, 28, mouth.width, SH110X_BLACK); } 
  else if (currentEmotion == GIDDY) { display.fillCircle(64, mouth.y, mouth.width/2, SH110X_WHITE); display.fillCircle(64, mouth.y, mouth.width/2 - 3, SH110X_BLACK); } 
  else if (currentEmotion == RECOVERING || currentEmotion == SURPRISED) { display.drawCircle(64, mouth.y, mouth.width/2, SH110X_WHITE); } 
  else if (currentEmotion == SLEEPY && mouth.width > 15) { display.fillCircle(64, mouth.y, mouth.width/2, SH110X_WHITE); } 
  else { display.fillRect(64 - mouth.width/2, mouth.y, mouth.width, 2, SH110X_WHITE); }
}

void drawParticles() {
  display.setTextColor(SH110X_WHITE); 
  for(int i=0; i<8; i++) {
    if(particles[i].active) {
       if(particles[i].type == 0) { int px = particles[i].x; int py = particles[i].y; display.drawPixel(px, py, SH110X_WHITE); display.drawPixel(px+2, py, SH110X_WHITE); display.drawPixel(px+1, py+1, SH110X_WHITE); display.drawPixel(px+1, py+2, SH110X_WHITE); } 
       else { display.setCursor(particles[i].x, particles[i].y); display.setTextSize(1); display.print("z"); }
    }
  }
}

void renderDevMode() {
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SH110X_WHITE);
  display.fillRect(0, 0, 128, 12, SH110X_WHITE); display.setTextColor(SH110X_BLACK); display.setCursor(20, 2); display.print("DEV / SETUP");
  display.setTextColor(SH110X_WHITE); display.setCursor(0, 16);
  if (WiFi.status() == WL_CONNECTED) {
      display.print("WIFI: "); display.print(WiFi.SSID());
      display.setCursor(0, 26); display.print("IP: "); display.print(WiFi.localIP());
  } else {
      display.print("No WiFi connected");
      display.setCursor(0, 26); display.print("Connect via hotspot");
  }
  if (wifiPortalActive) {
      unsigned long remaining = (WIFI_PORTAL_TIMEOUT - (millis() - devModeStartTime)) / 1000;
      display.setCursor(0, 36); display.print("AP: RobotBuddy_Setup");
      display.setCursor(0, 46); display.print("IP: 192.168.4.1");
      display.setCursor(0, 56); display.print("Closes in: "); display.print(remaining); display.print("s");
  } else {
      display.setCursor(0, 36); display.print("RAM: "); display.print(ESP.getFreeHeap() / 1024); display.print(" KB");
      display.setCursor(0, 50); display.print("Tap head: CALIBRATE");
  }
  display.display();
}

void renderFace() {
  display.clearDisplay(); 
  if(currentEmotion == GAME_MODE) { renderGame(); } else if (currentEmotion == CLOCK_MODE) { renderClock(); } else if (currentEmotion == POMODORO_MODE) { renderPomodoro(); }
  else if (currentEmotion == CUSTOM) { if (customBitmapLoaded) display.drawBitmap(0, 0, customBuffer, 128, 64, SH110X_WHITE); else { display.setTextSize(1); display.setCursor(25, 30); display.print("LOADING..."); } } 
  else { drawEye(leftEye, true); drawEye(rightEye, false); drawMouth(); drawParticles(); if(currentEmotion == ANGRY) drawAngerSymbol(); if(currentEmotion == DANCE) { if((millis() / 200) % 2 == 0) display.drawCircle(10, 10, 4, SH110X_WHITE); else display.drawCircle(118, 10, 4, SH110X_WHITE); } }
  if(currentEmotion == OTA_MODE) { display.setCursor(30, 55); display.print("UPDATING..."); }
  if (showingModeText) {
    if (millis() - modeTextTimer < 1500) { 
        int textWidth = (6 + modeText.length()) * 6; int startX = (128 - textWidth) / 2; display.setCursor(startX, 2); display.setTextSize(1); display.setTextColor(SH110X_WHITE, SH110X_BLACK); display.print("MODE: "); display.print(modeText);
    } else showingModeText = false; 
  }
  display.display();
}

void loop() {
  if (wifiPortalActive) {
    wm.process();
    if (millis() - devModeStartTime > WIFI_PORTAL_TIMEOUT) { stopWifiPortal(); }
  }

  if (WiFi.status() == WL_CONNECTED) {
      if (!wifiPortalActive) { server.handleClient(); ElegantOTA.loop(); }
      checkFirebase();
      
      if (millis() - lastWeatherUpdate >= weatherInterval || firstWeatherFetch) {
         lastWeatherUpdate = millis(); firstWeatherFetch = false; fetchWeather(); 
      }
      
      if (isDevMode && millis() - lastTelemetryPush > 2500) {
         lastTelemetryPush = millis();
         FirebaseJson teleJson;
         teleJson.set("ssid", WiFi.SSID());
         teleJson.set("ip", WiFi.localIP().toString());
         teleJson.set("mac", WiFi.macAddress());
         teleJson.set("ram", ESP.getFreeHeap() / 1024);
         Firebase.setJSON(firebaseData, "/shoans_secret_vault_7788/telemetry", teleJson);
      }
  }

  handleModeButton(); 

  if(millis() - lastUpdateTime >= frameDelay) {
    lastUpdateTime = millis();
    if (isDevMode) {
       renderDevMode(); if (digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_STATE) calibrateMPU();
    } else {
       sensors_event_t a, g, temp;
       if(currentEmotion != OTA_MODE) { mpu.getEvent(&a, &g, &temp); a.acceleration.x -= accelOffsetX; a.acceleration.y -= accelOffsetY; a.acceleration.z -= accelOffsetZ; } 
       else { a.acceleration.x = 0; a.acceleration.y = 0; a.acceleration.z = 0; }
       updatePomodoro(a); determineBehavior(a, g); updatePhysics(a); handleParticles(); handleBlinking(millis()); renderFace(); 
    }
  }
}