#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>  // TODO: not sure if we need this
#include <ESP8266mDNS.h>
#include <FS.h>
#include <TimeLib.h>
#include <NtpClientLib.h>
#include <ArduinoJson.h>
#include <SPI.h>

// configure sensor
#include "Adafruit_CCS811.h"
#define CS 15 //define chip select line for manual control

Adafruit_CCS811 ccs;
uint8_t sensordrivemode = CCS811_DRIVE_MODE_250MS;

// Put in your wifi credentials here
const char* ssid = ;       
const char* password = ;

// server 
ESP8266WebServer server (80);

// buffer and data storage 
// latest readings; initially set to very negative values 
float   temp = -999 ;
float   eCO2 = -999 ;
float   TVOC = -999;

// history data
int sizeHist = 5;    // 50 samples for each metric in history 
const int buff_size = 250;
const long intervalHist = 1000;  // in ms
unsigned long previousMillis = intervalHist;  // Dernier point enregistré dans l'historique - time of last point added

// LED data
unsigned int NLED = 42;     // total number of LEDs on the strip
uint8_t LEDmode = 0;        //LED mode byte
uint8_t LEDreceive;

// Json buffer to keep track of measurements
StaticJsonBuffer<10000> jsonBuffer; 
char json[buff_size]; 
JsonObject& root = jsonBuffer.createObject();
JsonArray& timestamp = root.createNestedArray("timestamp"); 
JsonArray& hist_temp = root.createNestedArray("temp"); 
JsonArray& hist_eCO2 = root.createNestedArray("eCO2");
JsonArray& hist_TVOC = root.createNestedArray("TVOC");
#define HISTORY_FILE "/history.json"

/* 
 *  Set up the network time protocol for timing. 
 * Updates the LEDmode byte based on user entrance on the website. 
 * Use ESP8266 as the SPI master to communicate with MSP430.
 */


void setupNTP() {
  NTP.onNTPSyncEvent([](NTPSyncEvent_t error) {
    if (error) {
      Serial.print("Time Sync error: ");
      if (error == noResponse)
        Serial.println("NTP server not reachable");
      else if (error == invalidAddress)
        Serial.println("Invalid NTP server address");
      }
    else {
      Serial.print("Got NTP time: ");
      Serial.println(NTP.getTimeDateString(NTP.getLastNTPSync()));
    }
  });
  
  // Serveur NTP, decalage horaire, heure été - NTP Server, time offset, daylight 
  NTP.begin("pool.ntp.org", 0, true); 
  NTP.setInterval(60000);
  delay(500);  
  
}


/*
 * Set up the sensor drive mode and 
 */
void setupsensor(void){
  if(!ccs.begin()){
    Serial.println("Failed to start sensor! Please check your wiring.");
    while(1);
  }

  //calibrate temperature sensor
  while(!ccs.available());
  float temp = ccs.calculateTemperature();
  ccs.setTempOffset(temp - 25.0);   // set temperature offset
  ccs.setDriveMode(sensordrivemode);   // set sensor drive mode
}


/*
 * Sends the LED mode to the SPI slave. 
 */
void send_LEDmode() {
  Serial.print("LEDmode to send: ");
  Serial.print(LEDmode);
  digitalWrite(SS, LOW);
  LEDreceive = SPI.transfer(LEDmode);
  Serial.print("LEDmode received: ");
  Serial.print(LEDreceive);
}


/*
 * Configure SPI settings. 
 */
void setupSPI_test() {
  SPI.begin();
  pinMode(SS, OUTPUT);
  digitalWrite(SS, HIGH);
  SPI.beginTransaction (SPISettings (100000, MSBFIRST, SPI_MODE0));
  // set maximum speed of communication, byte format, clock polarity and phase 
  delay(1000);
//  send_test("Hello Slave!");
}


/*
 * Triggered when the user sends in LED mode and sets the 
 * global LEDmode variable for transmission. 
 */
void receiveLED() {
    String gpio = server.arg("id");  
    int success = 1;
    String state = server.arg("state");

    if (gpio == "Off") {
      LEDmode = 0;  
    } else if (gpio == "Rainbow") {
      LEDmode = 1;
    } else if (gpio == "Berry") {
      LEDmode = 2;  
    } else if (gpio == "Tropical") {
      LEDmode = 3;  
    } else if (gpio == "Mermaid") {
      LEDmode = 4;
    }
    
    send_LEDmode();
    Serial.println(LEDmode);
    String json = "{\"gpio\":\"" + String(gpio) + "\",";
    json += "\"state\":\"" + String(state) + "\",";
    json += "\"success\":\"" + String(success) + "\"}";

    server.send(200, "application/json", json);
    Serial.println("GPIO update");
}


/*
 * Load the history file from SPIFFS.
 */
void loadHistory(){
  File file = SPIFFS.open(HISTORY_FILE, "r");
  if (!file){
    Serial.println("Aucun historique existe - No History Exist");
  } else {
    size_t size = file.size();
    if ( size == 0 ) {
      Serial.println("Fichier historique vide  History file empty !");
    } else {
      std::unique_ptr<char[]> buf (new char[size]);
      file.readBytes(buf.get(), size);
      JsonObject& root = jsonBuffer.parseObject(buf.get());
      if (!root.success()) {
        Serial.println("Impossible de lire le JSON - Impossible to read JSON file");
      } else {
        Serial.println("Historique charge - History loaded");
        root.printTo(Serial);  
      }
    }
    file.close();
  }
}

/*
 * Save the history file.
 */
void saveHistory(){          
  File historyFile = SPIFFS.open(HISTORY_FILE, "w");
  root.printTo(historyFile); // Exporte et enregsitre le JSON dans la zone SPIFFS - Export and save JSON object to SPIFFS area
  historyFile.close();  
}

/*
 * Send json history file. 
 */
void sendHistory(){  
  root.printTo(json, sizeof(json));             // Export du JSON dans une chaine - Export JSON object as a string
  Serial.println(json);
  server.send(200, "application/json", json);   // Envoi l'historique au client Web - Send history data to the web client
  Serial.println("History File!");
     
}

/*
 * Update the measurements in the buffer as well as the 
 * history file. 
 */
void addMeasurementsToHist(){
  unsigned long currentMillis = millis();
  if ( currentMillis - previousMillis > intervalHist ) {
    previousMillis = currentMillis;
    long int tps = NTP.getTime();
//    Serial.println("Checking NTP gettime!");
    timestamp.add(tps);
//    Serial.println(timestamp.size());
    hist_temp.add(double_with_n_digits(temp, 2));  // 2 digits for each
    hist_eCO2.add(double_with_n_digits(eCO2, 2));
    hist_TVOC.add(double_with_n_digits(TVOC, 2));

    if ( hist_temp.size() > sizeHist ) {
      Serial.println("Array is full!");
      timestamp.removeAt(0);
      hist_temp.removeAt(0);
      hist_eCO2.removeAt(0);
      hist_TVOC.removeAt(0);
    } 
    saveHistory();
  }
}

void setup() {
  
  Serial.begin(230400);
  Serial.println("started");

    // Set up the sensor 
  setupsensor();
  
  // setup NTP 
  setupNTP();

  // setup SPI
  setupSPI_test();
 
  // setup WIFI
  WiFi.begin ( ssid, password );
  int tentativeWiFi = 0;
  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 ); Serial.print ( "." );
    tentativeWiFi++;
    if ( tentativeWiFi > 20 ) {       // reset if wait time is too long
      ESP.reset();
      while(true)
        delay(1);
    }
  }
  
  // WiFi connexion is OK
  Serial.println ( "" );
  Serial.print ( "Connected to " ); Serial.println ( ssid );
  Serial.print ( "IP address: " ); Serial.println ( WiFi.localIP() );

  // SPIFFS for storing web content 
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS Mount failed");        // Serious problem with SPIFFS 
  } else { 
    Serial.println("SPIFFS Mount succesfull");
    loadHistory();
  }
  delay(50);

  // set server behaviors
  server.on("/chart.json", sendHistory);
  server.on("/gpio", receiveLED);       // TODO (Tianyi): add the LED web control here 

  // not exactly sure what these are doing
  server.serveStatic("/", SPIFFS, "/index.html");

  server.begin();
  Serial.println( "HTTP server started" );
   
  Serial.print("Uptime :");
  Serial.println(NTP.getUptime());
  Serial.print("LastBootTime :");
  Serial.println(NTP.getLastBootTime());
}


void update_sensor_vals() {
  if(ccs.available()){
    temp = ccs.calculateTemperature();    // Temperature
    if(!ccs.readData()){
      eCO2 = ccs.geteCO2();    // CO2
      Serial.print("CO2: ");
      Serial.print(eCO2);
      TVOC = ccs.getTVOC();     // VOC
      Serial.print("ppm, TVOC: ");
      Serial.print(TVOC);
      Serial.print("ppb   Temp:");
      Serial.println(temp);
    }
    else{
      Serial.println("SENSOR ERROR!");
      while(1);
    }
  }
  if ( isnan(temp) || isnan(eCO2) || isnan(TVOC) ) {
    //Error, no valid value
    Serial.println("Sensor readings contain NANs!");
  } else {
    addMeasurementsToHist();
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  server.handleClient();
  update_sensor_vals();
}

