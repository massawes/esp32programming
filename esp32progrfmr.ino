/* -----------------------------------------------------------------------------
  - Project: Biometric attendance system using ESP32
  - Author:  https://www.youtube.com/ElectronicsTechHaIs
  - Date:  29/02/2020
   -----------------------------------------------------------------------------
  This code was created by Electronics Tech channel for 
  the Biometric attendance project with ESP32.
   ---------------------------------------------------------------------------*/
//*******************************libraries********************************
//ESP32----------------------------
#include <WiFi.h>
#include <HTTPClient.h>
//LCD------------------------------
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Fingerprint.h>  //https://github.com/adafruit/Adafruit-Fingerprint-Sensor-Library
//************************************************************************
//Fingerprint scanner Pins (Serial2 pins Rx2 & Tx2)
#define Finger_Rx 16    //Rx2
#define Finger_Tx 17    //Tx2
#define BUZZER_PIN 4
// LCD 16x2 I2C address is commonly 0x27 or 0x3F
LiquidCrystal_I2C lcd(0x27, 16, 2);
//************************************************************************
HardwareSerial mySerial(2); //ESP32 Hardware Serial 2
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
//************************************************************************
/* Set these to your desired credentials. */
const char *ssid = "CYBER";
const char *password = "123456789";
const char* device_uid  = "a8d832ed";
//************************************************************************
String API_BASE_URL = "http://192.168.1.10/api/esp32";
//************************************************************************
int FingerID = 0;                                  // The Fingerprint ID from the scanner 
bool device_Mode = true;                            // true = Enrollment mode, false = Attendance mode
bool firstConnect = false;
uint8_t id;
unsigned long previousMillis = 0;
unsigned long lastAttendanceMillis = 0;
unsigned long lastModeCheckMillis = 0;
unsigned long lastEnrollmentCheckMillis = 0;
unsigned long lastDeletionCheckMillis = 0;
int lastAttendanceFinger = -1;
int lastDisplayState = 99;

const unsigned long WIFI_RETRY_INTERVAL = 10000UL;
const unsigned long ATTENDANCE_COOLDOWN = 4000UL;
const unsigned long MODE_CHECK_INTERVAL = 25000UL;
const unsigned long ENROLLMENT_CHECK_INTERVAL = 10000UL;
const unsigned long DELETION_CHECK_INTERVAL = 15000UL;
const uint16_t HTTP_TIMEOUT_MS = 5000;
//************************************************************************
void beepBuzzer(uint8_t times, uint16_t onMs, uint16_t offMs) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(onMs);
    digitalWrite(BUZZER_PIN, LOW);
    if (i + 1 < times) {
      delay(offMs);
    }
  }
}

void showTextMessage(const String &line1, const String &line2 = "", uint8_t textSize = 2) {
  (void)textSize;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(line2.substring(0, 16));
}

void showIdleMessage() {
  showTextMessage("Place finger", "For attendance", 1);
}

bool sendGetRequest(const String &path, String &payload, int &httpCode) {
  payload = "";
  httpCode = -1;

  if (!WiFi.isConnected()) {
    Serial.println("HTTP skipped: WiFi not connected");
    return false;
  }

  HTTPClient http;
  String link = API_BASE_URL + path;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(link);
  http.addHeader("Accept", "application/json");

  httpCode = http.GET();
  if (httpCode > 0) {
    payload = http.getString();
  } else {
    Serial.print("HTTP GET failed: ");
    Serial.println(httpCode);
  }

  http.end();
  return httpCode > 0;
}

bool sendPostRequest(const String &path, const String &jsonBody, String &payload, int &httpCode) {
  payload = "";
  httpCode = -1;

  if (!WiFi.isConnected()) {
    Serial.println("HTTP skipped: WiFi not connected");
    return false;
  }

  HTTPClient http;
  String link = API_BASE_URL + path;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(link);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  httpCode = http.POST(jsonBody);
  if (httpCode > 0) {
    payload = http.getString();
  } else {
    Serial.print("HTTP POST failed: ");
    Serial.println(httpCode);
  }

  http.end();
  return httpCode > 0;
}

String jsonValue(const String &payload, const String &key) {
  String pattern = "\"" + key + "\":";
  int start = payload.indexOf(pattern);
  if (start < 0) {
    return "";
  }

  start += pattern.length();
  while (start < payload.length() && payload[start] == ' ') {
    start++;
  }

  if (start < payload.length() && payload[start] == '"') {
    start++;
    int end = payload.indexOf('"', start);
    if (end < 0) {
      return "";
    }
    return payload.substring(start, end);
  }

  int end = payload.indexOf(',', start);
  if (end < 0) {
    end = payload.indexOf('}', start);
  }
  if (end < 0) {
    return "";
  }

  String value = payload.substring(start, end);
  value.trim();
  return value;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  //-----------initiate LCD-------------
  lcd.init();
  lcd.backlight();
  showTextMessage("Starting...", "Please wait", 1);
  //---------------------------------------------
  connectToWiFi();
  //---------------------------------------------
  // Set the data rate for the sensor serial port
  mySerial.begin(57600, SERIAL_8N1, Finger_Rx, Finger_Tx);
  finger.begin(57600);
  Serial.println("\n\nAdafruit finger detect test");

  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
    showTextMessage("Sensor Ready", "", 1);
  } else {
    Serial.println("Did not find fingerprint sensor :(");
    showTextMessage("Sensor Error", "Check wiring", 1);
    beepBuzzer(3, 150, 100);
    while (1) { delay(1); }
  }
  //---------------------------------------------
  finger.getTemplateCount();
  Serial.print("Sensor contains "); Serial.print(finger.templateCount); Serial.println(" templates");
  Serial.println("Waiting for valid finger...");
  CheckMode();
}
//************************************************************************
void loop() {
  //check if there's a connection to Wi-Fi or not
  if(!WiFi.isConnected()){
    if (millis() - previousMillis >= WIFI_RETRY_INTERVAL) {
      previousMillis = millis();
      connectToWiFi();    //Retry to connect to Wi-Fi
    }
  }

  if (millis() - lastModeCheckMillis >= MODE_CHECK_INTERVAL) {
    lastModeCheckMillis = millis();
    CheckMode();
  }

  if (device_Mode) {
    if (millis() - lastEnrollmentCheckMillis >= ENROLLMENT_CHECK_INTERVAL) {
      lastEnrollmentCheckMillis = millis();
      ChecktoAddID();
    }

    if (millis() - lastDeletionCheckMillis >= DELETION_CHECK_INTERVAL) {
      lastDeletionCheckMillis = millis();
      ChecktoDeleteID();
    }
  }

  if (!device_Mode) {
    CheckFingerprint();   //Check the sensor if the there a finger.
  }
  delay(30);
}
//************************************************************************
void CheckFingerprint(){
//  unsigned long previousMillisM = millis();
//  Serial.println(previousMillisM);
  // If there no fingerprint has been scanned return -1 or -2 if there an error or 0 if there nothing, The ID start form 1 to 127
  // Get the Fingerprint ID from the Scanner
  FingerID = getFingerprintID();
  if (FingerID == 0) {
    lastAttendanceFinger = -1;
  }
  DisplayFingerprintID();
//  Serial.println(millis() - previousMillisM);
  
}
//************Display the fingerprint ID state on the OLED*************
void DisplayFingerprintID(){
  //Fingerprint has been detected 
  if (FingerID > 0){
    if (FingerID != lastAttendanceFinger || millis() - lastAttendanceMillis >= ATTENDANCE_COOLDOWN) {
      lastAttendanceFinger = FingerID;
      lastAttendanceMillis = millis();
      lastDisplayState = 1;
      showTextMessage("Fingerprint OK", "Sending...", 1);
      beepBuzzer(1, 80, 0);
      SendFingerprintID(FingerID); // Send the Fingerprint ID to the website.
    }
  }
  //---------------------------------------------
  //No finger detected
  else if (FingerID == 0 && lastDisplayState != 0){
    lastDisplayState = 0;
    showIdleMessage();
  }
  //---------------------------------------------
  //Didn't find a match
  else if (FingerID == -1 && lastDisplayState != -1){
    lastDisplayState = -1;
    showTextMessage("No Match", "Try again", 1);
    beepBuzzer(2, 60, 60);
  }
  //---------------------------------------------
  //Didn't find the scanner or there an error
  else if (FingerID == -2 && lastDisplayState != -2){
    lastDisplayState = -2;
    showTextMessage("Sensor Error", "Try again", 1);
    beepBuzzer(3, 60, 60);
  }
}
//************send the fingerprint ID to the website*************
void SendFingerprintID( int finger ){
  Serial.println("Sending the Fingerprint ID");
  int httpCode;
  String payload;

  String body = "{\"device_uid\":\"" + String(device_uid) + "\",\"fingerprint_id\":" + String(finger) + "}";
  if (!sendPostRequest("/attendance", body, payload, httpCode)) {
    showTextMessage("WiFi/Server", "Not ready", 1);
    beepBuzzer(3, 60, 60);
    return;
  }

  Serial.println(httpCode);   //Print HTTP return code
  Serial.println(payload);    //Print request response payload
  Serial.println(finger);     //Print fingerprint ID

  String status = jsonValue(payload, "status");
  String user_name = jsonValue(payload, "student_name");
  String message = jsonValue(payload, "message");

  if (status == "success") {
    showTextMessage("Welcome", user_name, 2);
    beepBuzzer(2, 70, 50);
  }
  else {
    showTextMessage("Attendance Err", message, 1);
    beepBuzzer(3, 60, 60);
  }
}
//********************Get the Fingerprint ID******************
int  getFingerprintID() {
  uint8_t p = finger.getImage();
  switch (p) {
    case FINGERPRINT_OK:
      //Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      //Serial.println("No finger detected");
      return 0;
    case FINGERPRINT_PACKETRECIEVEERR:
      //Serial.println("Communication error");
      return -2;
    case FINGERPRINT_IMAGEFAIL:
      //Serial.println("Imaging error");
      return -2;
    default:
      //Serial.println("Unknown error");
      return -2;
  }
  // OK success!
  p = finger.image2Tz();
  switch (p) {
    case FINGERPRINT_OK:
      //Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      //Serial.println("Image too messy");
      return -1;
    case FINGERPRINT_PACKETRECIEVEERR:
      //Serial.println("Communication error");
      return -2;
    case FINGERPRINT_FEATUREFAIL:
      //Serial.println("Could not find fingerprint features");
      return -2;
    case FINGERPRINT_INVALIDIMAGE:
      //Serial.println("Could not find fingerprint features");
      return -2;
    default:
      //Serial.println("Unknown error");
      return -2;
  }
  // OK converted!
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    //Serial.println("Found a print match!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    //Serial.println("Communication error");
    return -2;
  } else if (p == FINGERPRINT_NOTFOUND) {
    //Serial.println("Did not find a match");
    return -1;
  } else {
    //Serial.println("Unknown error");
    return -2;
  }   
  // found a match!
  Serial.print("Found ID #"); Serial.print(finger.fingerID); 
  Serial.print(" with confidence of "); Serial.println(finger.confidence); 

  return finger.fingerID;
}
//******************Check if there a Fingerprint ID to delete******************
void ChecktoDeleteID(){
  Serial.println("Check to Delete ID");
  int httpCode;
  String payload;

  if (sendGetRequest("/deletion/request/" + String(device_uid), payload, httpCode) && jsonValue(payload, "status") == "pending") {
    String del_id = jsonValue(payload, "fingerprint_id");
    Serial.println(del_id);
    if (deleteFingerprint(del_id.toInt()) == FINGERPRINT_OK) {
      String confirmPayload;
      int confirmHttpCode;
      String body = "{\"device_uid\":\"" + String(device_uid) + "\",\"fingerprint_id\":" + del_id + "}";
      sendPostRequest("/deletion/confirm", body, confirmPayload, confirmHttpCode);
      delay(500);
    }
  }
}
//******************Delete Finpgerprint ID*****************
uint8_t deleteFingerprint( int id) {
  uint8_t p = -1;
  
  p = finger.deleteModel(id);

  if (p == FINGERPRINT_OK) {
    //Serial.println("Deleted!");
    showTextMessage("Deleted", "Success", 1);
    beepBuzzer(2, 80, 50);
    return p;
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    //Serial.println("Communication error");
    showTextMessage("Comm error", "Delete failed", 1);
    beepBuzzer(3, 60, 60);
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    //Serial.println("Could not delete in that location");
    showTextMessage("Bad location", "Delete failed", 1);
    beepBuzzer(3, 60, 60);
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    //Serial.println("Error writing to flash");
    showTextMessage("Flash error", "Delete failed", 1);
    beepBuzzer(3, 60, 60);
    return p;
  } else {
    //Serial.print("Unknown error: 0x"); Serial.println(p, HEX);
    showTextMessage("Unknown error", "Delete failed", 1);
    beepBuzzer(3, 60, 60);
    return p;
  }   
}
//******************Check if there a Fingerprint ID to add******************
void ChecktoAddID(){
//  Serial.println("Check to Add ID");
  int httpCode;
  String payload;

  if (sendGetRequest("/enrollment/request/" + String(device_uid), payload, httpCode) && jsonValue(payload, "status") == "pending") {
    String add_id = jsonValue(payload, "fingerprint_id");
    Serial.println(add_id);
    id = add_id.toInt();
    getFingerprintEnroll();
  }
}
//******************Check the Mode*****************
void CheckMode(){
  Serial.println("Check Mode");
  int httpCode;
  String payload;

  if (sendGetRequest("/device-mode/" + String(device_uid), payload, httpCode)) {
    String dev_mode = jsonValue(payload, "mode");
    bool enrollmentMode = dev_mode.toInt() == 0;

    if(!firstConnect || device_Mode != enrollmentMode){
      firstConnect = true;
      device_Mode = enrollmentMode;
      lastDisplayState = 99;
      lastEnrollmentCheckMillis = millis();
      lastDeletionCheckMillis = millis();

      if(device_Mode){
        Serial.println("Device Mode: Enrollment");
        showTextMessage("Enroll Mode", "Ready", 1);
      }
      else{
        Serial.println("Device Mode: Attendance");
        showIdleMessage();
      }
    }
  }
//  Serial.print("Number of Timers: ");
//  Serial.println(timer.getNumTimers());
}
//******************Enroll a Finpgerprint ID*****************
uint8_t getFingerprintEnroll() {
  int p = -1;
  showTextMessage("Enroll finger", "Place finger", 1);
  while (p != FINGERPRINT_OK) {
      
    p = finger.getImage();
    switch (p) {
    case FINGERPRINT_OK:
      //Serial.println("Image taken");
      showTextMessage("Finger read", "First scan OK", 1);
      break;
    case FINGERPRINT_NOFINGER:
      //Serial.println(".");
      showTextMessage("Place finger", "Scanning...", 1);
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      showTextMessage("Comm error", "Try again", 1);
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      showTextMessage("Image error", "Try again", 1);
      break;
    default:
      Serial.println("Unknown error");
      showTextMessage("Unknown error", "Try again", 1);
      break;
    }
  }
  
  // OK success!
  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      showTextMessage("First scan", "Saved", 1);
      break;
    case FINGERPRINT_IMAGEMESS:
      showTextMessage("Image messy", "Try again", 1);
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      return p;
  }
  showTextMessage("Remove finger", "", 1);
  beepBuzzer(1, 120, 0);
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  Serial.print("ID "); Serial.println(id);
  p = -1;
  showTextMessage("Second scan", "Place again", 1);
  while (p != FINGERPRINT_OK) {
    
    p = finger.getImage();
    switch (p) {
    case FINGERPRINT_OK:
      //Serial.println("Image taken");
      showTextMessage("Finger read", "Second scan OK", 1);
      break;
    case FINGERPRINT_NOFINGER:
      //Serial.println(".");
      showTextMessage("Place same", "finger again", 1);
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      break;
    default:
      Serial.println("Unknown error");
      break;
    }
  }
  // OK success!

  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      //Serial.println("Image converted");
      showTextMessage("Second scan", "Saved", 1);
      break;
    case FINGERPRINT_IMAGEMESS:
      //Serial.println("Image too messy");
      showTextMessage("Image messy", "Try again", 1);
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      return p;
  }
  
  // OK converted!
  Serial.print("Creating model for #");  Serial.println(id);
  
  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
    showTextMessage("Fingerprints", "Matched", 1);
    beepBuzzer(2, 70, 50);
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
      Serial.println("Communication error");
    beepBuzzer(3, 60, 60);
    return p;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
      Serial.println("Fingerprints did not match");
      showTextMessage("No match", "Enroll failed", 1);
      beepBuzzer(3, 60, 60);
    return p;
  } else {
      Serial.println("Unknown error");
    beepBuzzer(3, 60, 60);
    return p;
  }   
  
  Serial.print("ID "); Serial.println(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
    showTextMessage("Fingerprint", "Stored", 1);
    beepBuzzer(2, 90, 60);
    confirmAdding(id);
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    beepBuzzer(3, 60, 60);
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not store in that location");
    beepBuzzer(3, 60, 60);
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    beepBuzzer(3, 60, 60);
    return p;
  } else {
    Serial.println("Unknown error");
    beepBuzzer(3, 60, 60);
    return p;
  }   
}
//******************Check if there a Fingerprint ID to add******************
void confirmAdding(int id){
  Serial.println("confirm Adding");
  int httpCode;
  String payload;

  String body = "{\"device_uid\":\"" + String(device_uid) + "\",\"fingerprint_id\":" + String(id) + "}";
  if(sendPostRequest("/enrollment/confirm", body, payload, httpCode) && httpCode == 200){
    showTextMessage("Enroll Done", jsonValue(payload, "student_name"), 1);
    Serial.println(payload);
    delay(1500);
  }
  else{
    Serial.println("Error Confirm!!");
    showTextMessage("Enroll saved", "Server error", 1);
  }
}
//********************connect to the WiFi******************
void connectToWiFi(){
    WiFi.mode(WIFI_OFF);        //Prevents reconnection issue (taking too long to connect)
    delay(1000);
    WiFi.mode(WIFI_STA);
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    showTextMessage("Connecting WiFi", String(ssid).substring(0, 16), 1);
    
    uint32_t periodToConnect = 30000L;
    for(uint32_t StartToConnect = millis(); (millis()-StartToConnect) < periodToConnect;){
      if ( WiFi.status() != WL_CONNECTED ){
        delay(500);
        Serial.print(".");
      } else{
        break;
      }
    }
    
    if(WiFi.isConnected()){
      Serial.println("");
      Serial.println("Connected");
      showTextMessage("WiFi Connected", "Ready", 1);
      beepBuzzer(1, 120, 0);
      
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());  //IP address assigned to your ESP
    }
    else{
      Serial.println("");
      Serial.println("Not Connected");
      showTextMessage("WiFi Failed", "Retrying...", 1);
      beepBuzzer(2, 80, 80);
      WiFi.mode(WIFI_OFF);
      delay(1000);
    }
    delay(1000);
}
//=======================================================================
