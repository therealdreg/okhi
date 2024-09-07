#include <Arduino.h>
#line 1 "C:\\Users\\dreg\\Desktop\\BlinkRGB\\BlinkRGB.ino"
int LED_BUILTIN = 2;
#line 2 "C:\\Users\\dreg\\Desktop\\BlinkRGB\\BlinkRGB.ino"
void setup();
#line 5 "C:\\Users\\dreg\\Desktop\\BlinkRGB\\BlinkRGB.ino"
void loop();
#line 2 "C:\\Users\\dreg\\Desktop\\BlinkRGB\\BlinkRGB.ino"
void setup() {
pinMode (LED_BUILTIN, OUTPUT);
}
void loop() {
digitalWrite(LED_BUILTIN, HIGH);
delay(1000);
digitalWrite(LED_BUILTIN, LOW);
delay(1000);
}
