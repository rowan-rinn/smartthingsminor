#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>

#define TURBIDITY_PIN 34 // Analoge pin voor de turbidity-sensor
#define LED_PIN 2        // GPIO 2 voor de LED

WebServer server(80); // Webserver op poort 80

// Functie: HTML-header met CSS en JavaScript voor real-time updates
String getHtmlHeader() {
  return "<!DOCTYPE html>\n"
         "<html>\n"
         "<head>\n"
         "<title>ESP32 Webinterface</title>\n"
         "<style>\n"
         "  body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; background-color: #f4f4f9; }\n"
         "  h1 { color: #333; }\n"
         "  p { color: #666; font-size: 18px; }\n"
         "  .container { max-width: 600px; margin: auto; padding: 20px; }\n"
         "  button { padding: 10px 20px; font-size: 16px; color: #fff; background-color: #007BFF; border: none; border-radius: 5px; cursor: pointer; }\n"
         "  button:hover { background-color: #0056b3; }\n"
         "  .link { text-decoration: none; }\n"
         "  .card { margin: 20px auto; padding: 15px; border-radius: 8px; background: #fff; box-shadow: 0 4px 8px rgba(0,0,0,0.2); }\n"
         "</style>\n"
         "<script>\n"
         "  function updateTurbidity() {\n"
         "    fetch('/turbidity/data')\n"
         "      .then(response => response.json())\n"
         "      .then(data => {\n"
         "        document.getElementById('turbidity').innerText = data.turbidity + ' NTU';\n"
         "        document.getElementById('voltage').innerText = data.voltage + ' V';\n"
         "        document.getElementById('time').innerText = new Date().toLocaleTimeString();\n"
         "      })\n"
         "      .catch(error => {\n"
         "        console.error('Fout bij ophalen van turbidity data:', error);\n"
         "      });\n"
         "  }\n"
         "  setInterval(updateTurbidity, 1000); // Update elke seconde\n"
         "</script>\n"
         "</head>\n"
         "<body>\n"
         "<div class='container'>\n";
}


// Functie: HTML-footer
String getHtmlFooter() {
  return "</div></body></html>";
}

// Functie: Hoofdpagina
void handleRoot() {
  String html = getHtmlHeader();
  html += "<h1>ESP32 Webinterface</h1>\
           <div class='card'>\
             <p>Controleer de LED-status of bekijk turbidity-informatie:</p>\
             <p><a href='/led/on' class='link'><button>LED Aan</button></a></p>\
             <p><a href='/led/off' class='link'><button>LED Uit</button></a></p>\
           </div>\
           <div class='card'>\
             <h2>Realtime Turbidity</h2>\
             <p><strong>Turbidity:</strong> <span id='turbidity'>Laden...</span></p>\
             <p><strong>Voltage:</strong> <span id='voltage'>Laden...</span></p>\
             <p><strong>Tijd:</strong> <span id='time'>" + String(millis() / 1000) + " sec</span></p>\
             <p><a href='/wifi/reset' class='link'><button>Reset Wi-Fi Instellingen</button></a></p>\
           </div>";
  html += getHtmlFooter();
  server.send(200, "text/html", html);
}

// Functie: LED Aan
void handleLEDOn() {
  digitalWrite(LED_PIN, HIGH);
  String html = getHtmlHeader();
  html += "<h1>LED Status</h1>\
           <div class='card'>\
             <p>De LED is <strong>AAN</strong>.</p>\
             <p><a href='/' class='link'><button>Terug naar Hoofdmenu</button></a></p>\
           </div>";
  html += getHtmlFooter();
  server.send(200, "text/html", html);
}

// Functie: LED Uit
void handleLEDOff() {
  digitalWrite(LED_PIN, LOW);
  String html = getHtmlHeader();
  html += "<h1>LED Status</h1>\
           <div class='card'>\
             <p>De LED is <strong>UIT</strong>.</p>\
             <p><a href='/' class='link'><button>Terug naar Hoofdmenu</button></a></p>\
           </div>";
  html += getHtmlFooter();
  server.send(200, "text/html", html);
}

// Functie: Realtime Turbidity Data (JSON)
void handleTurbidityData() {
  int sensorValue = analogRead(TURBIDITY_PIN);
  float voltage = sensorValue * (3.3 / 4095.0);
  float turbidity = -1120.4 * sq(voltage) + 5742.3 * voltage - 4352.9;
  if (turbidity < 0) turbidity = 0;

  String json = "{\"turbidity\": " + String(turbidity, 2) + ", \"voltage\": " + String(voltage, 2) + "}";
  server.send(200, "application/json", json);
}

void handleWiFiReset() {
  server.send(200, "text/html", "<h1>Wi-Fi Reset</h1><p>De Wi-Fi-instellingen worden gereset...</p>");
  delay(2000);
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  
  // Pin configuratie
  pinMode(TURBIDITY_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Zorg dat LED uit staat bij start

  // Verbinden met WiFi
  WiFiManager wifiManager;
  if (!wifiManager.autoConnect("ESP32_ConfigPortal")) {
    Serial.println("Kan geen verbinding maken. Herstart...");
    ESP.restart();
  }
  Serial.println("WiFi verbonden!");

  // Webserver routes
  server.on("/", handleRoot);
  server.on("/turbidity/data", handleTurbidityData); // Realtime data endpoint
  server.on("/led/on", handleLEDOn);
  server.on("/led/off", handleLEDOff);
  server.on("/wifi/reset", handleWiFiReset);

  server.begin();
  Serial.println("Webserver gestart.");
}

void loop() {
  server.handleClient(); // Verwerk inkomende HTTP-verzoeken
}


void loop() {
  server.handleClient(); // Verwerk inkomende HTTP-verzoeken
}
