// #include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
// #include "McOffskyESPHttpClient.h"
#include "config.h"

#define null 0

//

// consts
//

const unsigned int HOUR = 1000 * 3600; // in ms
const unsigned long corrTZ = 3; // +3h

// see config.h
// const char SSID[] = "";
// const char PASSW[] = "";

ESP8266WebServer server(80);

const unsigned long startTime = millis();


//
// NTP
//
unsigned int ntpLocalPort = 2390;      // local port to listen for UDP packets

/* Don't hardwire the IP address or we won't get the benefits of the pool.
    Lookup the IP address for the host name instead */
//IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

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
unsigned long autonomousDisaledAt = null; // ms
unsigned long startedAt = null; // s since 1970 aka epoch

// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0; // the last time the output pin was toggled
unsigned long debounceDelay = 50;   // the debounce time; increase if the output flickers

//
//
//

int lastMovingTime = 0;
int prevMoving = false;
bool isLightOn = false;

//
// life-cycle
//

void setup(void)
{
  //ets_intr_lock(15); // all intruptt off
  //ets_wdt_disable();

  //
  // pins
  //
  
  pinMode(buttonPin, INPUT);
  pinMode(pirPin, INPUT);
  pinMode(lightPin, OUTPUT);
  //  pinMode(led, OUTPUT);
  //  digitalWrite(led, 0);
  isLightOn = digitalRead(lightPin);

  //
  // serial, wifi
  //
  
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

  //
  // mDNS
  //
  
  if (MDNS.begin(DEV_ID))
  {
    Serial.println("MDNS responder started");
  }

  //
  // http server
  //
  
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

  //
  // ntp client
  //

  Serial.println("Starting UDP");
  udp.begin(ntpLocalPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  getNtpTime();

}

void loop(void)
{
  server.handleClient();
  debounceLightButton();
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

bool toggleLight(int isOn)
{
  digitalWrite(lightPin, isOn);
  isLightOn = isOn;
  // reportLightState(isOn);
  return isLightOn;
}
bool hasMoving(void)
{
  return digitalRead(pirPin);
}
bool isButtonPressed(void)
{
  return buttonStateFixed;
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

//
// utils
//


// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress& address) {
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}


void getNtpTime () {
  printf("NTP Client: get a random server from the pool\n");
  WiFi.hostByName(ntpServerName, timeServerIP);

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(1000);

  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
  } else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = ");
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    startedAt = epoch;
    // print Unix time:
    Serial.println(epoch);


    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    Serial.print((epoch  % 86400L) / 3600 + corrTZ); // print the hour (86400 equals secs per day)
    Serial.print(':');
    if (((epoch % 3600) / 60) < 10) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ((epoch % 60) < 10) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.println(epoch % 60); // print the second
  }
} // getNtpTime

bool getNightTime () {
  unsigned long hour = (getTimestamp() % 86400L) / 3600;
  return hour > 0 && hour < 9;
}

// @returns epoch secs
unsigned long getTimestamp () {
  return startedAt + millis() / 1000 + corrTZ * 3600;
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
      if (buttonState == HIGH)
      {
        buttonStateFixed = !buttonStateFixed;
        printf("button: high edge\n");
      }
      toggleLight(buttonStateFixed);
      toggleAutonomousMode();
      // todo PATCH //hub/controls/:id?value=:buttonState
    }
  }
}

void reactLightOnMovement()
{
  bool moving = hasMoving();
  if (getNightTime() && isLightOn) {
    printf("\nNight time --> No light\n");
    toggleLight(0);
    return;
  }

  if (millis() - lastMovingTime > HOUR && isLightOn) {
    printf("moving timeout --> turn light off\n");
    toggleLight(0);
    return;
  }
  
  if (moving) {
    if (!isLightOn) {
      toggleLight(1);
      printf("> moving <\n");
    }
    lastMovingTime = millis();
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
