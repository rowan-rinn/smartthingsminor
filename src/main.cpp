#include <Arduino.h>
#include <SPI.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>

constexpr const uint8_t TURBIDITY_PIN = 34;           // Analoge pin voor de turbidity-sensor
constexpr const uint8_t LED_PIN       = LED_BUILTIN;  // GPIO 2 voor de LED

TFT_eSPI tft = TFT_eSPI();

WiFiManager wifiManager;
WebServer   server(80);  // Webserver op poort 80

enum class TFTColor : uint16_t
{
    BLACK   = TFT_BLACK,
    BLUE    = TFT_BLUE,
    RED     = TFT_RED,
    GREEN   = TFT_GREEN,
    CYAN    = TFT_CYAN,
    MAGENTA = TFT_MAGENTA,
    YELLOW  = TFT_YELLOW,
    WHITE   = TFT_WHITE
};

TFTColor colors[] = {TFTColor::BLACK,
                     TFTColor::BLUE,
                     TFTColor::RED,
                     TFTColor::GREEN,
                     TFTColor::CYAN,
                     TFTColor::MAGENTA,
                     TFTColor::YELLOW,
                     TFTColor::WHITE};

// Functie: HTML-header met CSS en JavaScript voor real-time updates
String getHtmlHeader()
{
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
           "  .banner { width: 100%; display: flex; justify-content: center; align-items: center; background-color: #fff; padding: 10px 0; }\n"
           "  .svg-container { max-width: 600px; width: 100%; }\n"
           "  .data-section { margin-top: 20px; padding: 15px; background-color: #ffffff; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.2); }\n"
           "  .data-item { margin: 10px 0; font-size: 18px; }\n"
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
           "  setInterval(updateTurbidity, 1000);\n"
           "</script>\n"
           "</head>\n"
           "<body>\n"
           "<div class='banner'>\n"
           "<div class='svg-container'>\n"
           "<svg viewBox='0 0 1080 100' preserveAspectRatio='xMidYMid slice' xmlns='http://www.w3.org/2000/svg' width='100%' height='100'>\n"
           "  <rect style='fill:#ffffff;stroke:none;' width='1080' height='100' />\n"
           "  <g transform='scale(0.5) translate(450, -90)' style='stroke:none;'>\n"
           "    <path style='fill:#01bf63;stroke:none;' d='m 251.81923,282.83088 c 0,0 -105.37212,1.47718 0.006,-147.22529 105.3786,148.70295 0.006,147.22577 0.006,147.22577' />\n"
           "    <path style='fill:#ffffff;stroke:none;' d='m 210.39692,266.4831 c 0,0 56.52674,5.58289 93.74598,-28.14706 37.21925,-33.72994 0.46524,-37.21924 0.46524,-37.21924 0,0 -44.89571,56.99197 -103.51603,42.80213 -58.62031,-14.18984 9.30481,22.56417 9.30481,22.56417 z' />\n"
           "    <path style='fill:#d8d4d5;stroke:none;' d='m 185.46511,214.04958 c 0,0 -35.50385,51.25721 23.74761,54.07871 0,0 51.25721,2.35125 100.63342,-40.44147 0,0 58.45213,-54.88295 -16.27049,-64.19344 0,0 37.26464,12.18139 6.98072,49.79206 0,0 -23.44561,28.57434 -63.01009,37.61067 0,0 -68.50547,16.67025 -52.08117,-36.84653 z' />\n"
           "    <path style='fill:#ffffff;stroke:none;' d='m 217.44432,204.23819 c 0,0 -10.03344,24.84472 -5.25562,41.88564 l 2.22966,0.31852 c 0,0 -0.47779,-16.40388 3.02596,-42.20416 z' />\n"
           "  </g>\n"
           "  <text style='font-size:36px;font-family:Futura;font-weight:bold;fill:#241c1c;' x='400' y='60'>\n"
           "    <tspan style='fill:#605b56;'>Pure</tspan>\n"
           "    <tspan style='fill:#01bf63;'>FIo</tspan>\n"
           "  </text>\n"
           "  <text style='font-size:12px;font-family:Futura;font-weight:bold;fill:#605b56;' x='410' y='85'>RESIN FILTERS</text>\n"
           "</svg>\n"
           "</div>\n"
           "</div>\n"
           "</body>\n"
           "</html>\n";
}

// Functie: HTML-footer
String getHtmlFooter() { return "</div></body></html>"; }

// Functie: Hoofdpagina
void handleRoot()
{
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
void handleLEDOn()
{
    digitalWrite(LED_PIN, HIGH);
    String html = getHtmlHeader();
    html +=
        "<h1>LED Status</h1>\
           <div class='card'>\
             <p>De LED is <strong>AAN</strong>.</p>\
             <p><a href='/' class='link'><button>Terug naar Hoofdmenu</button></a></p>\
           </div>";
    html += getHtmlFooter();
    server.send(200, "text/html", html);
}

// Functie: LED Uit
void handleLEDOff()
{
    digitalWrite(LED_PIN, LOW);
    String html = getHtmlHeader();
    html +=
        "<h1>LED Status</h1>\
           <div class='card'>\
             <p>De LED is <strong>UIT</strong>.</p>\
             <p><a href='/' class='link'><button>Terug naar Hoofdmenu</button></a></p>\
           </div>";
    html += getHtmlFooter();
    server.send(200, "text/html", html);
}

// Functie: Realtime Turbidity Data (JSON)
void handleTurbidityData()
{
    int   sensorValue = analogRead(TURBIDITY_PIN);
    float voltage     = sensorValue * (3.3 / 4095.0);
    float turbidity   = -1120.4 * sq(voltage) + 5742.3 * voltage - 4352.9;
    if (turbidity < 0) turbidity = 0;

    String json = "{\"turbidity\": " + String(turbidity, 2) + ", \"voltage\": " + String(voltage, 2) + "}";
    server.send(200, "application/json", json);
}

void handleWiFiReset()
{
    server.send(200, "text/html", "<h1>Wi-Fi Reset</h1><p>De Wi-Fi-instellingen worden gereset...</p>");
    delay(2000);
    wifiManager.resetSettings();
    ESP.restart();
}

void configModeCallback(WiFiManager* myWiFiManager)
{
    Serial.println("Config mode!");

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0, 2);
    tft.printf("%s IP-address:\n%s\n",
               myWiFiManager->getConfigPortalSSID().c_str(),
               WiFi.softAPIP().toString().c_str());
    Serial.printf("%s IP-address: %s\n",
                  myWiFiManager->getConfigPortalSSID().c_str(),
                  WiFi.softAPIP().toString().c_str());
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Starting ESP!");
    Serial.println("TFT Begin!");
    tft.begin();
    tft.setCursor(0, 0, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.println("TFT Setup Done!");

    // Pin configuratie
    pinMode(TURBIDITY_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);  // Zorg dat LED uit staat bij start

    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0, 2);
    tft.println("Starting WiFi Manager!");
    Serial.println("Starting WiFi Manager!");

    // Verbinden met WiFi
    wifiManager.setAPCallback(configModeCallback);
    if (wifiManager.autoConnect("ESP32_ConfigPortal"))
    {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(0, 0, 2);
        tft.printf("WiFi connected!\nIP-address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("WiFi connected!\nIP-address: %s\n", WiFi.localIP().toString().c_str());
    }

    // Webserver routes
    server.on("/", handleRoot);
    server.on("/turbidity/data", handleTurbidityData);  // Realtime data endpoint
    server.on("/led/on", handleLEDOn);
    server.on("/led/off", handleLEDOff);
    server.on("/wifi/reset", handleWiFiReset);

    server.begin();
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.println("Webserver started.");
    Serial.println("Webserver started.");
}

void loop()
{
    server.handleClient();  // Verwerk inkomende HTTP-verzoeken
}
