#ifndef __COM_ESP_C__
#define __COM_ESP_C__

const char *password = "0123456789";

// #define AS_WIFI_CLIENT 1 // ONLY FOR DEVELOPMENT
#ifdef AS_WIFI_CLIENT
#warning "AS_WIFI_CLIENT defined"
#endif

void wifi_set(const char *ssid)
{
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
    while (WiFi.status() != WL_CONNECTED)
    { // Wait for the Wi-Fi to connect
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
}

#endif // __COM_ESP_C__
