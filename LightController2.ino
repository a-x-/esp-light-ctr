#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
// #include "McOffskyESPHttpClient.h"

//
// consts
//

// TODO: use EEPROM or config file
// const char DEV_TYPE[] = "controller-slave";
// const char DEV_ID[] = "lc--crd--1"; // light-controller--corridor--1

// const char MASTER_HOST[] = "rpi2main.local";

const char SSID[] = "YOUR SSID";
const char PASSW[] = "YOUR PASSWD";


ESP8266WebServer server(80);

const int startTime = millis();

//
// pins
//

//const int led = 13;
const int pirPin = 12;    // IN
const int lightPin = 5;   // OUT; relay
const int buttonPin = 14; // manual switcher

//
// debounce
//

int buttonState;           // the current reading from the input pin
int lastButtonState = LOW; // the previous reading from the input pin
int buttonStateFixed;      // switch button state converted from push-button state
bool isAutonomous = true;
unsigned int autonomousDisaledAt = null; // todo

// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0; // the last time the output pin was toggled
unsigned long debounceDelay = 50;   // the debounce time; increase if the output flickers

//
//
//

int lastMovingTime = 0;
int prevMoving = false;

//
// life-cycle
//

void setup(void)
{
  //ets_intr_lock(15); // all intruptt off
  //ets_wdt_disable();

  pinMode(buttonPin, INPUT);
  pinMode(pirPin, INPUT);
  pinMode(lightPin, OUTPUT);

  //  pinMode(led, OUTPUT);
  //  digitalWrite(led, 0);
  Serial.begin(1000000);
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

  if (MDNS.begin(DEV_ID))
  {
    Serial.println("MDNS responder started");
  }

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
    server.send(200, "text/plain", isLightOn() ? "on" : "off");
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

void loop(void)
{
  server.handleClient();
  debounceLightButton();
  reactLightOnMovement();
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

bool toggleLight(int isOn)
{
  digitalWrite(lightPin, isOn);
  // reportLightState(isOn);
  return isLightOn();
}
bool isLightOn(void)
{
  return digitalRead(lightPin);
}
bool hasMoving(void)
{
  prevMoving = digitalRead(pirPin);
  return prevMoving;
}
bool isButtonPressed(void)
{
  return buttonStateFixed;
}
const String getUptime(void)
{
  int delta = millis() - startTime;
  //  char res[50];

  int m = int((delta / (1000 * 60)) % 60);
  int h = int((delta / (1000 * 60 * 60)) % 24);
  int d = int((delta / (1000 * 60 * 60 * 24)) % 365);

  //  os_sprintf(res, "%1.0d days, %2.0d:%2.0d:", d, h, m);
  //  sprintf(res, "%d", delta);
  //  return res; //f2s(delta, 0);
  return (String(d) + " days, " + String(h) + ":" + String(m));
}

//
// utils
//

// /* float to string
//  * f is the float to turn into a string
//  * p is the precision (number of decimals)
//  * return a string representation of the float.
//  */
// char *f2s(float f, int p)
// {
//   char *pBuff;                  // use to remember which part of the buffer to use for dtostrf
//   const int iSize = 10;         // number of bufffers, one for each float before wrapping around
//   static char sBuff[iSize][20]; // space for 20 characters including NULL terminator for each float
//   static int iCount = 0;        // keep a tab of next place in sBuff to use
//   pBuff = sBuff[iCount];        // use this buffer
//   if (iCount >= iSize - 1)
//   {             // check for wrap
//     iCount = 0; // if wrapping start again and reset
//   } else {
//     iCount++; // advance the counter
//   }
//   return dtostrf(f, 0, p, pBuff); // call the library function
// }

// void toggleAutonomousMode () {
//   if (isAutonomous) {
//     isAutonomous = false;
//     // todo: disable for 8 hours
//   } else {
//     isAutonomous = true;
//   }
// }

void debounceLightButton()
{
  // read the state of the switch into a local variable:
  int reading = digitalRead(buttonPin);

  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH), and you've waited long enough
  // since the last press to ignore any noise:

  // If the switch changed, due to noise or pressing:
  if (reading != lastButtonState)
  {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }
  // save the reading. Next time through the loop, it'll be the lastButtonState:
  lastButtonState = reading;

  if ((millis() - lastDebounceTime) > debounceDelay)
  {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != buttonState)
    {
      buttonState = reading;
      if (buttonState == HIGH) {
        buttonStateFixed = !buttonStateFixed;
      }
      // toggleAutonomousMode();
      // todo PATCH //hub/controls/:id?value=:buttonState
    }
  }
}

void reactLightOnMovement()
{
  if (hasMoving())
  {
    toggleLight(1);
    if (!prevMoving)
      printf("> moving <");
    lastMovingTime = millis();
  }
  else if (millis() - lastMovingTime > 3600 * 1000)
  {
    toggleLight(0);
    printf("moving timeout --> turn light off");
  }
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
