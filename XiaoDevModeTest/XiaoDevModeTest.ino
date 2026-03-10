#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h> 
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// NETWORK & OTA
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <WiFiManager.h> 
#include <ElegantOTA.h> 
#include <FirebaseESP32.h>

// =============================================================
// SECRETS
// =============================================================
#include "secrets.h" // Your FIREBASE_HOST and FIREBASE_AUTH are safely hidden in here!
// =============================================================

// SCREEN SETTINGS
#define SCREEN_ADDRESS 0x3C 

// SENSOR SETTINGS
#define SHAKE_THRESHOLD 15.0  
#define GRAVITY_DEADZONE 1.5  
#define TOUCH_PIN 5             // D3 on XIAO
#define MODE_BUTTON_PIN 21      // D6 on XIAO for Mode Changing
#define TOUCH_ACTIVE_STATE HIGH // TTP223 goes HIGH when touched

// INITIALIZE 1.3" DISPLAY
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);

Adafruit_MPU6050 mpu;
WebServer server(80);

// FIREBASE
FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;
String firebaseEmotion = "AUTO"; 

// VARS
unsigned long lastUpdateID = 0;      
unsigned long lastFirebaseCheck = 0; 
int touchCounter = 0; 

// --- MODE BUTTON & DEV MODE VARS ---
int currentModeIndex = 0;
String modeNames[] = {"AUTO", "HAPPY", "ANGRY", "SURPRISED", "GIDDY", "DANCE", "GAME", "LOVE", "SLEEPY"};
const int numModes = 9;

bool lastButtonState = HIGH;
unsigned long modeTextTimer = 0;
bool showingModeText = false;
String modeText = "";

// Advanced Button Tracking
unsigned long buttonPressStartTime = 0;
bool buttonIsPressed = false;
bool longPressTriggered = false;
bool isDevMode = false; // Flag for Developer Mode

// BUFFER
uint8_t customBuffer[1024]; 
bool customBitmapLoaded = false;
unsigned long lastDownloadAttempt = 0; 

// DATA STRUCTURES
struct Eye { 
  float x, y, targetX, targetY, defaultX, defaultY; 
  float width, height, targetWidth, targetHeight; 
  float topEyelid, bottomEyelid, targetTopEyelid, targetBottomEyelid; 
};

Eye leftEye = {35, 32, 35, 32, 35, 32, 28, 38, 28, 38, 0, 0, 0, 0};
Eye rightEye = {93, 32, 93, 32, 93, 32, 28, 38, 28, 38, 0, 0, 0, 0};

struct Mouth { float y, targetY, width, targetWidth; };
Mouth mouth = {55, 55, 16, 16};

struct Particle { float x, y; bool active; int type; }; 
Particle particles[8]; 

enum Emotion { HAPPY, LOOKING, GIDDY, SLEEPY, RECOVERING, OTA_MODE, ANGRY, LOVE, SURPRISED, DANCE, GAME_MODE, CUSTOM }; 
Emotion currentEmotion = HAPPY;

// TIMERS & PHYSICS
unsigned long lastUpdateTime = 0;
const int frameDelay = 16;       
float smoothingFactor = 0.20; 

// Recovery / Shake
unsigned long recoveryStartTime = 0;
const int recoveryDuration = 1000; 
unsigned long lastShakeTime = 0;   
const int giddyHoldTime = 500;     

// Blinking & Yawning
unsigned long lastBlinkTime = 0;
unsigned long blinkInterval = 3000;
bool isBlinking = false;
unsigned long lastYawnTime = 0;
bool isYawning = false;

// ---------------- CUSTOM BITMAP DECODER ----------------
void loadCustomBitmap() {
  if(millis() - lastDownloadAttempt < 3000) return;
  lastDownloadAttempt = millis();

  memset(customBuffer, 0, 1024);
  int bufferIndex = 0;

  display.clearDisplay();
  display.setCursor(20, 25);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.print("DOWNLOADING...");
  display.display();

  for (int part = 0; part < 4; part++) {
    // UPDATED VAULT PATH
    String path = "/shoans_secret_vault_7788/img_parts/" + String(part);
    if (Firebase.getString(firebaseData, path)) {
      String hexStr = firebaseData.stringData();
      int len = hexStr.length();
      for (int i = 0; i < len; i += 2) {
        char byteChars[3] = { hexStr[i], hexStr[i+1], '\0' };
        if (bufferIndex < 1024) {
          customBuffer[bufferIndex++] = (uint8_t) strtol(byteChars, NULL, 16);
        }
      }
    } else {
      return; 
    }
  }
  customBitmapLoaded = true;
}

// ---------------- CINEMATIC ENGINE ----------------
void renderFace(); 
void renderDevMode(); // NEW
void drawEye(Eye &e, bool isLeft);
void drawMouth();
void setEmotion(Emotion e);

void performAction(int durationMs, int logicType, float p1, float p2) {
  unsigned long start = millis();
  while(millis() - start < durationMs) {
    long elapsed = millis() - start;
    if (logicType == 1) { 
       float wave = sin(((float)elapsed / durationMs) * 3.14159);
       mouth.targetWidth = 8 + (wave * (p1 - 8)); 
       leftEye.targetHeight = 3 - (wave * (3 - p2)); rightEye.targetHeight = 3 - (wave * (3 - p2));
    }
    else if (logicType == 2) { 
       float wave = sin(elapsed / 40.0) * p1;
       leftEye.targetX = leftEye.defaultX + wave; rightEye.targetX = rightEye.defaultX + wave;
    }
    float smooth = 0.15; 
    leftEye.x += (leftEye.targetX - leftEye.x) * smooth; leftEye.y += (leftEye.targetY - leftEye.y) * smooth;
    leftEye.width += (leftEye.targetWidth - leftEye.width) * smooth; leftEye.height += (leftEye.targetHeight - leftEye.height) * smooth;
    leftEye.topEyelid += (leftEye.targetTopEyelid - leftEye.topEyelid) * smooth;
    rightEye.x += (rightEye.targetX - rightEye.x) * smooth; rightEye.y += (rightEye.targetY - rightEye.y) * smooth;
    rightEye.width += (rightEye.targetWidth - rightEye.width) * smooth; rightEye.height += (rightEye.targetHeight - rightEye.height) * smooth;
    rightEye.topEyelid += (rightEye.targetTopEyelid - rightEye.topEyelid) * smooth;
    mouth.width += (mouth.targetWidth - mouth.width) * smooth;
    display.clearDisplay(); drawEye(leftEye, true); drawEye(rightEye, false); drawMouth(); display.display(); delay(10); 
  }
}

void playWakeUpSequence() {
  currentEmotion = SLEEPY; 
  leftEye.height = 3; rightEye.height = 3; leftEye.topEyelid = 0; rightEye.topEyelid = 0; mouth.width = 8;
  leftEye.x = leftEye.defaultX; leftEye.y = leftEye.defaultY; rightEye.x = rightEye.defaultX; rightEye.y = rightEye.defaultY;
  performAction(2500, 1, 30, 1); 
  leftEye.targetHeight = 15; rightEye.targetHeight = 15; leftEye.targetTopEyelid = 8; rightEye.targetTopEyelid = 8;
  for(int i=0; i<3; i++) { mouth.targetWidth = 14; performAction(200, 0, 0, 0); mouth.targetWidth = 8;  performAction(200, 0, 0, 0); }
  leftEye.targetHeight = 2; rightEye.targetHeight = 2; leftEye.targetTopEyelid = 0; rightEye.targetTopEyelid = 0; performAction(300, 0, 0, 0);
  leftEye.targetHeight = 15; rightEye.targetHeight = 15; leftEye.targetTopEyelid = 5; rightEye.targetTopEyelid = 5; performAction(400, 0, 0, 0);
  performAction(1000, 2, 4.0, 0);
  leftEye.targetX = leftEye.defaultX; rightEye.targetX = rightEye.defaultX;
  leftEye.targetHeight = 2; rightEye.targetHeight = 2; leftEye.targetTopEyelid = 0; rightEye.targetTopEyelid = 0; performAction(200, 0, 0, 0);
  leftEye.targetHeight = 12; rightEye.targetHeight = 12; leftEye.targetTopEyelid = 4; rightEye.targetTopEyelid = 4; performAction(500, 0, 0, 0); 
  setEmotion(HAPPY); 
}

void drawBootScreen(String status, int progress) {
  display.clearDisplay();
  display.fillRect(leftEye.defaultX - 14, leftEye.defaultY, 28, 3, SH110X_WHITE);
  display.fillRect(rightEye.defaultX - 14, rightEye.defaultY, 28, 3, SH110X_WHITE);
  display.fillRect(60, 50, 8, 2, SH110X_WHITE); 
  int16_t x1, y1; uint16_t w, h; display.setTextSize(1); display.getTextBounds(status, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 15); display.print(status);
  display.drawRect(14, 58, 100, 6, SH110X_WHITE); display.fillRect(16, 60, progress, 2, SH110X_WHITE); display.display();
}

void smoothLoad(String status, int start, int end) {
  for(int i = start; i <= end; i++) { drawBootScreen(status, i); delay(5); }
}

void configModeCallback(WiFiManager *myWiFiManager) {
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0); display.println("WIFI FAILED!"); display.drawLine(0, 10, 128, 10, SH110X_WHITE);
  display.setCursor(0, 20); display.println("1. Connect Phone to:"); display.setCursor(10, 30); display.println("RobotBuddy_Setup"); 
  display.setCursor(0, 45); display.println("2. Sign In to WiFi"); display.display();
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  Wire.begin(); 
  Wire.setClock(400000); 
  
  pinMode(TOUCH_PIN, INPUT);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP); 

  if(!display.begin(0x3C, true)) { 
    Serial.println("OLED Failed"); for(;;); 
  }
  display.setTextColor(SH110X_WHITE);
  
  smoothLoad("Booting...", 0, 20);
  if (!mpu.begin()) {
    display.clearDisplay(); display.setCursor(0,0); display.print("MPU ERROR"); display.display(); delay(2000);
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G); mpu.setGyroRange(MPU6050_RANGE_500_DEG); mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }
  smoothLoad("Sensors OK", 20, 40);
  smoothLoad("Searching WiFi...", 40, 60);
  
  WiFiManager wm; wm.setAPCallback(configModeCallback); wm.setConfigPortalTimeout(180); 
  if(!wm.autoConnect("RobotBuddy_Setup")) { ESP.restart(); }

  smoothLoad("Connected!", 60, 100); delay(1000); 

  server.on("/", []() { server.send(200, "text/plain", "Maggy Online"); });
  ElegantOTA.begin(&server); ElegantOTA.onStart([]() { setEmotion(OTA_MODE); }); server.begin();

  config.host = FIREBASE_HOST; config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth); Firebase.reconnectWiFi(true);
  
  firebaseData.setBSSLBufferSize(1024, 512); 
  firebaseData.setResponseSize(2560); 
  
  playWakeUpSequence();
  for(int i=0; i<8; i++) particles[i].active = false; lastYawnTime = millis();
}

// ---------------- LOGIC ----------------
void setEmotion(Emotion e) {
  if (currentEmotion == e && e != LOOKING && e != GIDDY) return;
  if (e == LOVE) { leftEye.width = 5; leftEye.height = 5; rightEye.width = 5; rightEye.height = 5; }
  
  if (e == CUSTOM) {
     customBitmapLoaded = false; 
     loadCustomBitmap();
  }
  
  currentEmotion = e;
  
  // Defaults
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
    case GAME_MODE: break;
    case CUSTOM: break; 
    case OTA_MODE: leftEye.targetWidth = 28; leftEye.targetHeight = 28; rightEye.targetWidth = 28; rightEye.targetHeight = 28; mouth.targetWidth = 10; break;
  }
}

void checkFirebase() {
  if (millis() - lastFirebaseCheck > 250) {
    lastFirebaseCheck = millis();
    
    // UPDATED VAULT PATH
    if (Firebase.getString(firebaseData, "/shoans_secret_vault_7788/emotion")) {
      String newVal = firebaseData.stringData();
      if (newVal != firebaseEmotion) {
        firebaseEmotion = newVal;
        for(int i=0; i<numModes; i++) {
            if(modeNames[i] == firebaseEmotion) currentModeIndex = i;
        }
      }
    }

    if (firebaseEmotion == "CUSTOM") {
       // UPDATED VAULT PATH
       if (Firebase.getInt(firebaseData, "/shoans_secret_vault_7788/updateID")) {
          unsigned long serverID = firebaseData.intData();
          if (serverID != lastUpdateID) {
             lastUpdateID = serverID;
             if (currentEmotion == CUSTOM) loadCustomBitmap(); 
          }
       }
    }
  }
}

// --- NEW: LONG PRESS BUTTON HANDLER ---
void handleModeButton() {
  bool currentButtonState = digitalRead(MODE_BUTTON_PIN);
  
  // 1. Button is first pressed down
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    delay(50); // Debounce
    if (digitalRead(MODE_BUTTON_PIN) == LOW) {
      buttonIsPressed = true;
      buttonPressStartTime = millis();
      longPressTriggered = false;
    }
  }

  // 2. Button is currently being held down
  if (currentButtonState == LOW && buttonIsPressed) {
    if (!longPressTriggered && (millis() - buttonPressStartTime >= 5000)) {
      longPressTriggered = true; // Mark that we hit 5 seconds
      
      // Toggle Dev Mode ON
      if (!isDevMode) {
         isDevMode = true;
      }
    }
  }

  // 3. Button is released
  if (currentButtonState == HIGH && lastButtonState == LOW) {
    delay(50); // Debounce
    if (digitalRead(MODE_BUTTON_PIN) == HIGH) {
      buttonIsPressed = false;
      
      // If it was a quick click (NOT a long press)
      if (!longPressTriggered) {
         if (isDevMode) {
            // If we are in dev mode, a click simply exits it.
            isDevMode = false;
         } else {
            // Normal behavior: Cycle to the next mode
            currentModeIndex = (currentModeIndex + 1) % numModes;
            firebaseEmotion = modeNames[currentModeIndex];
            
            modeText = firebaseEmotion;
            showingModeText = true;
            modeTextTimer = millis();

            // UPDATED VAULT PATH
            Firebase.setString(firebaseData, "/shoans_secret_vault_7788/emotion", firebaseEmotion);
         }
      }
    }
  }
  lastButtonState = currentButtonState;
}

void determineBehavior(sensors_event_t &a, sensors_event_t &g) {
  if (currentEmotion == OTA_MODE) return; 

  if (firebaseEmotion != "AUTO" && firebaseEmotion != "") {
     if (currentEmotion == GIDDY && firebaseEmotion != "GIDDY") { setEmotion(RECOVERING); recoveryStartTime = millis(); return; }
     if (currentEmotion == RECOVERING) { if (millis() - recoveryStartTime < recoveryDuration) return; }

     if (firebaseEmotion == "CUSTOM") { setEmotion(CUSTOM); return; }
     if (firebaseEmotion == "HAPPY") setEmotion(HAPPY);
     else if (firebaseEmotion == "ANGRY") setEmotion(ANGRY);
     else if (firebaseEmotion == "SLEEPY") setEmotion(SLEEPY);
     else if (firebaseEmotion == "LOVE") setEmotion(LOVE);
     else if (firebaseEmotion == "SURPRISED") setEmotion(SURPRISED);
     else if (firebaseEmotion == "DANCE") setEmotion(DANCE);
     else if (firebaseEmotion == "GAME") setEmotion(GAME_MODE);
     else if (firebaseEmotion == "GIDDY") { setEmotion(GIDDY); lastShakeTime = millis(); }
     return;
  }

  if (currentEmotion == CUSTOM) setEmotion(HAPPY); 

  float totalAccel = sqrt(a.acceleration.x * a.acceleration.x + a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z);
  if (totalAccel > SHAKE_THRESHOLD) { setEmotion(GIDDY); lastShakeTime = millis(); return; }

  if (currentEmotion == GIDDY) {
      if (millis() - lastShakeTime > giddyHoldTime) { setEmotion(RECOVERING); recoveryStartTime = millis(); }
      return; 
  }
  if (currentEmotion == RECOVERING) { if (millis() - recoveryStartTime < recoveryDuration) return; }

  if (digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_STATE) { touchCounter++; } else { touchCounter = 0; }
  if (touchCounter > 5) { setEmotion(LOVE); return; }

  if (a.acceleration.z < -5.0) { setEmotion(SLEEPY); return; }
  if (abs(a.acceleration.x) > GRAVITY_DEADZONE || abs(a.acceleration.y) > GRAVITY_DEADZONE) { setEmotion(LOOKING); return; }
  
  setEmotion(HAPPY);
}

// ---------------- PHYSICS & ANIMATION ----------------
void updatePhysics(sensors_event_t &a) {
  if (currentEmotion == GAME_MODE || currentEmotion == CUSTOM) return; 
  
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
         if (dancePhase == 0) { offsetX = sin(now / 100.0) * 5.0; offsetY = cos(now / 50.0) * 3.0; }
         else if (dancePhase == 1) { offsetX = 0; offsetY = sin(now / 40.0) * 6.0; }
         else { leftEye.targetY = leftEye.defaultY + sin(now / 100.0) * 5.0; rightEye.targetY = rightEye.defaultY - sin(now / 100.0) * 5.0; offsetX = 0; offsetY = 0; }
         float beat = abs(sin(now / 100.0)) * 5.0; leftEye.targetHeight = 20 + beat; rightEye.targetHeight = 20 + beat;
      } else if (currentEmotion == ANGRY) { offsetX = random(-2, 3); offsetY = random(-2, 3); }
      else if (currentEmotion == SLEEPY) {
         if (!isYawning && now - lastYawnTime > 6000 + random(5000)) { isYawning = true; lastYawnTime = now; }
         if (isYawning) {
            long yawnProg = now - lastYawnTime;
            if (yawnProg < 1000) { mouth.targetWidth = 20; leftEye.targetHeight = 2; rightEye.targetHeight = 2; leftEye.targetTopEyelid = 0; rightEye.targetTopEyelid = 0; } 
            else if (yawnProg < 2500) { mouth.targetWidth = 20; } 
            else if (yawnProg < 3500) { mouth.targetWidth = 6; leftEye.targetHeight = 35; rightEye.targetHeight = 35; leftEye.targetTopEyelid = 28; rightEye.targetTopEyelid = 28; } 
            else { isYawning = false; lastYawnTime = now; }
         } else { mouth.targetWidth = 6; leftEye.targetHeight = 35; rightEye.targetHeight = 35; leftEye.targetTopEyelid = 28; rightEye.targetTopEyelid = 28; }
         offsetY = sin(now / 1000.0) * 1.0; 
      } else if (currentEmotion == LOVE) {
         float pulse = sin(now / 200.0) * 4.0;
         leftEye.targetWidth = 30 + pulse;  leftEye.targetHeight = 30 + pulse;
         rightEye.targetWidth = 30 + pulse; rightEye.targetHeight = 30 + pulse;
         offsetX = cos(now / 500.0) * 1.0; offsetY = sin(now / 500.0) * 1.0;
      }
      if (currentEmotion == LOOKING || (firebaseEmotion == "AUTO" && currentEmotion == HAPPY)) {
         offsetX += constrain(a.acceleration.y * 2.0, -15, 15); offsetY += constrain(-a.acceleration.x * 2.0, -10, 10); 
      }
      if (currentEmotion != DANCE || ((now / 4000) % 3 != 2)) {
        leftEye.targetY = leftEye.defaultY + offsetY; rightEye.targetY = rightEye.defaultY + offsetY;
      }
      leftEye.targetX = leftEye.defaultX + offsetX; rightEye.targetX = rightEye.defaultX + offsetX; 
  }
  leftEye.x += (leftEye.targetX - leftEye.x) * smoothingFactor; leftEye.y += (leftEye.targetY - leftEye.y) * smoothingFactor;
  leftEye.width += (leftEye.targetWidth - leftEye.width) * smoothingFactor; leftEye.height += (leftEye.targetHeight - leftEye.height) * smoothingFactor;
  leftEye.topEyelid += (leftEye.targetTopEyelid - leftEye.topEyelid) * smoothingFactor; leftEye.bottomEyelid += (leftEye.targetBottomEyelid - leftEye.bottomEyelid) * smoothingFactor;
  rightEye.x += (rightEye.targetX - rightEye.x) * smoothingFactor; rightEye.y += (rightEye.targetY - rightEye.y) * smoothingFactor;
  rightEye.width += (rightEye.targetWidth - rightEye.width) * smoothingFactor; rightEye.height += (rightEye.targetHeight - rightEye.height) * smoothingFactor;
  rightEye.topEyelid += (rightEye.targetTopEyelid - rightEye.topEyelid) * smoothingFactor; rightEye.bottomEyelid += (rightEye.targetBottomEyelid - rightEye.bottomEyelid) * smoothingFactor;
  mouth.width += (mouth.targetWidth - mouth.width) * smoothingFactor;
}

void handleParticles() {
  int typeToSpawn = -1;
  if(currentEmotion == LOVE && random(100) < 10) typeToSpawn = 0; 
  if(currentEmotion == SLEEPY && random(100) < 10) typeToSpawn = 1; 
  if(typeToSpawn != -1) {
    for(int i=0; i<8; i++) { if(!particles[i].active) { particles[i].active = true; particles[i].type = typeToSpawn; particles[i].x = random(20, 108); particles[i].y = 64; break; } }
  }
  for(int i=0; i<8; i++) {
    if(particles[i].active) { particles[i].y -= 1.0; particles[i].x += sin((millis() + i*100) / 200.0) * 0.5; if(particles[i].y < -10) particles[i].active = false; }
  }
}

void handleBlinking(unsigned long currentTime) {
  if (currentEmotion == SLEEPY || currentEmotion == GIDDY || currentEmotion == RECOVERING || currentEmotion == GAME_MODE || currentEmotion == CUSTOM) return; 
  if(!isBlinking && currentTime - lastBlinkTime > blinkInterval) { isBlinking = true; lastBlinkTime = currentTime; blinkInterval = 2000 + random(3000); }
  if(isBlinking) { if(currentTime - lastBlinkTime < 60) { leftEye.height = 2; rightEye.height = 2; } else if (currentTime - lastBlinkTime < 120) { isBlinking = false; } }
}

// ---------------- RENDERERS ----------------
void drawPacman(long t) {
  long cycleTime = t % 8000; int pacX = (cycleTime / 5) % 180 - 20; int mouthOpen = abs(sin(t / 50.0)) * 10;
  display.fillCircle(pacX, 32, 10, SH110X_WHITE); display.fillTriangle(pacX, 32, pacX + 12, 32 - mouthOpen, pacX + 12, 32 + mouthOpen, SH110X_BLACK);
  for(int i=0; i<6; i++) { int dotX = (i * 30) + 10; if(dotX > pacX + 5) display.fillCircle(dotX, 32, 2, SH110X_WHITE); }
}
void drawDino(long t) {
  long cycleTime = t % 8000; int dinoY = 40; int cactusX = 140 - ((cycleTime / 5) % 180);
  if(cactusX > 10 && cactusX < 30) dinoY -= 20; 
  display.fillRect(20, dinoY, 10, 10, SH110X_WHITE); display.fillRect(25, dinoY-5, 8, 5, SH110X_WHITE); 
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
    display.fillRect(x, y, 7, 7, SH110X_WHITE); 
    if(i == segs-1) { display.fillRect(x+2, y+2, 1, 1, SH110X_BLACK); display.fillRect(x+5, y+2, 1, 1, SH110X_BLACK); }
    x += (8 * dir); if(x > 120 || x < 0) { x = (x > 120) ? 120 : 0; y += 8; dir *= -1; } 
  }
  int appleIndices[] = {12, 28, 40, 55, 75, 90, 110, 125}; 
  for(int k=0; k<8; k++) {
     if(segs < appleIndices[k]) {
        int idx = appleIndices[k]; int r = idx / 16; int c = idx % 16; if (r % 2 == 1) c = 15 - c; 
        display.fillCircle((c*8)+3, (r*8)+3, 2, SH110X_WHITE); display.drawPixel((c*8)+3, (r*8), SH110X_WHITE);
     }
  }
}
void drawTetrisMatch(long t) {
  long cycle = t % 8000; int baseX = 50; int baseY = 60; int blk = 8; int sp = 1; 
  int yRed = (cycle < 2000) ? map(cycle, 0, 2000, -20, baseY-(blk*2)) : baseY-(blk*2); if(yRed > baseY - (blk*2)) yRed = baseY - (blk*2);
  display.fillRect(baseX + blk+sp, yRed, blk, blk, SH110X_WHITE); display.fillRect(baseX + (blk*2)+sp*2, yRed, blk, blk, SH110X_WHITE);
  display.fillRect(baseX + blk+sp, yRed+blk+sp, blk, blk, SH110X_WHITE); display.fillRect(baseX + (blk*2)+sp*2, yRed+blk+sp, blk, blk, SH110X_WHITE);
  if(cycle > 2000) { int yBlue = (cycle < 4000) ? map(cycle, 2000, 4000, -20, baseY-(blk*3)-sp) : baseY-(blk*3)-sp; if(yBlue > baseY - (blk*3) - sp) yBlue = baseY - (blk*3) - sp; display.fillRect(baseX, yBlue, blk, blk, SH110X_WHITE); display.fillRect(baseX, yBlue+blk+sp, blk, blk, SH110X_WHITE); display.fillRect(baseX, yBlue+(blk*2)+sp*2, blk, blk, SH110X_WHITE); display.fillRect(baseX+blk+sp, yBlue, blk, blk, SH110X_WHITE); }
  if(cycle > 4000) { int yGreen = (cycle < 6000) ? map(cycle, 4000, 6000, -20, baseY-(blk*4)-sp*2) : baseY-(blk*4)-sp*2; if(yGreen > baseY - (blk*4) - sp*2) yGreen = baseY - (blk*4) - sp*2; display.fillRect(baseX, yGreen, blk, blk, SH110X_WHITE); display.fillRect(baseX+blk+sp, yGreen, blk, blk, SH110X_WHITE); display.fillRect(baseX+(blk*2)+sp*2, yGreen, blk, blk, SH110X_WHITE); display.fillRect(baseX+(blk*2)+sp*2, yGreen+blk+sp, blk, blk, SH110X_WHITE); }
  if(cycle > 6000) { if((cycle / 100) % 2 == 0) display.drawRect(baseX-2, baseY-(blk*4)-4, (blk*3)+6, (blk*4)+6, SH110X_WHITE); }
}
void renderGame() {
   display.clearDisplay(); unsigned long now = millis();
   int game = (now / 8000) % 5;
   if(game == 0) drawPacman(now); else if(game == 1) drawDino(now); else if(game == 2) drawPong(now); else if(game == 3) drawSnake(now); else drawTetrisMatch(now);
}

// ---------------- RENDER ----------------
void drawAngerSymbol() {
  display.drawLine(100, 15, 105, 10, SH110X_WHITE); display.drawLine(105, 10, 110, 15, SH110X_WHITE);
  display.drawLine(100, 22, 105, 17, SH110X_WHITE); display.drawLine(105, 17, 110, 22, SH110X_WHITE);
  display.drawLine(90, 20, 95, 10, SH110X_WHITE); display.drawLine(115, 20, 110, 10, SH110X_WHITE);
}
void drawEye(Eye &e, bool isLeft) {
  if(currentEmotion == LOVE) {
     int w = e.width; int h = e.height; int hX = e.x; int hY = e.y;
     display.fillCircle(hX - w/4, hY - h/4, w/4, SH110X_WHITE); display.fillCircle(hX + w/4, hY - h/4, w/4, SH110X_WHITE);
     display.fillTriangle(hX - w/2, hY - h/4, hX + w/2, hY - h/4, hX, hY + h/2, SH110X_WHITE); return;
  }
  float radius = e.width / 2.0; if(radius > e.height/2.0) radius = e.height/2.0;
  display.fillRoundRect(e.x - e.width/2, e.y - e.height/2, e.width, e.height, radius, SH110X_WHITE);
  if(e.topEyelid > 0) display.fillRect(e.x - e.width/2, e.y - e.height/2, e.width, e.topEyelid, SH110X_BLACK);
  if(e.bottomEyelid > 0) display.fillRect(e.x - e.width/2, (e.y + e.height/2) - e.bottomEyelid, e.width, e.bottomEyelid, SH110X_BLACK);
  if (currentEmotion == ANGRY || currentEmotion == RECOVERING) {
     int slant = 10; if(isLeft) display.fillTriangle(e.x + e.width/2, e.y - e.height/2, e.x, e.y - e.height/2, e.x + e.width/2, e.y - e.height/2 + slant, SH110X_BLACK);
     else display.fillTriangle(e.x - e.width/2, e.y - e.height/2, e.x, e.y - e.height/2, e.x - e.width/2, e.y - e.height/2 + slant, SH110X_BLACK);
  }
}
void drawMouth() {
  if (currentEmotion == HAPPY || currentEmotion == LOVE || currentEmotion == DANCE) {
     display.fillCircle(64, mouth.y, mouth.width/2, SH110X_WHITE); display.fillRect(50, mouth.y - mouth.width, 28, mouth.width, SH110X_BLACK); 
  } else if (currentEmotion == GIDDY) {
     display.fillCircle(64, mouth.y, mouth.width/2, SH110X_WHITE); display.fillCircle(64, mouth.y, mouth.width/2 - 3, SH110X_BLACK); 
  } else if (currentEmotion == RECOVERING || currentEmotion == SURPRISED) {
     display.drawCircle(64, mouth.y, mouth.width/2, SH110X_WHITE);
  } else if (currentEmotion == SLEEPY && mouth.width > 15) {
     display.fillCircle(64, mouth.y, mouth.width/2, SH110X_WHITE);
  } else { display.fillRect(64 - mouth.width/2, mouth.y, mouth.width, 2, SH110X_WHITE); }
}

void drawParticles() {
  display.setTextColor(SH110X_WHITE); 
  for(int i=0; i<8; i++) {
    if(particles[i].active) {
       if(particles[i].type == 0) { // Heart
          int px = particles[i].x; int py = particles[i].y;
          display.drawPixel(px, py, SH110X_WHITE); display.drawPixel(px+2, py, SH110X_WHITE);
          display.drawPixel(px+1, py+1, SH110X_WHITE); display.drawPixel(px+1, py+2, SH110X_WHITE);
       } else { // Zzz
          display.setCursor(particles[i].x, particles[i].y); display.setTextSize(1); display.print("z");
       }
    }
  }
}

// --- NEW: DEV MODE RENDERER ---
void renderDevMode() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  
  // Header
  display.fillRect(0, 0, 128, 12, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setCursor(20, 2);
  display.print("NETWORK DIAG");
  
  // Data
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 18);
  display.print("SSID: "); display.println(WiFi.SSID());
  display.print("IP:   "); display.println(WiFi.localIP());
  display.print("MAC:  "); display.println(WiFi.macAddress());
  display.print("RAM:  "); display.print(ESP.getFreeHeap() / 1024); display.println(" KB free");
  
  display.display();
}

void renderFace() {
  display.clearDisplay(); 

  if(currentEmotion == GAME_MODE) { 
      renderGame(); 
  } else if (currentEmotion == CUSTOM) {
     if (customBitmapLoaded) {
        display.drawBitmap(0, 0, customBuffer, 128, 64, SH110X_WHITE);
     } else {
        display.setTextSize(1); display.setCursor(25, 30); display.print("LOADING...");
     }
  } else {
     drawEye(leftEye, true); drawEye(rightEye, false); drawMouth(); drawParticles();
     if(currentEmotion == ANGRY) drawAngerSymbol();
     if(currentEmotion == DANCE) { if((millis() / 200) % 2 == 0) display.drawCircle(10, 10, 4, SH110X_WHITE); else display.drawCircle(118, 10, 4, SH110X_WHITE); }
  }
  
  if(currentEmotion == OTA_MODE) { display.setCursor(30, 55); display.print("UPDATING..."); }

  if (showingModeText) {
    if (millis() - modeTextTimer < 1500) { 
        int textWidth = (6 + modeText.length()) * 6; 
        int startX = (128 - textWidth) / 2;
        
        display.setCursor(startX, 2);
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE, SH110X_BLACK); 
        display.print("MODE: ");
        display.print(modeText);
    } else {
        showingModeText = false; 
    }
  }

  display.display();
}

void loop() {
  server.handleClient(); ElegantOTA.loop(); 
  
  // Only sync to cloud if we aren't debugging
  if (!isDevMode) {
    checkFirebase(); 
  }

  handleModeButton(); 

  if(millis() - lastUpdateTime >= frameDelay) {
    lastUpdateTime = millis();
    
    // DEV MODE OVERRIDE
    if (isDevMode) {
       renderDevMode();
    } else {
       // NORMAL MODE
       sensors_event_t a, g, temp;
       if(currentEmotion != OTA_MODE) { mpu.getEvent(&a, &g, &temp); }
       else { a.acceleration.x = 0; a.acceleration.y = 0; a.acceleration.z = 0; }
       
       determineBehavior(a, g);
       updatePhysics(a);
       handleParticles(); 
       handleBlinking(millis());
       renderFace(); 
    }
  }
}