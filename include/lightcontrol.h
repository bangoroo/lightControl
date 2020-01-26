
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <Arduino.h>
#include <ArduinoOTA.h>
#include "credentials.h"

//LED library
#include <WS2812FX.h>
#include <FastLED.h>
#define FASTLED_INTERRUPT_RETRY_COUNT 0

//Temp sensor library
#include <DHT.h>
#include <DHT_U.h>

//Timer
//Timer in min (1000ms * 60s * min)
//Durration of Effect in effect All (15s)
#define TIMER_MS_AUTO 1000*15
//duration for refresh Temperature (10min)
#define TIMER_MS_TEMP 1000*60*10
//duration for button reset (5min)
#define TIMER_MS_BUTTON 1000*60*5

//FileSystem
#include "FS.h"

/****************************************FOR Sensors***************************************/
//PIN of PIR
#define PIR_PIN D0

//PIN LDR
#define LDR_PIN D2

//PIN Sound sensor
#define MIC_PIN D5

//PIN DHT11
#define TEMP_PIN D1

//PIN BUTTON
#define BUTTON_PIN D7

//PIN BUZZER
#define BUTTON_LED_PIN D8

//Type temperature sensor
#define DHTTYPE DHT11   // DHT 11

/****************************************FOR ws2812FX***************************************/
//Number of LEDs
#define NUM_LEDS 256
//Pin of WS2812B strip
#define LED_PIN 0 //D3 = GIPO0 // D4 = GIPO 2
#define DATA_PIN 3  //same as LED Pin, but for FastLED

/*
 Parameter 1 = number of pixels in strip
 Parameter 2 = Arduino pin number (most are valid)
 Parameter 3 = pixel type flags, add together as needed:
   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)
*/
WS2812FX ws2812fx = WS2812FX(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);


/**************************** FOR OTA **************************************************/
#define SENSORNAME "LEDControl" //change this to whatever you want to call your device
//#define OTApassword "OTA" //the password you will need to enter to upload remotely via the ArduinoIDE
int OTAport = 8266;

/************* MQTT TOPICS (change these topics as you wish)  **************************/
const char* light_state_topic = "home/FF_Bedroom_LED_1";
const char* light_set_topic = "home/FF_Bedroom_LED_1/set";
const char* light_notify_topic = "home/FF_Bedroom_LED_1/notify";

const char* temp_state_topic = "home/FF_Bedroom_TEMP_1";
const char* temp_set_topic = "home/FF_Bedroom_TEMP_1/set";

const char* on_cmd = "ON";
const char* off_cmd = "OFF";
const char* effect = "static";
const char* effectsave = "";
String effectString = "static"; //store effect name
String oldeffectString = "solid";

// Initialize DHT sensor.
DHT dht(TEMP_PIN, DHTTYPE);
//variables for DHT11 data
float humidity = 0;
float temperature = 0;
float heatIndex = 0;
 
//state of PIR
int motionState = 0; //0 = no motion, 1 = motion

//state external ldr
bool extLDR = 1; //bool if external LDR is triggerrt

//Button pressed
bool buttonPressed = false;



/****************************************FOR JSON***************************************/
const int BUFFER_SIZE = JSON_OBJECT_SIZE(10);
#define MQTT_MAX_PACKET_SIZE 512

/********************************************** GLOBALS ********************************/
// set brightness
byte brightness = 127;//max 255

//color variables
byte red = 255;
byte green = 255;
byte blue = 255;

byte realRed = 0;
byte realGreen = 0;
byte realBlue = 0;

//LED state
//Regular on
bool stateOn = false;
//motion detection on
bool motionOn = false;
//strip already on?
bool alreadyON = false;
//effect Speed
unsigned int transitionTime = 0;
//LED effect number
unsigned int fxmode = 0;
//bool for play All
bool playAll = false;
//bool for PIR auto mode
bool autoMode = false;
//Which library to use (FastLED or NeoPixel)
bool FastLEDmode = false;

//variables for Timer
unsigned long last_change_auto = 0;
unsigned long last_change_temp = 0;
unsigned long last_change_button = 0;

//amount of color in array
#define NUM_COLORS 3
uint32_t colorArray[NUM_COLORS] = {0xff0000, 0x00ff00, 0x0000ff};
//variable for colorcode in hex
uint32_t hexC;


//enable DEBUG
#ifdef DEBUG_ESP_PORT
#define DEBUG_MSG(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
#else
#define DEBUG_MSG(...)
#endif

void setup_wifi();
void callback(char* topic, byte* payload, unsigned int length);
bool processJson(char* message);
void sendState() ;
void selectMode();
void setRecivedColor();
void setColor(int inR, int inG, int inB) ;
void setPixel(int Pixel, byte red, byte green, byte blue);
void showleds();
void fastLedEffects();
int calculateStep(int prevValue, int endValue);
int calculateVal(int step, int val, int i);
void setupStripedPalette( CRGB A, CRGB AB, CRGB B, CRGB BA);
void fadeall();
void Fire2012WithPalette();
void addGlitter( fract8 chanceOfGlitter);
void addGlitterColor( fract8 chanceOfGlitter, int red, int green, int blue);
void sunrise() ;
void mover();
void ChangeMeMover();
void fillmiddle(int effectMode);
void fillEnd(int effectMode) ;
void soundEffect(int soundMode);
void rainbow_beat();
void rainbow_beatwave();
void BouncingColoredBalls(int BallCount, boolean continuous);
void FillLEDsFromPaletteColors(uint8_t colorIndex);
void ChangePalettePeriodically();
void demoMode();
void buttonCheck();
//void sendButtonState() ;
void gettemperature();
void motionCheck();
bool ldrCheck() ;
bool loadConfig();
bool saveConfig();
void sendToMqtt(String path, bool val, const char* destinationTopic);
void sendToMqtt(DynamicJsonDocument jsonDoc, const char* destinationTopic);