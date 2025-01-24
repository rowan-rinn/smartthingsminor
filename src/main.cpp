#include <Arduino.h>
#include <SPI.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>
#include <AccelStepper.h>

constexpr static const uint8_t  TFT_FONT_SIZE             = TFT_FONT_STYLE == 2 ? 16 : 8;
constexpr static const uint8_t  TFT_FONT_SIZE_MULTIPLIER  = TFT_FONT_STYLE == 2 ? 2 : 3;
constexpr static const uint8_t  MOTOR_MICROSTEPS          = 1;
constexpr static const uint16_t MOTOR_RPM                 = 20;
constexpr static const bool     PUMP_STATE_DEFAULT        = false;
static String                   WEBSERVER_IP_ADDRESS_TEXT = "";
static String                   WEBSERVER_TURBIDITY_DATA  = "";
static String                   TFT_TURBIDITY_DATA        = "";
static float                    tft_prev_turbidity        = -1.0F;
static float                    ws_prev_turbidity         = -1.0F;
static uint8_t                  led_state                 = LOW;
static bool                     pump_state                = PUMP_STATE_DEFAULT;

/**
 * @brief AccelStepper object, providing the motor control functionality
 * 
 */
AccelStepper stepper(AccelStepper::DRIVER, MOTOR_STEP_PIN, MOTOR_DIRECTION_PIN);

/**
 * @brief TFT_eSPI object, providing the display functionality
 * 
 */
TFT_eSPI tft = TFT_eSPI();

/**
 * @brief WiFiManager object, providing the WiFi connection functionality
 * 
 */
WiFiManager wifiManager;

/**
 * @brief WebServer object, providing the web server functionality
 * 
 */
WebServer server(WEBSERVER_PORT);

TaskHandle_t      get_data_task_handle  = NULL;
TaskHandle_t      motor_task_handle     = NULL;
TaskHandle_t      tft_touch_task_handle = NULL;
TaskHandle_t      webserver_task_handle = NULL;
SemaphoreHandle_t semaphore_pump_state  = NULL;

bool get_pump_state(bool& dest)
{
    if (semaphore_pump_state != NULL)
    {
        if (xSemaphoreTake(semaphore_pump_state, 2) == pdTRUE)
        {
            dest = pump_state;
            xSemaphoreGive(semaphore_pump_state);

            return true;
        }
        else { log_w("Could not take semaphore_pump_state"); }
    }
    else { log_w("semaphore_pump_state is NULL"); }

    return false;
}

bool set_pump_state(bool state)
{
    if (semaphore_pump_state != NULL)
    {
        if (xSemaphoreTake(semaphore_pump_state, static_cast<TickType_t>(100 / portTICK_PERIOD_MS)) == pdTRUE)
        {
            pump_state = state;
            xSemaphoreGive(semaphore_pump_state);

            return true;
        }
        else { log_w("Could not take semaphore_pump_state"); }
    }
    else { log_w("semaphore_pump_state is NULL"); }

    return false;
}

constexpr const uint16_t to_tft_y(uint8_t row, uint8_t fonst_size_multiplier = TFT_FONT_SIZE_MULTIPLIER)
{
    return row * TFT_FONT_SIZE * fonst_size_multiplier;
}

void tft_clear_row(uint8_t  row,
                   uint8_t  font_size_height = TFT_FONT_SIZE * TFT_FONT_SIZE_MULTIPLIER,
                   uint16_t color            = TFT_BLACK)
{
    tft.fillRect(0, to_tft_y(row), tft.width(), font_size_height, color);
}

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
}

void display_pump_state(uint8_t row = 7)
{
    tft_text_setup(false, row, TFT_ORANGE, TFT_BLACK);
    tft.printf("Pump State:\n");
    tft_clear_row(row + 1);

    bool pump_state_local;
    if (get_pump_state(pump_state_local))
    {
        tft_text_setup(false, row + 1, pump_state_local ? TFT_GREEN : TFT_RED);
        tft.printf("%s", pump_state_local ? "ON" : "OFF");
    }
    else { log_w("get_pump_state failed!"); }
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

// Function: Change Pump State
void changePumpState(bool state)
{
    bool pump_state_local;
    if (get_pump_state(pump_state_local))
    {
        if (pump_state_local != state)
        {
            if (set_pump_state(state))
            {
                changeLedState(state ? HIGH : LOW);
                display_pump_state(7);

                if (state)
                {
                    Serial.println("MOTOR START");
                    stepper.setSpeed(MOTOR_RPM * MOTOR_STEPS_PER_REV * MOTOR_MICROSTEPS);
                    stepper.runSpeed();
                }
                else
                {
                    Serial.println("MOTOR STOP");
                    stepper.setSpeed(0);
                    stepper.stop();
                }
            }
            else { log_w("set_pump_state failed!"); }
        }
    }
    else { log_w("get_pump_state failed!"); }
}

bool get_turbidity_data(float& prev_turbidity, bool serial_print = false, bool tft_print = true, uint8_t row = 4)
{
    uint16_t curr_sensor_value = analogRead(TURBIDITY_PIN);
    float    voltage           = (TURBIDITY_SENSOR_INPUT_VOLTAGE * static_cast<float>(curr_sensor_value)) / 4095.0F;
    float    turbidity_raw     = -1120.4F * sq(voltage) + 5742.3F * voltage - 4352.9F;
    float    turbidity         = turbidity_raw < 0 ? 0 : turbidity_raw;
    bool     new_data          = abs(turbidity - prev_turbidity) > 0.0F;

    if (!new_data) { return false; }

    WEBSERVER_TURBIDITY_DATA = "{\"turbidity\": " + String(turbidity, 2) + ", \"voltage\": " + String(voltage, 2) + "}";
    TFT_TURBIDITY_DATA       = "Turbidity: " + String(turbidity, 2) + " NTU\nVoltage:   " + String(voltage, 2) + " V";

    if (tft_print && new_data)
    {
        prev_turbidity = turbidity;
        tft_text_setup(false, row);
        tft_clear_row(row);
        tft_clear_row(row + 1);
        tft.println(TFT_TURBIDITY_DATA.c_str());

        if (serial_print) { Serial.println(WEBSERVER_TURBIDITY_DATA.c_str()); }
    }

    return true;
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
             <p>Control the Pump state or check the turbidity values:</p>\
             <p><a href='/pump/on' class='link'><button>Pump ON</button></a></p>\
             <p><a href='/pump/off' class='link'><button>Pump OFF</button></a></p>\
           </div>\
           <div class='card'>\
             <h2>Realtime Turbidity</h2>\
             <p><strong>Turbidity:</strong> <span id='turbidity'>Laden...</span></p>\
             <p><strong>Voltage:</strong> <span id='voltage'>Laden...</span></p>\
             <p><strong>Time:</strong> <span id='time'>" + String(millis() / 1'000) + " sec</span></p>\
             <p><a href='/wifi/reset' class='link'><button>Reset Wi-Fi Settings</button></a></p>\
           </div>";
    html += getHtmlFooter();
    server.send(200, "text/html", html);
}

// Function: Webserver LED On
void handlePumpOn()
{
    changePumpState(true);
    String html = getHtmlHeader();
    html +=
        "<h1>Pump Status</h1>\
           <div class='card'>\
             <p>The pump is now <strong>ON</strong>.</p>\
             <p><a href='/' class='link'><button>Back to Home</button></a></p>\
           </div>";
    html += getHtmlFooter();
    server.send(200, "text/html", html);
}

// Function: Webserver LED Off
void handlePumpOff()
{
    changePumpState(false);
    String html = getHtmlHeader();
    html +=
        "<h1>Pump Status</h1>\
           <div class='card'>\
             <p>The pump is now <strong>OFF</strong>.</p>\
             <p><a href='/' class='link'><button>Back to Home</button></a></p>\
           </div>";
    html += getHtmlFooter();
    server.send(200, "text/html", html);
}

// Function: Realtime Turbidity Data (JSON)
void handleTurbidityData()
{
    if (get_turbidity_data(ws_prev_turbidity, false, false, 4))
    {
        server.send(200, "application/json", WEBSERVER_TURBIDITY_DATA);
    }
}

void handleWiFiReset()
{
    server.send(200, "text/html", "<h1>Wi-Fi Reset</h1><p>Wi-Fi settings are being reset...</p>");
    delay(2'000);
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

void display_button(const char* text,
                    uint16_t    x,
                    uint16_t    y,
                    uint16_t    w,
                    uint16_t    h,
                    uint16_t    bg_color,
                    uint16_t    fg_color = TFT_WHITE)
{
    tft.fillRoundRect(x, y, w, h, 8, bg_color);
    tft.drawRoundRect(x, y, w, h, 8, TFT_BLACK);
    tft.setTextColor(fg_color);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(text, x + w / 2, y + h / 2);
}

bool is_button_pressed(uint16_t touch_x, uint16_t touch_y, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t x1 = x;
    uint16_t y1 = y;
    uint16_t x2 = x + w;
    uint16_t y2 = y + h;

    return touch_x >= x1 && touch_x <= x2 && touch_y >= y1 && touch_y <= y2;
}

void update_buttons(uint16_t touch_x, uint16_t touch_y)
{
    bool button_on_pressed  = is_button_pressed(touch_x, touch_y, 20, 220, 180, 80);
    bool button_off_pressed = is_button_pressed(touch_x, touch_y, 280, 220, 180, 80);

    bool pump_state_local;
    if (get_pump_state(pump_state_local))
    {
        bool result = (button_on_pressed && !button_off_pressed)   ? true
                      : (button_off_pressed && !button_on_pressed) ? false
                                                                   : pump_state_local;

        changePumpState(result);
    }
    else { log_w("get_pump_state failed!"); }
}

void init_motor()
{
    Serial.println("PUMP INIT");

    pinMode(MOTOR_MS1_PIN, OUTPUT);  // Microstep1 pin as output
    pinMode(MOTOR_MS2_PIN, OUTPUT);  // Microstep2 pin as output
    pinMode(MOTOR_MS3_PIN, OUTPUT);  // Microstep3 pin as output

    digitalWrite(MOTOR_MS1_PIN, LOW);  // Set microstep1 pin to low
    digitalWrite(MOTOR_MS2_PIN, LOW);  // Set microstep2 pin to low
    digitalWrite(MOTOR_MS3_PIN, LOW);  // Set microstep3 pin to low

    stepper.setMaxSpeed(static_cast<float>(MOTOR_RPM * MOTOR_STEPS_PER_REV * MOTOR_MICROSTEPS));

    Serial.println("PUMP START");
    changePumpState(PUMP_STATE_DEFAULT);
}

void motor_task(void* parameter)
{
    init_motor();

    Serial.println("Entering Motor Task loop");
    while (true)
    {
        bool pump_state_local;
        if (get_pump_state(pump_state_local))
        {
            if (pump_state_local) { stepper.runSpeed(); }
        }
        else { log_w("get_pump_state failed!"); }

        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

void webserver_task(void* parameter)
{
    server.on("/", handleRoot);
    server.on("/turbidity/data", handleTurbidityData);  // Realtime data endpoint
    server.on("/pump/on", handlePumpOn);
    server.on("/pump/off", handlePumpOff);
    server.on("/wifi/reset", handleWiFiReset);

    server.begin();
    tft_text_setup(false, 0, TFT_GREEN, TFT_BLACK);
    tft.println("Webserver started.");
    Serial.println("Webserver started.");

    Serial.println("Entering Webserver Task loop");
    while (true)
    {
        server.handleClient();  // Process incoming HTTP requests
        delay(1);
    }
}

void tft_touch_task(void* parameter)
{
    Serial.println("Entering TFT Touch Task loop");
    while (true)
    {
        uint16_t        touch_x, touch_y;
        static uint16_t prev_x, prev_y;

        if (tft.getTouch(&touch_x, &touch_y))
        {
            prev_x = touch_x;
            prev_y = touch_y;

            update_buttons(prev_x, prev_y);
            display_pump_state(7);
        }

        delay(50);
    }
}

void get_data_task(void* parameter)
{
    Serial.println("Entering Get Data Task loop");
    while (true)
    {
        get_turbidity_data(tft_prev_turbidity, false, true, 4);  // Print turbidity data
        delay(1'000);
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Starting ESP!");

    semaphore_pump_state = xSemaphoreCreateMutex();
    Serial.println("Created semaphore_pump_state!");

    tft.begin();
    Serial.println("TFT Begin!");

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
        WEBSERVER_IP_ADDRESS_TEXT = "Webserver IP-address:\n" + WiFi.localIP().toString();
        tft.printf("%s\n%s", "WiFi connected!", WEBSERVER_IP_ADDRESS_TEXT.c_str());
        Serial.printf("%s\n%s", "WiFi connected!", WEBSERVER_IP_ADDRESS_TEXT.c_str());
    }

    get_turbidity_data(tft_prev_turbidity, false, true, 4);  // Print turbidity data (only on TFT display)
    display_pump_state(7);
    display_button("ON", 20, 220, 180, 80, TFT_GREEN, TFT_BLACK);
    display_button("OFF", 280, 220, 180, 80, TFT_RED, TFT_BLACK);

    xTaskCreatePinnedToCore(webserver_task, "webserver_task", 4096, NULL, 2, &webserver_task_handle, 0);
    xTaskCreatePinnedToCore(tft_touch_task, "tft_touch_task", 2048, NULL, 3, &tft_touch_task_handle, 0);
    xTaskCreatePinnedToCore(get_data_task, "get_data_task", 4096, NULL, 1, &get_data_task_handle, 0);
    xTaskCreatePinnedToCore(motor_task, "motor_task", 4096, NULL, 10, &motor_task_handle, 1);  // Main App core
}

void loop() {}
