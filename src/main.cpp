#ifndef USER_SETUP_LOADED
    #define USER_SETUP_LOADED (1)
#endif
#ifndef ILI9488_DRIVER
    #define ILI9488_DRIVER (1)
#endif
#ifndef LOAD_GLCD
    #define LOAD_GLCD (1)
#endif
#ifndef LOAD_FONT2
    #define LOAD_FONT2 (1)
#endif
#ifndef LOAD_FONT4
    #define LOAD_FONT4 (1)
#endif
#ifndef LOAD_FONT6
    #define LOAD_FONT6 (1)
#endif
#ifndef LOAD_FONT7
    #define LOAD_FONT7 (1)
#endif
#ifndef LOAD_FONT8
    #define LOAD_FONT8 (1)
#endif
#ifndef LOAD_GFXFF
    #define LOAD_GFXFF (1)
#endif
#ifndef SPI_FREQUENCY
    #define SPI_FREQUENCY (27000000)
#endif
#ifndef SPI_READ_FREQUENCY
    #define SPI_READ_FREQUENCY (16000000)
#endif
#ifndef SPI_TOUCH_FREQUENCY
    #define SPI_TOUCH_FREQUENCY (2500000)
#endif

#ifndef TURBIDITY_PIN
    #define TURBIDITY_PIN (34)  // Analog pin for the turbidity sensor
#endif
#ifndef LED_PIN
    #define LED_PIN (13)  // GPIO 2 for the LED
#endif
#ifndef TFT_MISO
    #define TFT_MISO (19)
#endif
#ifndef TFT_MOSI
    #define TFT_MOSI (23)
#endif
#ifndef TFT_SCLK
    #define TFT_SCLK (18)
#endif
#ifndef TFT_CS
    #define TFT_CS (5)
#endif
#ifndef TFT_DC
    #define TFT_DC (2)
#endif
#ifndef TFT_RST
    #define TFT_RST (15)
#endif
#ifndef TOUCH_CS
    #define TOUCH_CS (4)
#endif

#include <Arduino.h>
#include <SPI.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>

constexpr static const uint8_t TFT_FONT_STYLE           = 1;
constexpr static const uint8_t TFT_FONT_SIZE            = TFT_FONT_STYLE == 2 ? 16 : 8;
constexpr static const uint8_t TFT_FONT_SIZE_MULTIPLIER = TFT_FONT_STYLE == 2 ? 2 : 3;
static uint8_t                 led_state                = LOW;
TFT_eSPI                       tft                      = TFT_eSPI();
WiFiManager                    wifiManager;
WebServer                      server(80);  // Webserver op poort 80

constexpr const uint16_t to_tft_y(uint8_t row, uint8_t fonst_size_multiplier = TFT_FONT_SIZE_MULTIPLIER)
{
    return row * TFT_FONT_SIZE * fonst_size_multiplier;
}

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

void tft_text_setup(bool     clear_screen = false,
                    uint8_t  row          = 0,
                    uint16_t fg_color     = TFT_WHITE,
                    uint16_t bg_color     = TFT_BLACK,
                    uint8_t  font_style   = TFT_FONT_STYLE,
                    uint8_t  font_size    = TFT_FONT_SIZE_MULTIPLIER)
{
    if (clear_screen) { tft.fillScreen(TFT_BLACK); }
    tft.setRotation(3);
    tft.setCursor(0, to_tft_y(row));
    tft.setTextFont(font_style);
    tft.setTextColor(fg_color, bg_color);
    tft.setTextSize(font_size);
    tft.print("");
}

// Function: HTML-header with CSS and JavaScript for real-time updates
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

// Function: HTML-footer
String getHtmlFooter() { return "</div></body></html>"; }

// Function: Main Page
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

// Function: Change LED State
void changeLedState(uint8_t state)
{
    if (led_state != state)
    {
        digitalWrite(LED_PIN, state);
        led_state = state;
    }
}

// Function: Webserver LED On
void handleLEDOn()
{
    changeLedState(HIGH);
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

// Function: Webserver LED Off
void handleLEDOff()
{
    changeLedState(LOW);
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

String get_turbidity_data(bool print = true)
{
    uint16_t sensorValue = analogRead(TURBIDITY_PIN);
    float    voltage     = static_cast<float>(sensorValue) * 3.3F / 4095.0F;
    float    turbidity   = -1120.4F * sq(voltage) + 5742.3F * voltage - 4352.9F;
    if (turbidity < 0) { turbidity = 0; }

    String json = "{\"turbidity\": " + String(turbidity, 2) + ", \"voltage\": " + String(voltage, 2) + "}";

    if (print)
    {
        tft_text_setup(false, 6);
        tft.printf("Turbidity: %.2f NTU\nVoltage:   %.2f   V\n", turbidity, voltage);
        Serial.println(json.c_str());
    }

    return json;
}

// Function: Realtime Turbidity Data (JSON)
void handleTurbidityData()
{
    String json = get_turbidity_data(false);
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
    tft_text_setup(false, 2, TFT_YELLOW, TFT_BLACK);
    tft.printf("Name:\n%s\n\nIP-address:\n%s\n",
               myWiFiManager->getConfigPortalSSID().c_str(),
               WiFi.softAPIP().toString().c_str());
    Serial.printf("Name: %s\nIP-address: %s\n",
                  myWiFiManager->getConfigPortalSSID().c_str(),
                  WiFi.softAPIP().toString().c_str());
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Starting ESP!");
    Serial.println("TFT Begin!");
    tft.begin();
    tft_text_setup(true);
    tft.println("TFT Setup Done!");
    Serial.println("TFT Setup Done!");

    // Pin configuration
    pinMode(TURBIDITY_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);  // Make sure the LED is off on startup
    led_state = LOW;

    tft_text_setup(true);
    tft.println("Starting WiFi Manager!");
    Serial.println("Starting WiFi Manager!");

    // Connect to WiFi
    wifiManager.setAPCallback(configModeCallback);
    if (wifiManager.autoConnect("ESP32_ConfigPortal"))
    {
        tft_text_setup(true, 0, TFT_GREEN, TFT_BLACK);
        tft.printf("WiFi connected!\nWebserver IP-address:\n%s\n", WiFi.localIP().toString().c_str());
        Serial.printf("WiFi connected!\nWebserver IP-address:\n%s\n", WiFi.localIP().toString().c_str());
    }

    // Webserver routes
    server.on("/", handleRoot);
    server.on("/turbidity/data", handleTurbidityData);  // Realtime data endpoint
    server.on("/led/on", handleLEDOn);
    server.on("/led/off", handleLEDOff);
    server.on("/wifi/reset", handleWiFiReset);

    server.begin();
    tft_text_setup(false, 4, TFT_CYAN, TFT_BLACK);
    tft.println("Webserver started.");
    Serial.println("Webserver started.");
}

void loop()
{
    uint16_t        x, y;
    static uint64_t prev_time_turbidity = millis();
    static uint64_t prev_time_touch     = millis();

    if (millis() - prev_time_turbidity >= 1'000)
    {
        prev_time_turbidity = millis();
        get_turbidity_data();  // Print turbidity data on TFT display
    }

    if (millis() - prev_time_touch >= 50)
    {
        prev_time_touch = millis();

        if (tft.getTouch(&x, &y)) { changeLedState(HIGH); }
        else { changeLedState(LOW); }
    }

    server.handleClient();  // Process incoming HTTP requests
}
