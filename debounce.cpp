// #include <Arduino.h>
#include <ESP8266WiFi.h>

#include "config.h"
#include "debounce.h"

void Debounce::call(void (*onChange)(void)) {
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

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == HIGH) {
        printf("button: high edge\n");
        buttonStateFixed = !buttonStateFixed;
        (*onChange)();
      }
      // todo PATCH //hub/controls/:id?value=:buttonState
    }
  }
}
