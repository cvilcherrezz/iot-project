#include <DHT.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "DFRobot_ENS160.h" // ENS160 library

// WiFi Credentials
const char* ssid = "";       // Change to your WiFi name
const char* password = ""; // Change to your WiFi password

// MQTT Broker Information
const char* mqtt_server = "test.mosquitto.org";  // Public broker
const int mqtt_port = 1883;  // Default MQTT port
const char* mqtt_client_id = "ESP8266-DHT21";  // Unique ID for the ESP8266

// MQTT Topics
const char* topic_temp = "home/temperature";
const char* topic_hum = "home/humidity";
const char* topic_co2 = "home/co2";
const char* topic_H_warn = "home/hum_warning";
const char* topic_T_warn = "home/temp_warning";
const char* topic_co2_warn = "home/co2_warning";

// Define the GPIO pin where the DHT21 data pin is connected
#define DHTPIN 2  // GPIO2 (D4 on NodeMCU)
#define DHTTYPE DHT21  // DHT21 (AM2301) sensor

#define FAN_PIN 14 // GPIO14 (D5) to control the fan

DHT dht(DHTPIN, DHTTYPE);

// ENS160 Air Quality Sensor
DFRobot_ENS160_I2C ens160(&Wire, 0x52);  // I2C address for ENS160

// WiFi & MQTT Clients
WiFiClient espClient;
PubSubClient client(espClient);

// Function to connect to WiFi
void setup_wifi() {
    delay(10);
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

// Function to connect to MQTT Broker
void reconnect_mqtt() {
    while (!client.connected()) {
        Serial.print("Connecting to MQTT broker... ");
        if (client.connect(mqtt_client_id)) {
            Serial.println("Connected!");
        } else {
            Serial.print("Failed. rc=");
            Serial.print(client.state());
            Serial.println(" Retrying in 2 seconds...");
            delay(2000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    setup_wifi();
    client.setServer(mqtt_server, mqtt_port);
    dht.begin();

    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(FAN_PIN, LOW);

    // Initialize I2C for ENS160 sensor
    Wire.begin(4, 5);  // SDA = GPIO4 (D2), SCL = GPIO5 (D1)
    
    if (ens160.begin() == 0) {
        Serial.println("ENS160 sensor initialized successfully!");
        ens160.setPWRMode(ENS160_STANDARD_MODE);  // Set to standard mode
    } else {
        Serial.println("Failed to find ENS160 sensor, please check the connection!");
        while (1); // Halt execution
    }

    Serial.println("Setup OK...");
}

void loop() {
    if (!client.connected()) {
        reconnect_mqtt();
    }
    client.loop();

    // Read temperature and humidity
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature(); // Celsius

    // Check if readings are valid
    if (isnan(humidity) || isnan(temperature)) {
        Serial.println("Failed to read from DHT sensor!");
    } else {
        // Convert readings to strings
        char tempStr[6], humStr[6];
        dtostrf(temperature, 4, 2, tempStr);
        dtostrf(humidity, 4, 2, humStr);

        // Publish temperature and humidity data
        client.publish(topic_temp, tempStr);
        client.publish(topic_hum, humStr);

        Serial.print("Humidity: ");
        Serial.print(humStr);
        Serial.print(" %\t");
        Serial.print("Temperature: ");
        Serial.print(tempStr);
        Serial.print(" °C\n");

        // Check warning conditions and publish alerts
        if (temperature > 30.0) {
            client.publish(topic_T_warn, "Warning: Temperature too high! Change the chamber to a cooler place.");
            Serial.println("Warning: Temperature too high!");
        }
        else if (temperature < 26){
            client.publish(topic_T_warn, "HEATER turned ON, temperature is too low!");
        }
        else {
            client.publish(topic_T_warn, "Temperature level is OK!");
        }
        
        
    }

    // Read TVOC and CO₂ data from ENS160
    uint16_t eco2 = ens160.getECO2();
    char co2Str[6];
    dtostrf(eco2, 4, 0, co2Str);

    // Publish ENS160 sensor data
    client.publish(topic_co2, co2Str);
    Serial.print("CO₂ (eCO₂) concentration: ");
    Serial.print(co2Str);
    Serial.println(" ppm");

    // Check fan activation conditions
    if (humidity > 85.0 || eco2 > 1000) {
        digitalWrite(FAN_PIN, HIGH);  // Turn on the fan
        if (eco2 > 1000 && humidity < 85.0 && humidity > 75.0){
          client.publish(topic_co2_warn, "FAN turned ON, CO2 too high!");
          client.publish(topic_H_warn, "Humidity level is OK!");
          Serial.println("Fan turned ON due to high CO2");
        }
        else if (eco2 < 1000 && humidity > 85.0){
          client.publish(topic_H_warn, "FAN turned ON, Humidity too high!");
          client.publish(topic_co2_warn, "CO2 level is OK!");
          Serial.println("Fan turned ON due to high humidity");
        }
        else if (eco2 > 1000 && humidity > 85.0){
          client.publish(topic_co2_warn, "FAN turned ON, CO2 too high!");
          client.publish(topic_H_warn, "FAN turned ON, Humidity too high!");
          Serial.println("Fan turned ON due to high humidity and CO2");
        }
    } else if (humidity > 75.0 && humidity <85.0 && eco2 < 1000) {
        digitalWrite(FAN_PIN, LOW);  // Turn off the fan
        Serial.println("Fan turned OFF, Humidity and CO2 levels are OK");
        client.publish(topic_co2_warn, "CO2 level is OK!");
        client.publish(topic_H_warn, "Humidity level is OK!");
    }
    else if (humidity < 75.0) {
          client.publish(topic_H_warn, "Warning: Humidity too low! Spray water in the chamber.");
          Serial.println("Warning: Humidity too low!");
          digitalWrite(FAN_PIN, LOW);  // Turn off the fan
        }

    delay(2000);  // Read and publish data every 2 seconds
}
