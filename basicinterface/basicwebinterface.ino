#include <WiFi.h>
#include <WebServer.h>

#define TURBIDITY_PIN 34 // Analoge pin voor de turbidity-sensor
#define LED_PIN 2        // GPIO 2 voor de LED

// WiFi-gegevens
const char* ssid = "23";         // Wi-Fi SSID
const char* password = "hoofdpijn"; // Wi-Fi wachtwoord

WebServer server(80); // Webserver op poort 80

// Functie: HTML-header met CSS
String getHtmlHeader() {
  return "<!DOCTYPE html>\
          <html>\
          <head>\
            <title>ESP32 Webinterface</title>\
            <style>\
              body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; background-color: #f4f4f9; }\
              h1 { color: #333; }\
              p { color: #666; font-size: 18px; }\
              .container { max-width: 600px; margin: auto; padding: 20px; }\
              button { padding: 10px 20px; font-size: 16px; color: #fff; background-color: #007BFF; border: none; border-radius: 5px; cursor: pointer; }\
              button:hover { background-color: #0056b3; }\
              .link { text-decoration: none; }\
              .card { margin: 20px auto; padding: 15px; border-radius: 8px; background: #fff; box-shadow: 0 4px 8px rgba(0,0,0,0.2); }\
            </style>\
          </head>\
          <body>\
            <div class='container'>";
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
             <p><a href='/turbidity' class='link'><button>Bekijk Turbidity</button></a></p>\
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

// Functie: Turbidity Weergave
void handleTurbidity() {
  // Lees analoge waarde van turbidity-sensor
  int sensorValue = analogRead(TURBIDITY_PIN);

  // Omrekenen naar spanning en NTU (voorbeeldformule, afhankelijk van sensor)
  float voltage = sensorValue * (3.3 / 4095.0);
  float turbidity = -1120.4 * sq(voltage) + 5742.3 * voltage - 4352.9;

  // Zorg ervoor dat NTU niet negatief wordt
  if (turbidity < 0) turbidity = 0;

  // HTML-pagina met turbidity-informatie
  String html = getHtmlHeader();
  html += "<h1>Turbidity Informatie</h1>\
           <div class='card'>\
             <p><strong>Sensorwaarde:</strong> " + String(sensorValue) + "</p>\
             <p><strong>Spanning:</strong> " + String(voltage, 2) + " V</p>\
             <p><strong>Turbidity:</strong> " + String(turbidity, 2) + " NTU</p>\
             <p><a href='/' class='link'><button>Terug naar Hoofdmenu</button></a></p>\
           </div>";
  html += getHtmlFooter();
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  
  // Pin configuratie
  pinMode(TURBIDITY_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Zorg dat LED uit staat bij start

  // Verbinden met WiFi
  WiFi.begin(ssid, password);
  Serial.print("Verbinden met WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi verbonden. IP-adres:");
  Serial.println(WiFi.localIP());

  // Webserver routes
  server.on("/", handleRoot);           // Hoofdpagina
  server.on("/led/on", handleLEDOn);    // LED Aan
  server.on("/led/off", handleLEDOff);  // LED Uit
  server.on("/turbidity", handleTurbidity); // Turbidity Weergave

  server.begin();
  Serial.println("Webserver gestart.");
}

void loop() {
  server.handleClient(); // Verwerk inkomende HTTP-verzoeken
}
