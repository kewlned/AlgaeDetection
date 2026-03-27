#include <Arduino.h>
#define ARDUINOJSON_DECODE_UNICODE 0
#include <ArduinoJson.h> // Version 7
#include <ESPmDNS.h>
#include <FS.h>
#include <LittleFS.h>
#include <SPI.h>
#include <StreamUtils.h>
#include <Ticker.h>
#include <WebServer.h>
#include <Wire.h>

#include "DFRobot_ESP_PH_WITH_ADC.h"
#include "EEPROM.h"
#include <Adafruit_ADS1X15.h>
#include <DFRobot_MAX31855.h>

#include "config.h"

#include "sensor_read.h"

#include "load_control.h"

#include "display_vendo.h"

#include "spiffs_vendo.h"

#include "savedata.h"

#include "license.h"

#include "servers_vendo.h"

#include "buttonhandler.h"
#include "mqtt_comms.h"

#include "utils_function.h"
#include <HTTPClient.h>
#include <SensorModbusMaster.h>
#include <WiFi.h>

// Define RS485 hardware serial
#define RX_PIN 19
#define TX_PIN 23

#define BGA_SENSOR
#define NITRATE_SENSOR
#define TEMP_PH_TURB
// #define BYPASS_FLAG
#define SEND_DATA

unsigned long update_interval = 15000;

int counter = 0;
enum sensor_address
{
    BGA_SENSOR_ADDRESS     = 0,
    NITRATE_SENSOR_ADDRESS = 2
};

HardwareSerial rs485Serial(1); // Use UART1
// ==========================================================================
//  Sensor Settings
// ==========================================================================

// Define the sensor's modbus address
byte modbusAddress = 0x01; // The sensor's modbus address, or SlaveID

// The Modbus baud rate the sensor uses
int32_t modbusBaudRate = 9600; // The baud rate the sensor uses

// Sensor Timing
// Edit these to explore
#define WARM_UP_TIME 3500 // milliseconds for sensor to respond to commands.
// ==========================================================================
//  Data Logger Options
// ==========================================================================
const int32_t serialBaud = 115200; // Baud rate for serial monitor

// Define pin number variables
const int sensorPwrPin  = 10; // The pin sending power to the sensor
const int adapterPwrPin = 22; // The pin sending power to the RS485 adapter
const int DEREPin       = -1; // The pin controlling Receive Enable and Driver Enable
                              // on the RS485 adapter, if applicable (else, -1)
                              // Setting HIGH enables the driver (arduino) to send text
                              // Setting LOW enables the receiver (sensor) to send text

modbusMaster modbus;
Ticker       timer_refresh;
Ticker       read_coinslot_refresh;
Ticker       read_billacceptor_refresh;
Ticker       lcd_refresh;
bool         bypass_read_flag = false;

void handle_timer_counter();

void LCD_init();

// Define buttons
ButtonHandler button_config(DEFAULT_CONFIG_BUTTON_PIN, 70, 2000, 4000);

// Define buttons
ButtonHandler buttons[] = {ButtonHandler(DEFAULT_BUTTON_1_PIN, 70),
                           ButtonHandler(DEFAULT_BUTTON_2_PIN, 70),
                           ButtonHandler(DEFAULT_BUTTON_3_PIN, 70),
                           ButtonHandler(DEFAULT_BUTTON_4_PIN, 70),
                           ButtonHandler(DEFAULT_BUTTON_5_PIN, 70)};

struct RelayConfig
{
    unsigned int &rate;
    unsigned int &plim;
    unsigned int &time;
};

RelayConfig relayConfigs[] = {
      {sys_config.r1rate, sys_config.r1plim, sys_config.r1time},
      {sys_config.r2rate, sys_config.r2plim, sys_config.r2time},
      {sys_config.r3rate, sys_config.r3plim, sys_config.r3time},
      {sys_config.r4rate, sys_config.r4plim, sys_config.r4time},
      {sys_config.r5rate, sys_config.r5plim, sys_config.r5time},
};

bool clear_flags[5] = {false, false, false, false, false}; // One clear_flag per button

TaskHandle_t Task2;
// Define button states
ButtonEvent prev_events[]     = {BUTTON_NONE, BUTTON_NONE, BUTTON_NONE, BUTTON_NONE, BUTTON_NONE};
ButtonEvent prev_event_config = BUTTON_NONE;

/**
 * @brief Handles the coin reader and MQTT communication.
 * This task is responsible for reading the coin acceptor, publishing data to the MQTT broker, and keeping the MQTT client connected.
 * @param pvParameters Not used.
 */
void Task2code(void *pvParameters)
{
    for(;;)
    {
        if(WiFi.isConnected())
        {
            publishMQTT();
        }
        mqttClient.loop();
    }
}

/**
 * @brief Handles button events.
 * This function is called when a button is pressed, and it handles the logic for starting, pausing, and stopping the timers.
 * @param index The index of the button that was pressed.
 * @param event The type of button event that occurred.
 */
void handleButtonEvent(int index, ButtonEvent event)
{
    static int pause_limit[] = {0, 0, 0, 0, 0}; // Pause limits for each button

    RelayConfig config = relayConfigs[index];

    switch(event)
    {
        case BUTTON_SHORT_PRESS:
            if(!timer_started_flags[index])
            {
                if(!timer_pause_flags[index])
                {
                    if(timer_counters[index] >= config.time)
                    { // Use config.time dynamically
                        prev_timer_millis[index]   = millis();
                        timer_started_flags[index] = true;
                        pause_limit[index]         = 0;
                        indicator_state(BUTTON_LED1 + index, GPIO_ON); // Adjust LED dynamically
                        load_state(RELAY1 + index, RELAY_ON);          // Adjust relay dynamically
                        LCD_display_multi_timer(timer_counters[index], index + 1, 0);
                    }
                    else if(fsm.coins >= config.rate)
                    { // Use config.rate dynamically
                        fsm.coins -= config.rate;
                        timer_counters[index]      = config.time;
                        prev_timer_millis[index]   = millis();
                        timer_started_flags[index] = true;
                        pause_limit[index]         = 0;
                        indicator_state(BUTTON_LED1 + index, GPIO_ON);
                        load_state(RELAY1 + index, RELAY_ON);
                        LCD_display_multi_timer(timer_counters[index], index + 1, 0);
                        totalSales += config.rate;
                        saveTotalSales(totalSales);
                    }
                }

                if(timer_pause_flags[index])
                {
                    timer_started_flags[index] = true;
                    timer_pause_flags[index]   = false;
                    indicator_state(BUTTON_LED1 + index, GPIO_ON);
                    load_state(RELAY1 + index, RELAY_ON);
                }
            }
            else
            {
                if(config.plim == 999 || pause_limit[index] < config.plim)
                { // Use config.plim dynamically
                    pause_limit[index]++;
                    timer_started_flags[index] = false;
                    timer_pause_flags[index]   = true;
                    indicator_state(BUTTON_LED1 + index, GPIO_OFF);
                    load_state(RELAY1 + index, GPIO_OFF);
                }
            }
            break;

        case BUTTON_LONG_PRESS:
            // if timer is not running, clear the timer
            if(!timer_started_flags[index] && !timer_pause_flags[index])
            {
                timer_counters[index] = 0;
                LCD_display_multi_timer(timer_counters[index], index + 1, 0);
            }
            break;
        default:
            break;
    }
}

void button_loop()
{
    button_config.update();
    ButtonEvent event_config = button_config.getEvent();

    if(event_config != prev_event_config && event_config != BUTTON_NONE)
    {
        prev_event_config = event_config;
        DEBUG_print("Button Config Event: ");
        DEBUG_println(event_config);
        switch(event_config)
        {
            case BUTTON_PRESSED:
                break;
            case BUTTON_SHORT_PRESS:
                delay(1000);
                bypass_read_flag = true;
                break;
            case BUTTON_MEDIUM_PRESS:
                DEBUG_println("Long press detected");
                load_state_toggle(RELAY2);
                break;
            case BUTTON_LONG_PRESS:
                break;
        }
    }
}

DFRobot_ESP_PH_WITH_ADC ph;

Adafruit_ADS1115 ads;

DFRobot_MAX31855 max31855(&Wire, 0x10);

enum adc_i2c_number
{
    ADC_TURBIDITY_NUM = 0,
    ADC_PH_NUM
};

int       retryCount = 0;
const int maxRetries = 20; // Try for 20 times (about 10 seconds total)

const char *serverUrl = "http://18.142.180.85/api/sensors";
const char *apiKey    = "bf466836378f1ae440a7797deaa8ff6eb9e24150474ecf9d5fd51eb367ce6d4d";

// Initialize success flag for set commands
bool success;

byte modbusBroadcastAddress = 0x00;

byte newModbusAddress = 0x01;

int16_t addressRegister = 0x001E;

int16_t serialNumberRegister = 0x0048;

bool ads_flag = true;

String prettyprintAddressHex(byte _modbusAddress)
{
    String addressHex = F("0x");
    if(_modbusAddress < 0x10)
    {
        addressHex += "0";
    }
    addressHex += String(_modbusAddress, HEX);
    return addressHex;
}

void set_address()
{
    Serial.println(F("\nRunning the 'getSetAddress()' example sketch."));

    Serial.println(F("\nWaiting for sensor and adapter to be ready."));
    Serial.print(F("    Warm up time (ms): "));
    Serial.println(WARM_UP_TIME);
    delay(WARM_UP_TIME);

    Serial.println(F("\nBroadcast modbus address:"));
    Serial.print(F("    Decimal: "));
    Serial.print(modbusBroadcastAddress, DEC);
    Serial.print(F(", Hexidecimal: "));
    Serial.println(prettyprintAddressHex(modbusBroadcastAddress));

    Serial.println(F("Getting current modbus address..."));
    byte oldModbusAddress = modbus.byteFromRegister(0x03, addressRegister, 1);
    Serial.print(F("    Decimal: "));
    Serial.print(oldModbusAddress, DEC);
    Serial.print(F(", Hexidecimal: "));
    Serial.println(prettyprintAddressHex(oldModbusAddress));

    if(oldModbusAddress == 0)
    {
        Serial.println(F("Modbus Address not found!"));
        Serial.println(F("Will scan possible addresses (in future)..."));
    };

    Serial.print(F("\nRestart the modbusMaster instance with the "));
    Serial.println(F("current device address."));
    modbus.begin(oldModbusAddress, rs485Serial, DEREPin);
    delay(WARM_UP_TIME);

    Serial.println(F("\nGetting sensor serial number..."));
    String SN = modbus.StringFromRegister(0x03, serialNumberRegister, 14);
    Serial.print(F("    Serial Number: "));
    Serial.println(SN);

    Serial.print(F("\nSetting sensor modbus address to: "));
    Serial.println(prettyprintAddressHex(newModbusAddress));
    success = modbus.byteToRegister(addressRegister, 1, newModbusAddress, true);
    if(success)
        Serial.println(F("    Success!"));
    else
        Serial.println(F("    Failed!"));
    modbus.begin(newModbusAddress, rs485Serial, DEREPin);
    delay(WARM_UP_TIME);

    Serial.println(F("\nGetting sensor serial number using the new address."));
    SN = modbus.StringFromRegister(0x03, serialNumberRegister, 14);
    Serial.print(F("    Serial Number: "));
    Serial.println(SN);
}
/**
 * @brief Setup function, runs once after reset.
 * This function initializes the hardware and software components of the system.
 */
void setup()
{
    load_init();
    WiFi.begin(ssid, password);
    while(WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        retryCount++;

        if(retryCount >= maxRetries)
        {
            Serial.println("Failed to connect to WiFi. Restarting...");
            indicator_state(BUZZER, GPIO_ON);
            delay(300);
            indicator_state(BUZZER, GPIO_OFF);
            delay(200);
            indicator_state(BUZZER, GPIO_ON);
            delay(200);
            indicator_state(BUZZER, GPIO_OFF);
            delay(500);
            ESP.restart();
        }
    }

    DEBUG_begin(115200);
    Serial.println("Connected to WiFi.");
    wifiConnect();
    deviceID = String((uint32_t) ESP.getEfuseMac() / 3);

    DEBUG_print("deviceID: ");
    DEBUG_println(deviceID);
    init_default_sys_config();

    rs485Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);

    modbus.begin(modbusAddress, rs485Serial, DEREPin);
    modbus.setDebugStream(&Serial);
    Serial.print(F("\nreadWriteRegister() Example "));

    Serial.println(F("\nWaiting for sensor and adapter to be ready."));
    Serial.print(F("    Warm up time (ms): "));
    Serial.println(WARM_UP_TIME);
    delay(WARM_UP_TIME);

    Serial.println(F("\nSelected modbus address:"));
    Serial.print(F("    integer: "));
    Serial.print(modbusAddress, DEC);
    Serial.print(F(", hexidecimal: "));
    Serial.println(prettyprintAddressHex(modbusAddress));

    modbus.setSlaveID(1);
    success = modbus.getRegisters(0x03, 0x00, 10);

#if defined(TEMP_PH_TURB)
    max31855.begin();
    EEPROM.begin(32);
    ads.setGain(GAIN_ONE);
    ph.begin();

    if(!ads.begin())
    {
        DEBUG_println("Failed to initialize ADS1115.");
        ads_flag = false;
    }
#endif

    LCD_init();
    lcdClear();
    lcdText(0, "System Starting", 1, 0);
    delay(2000);
    lcdClear();

    load_state(RELAY1, RELAY_OFF);
    load_state(RELAY2, RELAY_OFF);
    load_state(RELAY3, RELAY_OFF);
    load_state(RELAY4, RELAY_OFF);
    load_state(RELAY5, RELAY_OFF);

    indicator_state(BUTTON_LED1, GPIO_OFF);
    indicator_state(BUTTON_LED2, GPIO_OFF);
    indicator_state(BUTTON_LED3, GPIO_OFF);
    indicator_state(BUTTON_LED4, GPIO_OFF);
    indicator_state(BUTTON_LED5, GPIO_OFF);

    timer_refresh.attach_ms(10, handle_timer_counter);

    xTaskCreatePinnedToCore(Task2code, "Task2", 10000, NULL, 1, &Task2, 0);
    delay(500);

    indicator_state(BUZZER, GPIO_ON);
    delay(500);
    indicator_state(BUZZER, GPIO_OFF);
    load_state(RELAY0, RELAY_ON);
}

/**
 * @brief Reads the temperature from the MAX31855 sensor.
 * @return The temperature in degrees Celsius.
 */
float readTemperature()
{
    float temp = max31855.readCelsius();
    return temp;
}
/**
 * @brief Reads the turbidity from the turbidity sensor.
 * @return The turbidity in NTU.
 */
float readTurbidity()
{
    static float volt = 0;

    static int16_t adc_turbidity;

    adc_turbidity = ads.readADC_SingleEnded(ADC_TURBIDITY_NUM);

    volt = ads.computeVolts(adc_turbidity);

    Serial.print(", voltage_turbidity:");
    Serial.print(volt);
    if(volt < 2.5)
    {
        ntu = 3000;
    }
    else
    {
        ntu = (-1120.4 * (volt * volt)) + (5742.3 * volt) - 4353.8;
    }
    return ntu;
}

float round_to_dp(float in_value, int decimal_place)
{
    float multiplier = powf(10.0f, decimal_place);
    in_value         = roundf(in_value * multiplier) / multiplier;
    return in_value;
}
/**
 * @brief Sends sensor data to a web server.
 * @param phVoltage The pH value.
 * @param temp The temperature value.
 * @param turbidity The turbidity value.
 * @param algaeDensity The algae density value.
 * @param nitrate The nitrate value.
 */
void sendSensorData(float phVoltage, float temp, float turbidity, float algaeDensity, float nitrate)
{
    if(WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        http.begin(serverUrl);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("x-api-key", apiKey);
        String payload = "{\"pH\":\"" + String(phVoltage) + "\",\"temperature\":\"" + String(temp) + "\",\"turbidity\":\"" + String(turbidity)
                         + "\",\"algae_density\":\"" + String(algaeDensity) + "\",\"nitrate\":\"" + String(nitrate) + "\"}";
        int httpResponseCode = http.POST(payload);

        if(httpResponseCode > 0)
        {
            Serial.print("HTTP Response code: ");
            Serial.println(httpResponseCode);
            String response = http.getString();
            Serial.println(response);
        }
        else
        {
            Serial.print("Error on sending POST: ");
            Serial.println(httpResponseCode);
        }

        http.end();

        indicator_state(BUZZER, GPIO_ON);
        delay(100);
        indicator_state(BUZZER, GPIO_OFF);
        delay(100);
        indicator_state(BUZZER, GPIO_ON);
        delay(100);
        indicator_state(BUZZER, GPIO_OFF);
    }
    else
    {
        Serial.println("WiFi not connected");
    }
}

float convertModbusToFloat(uint16_t regHigh, uint16_t regLow)
{
    uint32_t rawData = ((uint32_t) regHigh << 16) | regLow; // Combine registers
    float    value;
    memcpy(&value, &rawData, sizeof(value)); // Convert to float
    return value;
}
/**
 * @brief Main loop of the program.
 * This function is called repeatedly and contains the main logic of the program.
 */
void loop()
{
    static unsigned long timepoint = millis();

    uint16_t nitrate_ppm_h, nitrate_ppm_l           = 0;
    uint16_t temperature_h, temperature_l           = 0;
    uint16_t electrode_signal_h, electrode_signal_l = 0;
    float    temperature;
    float    temperature_test;
    float    electrode_signal_mV;
    bool     success = 0;

#if defined(BYPASS_FLAG)
    bypass_read_flag = true;
#endif
    if(millis() - timepoint > update_interval || bypass_read_flag)
    {
        bypass_read_flag = false;
        timepoint        = millis();
#if defined(BGA_SENSOR)

        modbus.setSlaveID(BGA_SENSOR_ADDRESS); // Switch sensor address
        success = modbus.getRegisters(0x03, 0x00, 10);
        if(success)
        {
            bga_raw = modbus.uint16FromFrame(bigEndian, 9);

            Serial.print("Temperature in °C:");
            Serial.println(temperature);
            Serial.print("BGA cells/ml:");
            Serial.println(bga_raw);
            Serial.println();
        }
        else
        {
            modbus.setSlaveID(1);
            success = modbus.getRegisters(0x03, 0x00, 10);
        }

#endif
#if defined(NITRATE_SENSOR)
        delay(500);
        modbus.setSlaveID(NITRATE_SENSOR_ADDRESS);
        success = modbus.getRegisters(0x03, 0x00, 14);
        if(success)
        {
            nitrate_ppm_h       = modbus.uint16FromFrame(bigEndian, 5);
            nitrate_ppm_l       = modbus.uint16FromFrame(bigEndian, 3);
            nitrate_ppm         = convertModbusToFloat(nitrate_ppm_h, nitrate_ppm_l);
            electrode_signal_h  = modbus.uint16FromFrame(bigEndian, 9);
            electrode_signal_l  = modbus.uint16FromFrame(bigEndian, 7);
            electrode_signal_mV = convertModbusToFloat(electrode_signal_h, electrode_signal_l);
            temperature_h       = modbus.uint16FromFrame(bigEndian, 21);
            temperature_l       = modbus.uint16FromFrame(bigEndian, 19);
            temperature         = convertModbusToFloat(temperature_h, temperature_l);

            Serial.print("Ion concentration value in ppm:");
            Serial.println(nitrate_ppm);
            Serial.print("Electrode signal value in mV:");
            Serial.println(electrode_signal_mV);
            Serial.print("Temperature in °C:");
            Serial.println(temperature);
            Serial.println();
        }
#endif
#if defined(TEMP_PH_TURB)
        static int16_t adc_turbidity, adc_ph;
        static float   voltage_ph;
        if(temperature > 100.0 && temperature < 10.0)
        {
            temperature = 25.0;
        }
        ph.calibration(voltage_ph, temperature);
        if(ads_flag)
        {
            adc_ph = ads.readADC_SingleEnded(ADC_PH_NUM);
            ntu    = readTurbidity();
        }
        else
        {
            adc_ph = 0;
            ntu    = 0;
        }
        temperature_test = readTemperature();

        if(temperature_test > 1.00 && temperature_test <= 100.00)
        {
            temperature = temperature_test;
        }
        else
        {
            // temperature =
        }

        voltage_ph = adc_ph / 10;
        phValue    = ph.readPH(adc_ph / 10, temperature);

        Serial.print("adc_ph:");
        Serial.print(adc_ph);
        Serial.print(", voltage_ph:");
        Serial.print(ads.computeVolts(adc_ph));

        Serial.print(", temperature:");
        Serial.print(temperature);
        Serial.print(" degC");
        Serial.print(", pH:");
        Serial.print(phValue);
        Serial.print(", turbidity:");
        Serial.print(ntu);
        Serial.println(" NTU");

        if(adc_ph <= 0)
        {
            counter++;
            if(counter > 10)
            {
                RestartESP();
            }
        }

#if defined(SEND_DATA)
        sendSensorData(phValue, temperature, ntu, bga_raw, nitrate_ppm);
#endif
#endif
    }

    button_loop();
}

void handle_buzzer()
{
    static unsigned long buzzer_prev_millis = 0;
    static bool          buzzer_state       = false;

    if(start_buzzer_flag)
    {
        unsigned long buzzer_cycle_duration = 1000;
        unsigned long buzzer_on_time        = (buzzer_cycle_duration * sys_config.bcycle) / 100;
        unsigned long buzzer_off_time       = buzzer_cycle_duration - buzzer_on_time;

        if(buzzer_state && millis() - buzzer_prev_millis >= buzzer_on_time)
        {
            indicator_state(BUZZER, GPIO_OFF);
            buzzer_state       = false;
            buzzer_prev_millis = millis();
        }
        else if(!buzzer_state && millis() - buzzer_prev_millis >= buzzer_off_time)
        {
            indicator_state(BUZZER, GPIO_ON);
            buzzer_state       = true;
            buzzer_prev_millis = millis();
        }
    }
    else
    {
        indicator_state(BUZZER, GPIO_OFF);
        buzzer_state = false;
    }
}

void handle_buzzer_flag()
{
    start_buzzer_flag = false;
    for(int i = 0; i < 5; i++)
    {
        if(timer_counters[i] <= sys_config.btime && timer_started_flags[i])
        {
            start_buzzer_flag = true;
            break;
        }
    }
    handle_buzzer();
}
/**
 * @brief Handles the timers for the relays.
 * This function is called by a ticker and is responsible for decrementing the timers and turning off the relays when the timer expires.
 */
void handle_timer_counter()
{
    for(int i = 0; i < 5; i++)
    {
        if(timer_started_flags[i])
        {
            if(millis() - prev_timer_millis[i] >= 1000)
            {
                prev_timer_millis[i] = millis();
                if(timer_counters[i] > 0)
                {
                    timer_counters[i]--;
                    LCD_display_multi_timer(timer_counters[i], i + 1, 0);
                }
                else
                {
                    timer_started_flags[i] = false;
                    timer_pause_flags[i]   = false;
                    indicator_state(BUTTON_LED1 + i, GPIO_OFF);
                    load_state(RELAY1 + i, RELAY_OFF);
                }
            }
        }
        else if(timer_pause_flags[i])
        {
            if(millis() - prev_timer_millis[i] >= 500)
            {
                prev_timer_millis[i] = millis();
                clear_flags[i]       = !clear_flags[i];
                LCD_display_multi_timer(timer_counters[i], i + 1, clear_flags[i]);
                indicator_state(BUTTON_LED1 + i, clear_flags[i] ? GPIO_OFF : GPIO_ON);
            }
        }
    }

    handle_buzzer_flag();
}

#if LCDENABLE == 1
void LCD_init()
{
    if(sys_config.lcdnumber == 2)
    {
        displayCharLength = 16;
        displayRow        = 2;
    }
    LCD_12c_address_scan(displayCharLength, displayRow);
}
#endif