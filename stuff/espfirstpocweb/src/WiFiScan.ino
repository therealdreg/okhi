#include <WiFi.h>
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

// Configuración del WiFi
const char* ssid = "ESP";
const char* password = "0123456789";

// Configuración del servidor web
WebServer server(80);

// Buffer para almacenar datos y variables para manejar el buffer
const int BUFFER_SIZE = 512;
char buffer[BUFFER_SIZE];
volatile int bufferIndex = 0;

// Handle para el temporizador de FreeRTOS
TimerHandle_t timer;

// Función que se llama cuando se accede a la página raíz
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>Datos del Buffer</title>"
                "<script>"
                "function fetchBuffer() {"
                "  fetch('/buffer').then(response => response.text()).then(data => {"
                "    document.getElementById('bufferData').value = data;"
                "    document.getElementById('hexData').value = stringToHex(data);"
                "  });"
                "}"
                "function stringToHex(str) {"
                "  var hex = '';"
                "  for (var i = 0; i < str.length; i++) {"
                "    hex += str.charCodeAt(i).toString(16).padStart(2, '0') + ' ';"
                "  }"
                "  return hex.trim();"
                "}"
                "setInterval(fetchBuffer, 1000);"
                "</script>"
                "</head><body>"
                "<h1>Datos del Buffer</h1>"
                "<form>"
                "  <label for='bufferData'>Buffer:</label><br>"
                "  <textarea id='bufferData' rows='10' cols='50' readonly></textarea><br><br>"
                "</form>"
                "<form>"
                "  <label for='hexData'>Hexadecimal:</label><br>"
                "  <textarea id='hexData' rows='10' cols='50' readonly></textarea><br><br>"
                "</form>"
                "</body></html>";
  server.send(200, "text/html", html);
}

// Función que se llama cuando se accede a la ruta /buffer
void handleBuffer() {
  server.send(200, "text/plain", buffer);
}

// Función callback del temporizador que actualiza el buffer
void IRAM_ATTR onTimer(TimerHandle_t xTimer) {
  if (bufferIndex < BUFFER_SIZE - 1) {
    buffer[bufferIndex++] = 'A'; // Simulando la recepción de datos
    buffer[bufferIndex] = '\0';  // Asegurar que el buffer siempre esté terminado en nulo
  }
}

void setup() {
  Serial.begin(74880);
  delay(500);

  // Configuración del punto de acceso WiFi
  Serial.println("Configurando punto de acceso...");
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

  // Configurar y activar el temporizador de FreeRTOS
  timer = xTimerCreate("MyTimer", pdMS_TO_TICKS(1000), pdTRUE, (void*)0, onTimer);
  xTimerStart(timer, 0);
}

void loop() {
  server.handleClient();
}
