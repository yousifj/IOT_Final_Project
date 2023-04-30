
#include <Servo.h>
#include <Arduino.h>
#include <DFRobot_DHT20.h>
#include <WiFi.h>
#include "ThingSpeak.h" // always include thingspeak header file after other header files and custom macros
#include <WiFiClient.h>
#include <cstdint>
#include <HttpClient.h>
#include <ArduinoJson.h>
#include "gravity_soil_moisture_sensor.h"
#include <esp_sleep.h>


#define open 180
#define close 0

char ssid[] = "Home Wireless";    // your network SSID (name)
char pass[] = "245611TV"; // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;            // your network key Index number (needed only for WEP)

//object for the sensor
WiFiClient client;
DFRobot_DHT20 dht20;
GravitySoilMoistureSensor gravity_sensor;



#define Humidity_Offset 1000
#define Dry_Moisture_Value 100
Servo myservo;  // create servo object to control a servo
float lightValue;
float maxAnalogValue=4000;


//PINS
#define Moisture_Sensor 37
int servoPIN = 17;
int photoCellPIN = 36;

unsigned long myChannelNumber = 2112445;
const char * myWriteAPIKey = "6LNT6NPY7KK4QBEW";

// Initialize our values
String myStatus = "";
float dryLevel = 0;
float timeForGalonPerSecond = 2; //1/0.17 = seconds for 1 galon 
bool shouldSleep = false;
bool valveStatus = false;
float valveDuration = 2000; // open it for 20 seconds
bool goodWeatherForWatering = true;
long sleepTimeInterval = 3600000;//every hour
int badLightCounter = 0;
unsigned long requestCounter =0;
//functions

void handlePhysicalValve(int moistureValue);

void handelOnlinePlatform(float n1,float n2,float n3, float n4,const char* weatherCondtion);
const char* getWeather();
void hourLongDeepSleep();

void setup() {
    //Initialize serial and wait for port to open
    Serial.begin(9600);  // Initialize serial

    Serial.print("Searching for ESP8266...");
    // initialize ESP module
    WiFi.begin(ssid, pass);
    int connectionAttempts = 0; 
    // check for the presence of the shield
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        Serial.print(".");
        //connectionAttempts++;
        //check 2 mints have passed an no connection 
        if (connectionAttempts > 240){
          connectionAttempts = 0;
          hourLongDeepSleep();
          
        }
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("MAC address: ");
    Serial.println(WiFi.macAddress());

    //Initialize sensor
    while(dht20.begin()){
        Serial.println("DHT 20 Not Connected");
        delay(1000);
    }

    //Initialize sensor
    while(!gravity_sensor.Setup(Moisture_Sensor)){
        Serial.println("Soil Moisture Sensor was not detected. Check wiring!");
        delay(1000);
    }
    // attaches the servo on pin
    myservo.attach(servoPIN);

    ThingSpeak.begin(client);  // Initialize ThingSpeak

}

void loop() {
  //a new loop execution have been made 
  requestCounter++;
  //configer the timer so that this code runs every hour
  // calculate the time taken for the loop code to execute
  unsigned long loopTime = millis();

  // Connect or reconnect to WiFi
  int connectionAttempts = 0; 
  if(WiFi.status() != WL_CONNECTED){
      Serial.print("Attempting to connect to SSID: ");
      Serial.println(ssid);
      while(WiFi.status() != WL_CONNECTED){
          WiFi.begin(ssid, pass); 
          Serial.print(".");
          delay(100);
          connectionAttempts++;
          //check if 100 Attempts have been made
          if (connectionAttempts > 100){
            connectionAttempts = 0;
            //hourLongDeepSleep(); 
          }
        }
      Serial.println("\nConnected.");
  }
  //read the light vaule
  lightValue = analogRead(photoCellPIN);
  Serial.print("light vaule: ");
  Serial.println(lightValue);
  //read Moisture sensor
  uint16_t value = gravity_sensor.Read();
  int moistureValue = abs(value-Humidity_Offset);
  Serial.printf("Moisture: %d\n", moistureValue);

  //read temperature
  float temperature = dht20.getTemperature();
  float temperatureFahrenheit = (temperature * 9/5) + 32;
  float humidity = dht20.getHumidity()*100;
  Serial.printf("Temperature: %.2f degrees Fahrenheit, Humidity: %.2f%%\n", temperatureFahrenheit, humidity);
  //check if weather is good for wataring 
  const char* weatherCondtion =  getWeather();

  //only water on good days
  if(goodWeatherForWatering){
    handlePhysicalValve(moistureValue);
  }
  //post the info on the website
  handelOnlinePlatform(temperatureFahrenheit,humidity,moistureValue,lightValue,weatherCondtion);
    
  // put the ESP32 to sleep for sleepTime minus the time taken for the loop code to execute
  long sleepForTime = (sleepTimeInterval - loopTime)*1000;
  Serial.printf("sleepTime for : %d\n", sleepTimeInterval);
  Serial.printf("loopTime for : %d\n", loopTime);
  Serial.printf("Sleep for : %d\n", sleepForTime);
  
    //sleep(sleepForTime * 1000);
  //esp_sleep_enable_timer_wakeup(sleepForTime);
  //delay(100);
  //esp_deep_sleep_start();
  WiFi.disconnect(true);
  hourLongDeepSleep();
  delay(100);
  //delay(40000); // Wait 20 seconds to update the channel again
}

void handelOnlinePlatform(float temp,float hum,float mois, float light,const char* weatherCondtion) {
  // set the fields with the values
    
    ThingSpeak.setField(1, temp);
    ThingSpeak.setField(2, hum);
    ThingSpeak.setField(3, mois);
    ThingSpeak.setField(4, light);

    // figure out the status message
    
    //myStatus = String("field1 is greater than field2");
    if(dryLevel > 0 && goodWeatherForWatering){
      myStatus = "We waterd the garden with "+String(dryLevel)+" Gallons and weather condition is " + String(weatherCondtion);
    }else{
      myStatus = "No need to water and the weather condition is " + String(weatherCondtion);
    }
    //bad lighting condtions 
    if(lightValue>200){
      badLightCounter++;
    }
    //most vegetables and flowering plants need 12 to 16 hours of light per day
    //24 is the number requestCounter we make in 24 hours
    if(requestCounter > 24){
      if(badLightCounter > (requestCounter/2)){
            myStatus = String(myStatus) + " Lighting condition was bad for the day.\n Clear the view for your plants";
            //reset counters 
            requestCounter=0;
            badLightCounter=0;
          }
    }
    
    // set the status
    ThingSpeak.setStatus(myStatus);

    // write to the ThingSpeak channel
    int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if(x == 200){
      Serial.println("Channel update successful.");
    }
    else{
      Serial.println("Problem updating channel. HTTP error code " + String(x));
    }

}

void handlePhysicalValve(int moistureValue) {
    //uint16_t value;//Level of dryness higher means we have to use more water
    if (moistureValue < 250) {
        dryLevel = 4;
    } else if (moistureValue >= 250 && moistureValue <= 350) {
        dryLevel = 3;
    } else if (moistureValue > 350 && moistureValue <= 600) {
        dryLevel = 2;
    } else if (moistureValue > 600) {
        dryLevel = 0;
    }

    //typical garden hose with a diameter of 5/8 inch and a length of 50 feet can deliver
    //about 10 gallons of water per minute (GPM) or 0.17 gallons per second (G/s).
    //for dryLevel is the number of galons
    valveDuration = (dryLevel/timeForGalonPerSecond)*1000; //seconds to ms
    Serial.println("valveDuration: ");
    Serial.println(valveDuration);
    Serial.println(dryLevel);
    Serial.println(moistureValue);
    // open the valve for spsific amount of time
    if(valveStatus||dryLevel>0){
        myservo.write(open);
        delay(valveDuration);
        myservo.write(close);
        valveStatus = false;
    }else{
        myservo.write(close);
    }
}
const char* getWeather(){
  // Send HTTP request to OpenWeatherMap API
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?zip=92021&appid=d5a4031ac2508eff8ece3222c482dfa2";
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    // Parse JSON response
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    
    // Extract weather condition
    const char* condition = doc["weather"][0]["main"];
    Serial.println(condition);
    if (strcmp(condition, "Clouds") != 0 && strcmp(condition, "Clear") != 0) {
      // The weather condition is not "Clouds" or "Clear"
      goodWeatherForWatering = false;
      Serial.println("The weather is not clear or cloudy. Consider postponing garden watering.");
    } 
    return condition;
  } else {
    //do not block if no weather data was found
    goodWeatherForWatering = true;
    Serial.println("Failed to fetch weather data");
    return "Failed to fetch weather data";
  }
    

  http.end();
}

void hourLongDeepSleep(){
  Serial.print("Deep Sleeping for an hour");
  //deep sleep for an hour
  unsigned long long hourInNano = 60ULL * 60 * 1000000;
  unsigned long long halfHourInNano = hourInNano / 2;
  esp_sleep_enable_timer_wakeup(halfHourInNano);
  delay(100);
  esp_deep_sleep_start();
}