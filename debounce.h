#pragma once


// #include <Arduino.h>
#include <ESP8266WiFi.h>


class Debounce {
  public:
    bool buttonStateFixed = false;      // switch button state converted from push-button state
    
    // @param debounceDelay - the debounce time; increase if the output flickers
    Debounce (unsigned long _debounceDelay): debounceDelay(_debounceDelay) {}
    void call(void (*onChange)(void));

  private:
    bool buttonState = false;           // the current reading from the input pin
    bool lastButtonState = LOW;         // the previous reading from the input pin
    unsigned long debounceDelay;        // the debounce time; increase if the output flickers

    // the following variables are unsigned longs because the time, measured in
    // milliseconds, will quickly become a bigger number than can be stored in an int.
    unsigned long lastDebounceTime = 0; // the last time the output pin was toggled
};
