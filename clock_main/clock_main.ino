/*
 * VERSION 1.01
 * January 10, 2024
 * See Release notes for details on changes
 * By ResinChem Tech - licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * Adapted from original work published by Leon van den Beukel 
*/
#include <Wire.h>
#include <RtcDS3231.h>                        // Include RTC library by Makuna: https://github.com/Makuna/Rtc
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <FastLED.h>
#include <FS.h>                               // Please read the instructions on http://arduino.esp8266.com/Arduino/versions/2.3.0/doc/filesystem.html#uploading-files-to-file-system
#include <PubSubClient.h>
#include <ESP8266mDNS.h>                      // NEW for v1.00
#include <WiFiUdp.h>                          // NEW for v1.00
#include <ArduinoOTA.h>                       // NEW for v1.00
#include "Settings.h"
#define countof(a) (sizeof(a) / sizeof(a[0]))

// ==============================================================================================
//  *** REVIEW THE Settings.h and Credentials.h FILES AND SET TO MATCH YOUR ENVIRONMENT/BUILD ***
//      You may also set various default boot options in the Settings.h file
// ==============================================================================================
// Do not change any values below unless you are sure you know what you are doing!

byte oldMode = 0;
int oldTemp = 0;
bool ota_flag = true;               //v1.00
uint16_t ota_time_elapsed = 0;      //v1.00
uint16_t ota_time_window = 2500;    //v1.00 minimum time on boot for IP address to show in IDE ports


#if defined(WIFIMODE) && (WIFIMODE == 0 || WIFIMODE == 2)
  const char* APssid = "CLOCK_AP";        
  const char* APpassword = "1234567890";  
#endif
  
#if defined(WIFIMODE) && (WIFIMODE == 1 || WIFIMODE == 2)
  #include "Credentials.h"                    // Edit this file in the same directory as the .ino file and add your own credentials
  const char *ssid = SID;
  const char *password = PW;
  #if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)    // MQTT only available when on local wifi
    const char *mqttUser = MQTTUSERNAME;
    const char *mqttPW = MQTTPWD;
  #endif
#endif
WiFiClient espClient;
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)
  PubSubClient client(espClient);   
#endif
long lastReconnectAttempt = 0;

RtcDS3231<TwoWire> Rtc(Wire);
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdateServer;
CRGB LEDs[NUM_LEDS];

unsigned long prevTime = 0;
unsigned long prevTempTime = 0;                   // To limit MQTT Temp updates so as not to flood broker
byte r_val = 255;
byte g_val = 0;
byte b_val = 0;
bool dotsOn = true;
unsigned long countdownMilliSeconds;
unsigned long endCountDownMillis;

//
bool mqttConnected = false;                       // Will be set to true upon valid connection
bool timerRunning = false; 
unsigned long remCountdownMillis = 0;             // Stores value on pause/resume
unsigned long initCountdownMillis = 0;            // Stores initial last countdown value for reset    
//
long numbers[] = {
  0b000111111111111111111,  // [0] 0
  0b000111000000000000111,  // [1] 1
  0b111111111000111111000,  // [2] 2
  0b111111111000000111111,  // [3] 3
  0b111111000111000000111,  // [4] 4
  0b111000111111000111111,  // [5] 5
  0b111000111111111111111,  // [6] 6
  0b000111111000000000111,  // [7] 7
  0b111111111111111111111,  // [8] 8
  0b111111111111000111111,  // [9] 9
  0b000000000000000000000,  // [10] off
  0b111111111111000000000,  // [11] degrees symbol
  0b000000111111111111000,  // [12] C(elsius)
  0b111000111111111000000,  // [13] F(ahrenheit)
  0b000111000111111111111,  // [14] U
  0b111111111111111000000,  // [15] P
  0b000000000111111111000,  // [16] L
  0b111111000000111111111,  // {17] d
};

boolean reconnect() {
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)
  if (client.connect("ClockClient", mqttUser, mqttPW)) {
    // Once connected, publish an announcement...
    client.publish("stat/ledclock/status", "connected");
    // ... and resubscribe
    client.subscribe("cmnd/ledclock/#");
  }
  return client.connected();
#endif
}

void setup() {
  pinMode(3, FUNCTION_3);
  pinMode(1, FUNCTION_3);
  pinMode(COUNTDOWN_OUTPUT, OUTPUT);
  pinMode(MODE_PIN, INPUT_PULLUP);
  pinMode(V1_PIN, INPUT_PULLUP);
  pinMode(V0_PIN, INPUT_PULLUP);
  pinMode(H1_PIN, INPUT_PULLUP);
  pinMode(H0_PIN, INPUT_PULLUP);
  delay(200);

  // RTC DS3231 Setup
  Rtc.Begin();    
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);

  if (!Rtc.IsDateTimeValid()) {
      if (Rtc.LastError() != 0) {
          // we have a communications error see https://www.arduino.cc/en/Reference/WireEndTransmission for what the number means
          // Serial.print("RTC communications error = ");
          // Serial.println(Rtc.LastError());
      } else {
          // Common Causes:
          //    1) first time you ran and the device wasn't running yet
          //    2) the battery on the device is low or even missing
          // Serial.println("RTC lost confidence in the DateTime!");
          // following line sets the RTC to the date & time this sketch was compiled
          // it will also reset the valid flag internally unless the Rtc device is
          // having an issue
          Rtc.SetDateTime(compiled);
      }
  }

  WiFi.setSleepMode(WIFI_NONE_SLEEP);  

  delay(200);

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(LEDs, NUM_LEDS);  
  FastLED.setDither(false);
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MILLI_AMPS);
  fill_solid(LEDs, NUM_LEDS, CRGB::Black);
  FastLED.show();

  // WiFi - AP Mode or both
#if defined(WIFIMODE) && (WIFIMODE == 0 || WIFIMODE == 2) 
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(APssid, APpassword);    // IP is usually 192.168.4.1
#endif

  // WiFi - Local network Mode or both
#if defined(WIFIMODE) && (WIFIMODE == 1 || WIFIMODE == 2) 
  byte count = 0;
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    // Stop if cannot connect
    if (count >= 60) {
      // Could not connect to local WiFi      
      return;
    }
    delay(500);
    LEDs[count] = CRGB::Green;
    FastLED.show();
    count++;
  }
  IPAddress ip = WiFi.localIP();
#endif   
  // MQTT - Only if on local wifi and MQTT enabled
#if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
  byte mcount = 0;
  client.setServer(MQTTSERVER, MQTTPORT);
  client.setCallback(callback);
  while (!client.connected( )) {
    client.connect("ClockClient", mqttUser, mqttPW);
    if (mcount >= 60) {
      // Could not connect to MQTT broker
      return;
    }
    delay(500);
    LEDs[mcount] = CRGB::Blue;
    FastLED.show();
    mcount++;
  }
  mqttConnected = true;
  client.subscribe("cmnd/ledclock/#");
  client.publish("stat/ledclock/status", "connected");
  oldMode = clockMode;
  updateMqttMode();
#endif
  httpUpdateServer.setup(&server);
  
  //OTA Updates - new in V1.00
  ArduinoOTA.setHostname("led-clock");
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
  });
  ArduinoOTA.begin();
  
  // ************ Update MQTT stat vals with boot values *****************
  if (mqttConnected) {
    updateMqttBrightness(brightness);
    updateMqttClockDisplay(hourFormat);
    updateMqttTemperature();
    updateMqttTempSymbol(temperatureSymbol);
    updateMqttTempCorrection();
    updateMqttCountdownStartTime(initCountdownMillis);
    updateMqttCountdownStatus(timerRunning);
    updateMqttScoreboardScores();
    // Default boot colors
    updateMqttColor(0, clockColor.r, clockColor.g, clockColor.b);  
    updateMqttColor(1, countdownColor.r, countdownColor.g, countdownColor.b);
    updateMqttColor(2, countdownColorPaused.r, countdownColorPaused.g, countdownColorPaused.b);
    updateMqttColor(3, countdownColorFinalMin.r, countdownColorFinalMin.g, countdownColorFinalMin.b);
    updateMqttColor(4, temperatureColor.r, temperatureColor.g, temperatureColor.b);
    updateMqttColor(5, scoreboardColorLeft.r, scoreboardColorLeft.g, scoreboardColorLeft.b);
    updateMqttColor(6, scoreboardColorRight.r, scoreboardColorRight.g, scoreboardColorRight.b);
  }
  // Handlers
  server.on("/color", HTTP_POST, []() {    
    r_val = server.arg("r").toInt();
    g_val = server.arg("g").toInt();
    b_val = server.arg("b").toInt();
    clockColor = CRGB(r_val, g_val, b_val);
    server.send(200, "text/json", "{\"result\":\"ok\"}");
    if (mqttConnected) {
      updateMqttColor(0, r_val, g_val, b_val);
    }
  });

  server.on("/setdate", HTTP_POST, []() { 
    // Sample input: date = "Dec 06 2009", time = "12:34:56"
    // Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec
    String datearg = server.arg("date");
    String timearg = server.arg("time");
    char d[12];
    char t[9];
    datearg.toCharArray(d, 12);
    timearg.toCharArray(t, 9);
    RtcDateTime compiled = RtcDateTime(d, t);
    Rtc.SetDateTime(compiled);   
    clockMode = 0;     
    server.send(200, "text/json", "{\"result\":\"ok\"}");
  });

  server.on("/brightness", HTTP_POST, []() {    
    brightness = server.arg("brightness").toInt();    
    server.send(200, "text/json", "{\"result\":\"ok\"}");
    if (mqttConnected) {
      //char outpayload[3];
      //sprintf(outpayload, "%3u", server.arg("brightness").toInt());
      //client.publish("stat/ledclock/brightness", outpayload, true);
      updateMqttBrightness(brightness);
    }
  });
  
  server.on("/countdown", HTTP_POST, []() {    
    countdownMilliSeconds = server.arg("ms").toInt();     
    byte cd_r_val = server.arg("r").toInt();
    byte cd_g_val = server.arg("g").toInt();
    byte cd_b_val = server.arg("b").toInt();
    digitalWrite(COUNTDOWN_OUTPUT, LOW);
    countdownColor = CRGB(cd_r_val, cd_g_val, cd_b_val); 
    endCountDownMillis = millis() + countdownMilliSeconds;
    initCountdownMillis = countdownMilliSeconds;          // store for reset to init value
    allBlank(); 
    clockMode = 1;    
    timerRunning = true; 
    server.send(200, "text/json", "{\"result\":\"ok\"}");
    if (mqttConnected) {
      updateMqttColor(1, server.arg("r").toInt(), server.arg("g").toInt(), server.arg("b").toInt());
      updateMqttCountdownStartTime(initCountdownMillis);
      updateMqttCountdownStatus(timerRunning);
    }
  });

  server.on("/temperature", HTTP_POST, []() {   
    temperatureCorrection = server.arg("correction").toInt();
    temperatureSymbol = server.arg("symbol").toInt();
    clockMode = 2;     
    server.send(200, "text/json", "{\"result\":\"ok\"}");
    if (mqttConnected) {
      //char outCorr[10];
      //sprintf(outCorr, "%4.1f", temperatureCorrection);
      //client.publish("stat/ledclock/temperature/correction", outCorr, true);
      updateMqttTempCorrection();
      updateMqttTempSymbol(temperatureSymbol);
      updateMqttTemperature();
    }
  });  

  server.on("/scoreboard", HTTP_POST, []() {   
    scoreboardLeft = server.arg("left").toInt();
    scoreboardRight = server.arg("right").toInt();
    scoreboardColorLeft = CRGB(server.arg("rl").toInt(),server.arg("gl").toInt(),server.arg("bl").toInt());
    scoreboardColorRight = CRGB(server.arg("rr").toInt(),server.arg("gr").toInt(),server.arg("br").toInt());
    clockMode = 3;     
    server.send(200, "text/json", "{\"result\":\"ok\"}");
    if (mqttConnected) {
      updateMqttColor(5, server.arg("rl").toInt(),server.arg("gl").toInt(),server.arg("bl").toInt());
      updateMqttColor(6, server.arg("rr").toInt(),server.arg("gr").toInt(),server.arg("br").toInt());
      updateMqttScoreboardScores();
    }
  });  

  server.on("/hourformat", HTTP_POST, []() {   
    hourFormat = server.arg("hourformat").toInt();
    clockMode = 0;     
    server.send(200, "text/json", "{\"result\":\"ok\"}");
    if (mqttConnected) {
      //char outpayload[2];
      //sprintf(outpayload, "%2u", server.arg("hourformat").toInt());
      //client.publish("stat/ledclock/clock/display", outpayload, true);
      updateMqttClockDisplay(hourFormat);
    }
  }); 

  server.on("/clock", HTTP_POST, []() {       
    clockMode = 0;     
    server.send(200, "text/json", "{\"result\":\"ok\"}");
  });  

  // New web commands for OTA updates - v1.00
  server.on("/restart",[](){
    server.send(200, "text/html", "<h1>Restarting...</h1>");
    delay(1000);
    ESP.restart();
  });
  server.on("/otaupdate",[]() {
    server.send(200, "text/html", "<h1>Ready for upload...<h1><h3>Start upload from IDE now</h3>");
    ota_flag = true;
    ota_time_window = 20000;
    ota_time_elapsed = 0;
  });
  
  // Before uploading the files with the "ESP8266 Sketch Data Upload" tool, zip the files (if not already zipped) with the command "gzip -r ./data/" (on Windows you can do this with a Git Bash)
  // *.gz files are automatically unpacked and served from your ESP (so you don't need to create a handler for each file).
  server.serveStatic("/", SPIFFS, "/", "max-age=86400");
  server.begin();     

  SPIFFS.begin();
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
  }
  
  digitalWrite(COUNTDOWN_OUTPUT, LOW);

};

// *************** MQTT Message Processing *********************

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String message = (char*)payload;
  
  // Mode
  if (strcmp(topic,"cmnd/ledclock/mode")==0){
    clockMode = message.toInt();
    updateMqttMode();
    return;
   
  // Brightness
  } else if (strcmp(topic,"cmnd/ledclock/brightness") == 0) {
      brightness = message.toInt();
      updateMqttBrightness(brightness);
      return;
  
  // Buzzer
  } else if (strcmp(topic, "cmnd/ledclock/buzzer") == 0) {
      int buzzTime = message.toInt();
      if (buzzTime > 9999)    //max time 9.99 sec to avoid blocking issues
        buzzTime = 9999;
      soundBuzzer(buzzTime);
  
  // Clock
    // Color
  } else if (strcmp(topic, "cmnd/ledclock/clock/color") == 0) {
    int firstDelim = message.indexOf(",");
    int secondDelim = message.indexOf(",", firstDelim+1);
    int mqqt_r_val = message.substring(0, firstDelim).toInt();
    int mqqt_g_val = message.substring(firstDelim+1, secondDelim).toInt();
    int mqqt_b_val = message.substring(secondDelim+1).toInt();
    clockColor = CRGB(mqqt_r_val, mqqt_g_val, mqqt_b_val);
    updateMqttColor(0, mqqt_r_val, mqqt_g_val, mqqt_b_val);
    // Display 12/24
  } else if (strcmp(topic, "cmnd/ledclock/clock/display") == 0) {
    hourFormat = message.toInt();
    clockMode = 0;
    updateMqttClockDisplay(hourFormat);
    // Time Set
  } else if (strcmp(topic, "cmnd/ledclock/clock/settime") == 0) {
    int timeDelim = message.indexOf(";");
    String datearg = message.substring(0, timeDelim);
    String timearg = message.substring(timeDelim+1);
    char d[12];
    char t[9];
    datearg.toCharArray(d, 12);
    timearg.toCharArray(t, 9);
    RtcDateTime compiled = RtcDateTime(d, t);
    Rtc.SetDateTime(compiled);   
    clockMode = 0;     

 // Countdown    
    // Color
  } else if (strcmp(topic, "cmnd/ledclock/countdown/color") == 0) {
    int firstDelim = message.indexOf(",");
    int secondDelim = message.indexOf(",", firstDelim+1);
    int mqqt_r_val = message.substring(0, firstDelim).toInt();
    int mqqt_g_val = message.substring(firstDelim+1, secondDelim).toInt();
    int mqqt_b_val = message.substring(secondDelim+1).toInt();
    countdownColor = CRGB(mqqt_r_val, mqqt_g_val, mqqt_b_val);
    updateMqttColor(1, mqqt_r_val, mqqt_g_val, mqqt_b_val);
    // Color Paused 
  } else if (strcmp(topic, "cmnd/ledclock/countdown/colorpaused") == 0) {
    int firstDelim = message.indexOf(",");
    int secondDelim = message.indexOf(",", firstDelim+1);
    int mqqt_r_val = message.substring(0, firstDelim).toInt();
    int mqqt_g_val = message.substring(firstDelim+1, secondDelim).toInt();
    int mqqt_b_val = message.substring(secondDelim+1).toInt();
    countdownColorPaused = CRGB(mqqt_r_val, mqqt_g_val, mqqt_b_val);
    updateMqttColor(2, mqqt_r_val, mqqt_g_val, mqqt_b_val);
    // Color Final Min
  } else if (strcmp(topic, "cmnd/ledclock/countdown/colorfinalmin") == 0) {
    int firstDelim = message.indexOf(",");
    int secondDelim = message.indexOf(",", firstDelim+1);
    int mqqt_r_val = message.substring(0, firstDelim).toInt();
    int mqqt_g_val = message.substring(firstDelim+1, secondDelim).toInt();
    int mqqt_b_val = message.substring(secondDelim+1).toInt();
    countdownColorFinalMin = CRGB(mqqt_r_val, mqqt_g_val, mqqt_b_val);
    updateMqttColor(3, mqqt_r_val, mqqt_g_val, mqqt_b_val);
  } else if (strcmp(topic, "cmnd/ledclock/countdown/starttime") == 0) {
    int firstDelim = message.indexOf(":");
    int secondDelim = message.indexOf(":", firstDelim+1);
    int hh_val = message.substring(0, firstDelim).toInt();
    int mm_val = message.substring(firstDelim+1, secondDelim).toInt();
    int ss_val = message.substring(secondDelim+1).toInt();
    if (hh_val > 23)
    hh_val = 23;
    if (mm_val > 59)
    mm_val = 59;
    if (ss_val > 59)
    ss_val = 59;
    timerRunning = false;     //Stop timer if running to set time
    initCountdownMillis =  (hh_val * 3600000) + (mm_val * 60000) + (ss_val * 1000);  //convert to ms
    countdownMilliSeconds = initCountdownMillis;
    remCountdownMillis = initCountdownMillis;
    endCountDownMillis = countdownMilliSeconds + millis();
    clockMode = 1;
    updateMqttCountdownStartTime(initCountdownMillis);
    updateMqttCountdownStatus(timerRunning);
  } else if (strcmp(topic, "cmnd/ledclock/countdown/action") == 0) {  
    int whichAction = message.toInt();
    switch(whichAction) {
      case 0:       // Start/resume countdown
        if (!timerRunning && remCountdownMillis > 0) {
          endCountDownMillis = millis() + remCountdownMillis;
          timerRunning = true;
          clockMode = 1;
          break;
        }
      case 1:       // Pause/stop countdown
        timerRunning = false;
        clockMode = 1;
        break;
      case 2:       // Stop (if running) and reset to last start time
        timerRunning = false;
        countdownMilliSeconds = initCountdownMillis;
        remCountdownMillis = initCountdownMillis;
        endCountDownMillis = countdownMilliSeconds + millis();
        clockMode = 1;
        break;
      case 3:       // Stop (if running), set to 00:00 and clear init start time
        timerRunning = false;
        countdownMilliSeconds = 0;
        endCountDownMillis = 0;
        remCountdownMillis = 0;
        initCountdownMillis = 0;
        updateMqttCountdownStartTime(initCountdownMillis);
        clockMode = 1;
        break; 
           
    }
    updateMqttCountdownStatus(timerRunning);

  // Temperature
    // Color
  } else if (strcmp(topic, "cmnd/ledclock/temperature/color") == 0) {
    int firstDelim = message.indexOf(",");
    int secondDelim = message.indexOf(",", firstDelim+1);
    int mqqt_r_val = message.substring(0, firstDelim).toInt();
    int mqqt_g_val = message.substring(firstDelim+1, secondDelim).toInt();
    int mqqt_b_val = message.substring(secondDelim+1).toInt();
    temperatureColor = CRGB(mqqt_r_val, mqqt_g_val, mqqt_b_val);
    updateMqttColor(4, mqqt_r_val, mqqt_g_val, mqqt_b_val);
    // Symbol (12 = C, 13 = F)
  } else if (strcmp(topic, "cmnd/ledclock/temperature/symbol") == 0) {
    temperatureSymbol = message.toInt();
    clockMode = 2;
    updateMqttTempSymbol(temperatureSymbol);
    updateMqttTemperature();
    // Correction
  } else if (strcmp(topic, "cmnd/ledclock/temperature/correction") == 0) {
    temperatureCorrection = message.toInt();
    clockMode = 2;
    updateMqttTempCorrection();
    updateMqttTemperature();
  
  // Scoreboard
    // Left Color
  } else if (strcmp(topic, "cmnd/ledclock/scoreboard/colorleft") == 0) {
    int firstDelim = message.indexOf(",");
    int secondDelim = message.indexOf(",", firstDelim+1);
    int mqqt_r_val = message.substring(0, firstDelim).toInt();
    int mqqt_g_val = message.substring(firstDelim+1, secondDelim).toInt();
    int mqqt_b_val = message.substring(secondDelim+1).toInt();
    scoreboardColorLeft = CRGB(mqqt_r_val, mqqt_g_val, mqqt_b_val);
    updateMqttColor(5, mqqt_r_val, mqqt_g_val, mqqt_b_val);
    // Right Color
  } else if (strcmp(topic, "cmnd/ledclock/scoreboard/colorright") == 0) {
    int firstDelim = message.indexOf(",");
    int secondDelim = message.indexOf(",", firstDelim+1);
    int mqqt_r_val = message.substring(0, firstDelim).toInt();
    int mqqt_g_val = message.substring(firstDelim+1, secondDelim).toInt();
    int mqqt_b_val = message.substring(secondDelim+1).toInt();
    scoreboardColorRight = CRGB(mqqt_r_val, mqqt_g_val, mqqt_b_val);
    updateMqttColor(6, mqqt_r_val, mqqt_g_val, mqqt_b_val);
  } else if (strcmp(topic, "cmnd/ledclock/scoreboard/scoreleft") == 0) {
    scoreboardLeft = message.toInt();
    if (scoreboardLeft > 99)
      scoreboardLeft = 0;
    clockMode = 3;
    updateMqttScoreboardScores();
  } else if (strcmp(topic, "cmnd/ledclock/scoreboard/scoreright") == 0) {
    scoreboardRight = message.toInt();
    if (scoreboardRight > 99)
      scoreboardRight = 0;
    clockMode = 3;
    updateMqttScoreboardScores();
  } else if (strcmp(topic, "cmnd/ledclock/scoreboard/scoreup") == 0) {
    unsigned int whichScore = message.toInt();     // 0=left score, 1=right score, 2=both
    switch(whichScore) {
      case 0:
        scoreboardLeft = scoreboardLeft + 1;
        if (scoreboardLeft > 99)
          scoreboardLeft = 0;
          break;
      case 1:
        scoreboardRight = scoreboardRight + 1;
        if (scoreboardRight > 99)
          scoreboardRight = 0;
          break;
      case 2:
        scoreboardLeft = scoreboardLeft + 1;
        if (scoreboardLeft > 99)
          scoreboardLeft = 0;
        scoreboardRight = scoreboardRight + 1;
        if (scoreboardRight > 99)
          scoreboardRight = 0;
          break;
    }
    clockMode = 3;
    updateMqttScoreboardScores();
  } else if (strcmp(topic, "cmnd/ledclock/scoreboard/scoredown") == 0) {
    unsigned int whichScore = message.toInt();     // 0=left score, 1=right score, 2=both
    switch(whichScore) {
      case 0:
        if (scoreboardLeft > 0)
          scoreboardLeft = scoreboardLeft - 1;
          break;
      case 1:
        if (scoreboardRight > 0)
          scoreboardRight = scoreboardRight - 1;
          break;
      case 2:
        if (scoreboardLeft > 0)
          scoreboardLeft = scoreboardLeft - 1;
        if (scoreboardRight > 0)
          scoreboardRight = scoreboardRight - 1;
          break;
    }
    clockMode = 3;
    updateMqttScoreboardScores();
  } else if (strcmp(topic, "cmnd/ledclock/scoreboard/reset") == 0) {
    unsigned int whichScore = message.toInt();     // 0=left score, 1=right score, 2=both
    switch(whichScore) {
      case 0:
        scoreboardLeft = 0;
        break;
      case 1:
        scoreboardRight = 0;
        break;
      case 2:
        scoreboardLeft = 0;
        scoreboardRight = 0;
        break;
    }
    clockMode = 3;
    updateMqttScoreboardScores();
  }
}

void loop(){
  // v1.00: OTA Update - on boot will pause for 2.5 seconds (needed to broadcast network mDNS)
  // When OTA flag set via HTML call, time to upload set at 20 sec. via server callback above.  Alter there if more time desired.
  if (ota_flag) {
    displayOTA();
    uint16_t ota_time_start = millis();
    while (ota_time_elapsed < ota_time_window) {
      ArduinoOTA.handle();  
      ota_time_elapsed = millis()-ota_time_start;   
      delay(10); 
    }
    ota_flag = false;
  }

  server.handleClient();
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)
  if (MQTTMODE == 1) {
    if (!client.connected()) {
      long now = millis();
      if (now - lastReconnectAttempt > 60000) {   //attempt reconnect once per minute. Drop usually a result of Home Assistant/broker server restart
        lastReconnectAttempt = now;
        // Attempt to reconnect
        if (reconnect()) {
          lastReconnectAttempt = 0;
        }
      }
    } else {
      // Client connected
      client.loop();
    }
  }
#endif
  int modeReading = digitalRead(MODE_PIN);
  int v1Reading = digitalRead(V1_PIN);
  int v0Reading = digitalRead(V0_PIN);
  int h1_Reading = digitalRead(H1_PIN);
  int h0_Reading = digitalRead(H0_PIN);

  // Mode button (White button)
  if (modeReading == LOW) { 
    clockMode = clockMode + 1; 
    delay(500);
  }
  if (clockMode > 3) {         
    clockMode = 0;
  }

  // V+ button: Increase visitor score / Add 1 min to timer when stopped (Red button)
  if (v1Reading == LOW && h0_Reading == !LOW) {           // Assure blue button not also pressed (resetting)
    if (clockMode == 3) {
      scoreboardLeft = scoreboardLeft + 1;
      if (scoreboardLeft > 99)
        scoreboardLeft = 0;
      if (mqttConnected)
        updateMqttScoreboardScores();
      } else if (clockMode == 1 && !timerRunning) {
      initCountdownMillis = initCountdownMillis + 60000;
      countdownMilliSeconds = initCountdownMillis;
      remCountdownMillis = initCountdownMillis;
      endCountDownMillis = countdownMilliSeconds + millis();
      if (mqttConnected)
        updateMqttCountdownStartTime(initCountdownMillis);
    }
    delay(500);
  }

  // V- button: Decrease visitor score / Add 5 seconds to timer when stopped (Green button)
  if (v0Reading == LOW) {
    if (clockMode == 3) {
      if (scoreboardLeft > 0) 
        scoreboardLeft = scoreboardLeft - 1;
      if (mqttConnected)
        updateMqttScoreboardScores();

    } else if (clockMode == 1 && !timerRunning) {
      initCountdownMillis = initCountdownMillis + 5000;
      countdownMilliSeconds = initCountdownMillis;
      remCountdownMillis = initCountdownMillis;
      endCountDownMillis = countdownMilliSeconds + millis();
      if (mqttConnected)
        updateMqttCountdownStartTime(initCountdownMillis);
    }
    delay(500);
  }

  // H+ button: Increase home score / start countdown (Yellow button)
  if (h1_Reading == LOW) {
    if (clockMode == 3) {
      scoreboardRight = scoreboardRight + 1;
      if (scoreboardRight > 99) 
        scoreboardRight = 0;
      if (mqttConnected)
        updateMqttScoreboardScores();

    } else if (clockMode == 1) {
      if (!timerRunning && remCountdownMillis > 0) {
      endCountDownMillis = millis() + remCountdownMillis;
      timerRunning = true;
      if (mqttConnected)
        updateMqttCountdownStatus(timerRunning);
      }
    }
    delay(500);
  }

  // H- button: Decrease home score / stop countdown (Blue button)
  if (h0_Reading == LOW && v1Reading == !LOW) {           // Assure Red button also not pressed (resetting)
    if (clockMode == 3) {
      if (scoreboardRight > 0) 
        scoreboardRight = scoreboardRight - 1;
      if (mqttConnected)
        updateMqttScoreboardScores();

    } else if (clockMode == 1) {
      if (!timerRunning) {
        // Reset timer to last start value
        countdownMilliSeconds = initCountdownMillis;
        remCountdownMillis = initCountdownMillis;
        endCountDownMillis = countdownMilliSeconds + millis();
      } else {
        timerRunning = false;
        if (mqttConnected)
          updateMqttCountdownStatus(timerRunning);
      }
    }
    delay(500);
  }

  // Both V+ and H- buttons (red and blue) pressed - Reset scores / set coundown timer to 0:00
  if (v1Reading == LOW && h0_Reading == LOW) {
    if (clockMode == 3) {
      scoreboardLeft = 0;
      scoreboardRight = 0;
      if (mqttConnected)
        updateMqttScoreboardScores();
    } else if (clockMode == 1  && !timerRunning) {
      countdownMilliSeconds = 0;
      endCountDownMillis = 0;
      remCountdownMillis = 0;
      initCountdownMillis = 0;
      if (mqttConnected)
        updateMqttCountdownStartTime(initCountdownMillis);
    }
    delay(500);
  }
  
  unsigned long currentMillis = millis();  
  if (currentMillis - prevTime >= 1000) {
    prevTime = currentMillis;

    if (clockMode == 0) {
      updateClock();
    } else if (clockMode == 1) {
      updateCountdown();
    } else if (clockMode == 2) {
      updateTemperature();      
    } else if (clockMode == 3) {
      updateScoreboard();            
    } 


    FastLED.setBrightness(brightness);
    FastLED.show();
    // Update MQTT here if enabled and connected
    if (mqttConnected) {
      unsigned long currTempMillis = millis();
      // Mode
      if (clockMode != oldMode) {
        updateMqttMode();
        oldMode = clockMode;
      }
      if ((temperatureUpdatePeriod > 0) && (currTempMillis - prevTempTime >= temperatureUpdatePeriod)) { 
        prevTempTime = currTempMillis;     
        updateMqttTemperature();
      }
    }
  }   
}

void displayNumber(byte number, byte segment, CRGB color) {
  /*
   * 
      __ __ __        __ __ __          __ __ __        12 13 14  
    __        __    __        __      __        __    11        15
    __        __    __        __      __        __    10        16
    __        __    __        __  42  __        __    _9        17
      __ __ __        __ __ __          __ __ __        20 19 18  
    __        65    __        44  43  __        21    _8        _0
    __        __    __        __      __        __    _7        _1
    __        __    __        __      __        __    _6        _2
      __ __ __       __ __ __           __ __ __       _5 _4 _3   
   */
 
  // segment from left to right: 3, 2, 1, 0
  byte startindex = 0;
  switch (segment) {
    case 0:
      startindex = 0;
      break;
    case 1:
      startindex = 21;
      break;
    case 2:
      startindex = 44;
      break;
    case 3:
      startindex = 65;
      break;    
  }

  for (byte i=0; i<21; i++){
    yield();
    LEDs[i + startindex] = ((numbers[number] & 1 << i) == 1 << i) ? color : alternateColor;
  } 
}

void allBlank() {
  for (int i=0; i<NUM_LEDS; i++) {
    LEDs[i] = CRGB::Black;
  }
  FastLED.show();
}

void updateClock() {  
  RtcDateTime now = Rtc.GetDateTime();
  // printDateTime(now);    

  int hour = now.Hour();
  int mins = now.Minute();
  int secs = now.Second();

  if (hourFormat == 12 && hour > 12) {
    hour = hour - 12;
  } else if (hourFormat == 12 && hour == 0) {     //fix for midnight - 1 am, where previously showed "0" for hour
    hour = 12;
  }
    
  
  byte h1 = hour / 10;
  byte h2 = hour % 10;
  byte m1 = mins / 10;
  byte m2 = mins % 10;  
  byte s1 = secs / 10;
  byte s2 = secs % 10;
  
  CRGB color = clockColor;     //CRGB(r_val, g_val, b_val);

  if (h1 > 0 || (hourFormat == 24 && hour == 0))  //display leading zero for midnight when 24-hour display
    displayNumber(h1,3,color);
  else 
    displayNumber(10,3,color);  // Blank
    
  displayNumber(h2,2,color);
  displayNumber(m1,1,color);
  displayNumber(m2,0,color); 

  displayDots(color);  
}

void updateCountdown() {

  if (countdownMilliSeconds == 0 && endCountDownMillis == 0) {
    // displayNumber(0,3,CRGB::Orange);
    // displayNumber(0,2,CRGB::Orange);
    // displayNumber(0,1,CRGB::Orange);
    // displayNumber(0,0,CRGB::Orange);  
    // LEDs[42] = CRGB::Orange;
    // LEDs[43] = CRGB::Orange;
    displayNumber(0,3,countdownColorPaused);
    displayNumber(0,2,countdownColorPaused);
    displayNumber(0,1,countdownColorPaused);
    displayNumber(0,0,countdownColorPaused);  
    LEDs[42] = countdownColorPaused;
    LEDs[43] = countdownColorPaused;
    return;    
  }

  if (!timerRunning) {
    unsigned long restMillis = remCountdownMillis;
    unsigned long hours = ((restMillis / 1000) / 60) / 60;
    unsigned long minutes = (restMillis / 1000) / 60;
    unsigned long seconds = restMillis / 1000;
    int remSeconds = seconds - (minutes * 60);
    int remMinutes = minutes - (hours * 60); 
    byte h1 = hours / 10;
    byte h2 = hours % 10;
    byte m1 = remMinutes / 10;
    byte m2 = remMinutes % 10;  
    byte s1 = remSeconds / 10;
    byte s2 = remSeconds % 10;
    if (hours > 0) {
      // hh:mm
      displayNumber(h1,3,countdownColorPaused); 
      displayNumber(h2,2,countdownColorPaused);
      displayNumber(m1,1,countdownColorPaused);
      displayNumber(m2,0,countdownColorPaused);  
    } else {
      // mm:ss   
      displayNumber(m1,3,countdownColorPaused);
      displayNumber(m2,2,countdownColorPaused);
      displayNumber(s1,1,countdownColorPaused);
      displayNumber(s2,0,countdownColorPaused);  
    }
    LEDs[42] = countdownColorPaused;
    LEDs[43] = countdownColorPaused;
    return;
  }
    
  unsigned long restMillis = endCountDownMillis - millis();
  unsigned long hours   = ((restMillis / 1000) / 60) / 60;
  unsigned long minutes = (restMillis / 1000) / 60;
  unsigned long seconds = restMillis / 1000;
  int remSeconds = seconds - (minutes * 60);
  int remMinutes = minutes - (hours * 60); 
  
  byte h1 = hours / 10;
  byte h2 = hours % 10;
  byte m1 = remMinutes / 10;
  byte m2 = remMinutes % 10;  
  byte s1 = remSeconds / 10;
  byte s2 = remSeconds % 10;

  remCountdownMillis = restMillis;        //Store current remaining in event of pause

  CRGB color = countdownColor;
  if (restMillis <= 60000) {
    color = countdownColorFinalMin;             //CRGB::Red;
  }

  if (hours > 0) {
    // hh:mm
    displayNumber(h1,3,color); 
    displayNumber(h2,2,color);
    displayNumber(m1,1,color);
    displayNumber(m2,0,color);  
  } else {
    // mm:ss   
    displayNumber(m1,3,color);
    displayNumber(m2,2,color);
    displayNumber(s1,1,color);
    displayNumber(s2,0,color);  
  }

  displayDots(color);  

  if (hours <= 0 && remMinutes <= 0 && remSeconds <= 0) {
    //endCountdown();
    countdownMilliSeconds = 0;
    endCountDownMillis = 0;
    remCountdownMillis = 0;
    timerRunning = false;
    FastLED.setBrightness(brightness);
    FastLED.show();
    digitalWrite(COUNTDOWN_OUTPUT, HIGH);
    delay(2000);  //sound for 2 seconds
    digitalWrite(COUNTDOWN_OUTPUT, LOW);
    updateMqttCountdownStatus(timerRunning);
    return;
  }  
}

void endCountdown() {
  allBlank();
  for (int i=0; i<NUM_LEDS; i++) {
    if (i>0)
      LEDs[i-1] = CRGB::Black;
    
    LEDs[i] = CRGB::Red;
    FastLED.show();
    delay(25);
  }  
}

void soundBuzzer(unsigned int buzzLength) {
    digitalWrite(COUNTDOWN_OUTPUT, HIGH);
    delay(buzzLength);  //time in millis
    digitalWrite(COUNTDOWN_OUTPUT, LOW);
  
}

void displayDots(CRGB color) {
  if (dotsOn) {
    LEDs[42] = color;
    LEDs[43] = color;
  } else {
    LEDs[42] = CRGB::Black;
    LEDs[43] = CRGB::Black;
  }

  dotsOn = !dotsOn;  
}

void hideDots() {
  LEDs[42] = CRGB::Black;
  LEDs[43] = CRGB::Black;
}

void updateTemperature() {
  RtcTemperature temp = Rtc.GetTemperature();
  float ftemp = temp.AsFloatDegC();
  // float ctemp = ftemp + temperatureCorrection;   // Apply correction after conversion to F

  if (temperatureSymbol == 13)
    ftemp = (ftemp * 1.8000) + 32;
    //ctemp = (ctemp * 1.8000) + 32;

  float ctemp = ftemp + temperatureCorrection;
  byte t1 = int(ctemp) / 10;
  byte t2 = int(ctemp) % 10;
  //CRGB color = CRGB(r_val, g_val, b_val);
  displayNumber(t1,3,temperatureColor);
  displayNumber(t2,2,temperatureColor);
  displayNumber(11,1,temperatureColor);
  displayNumber(temperatureSymbol,0,temperatureColor);
  hideDots();
}

void updateScoreboard() {
  byte sl1 = scoreboardLeft / 10;
  byte sl2 = scoreboardLeft % 10;
  byte sr1 = scoreboardRight / 10;
  byte sr2 = scoreboardRight % 10;

  displayNumber(sl1,3,scoreboardColorLeft);
  displayNumber(sl2,2,scoreboardColorLeft);
  displayNumber(sr1,1,scoreboardColorRight);
  displayNumber(sr2,0,scoreboardColorRight);
  hideDots();
}

void displayOTA() {
  //Display UPLd message while actively waiting OTA upload to start
  CRGB otaColor = CRGB::Red;
  displayNumber(14,3,otaColor);  //U
  displayNumber(15,2,otaColor);  //P
  displayNumber(16,1,otaColor);  //L
  displayNumber(17,0,otaColor);  //d
  hideDots();
  FastLED.setBrightness(brightness);
  FastLED.show();
}

// ******* MQTT Updating ***********
void updateMqttMode() {
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)
  switch (clockMode) {
    case 0:
      client.publish("stat/ledclock/mode", "Clock", true);
      break;
    case 1:
      client.publish("stat/ledclock/mode", "Countdown", true);
      break;
    case 2:
      client.publish("stat/ledclock/mode", "Temperature", true);
      break;
    case 3:
      client.publish("stat/ledclock/mode", "Scoreboard", true);
      break;
  }
#endif
}

void updateMqttBrightness(unsigned int bright_val) {
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)
  char outBright[5];
  sprintf(outBright, "%3u", bright_val);
  client.publish("stat/ledclock/brightness", outBright, true);
#endif
}

void updateMqttClockDisplay(unsigned int display_val) {
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)
  char outDisplay[3];
  sprintf(outDisplay, "%3u", display_val);
  client.publish("stat/ledclock/clock/display", outDisplay, true);
#endif
}

void updateMqttColor(unsigned int whichMode, unsigned int r_val, unsigned int g_val, unsigned int b_val) {
  // whichmode: 0 Clock, 1 Countdown, 2 CountdownPaused, 3 CountdownFinalMin, 4 Temp, 5 ScoreboardLeft, 6 ScoreboardRight
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)
  char outRed[5];
  char outGreen[5];
  char outBlue[5];
  sprintf(outRed, "%3u", r_val);
  sprintf(outGreen, "%3u", g_val);
  sprintf(outBlue, "%3u", b_val);
  switch (whichMode) {
    case 0:   //clock
      client.publish("stat/ledclock/clock/color/red", outRed, true);
      client.publish("stat/ledclock/clock/color/green", outGreen, true);
      client.publish("stat/ledclock/clock/color/blue", outBlue, true);
      break;
    case 1:   //countdown
      client.publish("stat/ledclock/countdown/color/red", outRed, true);
      client.publish("stat/ledclock/countdown/color/green", outGreen, true);
      client.publish("stat/ledclock/countdown/color/blue", outBlue, true);
      break;
    case 2:   //countdownPaused
      client.publish("stat/ledclock/countdown/colorpaused/red", outRed, true);
      client.publish("stat/ledclock/countdown/colorpaused/green", outGreen, true);
      client.publish("stat/ledclock/countdown/colorpaused/blue", outBlue, true);
      break;
    case 3:   //countdownFinalMin
      client.publish("stat/ledclock/countdown/colorfinalmin/red", outRed, true);
      client.publish("stat/ledclock/countdown/colorfinalmin/green", outGreen, true);
      client.publish("stat/ledclock/countdown/colorfinalmin/blue", outBlue, true);
      break;
    case 4:   //temp
      client.publish("stat/ledclock/temperature/color/red", outRed, true);
      client.publish("stat/ledclock/temperature/color/green", outGreen, true);
      client.publish("stat/ledclock/temperature/color/blue", outBlue, true);
      break;
    case 5:   //scoreboard left
      client.publish("stat/ledclock/scoreboard/colorleft/red", outRed, true);
      client.publish("stat/ledclock/scoreboard/colorleft/green", outGreen, true);
      client.publish("stat/ledclock/scoreboard/colorleft/blue", outBlue, true);
      break;
    case 6:   //scoreboard right
      client.publish("stat/ledclock/scoreboard/colorright/red", outRed, true);
      client.publish("stat/ledclock/scoreboard/colorright/green", outGreen, true);
      client.publish("stat/ledclock/scoreboard/colorright/blue", outBlue, true);
      break;
  }
#endif
}

void updateMqttCountdownStartTime(unsigned long initMillis) {
  //  Break into hh, mm and ss and post as hh:mm:ss
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)
  unsigned long hours   = ((initMillis / 1000) / 60) / 60;
  unsigned long minutes = (initMillis / 1000) / 60;
  unsigned long seconds = initMillis / 1000;
  int remSeconds = seconds - (minutes * 60);
  int remMinutes = minutes - (hours * 60); 
  byte h1 = hours / 10;
  byte h2 = hours % 10;
  byte m1 = remMinutes / 10;
  byte m2 = remMinutes % 10;  
  byte s1 = remSeconds / 10;
  byte s2 = remSeconds % 10;
  char outTime[10];
  sprintf(outTime, "%1u%1u:%1u%1u:%1u%1u", h1, h2, m1, m2, s1, s2);
  client.publish("stat/ledclock/countdown/starttime", outTime, true); 
#endif
}

void updateMqttCountdownStatus(bool curStatus) {
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)
  if (curStatus) {
    client.publish("stat/ledclock/countdown/status", "Running", true);
  } else {
    client.publish("stat/ledclock/countdown/status", "Stopped", true);
  }
#endif
}

void updateMqttTempSymbol(unsigned int symbolVal) {
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)  
  switch(symbolVal) {
    case 12:
      client.publish("stat/ledclock/temperature/symbol", "C", true);
      break;
    case 13:
      client.publish("stat/ledclock/temperature/symbol", "F", true);
      break;
    default:
      client.publish("stat/ledclock/temperature/symbol", "?", true);
      break;
  }
#endif
}

void updateMqttTempCorrection() {
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)
  char outCorr[10];
  sprintf(outCorr, "%4.1f", temperatureCorrection);
  client.publish("stat/ledclock/temperature/correction", outCorr, true);
#endif
}

void updateMqttTemperature() {
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)
  RtcTemperature temp = Rtc.GetTemperature();
  float ftemp = temp.AsFloatDegC();
  // float ctemp = ftemp + temperatureCorrection;      // Add correction after conversion??

  if (temperatureSymbol == 13) {
    //ctemp = (ctemp * 1.8000) + 32; 
    ftemp = (ftemp * 1.8000) + 32;   
  }
  float ctemp = ftemp + temperatureCorrection;
  char outTemp[10];
  sprintf(outTemp, "%4.1f", ctemp);
  client.publish("stat/ledclock/temperature", outTemp, true);
#endif
}

void updateMqttScoreboardScores() {
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)
  char outLeft[5];
  char outRight[5];
  sprintf(outLeft, "%3u", scoreboardLeft);
  sprintf(outRight, "%3u", scoreboardRight);
  client.publish("stat/ledclock/scoreboard/scoreleft", outLeft, true);
  client.publish("stat/ledclock/scoreboard/scoreright", outRight, true);
#endif
}
// **********************************
void printDateTime(const RtcDateTime& dt)
{
    char datestring[20];

    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
            dt.Month(),
            dt.Day(),
            dt.Year(),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
}
