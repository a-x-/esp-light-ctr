// #include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
// #include "McOffskyESPHttpClient.h"

#include "consts.h"
#include "config.h"
#include "ntp.h"
#include "debounce.h"

//
// consts
//

// see config.h, consts.h
// const char SSID[] = "";
// const char PASSW[] = "";
// const int pirPin = 12;    // IN
// const int lightPin = 5;   // OUT; relay
// const int buttonPin = 14; // manual switcher

ESP8266WebServer server(80);

const unsigned long startTime = millis();

Debounce debounceButton(/* delay */ 50);

//
//
//

int lastMovingTime = 0;
bool isLightOn = false;
bool prevMoving = false;
unsigned long startedAt = null; // s since 1970 aka epoch
bool isAutonomous = true;
unsigned long autonomousDisaledAt = null; // ms

//
// life-cycle
//

void setup(void)
{
  Serial.begin(1000000);
  setupPins();
  setupWifi();
  setupServer();
  startedAt = setupNtpClient();
  if (MDNS.begin(DEV_ID)) Serial.println("MDNS responder started");
}

void loop(void)
{
  server.handleClient();
  debounceButton.call([]() {
    toggleLight(debounceButton.buttonStateFixed);
    toggleAutonomousMode();
  });
  reactLightOnMovement();
  checkAutonomousMode();
}

//
// handlers
//

void handleRoot()
{
  //  digitalWrite(led, 1);
  server.send(200, "text/plain", "hello from light controller #1 on esp8266!");
  //  digitalWrite(led, 0);
}

void handleNotFound()
{
  //  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  //  digitalWrite(led, 0);
}

bool toggleLight(bool isOn)
{
  if (isLightOn == isOn) return isOn;
  
  digitalWrite(lightPin, isOn);
  isLightOn = isOn;
  // reportLightState(isOn);
  return isLightOn;
}

//
// utils
//

void setupPins () {
  pinMode(buttonPin, INPUT);
  pinMode(pirPin, INPUT);
  pinMode(lightPin, OUTPUT);
  //  pinMode(led, OUTPUT);
  //  digitalWrite(led, 0);
  isLightOn = digitalRead(lightPin);
}

void setupWifi () {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSW);
  Serial.println("");

  Serial.println("Wait for connection");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

}

void setupServer () {
  server.on("/", handleRoot);

  // Commands
  server.on("/on", []() {
    server.send(200, "text/plain", toggleLight(1) ? "on" : "off");
  });
  server.on("/off", []() {
    server.send(200, "text/plain", toggleLight(0) ? "on" : "off");
  });

  // Props
  server.on("/status", []() {
    server.send(200, "text/plain", isLightOn ? "on" : "off");
  });
  server.on("/moving", []() {
    server.send(200, "text/plain", hasMoving() ? "yes" : "no");
  });
  server.on("/pressed", []() {
    server.send(200, "text/plain", isButtonPressed() ? "yes" : "no");
  });
  server.on("/uptime", []() {
    server.send(200, "text/plain", getUptime());
  });

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

bool hasMoving(void)
{
  return digitalRead(pirPin);
}
bool isButtonPressed(void)
{
  return debounceButton.buttonStateFixed;
}
const String getUptime(void)
{
  unsigned int delta = millis() - startTime;
  //  char res[50];

  unsigned int m = (unsigned int)((delta / (1000 * 60)) % 60);
  unsigned int h = (unsigned int)((delta / (1000 * 60 * 60)) % 24);
  unsigned int d = (unsigned int)((delta / (1000 * 60 * 60 * 24)) % 365);

  return (String(d) + " days, " + String(h) + ":" + String(m));
}


bool getNightTime () {
  unsigned long hour = (getTimestamp() % 86400L) / 3600;
  return hour > 0 && hour < 9;
}

// @returns epoch secs
unsigned long getTimestamp () {
  return startedAt + millis() / 1000;
}

void toggleAutonomousMode()
{
  printf("toggleAutonomousMode\n");
  if (isAutonomous)
  {
    isAutonomous = false;
    // disable for 8 hours
    autonomousDisaledAt = millis();
    printf(" -> off\n");
  }
  else
  {
    // turn light on, enable autonomous mode again
    isAutonomous = true;
    autonomousDisaledAt = null;
    printf(" -> on\n");
  }
}

void checkAutonomousMode()
{
  if (millis() - autonomousDisaledAt > 8 * HOUR)
  {
    isAutonomous = true;
    autonomousDisaledAt = null;
    printf("checkAutonomousMode -> ON!\n");
  }
}

void reactLightOnMovement()
{
  bool moving = hasMoving();
  bool shouldBeLight = millis() - lastMovingTime <= HOUR;
  if (moving) lastMovingTime = millis();
  
  toggleLight(!getNightTime() && shouldBeLight);

  if (prevMoving != moving) {
    if (getNightTime()) printf("\nNight time --> No light\n");
    if (!shouldBeLight) printf("moving timeout --> turn light off\n");
    if (moving)         printf("> moving <\n");
  }

  prevMoving = moving;
}

// void reportMoving()
// {
//   const int moving = hasMoving();
//   if (hasMoving() != prevMoving)
//   {
//     // PATCH //hub/sensors/:id?value=:moving
//   }
// }

// void reportLightState(int state)
// {
//   // PATCH //hub/actuators/:id?value=:state
//   reportAnyState("actuators", "")
// }

// void reportAnyState(char *devType, char *devId, int state)
// {
//   wifi.sendHttpRequest(
//       MASTER_HOST, 80,
//       "PATCH",
//       (String(devType) + "/home-hub/" + String(devId)).c_str(),
//       String("{\"value\": \"") + String(state) + String("\"}"), "?test=1");
// }
