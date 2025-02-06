/** ----------------------------------------------------------------------------------------------------- 
 * @file main.cpp
 * @author Ben Groeneveld
 * @author Eliam Traas
 * @author Rowan de Heer
 * @author Lorenzo van Yperen
 * @author Jazz Aalbers
 * @author Marco Stefancich
 * 
 * @brief 
 * @version 0.1
 * @date 2024-2025
 *  ----------------------------------------------------------------------------------------------------- **/

/** ----------------------------------------------------------------------------------------------------- 
 * $ INCLUDES
 *  ----------------------------------------------------------------------------------------------------- **/

#include <Arduino.h>
#include <SPI.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>
#include <AccelStepper.h>

/** ----------------------------------------------------------------------------------------------------- 
 * $ GLOBAL VARIABLES
 *  ----------------------------------------------------------------------------------------------------- **/

#ifdef TURBIDITY_SENSOR_3V3
constexpr static const float TURBIDITY_SENSOR_INPUT_VOLTAGE = 3.3F;
#else
constexpr static const float TURBIDITY_SENSOR_INPUT_VOLTAGE = 5.0F;
#endif

#if !SCREEN_PORTRAIT && SCREEN_INVERTED
constexpr static const uint8_t SCREEN_ROTATION = 1;
#elif !SCREEN_PORTRAIT && !SCREEN_INVERTED
constexpr static const uint8_t SCREEN_ROTATION = 3;
#elif SCREEN_PORTRAIT && SCREEN_INVERTED
constexpr static const uint8_t SCREEN_ROTATION = 0;
#elif SCREEN_PORTRAIT && !SCREEN_INVERTED
constexpr static const uint8_t SCREEN_ROTATION = 2;
#endif

constexpr static const uint8_t TFT_FONT_SIZE            = TFT_FONT_STYLE == 2 ? 16 : 8;
constexpr static const uint8_t TFT_FONT_SIZE_MULTIPLIER = TFT_FONT_STYLE == 2 ? 2 : 3;
constexpr static const float   MOTOR_STEPS_PER_SECOND = (MOTOR_RPM * (MOTOR_STEPS_PER_REV * MOTOR_MICROSTEPS)) / 60.0F;
static String                  WEBSERVER_IP_ADDRESS_TEXT = "";
static uint8_t                 led_state                 = LOW;
static bool                    pump_state                = PUMP_STATE_DEFAULT;
static bool                    is_clean                  = false;
static bool                    manual_keep_pump_on       = false;

struct DataStats
{
    float value;
    float avg;
};

struct DataHistory
{
    float     history[TURBIDITY_HISTORY_SIZE] = {0.0F};
    DataStats current                         = {0.0F};
    DataStats previous                        = {-1.0F};
    bool      is_rising                       = false;
    bool      is_falling                      = false;
};

struct TurbidityData
{
    DataHistory ntu;
    DataHistory voltage;
    uint16_t    index = 0;
    // bool        is_text_data_json;
    String text_data      = "";
    String text_data_json = "";
};

static TurbidityData turbidity_data_s = {.text_data = "", .text_data_json = ""};

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

TaskHandle_t      get_data_task_handle          = NULL;
TaskHandle_t      motor_task_handle             = NULL;
TaskHandle_t      tft_touch_task_handle         = NULL;
TaskHandle_t      webserver_task_handle         = NULL;
SemaphoreHandle_t semaphore_pump_state          = NULL;
SemaphoreHandle_t semaphore_manual_keep_pump_on = NULL;
SemaphoreHandle_t semaphore_is_clean            = NULL;
SemaphoreHandle_t semaphore_turbidity_data      = NULL;

/** ----------------------------------------------------------------------------------------------------- 
 * $ FUNCTION DECLARATIONS
 *  ----------------------------------------------------------------------------------------------------- **/

constexpr const float to_voltage_raw(uint16_t analog_value)
{
    return static_cast<float>(analog_value) * (TURBIDITY_SENSOR_INPUT_VOLTAGE / 4096.0F);
}

constexpr const float to_voltage(uint16_t analog_value)
{
    return to_voltage_raw(analog_value) < 0 ? 0 : to_voltage_raw(analog_value);
}

constexpr const float to_ntu_raw(float voltage)
{
#if TURBIDITY_SENSOR_3V3
    return (-2572.2F * sq(voltage) + 8700.5F * voltage - 4352.9F);
#else
    return (-1120.4F * sq(voltage) + 5742.3F * voltage - 4352.9F);
#endif
}

constexpr const float to_ntu(float voltage) { return to_ntu_raw(voltage) < 0 ? 0 : to_ntu_raw(voltage); }

template<typename T>
bool semaphore_get(T& dest, T& src, SemaphoreHandle_t& semaphore_handle, const char* semaphore_name = "semaphore")
{
    if (semaphore_handle != NULL)
    {
        if (xSemaphoreTake(semaphore_handle, 2) == pdTRUE)
        {
            dest = src;
            xSemaphoreGive(semaphore_handle);

            return true;
        }
        else { log_w("Could not take %s", semaphore_name); }
    }
    else { log_w("%s is NULL", semaphore_name); }

    return false;
}

template<typename T>
bool semaphore_set(T&                 dest,
                   T                  value,
                   SemaphoreHandle_t& semaphore_handle,
                   const char*        semaphore_name = "semaphore",
                   uint32_t           timeout        = 100)
{
    if (semaphore_handle != NULL)
    {
        if (xSemaphoreTake(semaphore_handle, static_cast<TickType_t>(timeout / portTICK_PERIOD_MS)) == pdTRUE)
        {
            dest = value;
            xSemaphoreGive(semaphore_handle);

            return true;
        }
        else { log_w("Could not take %s", semaphore_name); }
    }
    else { log_w("%s is NULL", semaphore_name); }

    return false;
}

bool get_semaphore_turbidity_data(TurbidityData& dest)
{
    return semaphore_get(dest, turbidity_data_s, semaphore_turbidity_data, "semaphore_turbidity_data");
}
bool set_semaphore_turbidity_data(TurbidityData state)
{
    return semaphore_set(turbidity_data_s, state, semaphore_turbidity_data, "semaphore_turbidity_data", 100);
}

bool get_semaphore_is_clean_state(bool& dest)
{
    return semaphore_get(dest, is_clean, semaphore_is_clean, "semaphore_is_clean");
}
bool set_semaphore_is_clean_state(bool state)
{
    return semaphore_set(is_clean, state, semaphore_is_clean, "semaphore_is_clean", 50);
}

bool get_semaphore_manual_keep_pump_on_state(bool& dest)
{
    return semaphore_get(dest, manual_keep_pump_on, semaphore_manual_keep_pump_on, "semaphore_manual_keep_pump_on");
}
bool set_semaphore_manual_keep_pump_on_state(bool state)
{
    return semaphore_set(manual_keep_pump_on,
                         state,
                         semaphore_manual_keep_pump_on,
                         "semaphore_manual_keep_pump_on",
                         50);
}

bool get_semaphore_pump_state(bool& dest)
{
    return semaphore_get(dest, pump_state, semaphore_pump_state, "semaphore_pump_state");
}
bool set_semaphore_pump_state(bool state)
{
    return semaphore_set(pump_state, state, semaphore_pump_state, "semaphore_pump_state", 50);
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
    tft.setRotation(SCREEN_ROTATION);
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
    if (get_semaphore_pump_state(pump_state_local))
    {
        tft_text_setup(false, row + 1, pump_state_local ? TFT_GREEN : TFT_RED);
        tft.printf("%s", pump_state_local ? "ON" : "OFF");
    }
    else { log_w("get_semaphore_pump_state failed!"); }
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
    if (get_semaphore_pump_state(pump_state_local))
    {
        if (pump_state_local != state)
        {
            if (set_semaphore_pump_state(state))
            {
                // changeLedState(state ? HIGH : LOW);
                display_pump_state(7);

                if (state)
                {
                    Serial.println("MOTOR START");

#if USE_TURBIDITY_SENSOR
                    bool local_manual_keep_pump_on, local_is_clean;
                    if (get_semaphore_manual_keep_pump_on_state(local_manual_keep_pump_on)
                        && get_semaphore_is_clean_state(local_is_clean))
                    {
                        if (!local_manual_keep_pump_on && local_is_clean)
                        {
                            if (!set_semaphore_manual_keep_pump_on_state(true))
                            {
                                log_w("set_semaphore_manual_keep_pump_on_state failed!");
                            }
                        }
                    }
                    else { log_w("get_semaphore_manual_keep_pump_on_state or get_semaphore_is_clean_state failed!"); }
#endif

                    // Change motor to normal mode
                    digitalWrite(MOTOR_SLEEP_PIN, HIGH);
                    digitalWrite(MOTOR_RESET_PIN, HIGH);
                    digitalWrite(MOTOR_ENABLE_PIN, LOW);
                    delay(1);

                    stepper.setSpeed(MOTOR_STEPS_PER_SECOND);
                    stepper.runSpeed();
                }
                else
                {
                    Serial.println("MOTOR STOP");

#if USE_TURBIDITY_SENSOR
                    bool local_manual_keep_pump_on, local_is_clean;
                    if (get_semaphore_manual_keep_pump_on_state(local_manual_keep_pump_on)
                        && get_semaphore_is_clean_state(local_is_clean))
                    {
                        if (local_manual_keep_pump_on || !local_is_clean)
                        {
                            set_semaphore_manual_keep_pump_on_state(false);
                        }
                    }
                    else { log_w("get_semaphore_manual_keep_pump_on_state or get_semaphore_is_clean_state failed!"); }
#endif

                    stepper.setSpeed(0);
                    stepper.stop();

                    // Change motor to sleep mode
                    digitalWrite(MOTOR_SLEEP_PIN, LOW);
                    digitalWrite(MOTOR_RESET_PIN, LOW);
                    digitalWrite(MOTOR_ENABLE_PIN, HIGH);
                    delay(1);
                }
            }
            else { log_w("set_semaphore_pump_state failed!"); }
        }
    }
    else { log_w("get_semaphore_pump_state failed!"); }
}

bool get_turbidity_data(bool serial_print = false, bool tft_print = true, uint8_t row = 4)
{
    TurbidityData local_turbidity_data;
    if (!get_semaphore_turbidity_data(local_turbidity_data)) { return false; }

    uint16_t idx_local         = local_turbidity_data.index;
    uint16_t curr_sensor_value = analogRead(TURBIDITY_PIN);
    float    voltage_local     = to_voltage(curr_sensor_value);
    float    ntu_local         = to_ntu(voltage_local);

    local_turbidity_data.ntu.current.value          = ntu_local;
    local_turbidity_data.ntu.history[idx_local]     = ntu_local;
    local_turbidity_data.voltage.current.value      = voltage_local;
    local_turbidity_data.voltage.history[idx_local] = voltage_local;

    float    _sum   = 0.0F;
    uint16_t _count = 0;
    for (uint16_t i = 0; i < TURBIDITY_HISTORY_SIZE; i++)
    {
        float _voltage         = local_turbidity_data.voltage.history[i];
        float _voltage_rounded = roundf(_voltage * 1000.0F) / 1000.0F;
        if (_voltage_rounded > 0.0F && _voltage_rounded <= TURBIDITY_SENSOR_INPUT_VOLTAGE)
        {
            _sum += _voltage;
            _count++;
        }
    }

    local_turbidity_data.voltage.current.avg = _count > 0 ? _sum / static_cast<float>(_count) : 0.0F;
    local_turbidity_data.ntu.current.avg     = to_ntu(local_turbidity_data.voltage.current.avg);

    bool new_data = abs(local_turbidity_data.voltage.current.avg - local_turbidity_data.voltage.previous.avg) > 0.0F;

    float    lin_regr_sum_x        = 0.0F;
    float    lin_regr_sum_y        = 0.0F;
    float    lin_regr_sum_xy       = 0.0F;
    float    lin_regr_sum_x_square = 0.0F;
    float    lin_regr_coeff        = 0.0F;
    uint16_t lin_regr_count        = 0;

    for (uint16_t i = 0; i < TURBIDITY_HISTORY_SIZE; i++)
    {
        if (local_turbidity_data.voltage.history[i] > 0.0F)
        {
            lin_regr_sum_y += local_turbidity_data.voltage.history[i];
            lin_regr_sum_x += i;
            lin_regr_sum_xy += i * local_turbidity_data.voltage.history[i];
            lin_regr_sum_x_square += sq(i);
            lin_regr_count += 1;
        }
    }

    if (lin_regr_count > 1)
    {
        float n                    = static_cast<float>(lin_regr_count);
        float lin_regr_numerator   = (n * lin_regr_sum_xy) - (lin_regr_sum_x * lin_regr_sum_y);
        float lin_regr_denominator = (n * lin_regr_sum_x_square) - (lin_regr_sum_x * lin_regr_sum_x);
        lin_regr_coeff             = lin_regr_denominator != 0.0F ? lin_regr_numerator / lin_regr_denominator : 0.0F;

        local_turbidity_data.voltage.is_rising  = lin_regr_coeff > 0.0F;
        local_turbidity_data.voltage.is_falling = lin_regr_coeff < 0.0F;
    }

    bool is_clean_flag         = !local_turbidity_data.voltage.is_falling;
    local_turbidity_data.index = (idx_local + 1) % TURBIDITY_HISTORY_SIZE;
    bool local_clean_state = (local_turbidity_data.voltage.current.avg > TURBIDITY_VOLTAGE_THRESHOLD) && is_clean_flag;

    if (!set_semaphore_is_clean_state(local_clean_state)) { log_w("set_semaphore_is_clean_state failed!"); }

#if USE_TURBIDITY_SENSOR
    if (!local_clean_state)
    {
        if (!set_semaphore_manual_keep_pump_on_state(false)) { log_w("set_semaphore_is_clean_state failed!"); }
    }
#endif

#if USE_TURBIDITY_SENSOR
    bool local_manual_keep_pump_on;
    if (get_semaphore_manual_keep_pump_on_state(local_manual_keep_pump_on))
    {
        if (local_clean_state && !local_manual_keep_pump_on) { changePumpState(false); }
    }
#endif

    String is_clean_str                 = local_clean_state ? "YES" : "NO";
    local_turbidity_data.text_data_json = "{\"turbidity\": " + String(local_turbidity_data.ntu.current.value, 2)
                                          + ", \"voltage\": " + String(local_turbidity_data.voltage.current.value, 2)
                                          + ", \"avg_voltage\": " + String(local_turbidity_data.voltage.current.avg, 2)
                                          + "}";
    local_turbidity_data.text_data = "AVG Volt.: " + String(local_turbidity_data.voltage.current.avg, 2) + " V" + "\n"
                                     + "Voltage: " + String(local_turbidity_data.voltage.current.value, 2) + " V" + "\n"
                                     + "Is Clean?: " + is_clean_str;

#if SERIAL_DEBUG
    Serial.printf("CURRENT => [ NTU: %f NTU, Voltage: %f V, AnalogRead: %u, Coeff: %f, IsRising: %s ]\n",
                  ntu_local,
                  voltage_local,
                  curr_sensor_value,
                  lin_regr_coeff,
                  local_turbidity_data.voltage.is_rising ? "YES" : "NO");

    Serial.printf("AVERAGE => [ NTU: %f NTU, Voltage: %f V, IsRising: %s, Is Clean: %s ]\n",
                  local_turbidity_data.ntu.current.avg,
                  local_turbidity_data.voltage.current.avg,
                  local_turbidity_data.voltage.is_rising ? "YES" : "NO",
                  local_clean_state ? "YES" : "NO");

    #if SERIAL_DEBUG_HISTORY
    Serial.printf("HISTORY =>\n{\n");
    for (uint16_t i = 0; i < TURBIDITY_HISTORY_SIZE; i++)
    {
        Serial.printf("  [%u] => [ NTU: %f NTU, Voltage: %f V ]\n",
                      i,
                      local_turbidity_data.ntu.history[i],
                      local_turbidity_data.voltage.history[i]);
    }
    Serial.printf("}\n");
    #endif
#endif

    if (!new_data) { return false; }
    else if (tft_print && new_data)
    {
        local_turbidity_data.voltage.previous = local_turbidity_data.voltage.current;
        tft_text_setup(false, row);
        for (uint8_t i = 0; i < 3; i++) { tft_clear_row(row + i); }
        tft.println(local_turbidity_data.text_data.c_str());

        if (serial_print) { Serial.println(local_turbidity_data.text_data.c_str()); }
    }

    bool result = set_semaphore_turbidity_data(local_turbidity_data);

    return result;
}

// Function: HTML-header with CSS and JavaScript for real-time updates
String getHtmlHeader()
{
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
                        .banner { width: 100%; display: flex; justify-content: center; align-items: center; background-color: #fff; padding: 10px 0; }\
                        .svg-container { max-width: 600px; width: 100%; }\
                        .data-section { margin-top: 20px; padding: 15px; background-color: #ffffff; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.2); }\
                        .data-item { margin: 10px 0; font-size: 18px; }\
                    </style>\
                    <script>\
                        function updateTurbidity() {\
                        fetch('/turbidity/data')\
                            .then(response => response.json())\
                            .then(data => {\
                            document.getElementById('turbidity').innerText = data.turbidity + ' NTU';\
                            document.getElementById('avg_turbidity').innerText = data.avg_turbidity + ' NTU';\
                            document.getElementById('voltage').innerText = data.voltage + ' V';\
                            document.getElementById('avg_voltage').innerText = data.avg_voltage + ' V';\
                            })\
                            .catch(error => {\
                            console.error('Fout bij ophalen van turbidity data:', error);\
                            });\
                        }\
                        setInterval(updateTurbidity, 1000);\
                    </script>\
                </head>\
                <body>\
                    <div class='banner'>\
                        <div class='svg-container'>\
                            <svg viewBox='0 0 1080 100' preserveAspectRatio='xMidYMid slice' xmlns='http://www.w3.org/2000/svg' width='100%' height='100'>\
                                <rect style='fill:#ffffff;stroke:none;' width='1080' height='100' />\
                                <g transform='scale(0.5) translate(450, -90)' style='stroke:none;'>\
                                    <path style='fill:#01bf63;stroke:none;' d='m 251.81923,282.83088 c 0,0 -105.37212,1.47718 0.006,-147.22529 105.3786,148.70295 0.006,147.22577 0.006,147.22577' />\
                                    <path style='fill:#ffffff;stroke:none;' d='m 210.39692,266.4831 c 0,0 56.52674,5.58289 93.74598,-28.14706 37.21925,-33.72994 0.46524,-37.21924 0.46524,-37.21924 0,0 -44.89571,56.99197 -103.51603,42.80213 -58.62031,-14.18984 9.30481,22.56417 9.30481,22.56417 z' />\
                                    <path style='fill:#d8d4d5;stroke:none;' d='m 185.46511,214.04958 c 0,0 -35.50385,51.25721 23.74761,54.07871 0,0 51.25721,2.35125 100.63342,-40.44147 0,0 58.45213,-54.88295 -16.27049,-64.19344 0,0 37.26464,12.18139 6.98072,49.79206 0,0 -23.44561,28.57434 -63.01009,37.61067 0,0 -68.50547,16.67025 -52.08117,-36.84653 z' />\
                                    <path style='fill:#ffffff;stroke:none;' d='m 217.44432,204.23819 c 0,0 -10.03344,24.84472 -5.25562,41.88564 l 2.22966,0.31852 c 0,0 -0.47779,-16.40388 3.02596,-42.20416 z' />\
                                </g>\
                                <text style='font-size:36px;font-family:Futura;font-weight:bold;fill:#241c1c;' x='400' y='60'>\
                                    <tspan style='fill:#605b56;'>Pure</tspan>\
                                    <tspan style='fill:#01bf63;'>FIo</tspan>\
                                </text>\
                                <text style='font-size:12px;font-family:Futura;font-weight:bold;fill:#605b56;' x='410' y='85'>RESIN FILTERS</text>\
                            </svg>\
                        </div>\
                    </div>\
                </body>\
            </html>";
}

// Function: HTML-footer
String getHtmlFooter() { return "</div></body></html>"; }

// Function: Main Page
void handleRoot()
{
    String html = getHtmlHeader();
    html +=
        "<h1>ESP32 Webinterface</h1>\
            <p>Control the Pump state or check the turbidity values:</p>\
            <div class='card'>\
                <h2>Pump Settings</h2>\
                <p><a href='/pump/on' class='link'><button>Pump ON</button></a></p>\
                <p><a href='/pump/off' class='link'><button>Pump OFF</button></a></p>\
            </div>\
            <div class='card'>\
                <h2>Realtime Turbidity Data</h2>\
                <p><strong>Turbidity:</strong> <span id='turbidity'>Laden...</span></p>\
                <p><strong>Average Turbidity:</strong> <span id='avg_turbidity'>Laden...</span></p>\
                <p><strong>Voltage:</strong> <span id='voltage'>Laden...</span></p>\
                <p><strong>Average Voltage:</strong> <span id='avg_voltage'>Laden...</span></p>\
            </div>\
            <div class='card'>\
                <h2>Wi-Fi Settings</h2>\
                <p><a href='/wifi/reset' class='link'><button>Reset Wi-Fi Settings</button></a></p>\
            </div>";
    html += getHtmlFooter();
    server.send(200, "text/html", html);
}

// Function: Webserver LED On
void handlePumpOn()
{
    changePumpState(true);
    handleRoot();
}

// Function: Webserver LED Off
void handlePumpOff()
{
    changePumpState(false);
    handleRoot();
}

// Function: Realtime Turbidity Data (JSON)
void handleTurbidityData()
{
    if (get_turbidity_data(false, false, 4))
    {
        TurbidityData local_turbidity_data;
        if (get_semaphore_turbidity_data(local_turbidity_data))
        {
            server.send(200, "application/json", local_turbidity_data.text_data_json);
        }
        else { handleRoot(); }
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
    if (get_semaphore_pump_state(pump_state_local))
    {
        bool result = (button_on_pressed && !button_off_pressed)   ? true
                      : (button_off_pressed && !button_on_pressed) ? false
                                                                   : pump_state_local;

        log_i("NEW pump state: %s", result ? "ON" : "OFF");
        changePumpState(result);
    }
    else { log_w("get_semaphore_pump_state failed!"); }
}

void init_motor()
{
    Serial.println("PUMP INIT");

    pinMode(MOTOR_MS1_PIN, OUTPUT);     // Microstep1 pin as output
    pinMode(MOTOR_MS2_PIN, OUTPUT);     // Microstep2 pin as output
    pinMode(MOTOR_MS3_PIN, OUTPUT);     // Microstep3 pin as output
    pinMode(MOTOR_SLEEP_PIN, OUTPUT);   // Sleep pin as output
    pinMode(MOTOR_RESET_PIN, OUTPUT);   // Reset pin as output
    pinMode(MOTOR_ENABLE_PIN, OUTPUT);  // Enable pin as output

    digitalWrite(MOTOR_MS1_PIN, LOW);  // Set microstep1 pin to low
    digitalWrite(MOTOR_MS2_PIN, LOW);  // Set microstep2 pin to low
    digitalWrite(MOTOR_MS3_PIN, LOW);  // Set microstep3 pin to low

    digitalWrite(MOTOR_SLEEP_PIN, PUMP_STATE_DEFAULT ? HIGH : LOW);   // Set motor to normal or sleep mode
    digitalWrite(MOTOR_RESET_PIN, PUMP_STATE_DEFAULT ? HIGH : LOW);   // Set motor to normal or sleep mode
    digitalWrite(MOTOR_ENABLE_PIN, PUMP_STATE_DEFAULT ? LOW : HIGH);  // Set motor to normal or sleep mode

    stepper.setPinsInverted(PUMP_DIRECTION_INVERTED, false, false);
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
        if (get_semaphore_pump_state(pump_state_local))
        {
            if (pump_state_local) { stepper.runSpeed(); }
        }
        else { log_w("get_semaphore_pump_state failed!"); }

        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

void init_wifi()
{
    tft_text_setup(true);
    tft.println("Starting WiFi Manager!");
    Serial.println("Starting WiFi Manager!");

    // Connect to WiFi
    wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT);
    wifiManager.setConnectRetries(WIFI_CONNECT_RETRIES);
    wifiManager.setAPCallback(configModeCallback);

    if (wifiManager.autoConnect("ESP32_ConfigPortal"))
    {
        wifiManager.setWiFiAutoReconnect(false);

        tft_text_setup(true, 0, TFT_GREEN, TFT_BLACK);
        WEBSERVER_IP_ADDRESS_TEXT = "Webserver IP-address:\n" + WiFi.localIP().toString();
        tft.printf("%s\n%s", "WiFi connected!", WEBSERVER_IP_ADDRESS_TEXT.c_str());
        Serial.printf("%s\n%s", "WiFi connected!", WEBSERVER_IP_ADDRESS_TEXT.c_str());
    }
    else
    {
        tft_text_setup(true, 0, TFT_RED, TFT_BLACK);
        tft.printf("Failed to connect to WiFi!");
        Serial.println("Failed to connect to WiFi!");
    }
}

void wifi_task(void* parameter)
{
    Serial.println("Entering WiFi Task loop");
    while (true)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            tft_text_setup(true, 0, TFT_RED, TFT_BLACK);
            tft.println("WiFi disconnected!");
            Serial.println("WiFi disconnected!");

            init_wifi();
            delay(5'000);
        }
        else { delay(200); }
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
        if (WiFi.status() == WL_CONNECTED)
        {
            // Process incoming HTTP requests
            server.handleClient();
            delay(100);
        }
        else
        {
            tft_text_setup(true, 0, TFT_RED, TFT_BLACK);
            tft.println("WiFi disconnected!");
            Serial.println("WiFi disconnected!");

            init_wifi();
            delay(5'000);
        }
    }
}

void tft_touch_task(void* parameter)
{
    Serial.println("Entering TFT Touch Task loop");
    while (true)
    {
        uint16_t        raw_touch_x, raw_touch_y;
        static uint16_t prev_x, prev_y;

        if (tft.getTouch(&raw_touch_x, &raw_touch_y))
        {
            uint16_t touch_x = SCREEN_INVERTED ? SCREEN_WIDTH - raw_touch_x : raw_touch_x;
            uint16_t touch_y = SCREEN_INVERTED ? SCREEN_HEIGHT - raw_touch_y : raw_touch_y;
            prev_x           = touch_x;
            prev_y           = touch_y;

            update_buttons(prev_x, prev_y);
            display_pump_state(7);
        }

        delay(20);
    }
}

void get_data_task(void* parameter)
{
    Serial.println("Entering Get Data Task loop");
    while (true)
    {
        get_turbidity_data(false, true, 4);  // Print turbidity data
        delay(1'000);
    }
}

/** ----------------------------------------------------------------------------------------------------- 
 * $$ ARDUINO SETUP
 *  ----------------------------------------------------------------------------------------------------- **/

void setup()
{
    Serial.begin(115200);
    Serial.println("Starting ESP!");

    semaphore_pump_state = xSemaphoreCreateMutex();
    Serial.println("Created semaphore_pump_state!");
    semaphore_manual_keep_pump_on = xSemaphoreCreateMutex();
    Serial.println("Created semaphore_manual_keep_pump_on!");
    semaphore_is_clean = xSemaphoreCreateMutex();
    Serial.println("Created semaphore_is_clean!");
    semaphore_turbidity_data = xSemaphoreCreateMutex();
    Serial.println("Created semaphore_turbidity_data!");

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

    init_wifi();

    get_turbidity_data(false, true, 4);  // Print turbidity data (only on TFT display)
    display_pump_state(7);
    display_button("ON", 20, 220, 180, 80, TFT_GREEN, TFT_BLACK);
    display_button("OFF", 280, 220, 180, 80, TFT_RED, TFT_BLACK);

    xTaskCreatePinnedToCore(webserver_task, "webserver_task", 4096, NULL, 1, &webserver_task_handle, 0);
    xTaskCreatePinnedToCore(tft_touch_task, "tft_touch_task", 4096, NULL, 3, &tft_touch_task_handle, 0);
    xTaskCreatePinnedToCore(get_data_task, "get_data_task", 4096, NULL, 4, &get_data_task_handle, 0);
    xTaskCreatePinnedToCore(motor_task, "motor_task", 4096, NULL, 2, &motor_task_handle, 1);  // Main App core
}

/** ----------------------------------------------------------------------------------------------------- 
 * $$ ARDUINO LOOP
 *  ----------------------------------------------------------------------------------------------------- **/

void loop() {}
