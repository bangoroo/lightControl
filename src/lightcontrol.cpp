#include <Arduino.h>


#define FASTLED_INTERRUPT_RETRY_COUNT 0

#ifdef DEBUG_ESP_PORT
#define DEBUG_MSG(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
#else
#define DEBUG_MSG(...)
#endif

#include <WS2812FX.h>
#include <FastLED.h>

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
//#include <ArduinoOTA.h>


//Temp sensor library
#include <DHT.h>
#include <DHT_U.h>

//Timer in min (1000ms * 60s * min)

//Durration of Effect in effect All (5s)
#define TIMER_MS_AUTO 1000*5
//duration for refresh Temperature (10min)
#define TIMER_MS_TEMP 1000*60*10
//duration for button reset (5min)
#define TIMER_MS_BUTTON 1000*60*5

#include "FS.h"



/************ WIFI and MQTT Information (CHANGE THESE FOR YOUR SETUP) ******************/
const char* ssid = "FRITZ!Box 7490"; //type your WIFI information inside the quotes
const char* password = "**************";
const char* mqtt_server = "192.168.178.20";
const char* mqtt_username = "openhabian";
const char* mqtt_password = "*****";
const int mqtt_port = 1883;

/**************************** FOR OTA **************************************************/
#define SENSORNAME "LEDControl" //change this to whatever you want to call your device
//#define OTApassword "OTA" //the password you will need to enter to upload remotely via the ArduinoIDE
//int OTAport = 8266;

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


/****************************************FOR Sensors***************************************/
//PIN of PIR
#define PIR_PIN D0

//PIN LDR
#define LDR_PIN D2

//PIN Sound sensor
#define MIC_PIN D4

//PIN DHT11
#define TEMP_PIN D1

//PIN BUTTON
#define BUTTON_PIN D7

//PIN BUZZER
#define BUTTON_LED_PIN D8

//Type temperature sensor
#define DHTTYPE DHT11   // DHT 11
// Initialize DHT sensor.
DHT dht(TEMP_PIN, DHTTYPE);
//variables for DHT11 data
float humidity = 0;
float temperature = 0;
float heatIndex = 0;

//state of PIR
int motionState = 0; //0 = no motion, 1 = motion

//state ldr
int lightState = 0;//0 = no light, 1 = light

//Button pressed
bool buttonPressed = false;

/****************************************FOR ws2812FX***************************************/
//Number of LEDs
#define NUM_LEDS 256
//Pin of WS2812B strip
#define LED_PIN 0 //D7//2 // D4 = GIPO 2
#define DATA_PIN 3 //4 //same as LED Pin, but for FastLED

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

//store if color has change
boolean colorChanged = false;

//LED state
//Regular on
bool stateOn = false;
//motion detection on
bool motionOn = false;
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

/********************************** GLOBALS for EFFECTS ******************************/
//Sunrise
// total sunrise length, in minutes
static const uint8_t sunriseLength = 15;
// current gradient palette color index
uint8_t heatIndexled = 0; // start out at 0
bool resetSunrise = true;

//fill middle, fillEnd
unsigned int/* uint16_t*/ help_index_1 = (NUM_LEDS - 1) / 2;
unsigned int/* uint16_t*/ help_index_2 = NUM_LEDS - 1 ;

//mover
uint8_t thisdelaymover = 500;      // A delay value for the sequence(s).
uint8_t  thisfademover = 192;

//RAINBOW
uint8_t thishue = 0;               // Starting hue value.
uint8_t deltahue = 10;

//CANDYCANE
CRGBPalette16 currentPalettestriped; //for Candy Cane

CRGBPalette16 gPal; //for fire

//NOISE
static uint16_t dist;         // A random number for our noise generator.
uint16_t scale = 30;          // Wouldn't recommend changing this on the fly, or the animation will be really blocky.
uint8_t maxChanges = 48;      // Value for blending between palettes.
CRGBPalette16 targetPalette(OceanColors_p);
CRGBPalette16 currentPalette(CRGB::Black);
TBlendType    currentBlending;                                // NOBLEND or LINEARBLEND

//TWINKLE
#define DENSITY     80
int twinklecounter = 0;

//RIPPLE
uint8_t colour;                                               // Ripple colour is randomized.
int center = 0;                                               // Center of the current ripple.
int step = -1;                                                // -1 is the initializing step.
uint8_t myfade = 255;                                         // Starting brightness.
#define maxsteps 16                                           // Case statement wouldn't allow a variable.
uint8_t bgcol = 0;                                            // Background colour rotates.
int thisdelay = 20;                                           // Standard delay value.

//DOTS
uint8_t   count =   0;                                        // Count up to 255 and then reverts to 0
uint8_t fadeval = 224;                                        // Trail behind the LED's. Lower => faster fade.
uint8_t bpm = 30;

//LIGHTNING
uint8_t frequency = 50;                                       // controls the interval between strikes
uint8_t flashes = 8;                                          //the upper limit of flashes per strike
unsigned int dimmer = 1;
uint8_t ledstart;                                             // Starting location of a flash
uint8_t ledlen;
int lightningcounter = 0;

//FUNKBOX
int idex = 0;                //-LED INDEX (0 to NUM_LEDS-1
int TOP_INDEX = int(NUM_LEDS / 2);
int thissat = 255;           //-FX LOOPS DELAY VAR
uint8_t thishuepolice = 0;
int antipodal_index(int i) {
  int iN = i + TOP_INDEX;
  if (i >= TOP_INDEX) {
    iN = ( i + TOP_INDEX ) % NUM_LEDS;
  }
  return iN;
}

//FIRE
#define COOLING  55
#define SPARKING 120
bool gReverseDirection = false;

//BPM
uint8_t gHue = 0;

//CHRISTMAS
int toggle = 0;

//RANDOM STARS
const int NUM_STARS = NUM_LEDS / 10;
static int stars[NUM_STARS];

//SINE HUE
int hue_index = 0;
int led_index = 0;

//define clients
WiFiClient espClient;
PubSubClient client(espClient);

CRGB leds[NUM_LEDS];

/******************************** GLOBALS for fade/flash *******************************/
bool startFade = false;
bool onbeforeflash = false;
unsigned long lastLoop = 0;

int effectSpeed = 0;
bool inFade = false;
int loopCount = 0;
int stepR, stepG, stepB;
int redVal, grnVal, bluVal;

bool flash = false;
bool startFlash = false;
int flashLength = 0;
unsigned long flashStartTime = 0;
byte flashRed = red;
byte flashGreen = green;
byte flashBlue = blue;
byte flashBrightness = brightness;

void setup_wifi();
void callback(char* topic, byte* payload, unsigned int length);
bool processJson(char* message);
void sendState() ;
void selectMode();
void setRecivedColor();
void setColor(int inR, int inG, int inB) ;
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
void FillLEDsFromPaletteColors(uint8_t colorIndex);
void ChangePalettePeriodically();
void demoMode();
void buttonCheck();
//void sendButtonState() ;
void gettemperature();
void motionCheck();
void ldrCheck() ;
bool loadConfig();
bool saveConfig();
void sendToMqtt(String path, bool val, const char* destinationTopic);



/********************************** START SETUP*****************************************/
void setup() {

  //Start Serial
  Serial.begin(115200);

  //Serial.println("Start Setup");
  DEBUG_MSG("Start Setup\n");
  FastLED.addLeds<WS2812B, DATA_PIN , GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50);
  setupStripedPalette( CRGB::Red, CRGB::Red, CRGB::White, CRGB::White); //for CANDY CANE
  gPal = HeatColors_p; //for FIRE
  //Serial.println("FastLED setup complete");
  DEBUG_MSG("FastLED setup complete\n");

  //set PinMode of PIR
  pinMode(PIR_PIN, INPUT);

  //set PinMode of LED
  pinMode(BUTTON_LED_PIN, OUTPUT);

  //set PinMode Button
  pinMode(BUTTON_PIN, INPUT);

  //set PinMode of LDR
  pinMode(LDR_PIN, INPUT);

  //set PinMode of SOUND sensor
  pinMode(MIC_PIN, INPUT);
  //Serial.println("PinMode set");
  DEBUG_MSG("PinMode set\n");


  //start dht sensor
  dht.begin();
  //Serial.println("DHT start");
  DEBUG_MSG("DHT start\n");


  //Setup Wifi & mqtt
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  //Serial.println("Client ready");
  DEBUG_MSG("Client ready\n");

  /* //OTA SETUP
    ArduinoOTA.setPort(OTAport);
    // Hostname defaults to esp8266-[ChipID]
    ArduinoOTA.setHostname(SENSORNAME);

    // No authentication by default
    ArduinoOTA.setPassword(OTApassword);

    ArduinoOTA.onStart([]() {
     Serial.println("Starting");
    });
    ArduinoOTA.onEnd([]() {
     Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
     Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
     Serial.printf("Error[%u]: ", error);
     if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
     else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
     else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
     else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
     else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();*/

  Serial.println("Ready");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());


  /********************************** START SETUP ws2812FX*****************************************/
  Serial.print("WS2812FX Setup...");
  //initialize ws2812fx
  ws2812fx.init();
  ws2812fx.setBrightness(brightness);

  // parameters: index, start, stop, mode, color, speed, reverse
  //ws2812fx.setSegment(0,  0,  NUM_LEDS, FX_MODE_STATIC, 0x007BFF, 1000, false); // segment 0 is leds 0 - 300

  ws2812fx.start();

  Serial.println("Done");
  //loadSettingsFromEEPROM();
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }
  
  if (!loadConfig()) {
    Serial.println("Failed to load config");
  } else {
    Serial.println("Config loaded");
  }
  
}

/********************************** START SETUP WIFI*****************************************/
void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    ESP.wdtFeed();
    delay(500);
    yield();
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

/********************************** START RECONNECT*****************************************/
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(SENSORNAME, mqtt_username, mqtt_password)) {
      Serial.println("connected");
      client.subscribe(light_set_topic);

      ESP.wdtFeed();
      delay(500);
      //ws2812fx.setSegment(0, 0, NUM_LEDS, 1, colorArray, transitionTime, false); // segment 0 is leds 0 - 300
      sendState();
      sendToMqtt("button", buttonPressed, light_notify_topic);
      //sendButtonState();
      

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      yield();
      delay(5000);
    }
  }
}

/********************************** START MAIN LOOP*****************************************/
void loop() {

  //reconnect
  if (!client.connected()) {
    reconnect();
  }
  //check Wifi state
  if (WiFi.status() != WL_CONNECTED) {
    delay(1);
    yield();
    Serial.print("WIFI Disconnected. Attempting reconnection.");
    setup_wifi();
    return;
  }
  ESP.wdtFeed();
  client.loop();

  //ArduinoOTA.handle();

  delay(1);

  //Check Button
  buttonCheck();

  //Check LDR state
  ldrCheck();

  //Check for Motion
  if(!stateOn){
    motionCheck();
  }
  
  //refresh Temperature
  gettemperature();

  //start ws2812fx.service
  ws2812fx.service();

  //select mode
  selectMode();

  //change color
  if (colorChanged) {
    setRecivedColor();
  }

  //Start ws2812fx
  showleds();

  if (playAll) {
    demoMode();
  }
  yield();
}

/********************************** START CALLBACK*****************************************/
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  //read message
  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println(message);

  if (!processJson(message)) {
    return;
  }

  if (stateOn) {
    //Serial.println("MAPPING");
    realRed = map(red, 0, 255, 0, brightness);
    realGreen = map(green, 0, 255, 0, brightness);
    realBlue = map(blue, 0, 255, 0, brightness);
    /*Serial.println(realRed);
      Serial.println(realGreen);
      Serial.println(realBlue);
      Serial.print("        ");
      Serial.println(brightness);*/

  }
  else {

    realRed = 0;
    realGreen = 0;
    realBlue = 0;
  }
  startFade = true;
  inFade = false; // Kill the current fade

  Serial.println(effect);
  sendState();
}

/********************************** START PROCESS JSON*****************************************/
bool processJson(char* message) {
  //Serial.print("ProcessJSON...");
  DEBUG_MSG("ProcessJSON...");

  //StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);

  JsonObject& root = jsonBuffer.parseObject(message);

  if (!root.success()) {
    Serial.println("parseObject() failed");
    return false;
  }

  //read power state
  if (root.containsKey("state")) {
    if (strcmp(root["state"], on_cmd) == 0) {
      stateOn = true;
    } else if (strcmp(root["state"], off_cmd) == 0) {
      stateOn = false;
      onbeforeflash = false;
    }
  }
  //read brightness
  if (root.containsKey("brightness")) {
    brightness = root["brightness"];
    //brightness = brightness*2.55;
    brightness = map(brightness, 0, 100, 0, 255);
    Serial.print("Set brightness: ");
    Serial.println(brightness);
  }
  //read effect
  if (root.containsKey("effect")) {
    effect = root["effect"];
    effectString = effect;
    oldeffectString = effect;
  }
  //read color
  if (root.containsKey("color")) {
    red = root["color"]["r"];
    green = root["color"]["g"];
    blue = root["color"]["b"];
    colorChanged = true;
    //setColor(red, green, blue);
  }
  //read speed
  if (root.containsKey("transition")) {
    transitionTime = root["transition"];
    transitionTime = transitionTime * 10;
  }
  else if ( effectString == "static") {
    transitionTime = 0;
  }
  //read auto mode
  if (root.containsKey("auto")) {
    if (strcmp(root["auto"], on_cmd) == 0) {
      autoMode = true;
    } else if (strcmp(root["auto"], off_cmd) == 0) {
      autoMode = false;
    }
  }
  //Serial.println("Done");
  DEBUG_MSG("Done\n");
  //saveSettingsToEEPROM();
  if (!saveConfig()) {
    Serial.println("Failed to save config");
  } else {
    Serial.println("Config saved");
  }
  //loadSettingsFromEEPROM();

  return true;
}

/********************************** START SEND STATE*****************************************/
void sendState() {
  //Serial.print("SendState...");
  DEBUG_MSG("SendState");

  //StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);


  JsonObject& root = jsonBuffer.createObject();

  root["state"] = (stateOn) ? on_cmd : off_cmd;
  JsonObject& color = root.createNestedObject("color");
  color["r"] = red;
  color["g"] = green;
  color["b"] = blue;
  //light
  root["brightness"] = map(brightness, 0, 255, 0, 100);//brightness/2.55;
  root["effect"] = effectString.c_str();

  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  client.publish(light_state_topic, buffer, true);
 // Serial.println("Done");
  DEBUG_MSG("Done.\n");

}

/********************************** Select Mode ***********************************************/
void selectMode() {
  //Serial.println("selectMode...");
  DEBUG_MSG("selectMode\n");


  ESP.wdtFeed();
  //check which effect is selected
  for (int i = 0; i < ws2812fx.getModeCount(); i++) {

    if (effectString == ws2812fx.getModeName(i)) {
      //set Color
      fxmode = i;
      effectString = "";
      resetSunrise = true;//reset Sunrise see also 04_effects /EFFECT SUNRISE
      Serial.print("MODE: ");
      Serial.println(ws2812fx.getModeName(ws2812fx.getMode()));
      FastLEDmode = false;
    }
    yield();
  }
  //check if play All is selected
  if (effectString == "All") {
    playAll = true;
  } else {
    playAll = false;
  }

  //if effectString is not Empty then check for FastLED effects
  if (effectString.length() >= 1) {
    fastLedEffects();
  }

  //if automode is on set effect to static
  if (autoMode && !stateOn) {
    fxmode = 0;
  }
}

/********************************** START Set Color*****************************************/
void setRecivedColor() {

  if (colorChanged) {

    Serial.println("Change Color...");
    //convert color
    hexC = ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
    //shift colors
    for (int c = NUM_COLORS - 1; c >= 0; c--) {
      colorArray[c + 1] = colorArray[c];
      Serial.println(colorArray[c], HEX);
    }
    //change color
    colorArray[0] = hexC;
    colorChanged = false;
  }
}

void setColor(int inR, int inG, int inB) {
  Serial.println("Set Color...");
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].red   = inR;
    leds[i].green = inG;
    leds[i].blue  = inB;
  }
}

/********************************** START SHOW LEDS ***********************************************/
void showleds() {
  //Serial.println("ShowLeds");
  DEBUG_MSG("showLeds\n");
  //Update Segment
  // parameters: index, start, stop, mode, color, speed, reverse
  ESP.wdtFeed();
  if (!FastLEDmode) {
    //Serial.println("WS2812FX set Segment");
    DEBUG_MSG("WS2812FX setSegment\n");
    ws2812fx.setSegment(0, 0, NUM_LEDS, fxmode, colorArray, transitionTime, false); // segment 0 is leds 0 - 300

    // Serial.println("set Brightness");
    //set Brightness
    ws2812fx.setBrightness(brightness);

    if ((motionOn || stateOn) && !ws2812fx.isRunning()) {
      //ws2812fx.setSegment(0, 0, NUM_LEDS, fxmode, colorArray, transitionTime, false); // segment 0 is leds 0 - 300
      ws2812fx.start();
      Serial.println("WS2812FX Start");

    } else if ((!stateOn && !motionOn) && ws2812fx.isRunning()) {
      //ws2812fx.setColor(0x000000);
      // ws2812fx.setSegment(0, 0, NUM_LEDS, fxmode, colorArray, transitionTime, false); // segment 0 is leds 0 - 300
      ws2812fx.stop();
      Serial.println("WS2812FX Stop");
    }


  } else if (FastLEDmode && stateOn) {
   // Serial.println("FastLED show");
   DEBUG_MSG("FastLED show\n");
    if (ws2812fx.isRunning()) {
      ws2812fx.stop();
    }

    FastLED.setBrightness(brightness);
    FastLED.show();
    if ((transitionTime / 10) > 0 && (transitionTime / 10) < 130) { //Sets animation speed based on receieved value
      FastLED.delay(1000 / (transitionTime / 10));
      //delay(10*transitionTime);
      //Serial.println(brightness);
    }

  } else if (FastLEDmode && !stateOn) {
    //Serial.println("FastLED off");
    DEBUG_MSG("FastLED off\n");
    FastLEDmode = false;
    help_index_1 = (NUM_LEDS - 1) / 2;
    help_index_2 = NUM_LEDS - 1 ;
    FastLED.clear();
    FastLED.show();
  }



}

void fastLedEffects() {
  if (stateOn) {


    //EFFECT SUNRISE
    if (effectString == "Sunrise") {
      FastLEDmode = true;
      //check if sunrise was already selected
      if (resetSunrise) {
        heatIndexled = 0;
        resetSunrise = false;
      }
      //call sunrise
      sunrise();
    } else {
      resetSunrise = true;
    }


    //EFFECT rainbow beatwave
    if (effectString == "rainbow beatwave") {
      FastLEDmode = true;
      transitionTime = 1000;
      //call fill midddle
      rainbow_beatwave();
      EVERY_N_MILLISECONDS(100) {
        uint8_t maxChanges = 24;
        nblendPaletteTowardPalette(currentPalette, targetPalette, maxChanges);   // AWESOME palette blending capability.
      }

      EVERY_N_SECONDS(5) {                                        // Change the target palette to a random one every 5 seconds.
        targetPalette = CRGBPalette16(CHSV(random8(), 255, random8(128, 255)), CHSV(random8(), 255, random8(128, 255)), CHSV(random8(), 192, random8(128, 255)), CHSV(random8(), 255, random8(128, 255)));
      }
      showleds();
    }

    //EFFECT mover
    if (effectString == "mover") {
      FastLEDmode = true;
      if (transitionTime <= 600) {
        transitionTime = 1000;
      }
      //call mover effect
      ChangeMeMover();
      mover();
    }


    //EFFECT Pallete change
    if (effectString == "Pallete change") {
      FastLEDmode = true;
      ChangePalettePeriodically();
      // nblendPaletteTowardPalette() will crossfade current palette slowly toward the target palette.
      //
      // Each time that nblendPaletteTowardPalette is called, small changes
      // are made to currentPalette to bring it closer to matching targetPalette.
      // You can control how many changes are made in each call:
      //   - the default of 24 is a good balance
      //   - meaningful values are 1-48.  1=veeeeeeeery slow, 48=quickest
      //   - "0" means do not change the currentPalette at all; freeze

      EVERY_N_MILLISECONDS(100) {
        uint8_t maxChanges = 24;
        nblendPaletteTowardPalette(currentPalette, targetPalette, maxChanges);
      }

      EVERY_N_MILLISECONDS(thisdelay) {
        static uint8_t startIndex = 0;
        startIndex += 1;                                 // motion speed
        FillLEDsFromPaletteColors(startIndex);
      }

      showleds();

    }

    //EFFECT rainbow beat
    if (effectString == "rainbow beat") {
      FastLEDmode = true;
      //call fill midddle
      rainbow_beat();
    }

    //EFFECT fill middle random single color
    if (effectString == "Fill middle random multi") {
      FastLEDmode = true;
      //call fill midddle
      fillmiddle(0);
    }

    //EFFECT fill middle random
    if (effectString == "Fill middle") {
      FastLEDmode = true;
      //call fill midddle
      fillmiddle(1);
    }

    //EFFECT fill middle
    if (effectString == "Fill middle random single") {
      FastLEDmode = true;
      //call fill midddle
      fillmiddle(2);
    }

    //EFFECT fill end random color
    if (effectString == "Fill End random") {
      FastLEDmode = true;
      //call fill end
      fillEnd(0);
    }

    //EFFECT fill end choosen color
    if (effectString == "Fill End single") {
      FastLEDmode = true;
      //call fill end
      fillEnd(1);
    }
    //EFFECT fill end different colors
    if (effectString == "Fill End different") {
      FastLEDmode = true;
      //call fill end
      fillEnd(2);
    }

    //EFFECT sound reactive
    if (effectString == "Sound reactive 1") {
      FastLEDmode = true;
      //set speed to 80 %
      //transitionTime = 800;
      //call soundeffect
      soundEffect(1);
    }

    if (effectString == "Sound reactive 2") {
      FastLEDmode = true;
      //set speed to 80 %
      //transitionTime = 800;
      //call soundeffect
      soundEffect(2);
    }


    //EFFECT BPM
    if (effectString == "bpm") {
      FastLEDmode = true;
      uint8_t BeatsPerMinute = 62;
      CRGBPalette16 palette = PartyColors_p;
      uint8_t beat = beatsin8( BeatsPerMinute, 64, 255);
      for ( int i = 0; i < NUM_LEDS; i++) { //9948
        leds[i] = ColorFromPalette(palette, gHue + (i * 2), beat - gHue + (i * 10));
      }
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 30;
      }
      showleds();
    }


    //EFFECT Candy Cane
    if (effectString == "CandyCane") {
      FastLEDmode = true;
      static uint8_t startIndex = 0;
      startIndex = startIndex + 1; /* higher = faster motion */
      fill_palette( leds, NUM_LEDS,
                    startIndex, 16, /* higher = narrower stripes */
                    currentPalettestriped, 255, LINEARBLEND);
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 1;
      }

      showleds();
    }


    //EFFECT CONFETTI
    if (effectString == "confetti" ) {
      FastLEDmode = true;
      fadeToBlackBy( leds, NUM_LEDS, 25);
      int pos = random16(NUM_LEDS);
      leds[pos] += CRGB(realRed + random8(64), realGreen, realBlue);
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 30;
      }

      showleds();
    }


    //EFFECT CYCLON RAINBOW
    if (effectString == "cyclon rainbow") {                    //Single Dot Down
      FastLEDmode = true;
      static uint8_t hue = 0;
      // First slide the led in one direction
      for (int i = 0; i < NUM_LEDS; i++) {
        // Set the i'th led to red
        leds[i] = CHSV(hue++, 255, 255);
        // Show the leds
        showleds();
        // now that we've shown the leds, reset the i'th led to black
        // leds[i] = CRGB::Black;
        fadeall();
        yield();
        // Wait a little bit before we loop around and do it again
        delay(10);
      }
      for (int i = (NUM_LEDS) - 1; i >= 0; i--) {
        // Set the i'th led to red
        leds[i] = CHSV(hue++, 255, 255);
        // Show the leds
        showleds();
        // now that we've shown the leds, reset the i'th led to black
        // leds[i] = CRGB::Black;
        fadeall();
        yield();
        // Wait a little bit before we loop around and do it again
        delay(10);
      }
    }


    //EFFECT DOTS
    if (effectString == "dots") {
      FastLEDmode = true;
      uint8_t inner = beatsin8(bpm, NUM_LEDS / 4, NUM_LEDS / 4 * 3);
      uint8_t outer = beatsin8(bpm, 0, NUM_LEDS - 1);
      uint8_t middle = beatsin8(bpm, NUM_LEDS / 3, NUM_LEDS / 3 * 2);
      leds[middle] = CRGB::Purple;
      leds[inner] = CRGB::Blue;
      leds[outer] = CRGB::Aqua;
      nscale8(leds, NUM_LEDS, fadeval);

      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 30;
      }
      showleds();
    }


    //EFFECT FIRE
    if (effectString == "fire FastLED") {
      FastLEDmode = true;
      Fire2012WithPalette();
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 150;
      }
      showleds();
    }

    random16_add_entropy( random8());


    //EFFECT Glitter
    if (effectString == "glitter") {
      FastLEDmode = true;
      fadeToBlackBy( leds, NUM_LEDS, 20);
      addGlitterColor(80, realRed, realGreen, realBlue);
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 30;
      }
      showleds();
    }


    //EFFECT JUGGLE
    if (effectString == "juggle" ) {                           // eight colored dots, weaving in and out of sync with each other
      FastLEDmode = true;
      fadeToBlackBy(leds, NUM_LEDS, 20);
      for (int i = 0; i < 8; i++) {
        leds[beatsin16(i + 7, 0, NUM_LEDS - 1  )] |= CRGB(realRed, realGreen, realBlue);
      }
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 130;
      }
      showleds();
    }


    //EFFECT LIGHTNING
    if (effectString == "lightning") {
      FastLEDmode = true;
      twinklecounter = twinklecounter + 1;                     //Resets strip if previous animation was running
      if (twinklecounter < 2) {
        FastLED.clear();
        FastLED.show();
      }
      ledstart = random8(NUM_LEDS);           // Determine starting location of flash
      ledlen = random8(NUM_LEDS - ledstart);  // Determine length of flash (not to go beyond NUM_LEDS-1)
      for (int flashCounter = 0; flashCounter < random8(3, flashes); flashCounter++) {
        if (flashCounter == 0) dimmer = 5;    // the brightness of the leader is scaled down by a factor of 5
        else dimmer = random8(1, 3);          // return strokes are brighter than the leader
        fill_solid(leds + ledstart, ledlen, CHSV(255, 0, 255 / dimmer));
        showleds();    // Show a section of LED's
        delay(random8(4, 10));                // each flash only lasts 4-10 milliseconds
        fill_solid(leds + ledstart, ledlen, CHSV(255, 0, 0)); // Clear the section of LED's
        showleds();
        if (flashCounter == 0) delay (130);   // longer delay until next flash after the leader
        delay(50 + random8(100));             // shorter delay between strokes
      }
      delay(random8(frequency) * 100);        // delay between strikes
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 0;
      }
      showleds();
    }


    //EFFECT POLICE ALL
    if (effectString == "police all") {                 //POLICE LIGHTS (TWO COLOR SOLID)
      FastLEDmode = true;
      idex++;
      if (idex >= NUM_LEDS) {
        idex = 0;
      }
      int idexR = idex;
      int idexB = antipodal_index(idexR);
      int thathue = (thishuepolice + 160) % 255;
      leds[idexR] = CHSV(thishuepolice, thissat, 255);
      leds[idexB] = CHSV(thathue, thissat, 255);
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 30;
      }
      showleds();
    }

    //EFFECT POLICE ONE
    if (effectString == "police one") {
      FastLEDmode = true;
      idex++;
      if (idex >= NUM_LEDS) {
        idex = 0;
      }
      int idexR = idex;
      int idexB = antipodal_index(idexR);
      int thathue = (thishuepolice + 160) % 255;
      for (int i = 0; i < NUM_LEDS; i++ ) {
        if (i == idexR) {
          leds[i] = CHSV(thishuepolice, thissat, 255);
        }
        else if (i == idexB) {
          leds[i] = CHSV(thathue, thissat, 255);
        }
        else {
          leds[i] = CHSV(0, 0, 0);
        }
      }
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 30;
      }
      showleds();
    }


    //EFFECT RAINBOW
    if (effectString == "rainbow") {
      FastLEDmode = true;
      // FastLED's built-in rainbow generator
      static uint8_t starthue = 0;    thishue++;
      fill_rainbow(leds, NUM_LEDS, thishue, deltahue);
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 130;
      }
      showleds();
    }


    //EFFECT RAINBOW WITH GLITTER
    if (effectString == "rainbow with glitter") {               // FastLED's built-in rainbow generator with Glitter
      FastLEDmode = true;
      static uint8_t starthue = 0;
      thishue++;
      fill_rainbow(leds, NUM_LEDS, thishue, deltahue);
      addGlitter(80);
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 130;
      }
      showleds();
    }


    //EFFECT SIENLON
    if (effectString == "sinelon") {
      FastLEDmode = true;
      fadeToBlackBy( leds, NUM_LEDS, 20);
      int pos = beatsin16(13, 0, NUM_LEDS - 1);
      leds[pos] += CRGB(realRed, realGreen, realBlue);
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 150;
      }
      showleds();
    }


    //EFFECT TWINKLE
    if (effectString == "twinkle") {
      FastLEDmode = true;
      twinklecounter = twinklecounter + 1;
      if (twinklecounter < 2) {                               //Resets strip if previous animation was running
        FastLED.clear();
        FastLED.show();
      }
      const CRGB lightcolor(8, 7, 1);
      for ( int i = 0; i < NUM_LEDS; i++) {
        if ( !leds[i]) continue; // skip black pixels
        if ( leds[i].r & 1) { // is red odd?
          leds[i] -= lightcolor; // darken if red is odd
        } else {
          leds[i] += lightcolor; // brighten if red is even
        }
      }
      if ( random8() < DENSITY) {
        int j = random16(NUM_LEDS);
        if ( !leds[j] ) leds[j] = lightcolor;
      }

      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 0;
      }
      showleds();
    }

    //EFFECT CHRISTMAS ALTERNATE
    if (effectString == "christmas alternate") {
      FastLEDmode = true;
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((toggle + i) % 2 == 0) {
          leds[i] = CRGB::Crimson;
        }
        else {
          leds[i] = CRGB::DarkGreen;
        }
      }
      if (toggle == 0) {
        toggle = 1;
      }
      else {
        toggle = 0;
      }
      if (transitionTime == 0 or transitionTime == NULL) {
        transitionTime = 130;
      }
      showleds();
      delay(30);
    }

    //EFFECT RANDOM STARS
    if (effectString == "random stars") {
      FastLEDmode = true;
      if (toggle == 0)
      {
        for (int i = 0; i < NUM_STARS; i++)
        {
          stars[i] = random(0, NUM_LEDS);
        }
        fill_solid (&(leds[0]), NUM_LEDS, CHSV(160, 255, brightness));
        toggle = 1;
      }
      else if (toggle == 1)
      {
        for (int j = 0; j < NUM_STARS; j++)
        {
          leds[stars[j]] ++;
        }
        if (leds[stars[0]].r == 255)
        {
          toggle = -1;
        }
      }
      else if (toggle == -1)
      {
        for (int j = 0; j < NUM_STARS; j++)
        {
          leds[stars[j]] --; //.fadeLightBy(i);
        }
        if (leds[stars[0]] <= CHSV(160, 255, brightness))
        {
          toggle = 0;
        }
      }
      showleds();
    }

    //EFFECT "Sine Hue"
    if (effectString == "sine hue") {
      FastLEDmode = true;
      static uint8_t hue_index = 0;
      static uint8_t led_index = 0;
      if (led_index >= NUM_LEDS) {  //Start off at 0 if the led_index was incremented past the segment size in some other effect
        led_index = 0;
      }
      for (int i = 0; i < NUM_LEDS; i = i + 1)
      {
        leds[i] = CHSV(hue_index, 255, 255 - int(abs(sin(float(i + led_index) / NUM_LEDS * 2 * 3.14159) * 255)));
      }

      led_index++, hue_index++;

      if (hue_index >= 255) {
        hue_index = 0;
      }
      showleds();
    }


    EVERY_N_MILLISECONDS(10) {

      nblendPaletteTowardPalette(currentPalette, targetPalette, maxChanges);  // FOR NOISE ANIMATIon
      {
        gHue++;
      }

    }

      //EFFECT NOISE
      if (effectString == "noise") {
        FastLEDmode = true;
        for (int i = 0; i < NUM_LEDS; i++) {                                     // Just onE loop to fill up the LED array as all of the pixels change.
          uint8_t index = inoise8(i * scale, dist + i * scale) % 255;            // Get a value from the noise function. I'm using both x and y axis.
          leds[i] = ColorFromPalette(currentPalette, index, 255, LINEARBLEND);   // With that value, look up the 8 bit colour palette value and assign it to the current LED.
        }
        dist += beatsin8(10, 1, 4);                                              // Moving along the distance (that random number we started out with). Vary it a bit with a sine wave.
        // In some sketches, I've used millis() instead of an incremented counter. Works a treat.
        if (transitionTime == 0 or transitionTime == NULL) {
          transitionTime = 0;
        }
        showleds();
      }

      //EFFECT RIPPLE
      if (effectString == "ripple") {
        FastLEDmode = true;
        for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV(bgcol++, 255, 15);  // Rotate background colour.
        switch (step) {
          case -1:                                                          // Initialize ripple variables.
            center = random(NUM_LEDS);
            colour = random8();
            step = 0;
            break;
          case 0:
            leds[center] = CHSV(colour, 255, 255);                          // Display the first pixel of the ripple.
            step ++;
            break;
          case maxsteps:                                                    // At the end of the ripples.
            step = -1;
            break;
          default:                                                             // Middle of the ripples.
            leds[(center + step + NUM_LEDS) % NUM_LEDS] += CHSV(colour, 255, myfade / step * 2);   // Simple wrap from Marc Miller
            leds[(center - step + NUM_LEDS) % NUM_LEDS] += CHSV(colour, 255, myfade / step * 2);
            step ++;                                                         // Next step.
            break;
        }
        if (transitionTime == 0 or transitionTime == NULL) {
          transitionTime = 30;
        }
        showleds();
      }

    
    EVERY_N_SECONDS(5) {
      targetPalette = CRGBPalette16(CHSV(random8(), 255, random8(128, 255)), CHSV(random8(), 255, random8(128, 255)), CHSV(random8(), 192, random8(128, 255)), CHSV(random8(), 255, random8(128, 255)));
    }


    if (startFade && effectString == "solid") {
      // If we don't want to fade, skip it.
      if (transitionTime == 0) {
        setColor(realRed, realGreen, realBlue);

        redVal = realRed;
        grnVal = realGreen;
        bluVal = realBlue;

        startFade = false;
      }
      else {
        loopCount = 0;
        stepR = calculateStep(redVal, realRed);
        stepG = calculateStep(grnVal, realGreen);
        stepB = calculateStep(bluVal, realBlue);

        inFade = true;
      }
    }

    if (inFade) {
      startFade = false;
      unsigned long now = millis();
      if (now - lastLoop > transitionTime) {
        if (loopCount <= 1020) {
          lastLoop = now;

          redVal = calculateVal(stepR, redVal, loopCount);
          grnVal = calculateVal(stepG, grnVal, loopCount);
          bluVal = calculateVal(stepB, bluVal, loopCount);

          if (effectString == "solidFAST") {
            setColor(redVal, grnVal, bluVal); // Write current values to LED pins
          }
          loopCount++;
        }
        else {
          inFade = false;
        }
      }
    }
  }
}

/**************************** START TRANSITION FADER *****************************************/
// From https://www.arduino.cc/en/Tutorial/ColorCrossfader
/* BELOW THIS LINE IS THE MATH -- YOU SHOULDN'T NEED TO CHANGE THIS FOR THE BASICS
  The program works like this:
  Imagine a crossfade that moves the red LED from 0-10,
    the green from 0-5, and the blue from 10 to 7, in
    ten steps.
    We'd want to count the 10 steps and increase or
    decrease color values in evenly stepped increments.
    Imagine a + indicates raising a value by 1, and a -
    equals lowering it. Our 10 step fade would look like:
    1 2 3 4 5 6 7 8 9 10
  R + + + + + + + + + +
  G   +   +   +   +   +
  B     -     -     -
  The red rises from 0 to 10 in ten steps, the green from
  0-5 in 5 steps, and the blue falls from 10 to 7 in three steps.
  In the real program, the color percentages are converted to
  0-255 values, and there are 1020 steps (255*4).
  To figure out how big a step there should be between one up- or
  down-tick of one of the LED values, we call calculateStep(),
  which calculates the absolute gap between the start and end values,
  and then divides that gap by 1020 to determine the size of the step
  between adjustments in the value.
*/
int calculateStep(int prevValue, int endValue) {
  int step = endValue - prevValue; // What's the overall gap?
  if (step) {                      // If its non-zero,
    step = 1020 / step;          //   divide by 1020
  }

  return step;
}
/* The next function is calculateVal. When the loop value, i,
   reaches the step size appropriate for one of the
   colors, it increases or decreases the value of that color by 1.
   (R, G, and B are each calculated separately.)
*/
int calculateVal(int step, int val, int i) {
  if ((step) && i % step == 0) { // If step is non-zero and its time to change a value,
    if (step > 0) {              //   increment the value if step is positive...
      val += 1;
    }
    else if (step < 0) {         //   ...or decrement it if step is negative
      val -= 1;
    }
  }

  // Defensive driving: make sure val stays in the range 0-255
  if (val > 255) {
    val = 255;
  }
  else if (val < 0) {
    val = 0;
  }

  return val;
}

/**************************** START STRIPLED PALETTE *****************************************/
void setupStripedPalette( CRGB A, CRGB AB, CRGB B, CRGB BA) {
  currentPalettestriped = CRGBPalette16(
                            A, A, A, A, A, A, A, A, B, B, B, B, B, B, B, B
                            //    A, A, A, A, A, A, A, A, B, B, B, B, B, B, B, B
                          );
}

/********************************** START FADE************************************************/
void fadeall() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].nscale8(250);  //for CYCLon
  }
}

/********************************** START FIRE **********************************************/
void Fire2012WithPalette(){
  // Array of temperature readings at each simulation cell
  static byte heat[NUM_LEDS];

  // Step 1.  Cool down every cell a little
  for ( int i = 0; i < NUM_LEDS; i++) {
    heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / NUM_LEDS) + 2));
  }

  // Step 2.  Heat from each cell drifts 'up' and diffuses a little
  for ( int k = NUM_LEDS - 1; k >= 2; k--) {
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
  }

  // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
  if ( random8() < SPARKING ) {
    int y = random8(7);
    heat[y] = qadd8( heat[y], random8(160, 255) );
  }

  // Step 4.  Map from heat cells to LED colors
  for ( int j = 0; j < NUM_LEDS; j++) {
    // Scale the heat value from 0-255 down to 0-240
    // for best results with color palettes.
    byte colorindex = scale8( heat[j], 240);
    CRGB color = ColorFromPalette( gPal, colorindex);
    int pixelnumber;
    if ( gReverseDirection ) {
      pixelnumber = (NUM_LEDS - 1) - j;
    } else {
      pixelnumber = j;
    }
    leds[pixelnumber] = color;
  }
}

/********************************** START ADD GLITTER *********************************************/
void addGlitter( fract8 chanceOfGlitter){
  if ( random8() < chanceOfGlitter) {
    leds[ random16(NUM_LEDS) ] += CRGB::White;
  }
}

/********************************** START ADD GLITTER COLOR ****************************************/
void addGlitterColor( fract8 chanceOfGlitter, int red, int green, int blue){
  if ( random8() < chanceOfGlitter) {
    leds[ random16(NUM_LEDS) ] += CRGB(red, green, blue);
  }
}

/********************************Custom Palettes********************************************************/
//sunrise palette
DEFINE_GRADIENT_PALETTE( sunrise_gp ) {
  //Syntay: palette's indicies (0-255), red, green, blue (rgb color code)
  0,     0,  0,  0,   //black
  25,   255,  0,  0,   //dark orange
  102,   255, 77,  0,  //deep orange
  153,   255, 103, 0,  //orange
  204,   255, 129,  0, //light orange
  255,   255, 167,  0, //near yellow
};

CRGBPalette16 sunrisePal = sunrise_gp;//set pallete for sunrise


/********************************** Sunrise effect********************************************************/
void sunrise() {

  // how often (in seconds) should the heat color increase?
  // for the default of 30 minutes, this should be about every 7 seconds
  // 7 seconds x 256 gradient steps = 1,792 seconds = ~30 minutes
  static const float interval = ((float)(sunriseLength * 60) / 256) * 1000;

  // HeatColors_p is a gradient palette built in to FastLED
  // that fades from black to red, orange, yellow, white
  // feel free to use another palette or define your own custom one
  CRGB colorled = ColorFromPalette(sunrisePal, heatIndexled);

  // fill the entire strip with the current color
  fill_solid(leds, NUM_LEDS, colorled);

  // slowly increase the heat
  EVERY_N_MILLISECONDS(interval) {
    // stop incrementing at 255, we don't want to overflow back to 0
    if (heatIndexled < 255) {
      heatIndexled++;
      showleds();
    } else {
      heatIndexled = 0; //reset heatIndexled -> restart sunrise
    }
  }

}

/********************************** mover effect********************************************************/

void mover() {
  yield();
  static uint8_t hue = 0;
  for (int i = 0; i < NUM_LEDS; i++) {
    yield();
    leds[i] += CHSV(hue, 255, 255);
    yield();
    leds[(i + 5) % NUM_LEDS] += CHSV(hue + 85, 255, 255);     // We use modulus so that the location is between 0 and NUM_LEDS
    yield();
    leds[(i + 10) % NUM_LEDS] += CHSV(hue + 170, 255, 255);   // Same here.
    //show_at_max_brightness_for_power();
    showleds();
    fadeToBlackBy(leds, NUM_LEDS, thisfademover);                  // Low values = slower fade.
    yield();
    delay(thisdelaymover);                                         // UGH!!!! A blocking delay. If you want to add controls, they may not work reliably.
  }
} 


void ChangeMeMover() {                                             // A time (rather than loop) based demo sequencer. This gives us full control over the length of each sequence.
  uint8_t secondHand = (millis() / 1000) % 15;                // IMPORTANT!!! Change '15' to a different value to change duration of the loop.
  static uint8_t lastSecond = 99;                             // Static variable, means it's only defined once. This is our 'debounce' variable.
  if (lastSecond != secondHand) {                             // Debounce to make sure we're not repeating an assignment.
    yield();
    lastSecond = secondHand;
    switch (secondHand) {
      case  0: thisdelaymover = 20; thisfademover = 240; break;         // You can change values here, one at a time , or altogether.
      case  5: thisdelaymover = 50; thisfademover = 128; break;
      case 10: thisdelaymover = 100; thisfademover = 64; break;         // Only gets called once, and not continuously for the next several seconds. Therefore, no rainbows.
      case 15: break;
    }
  }
} 

/********************************** fill middle effect********************************************************/
/*
   effect mode:
   0 = random color,
   1 = choosen color,
   2 = single color middle
*/
void fillmiddle(int effectMode) {

  //reset help index 1 & 2
  if (help_index_1 <= 1) {
    help_index_1 = (NUM_LEDS - 1) / 2;
    help_index_2 = NUM_LEDS - 1;
    Serial.println("Reset index");
  }
  if (help_index_2 != NUM_LEDS - 1) {
    help_index_2 = NUM_LEDS - 1;
  }

  //create 2  colors
  CRGB fillColor_1;
  CRGB fillColor_2;
  switch (effectMode) {
    case 0:
      //create 2 random colors
      fillColor_1 = CHSV(random8(), 255, 255);
      fillColor_2 = CHSV(random8(), 255, 255);
      break;

    case 1:
      //choosen color
      fillColor_1 = CRGB(realRed, realGreen, realBlue);
      fillColor_2 = CRGB(realRed, realGreen, realBlue);
      break;

    case 2:
      //create 2 random colors
      fillColor_1 = CHSV(random8(), 255, 255);
      fillColor_2 = CHSV(random8(), 255, 255);
      break;

  }


  //LED run to middle
  for (int ix = 0; ix < help_index_1; ix++) {
    leds[ix] = fillColor_1; // CRGB::Blue;
    leds[help_index_2 - ix] = fillColor_2;//CRGB::Orange;
    showleds();
    delay(50);
    //turn led off
    leds[ix] = CRGB::Black;
    leds[help_index_2 - ix] = CRGB::Black;
    yield();
  }

  //fill middle stripe
  if (effectMode != 2) { //two color middle
    fill_gradient_RGB(leds, help_index_1, fillColor_1, help_index_2 - help_index_1, fillColor_2);
  } else { //single color middle
    fill_gradient_RGB(leds, help_index_1, fillColor_1, help_index_2 - help_index_1, fillColor_1);
  }
  help_index_1--;
  showleds();
}

/********************************** fill end effect********************************************************/
/*
   effect mode:
   0 = random color,
   1 = choosen color,
   2 = diffrent colors run/fill
*/
void fillEnd(int effectMode) {
  //create color
  CRGB fillColor_1; //running LED
  CRGB fillColor_2; //LEDs at end

  switch (effectMode) {
    case 0:
      //create 1 random color
      fillColor_1 = CHSV(random8(), 255, 255);
      fillColor_2 = fillColor_1;
      break;

    case 1:
      //use choosen colors
      fillColor_1 = CRGB(realRed, realGreen, realBlue);
      fillColor_2 = CRGB(realRed, realGreen, realBlue);
      break;

    case 2:
      //create 2 random colors, random end
      fillColor_1 = CHSV(random8(), 255, 255);
      fillColor_2 = CHSV(random8(), 255, 255);

  }

  //reset index
  if (help_index_2 <= 1) {
    help_index_2 = NUM_LEDS - 1;
    Serial.println("Reset index");
  }

  //running LED
  for (int i = 0; i < help_index_2; i++) {
    leds[i] = fillColor_1;
    showleds();
    delay(20);
    yield();
    leds[i] = CRGB::Black;
  }
  //set LEDs at end
  //leds[help_index_2] = fillColor_2;
  showleds();
  Serial.println(help_index_2);
  delay(50);
  if (help_index_2 == 299) {
    leds[help_index_2] = fillColor_2;
  } else {
    fill_gradient_RGB(leds, help_index_2, fillColor_2, NUM_LEDS - 1, fillColor_2);
  }
  showleds();
  help_index_2--;
  //Serial.println(help_index_2);
}

/********************************** Sound reactive effect********************************************************/
void soundEffect(int soundMode) {
  /*/soundMode 1 = blink to sound, with black
     soundMode 2 = blink to sound, always on
  */
  if (digitalRead(MIC_PIN) == 1 && stateOn) {
    fill_solid(leds, NUM_LEDS, CHSV(random8(), 255, 255));
    FastLED.show();
    //showleds();

    while (digitalRead(MIC_PIN) == 1) {
      //wait...
      delay(2);
    }

    // delay(5);
    if (soundMode == 1) {
      FastLED.clear();
    }
  } else {
    if (soundMode == 1 || !stateOn) {
      FastLED.clear();

    }
  }
}

/********************************** rainbow effects********************************************************/
//rainbow beat
void rainbow_beat() {

  uint8_t beatA = beatsin8(17, 0, 255);                        // Starting hue
  uint8_t beatB = beatsin8(13, 0, 255);
  fill_rainbow(leds, NUM_LEDS, (beatA + beatB) / 2, 8);        // Use FastLED's fill_rainbow routine.

}

//rainbow beatwave
void rainbow_beatwave() {

  uint8_t wave1 = beatsin8(9, 0, 255);                        // That's the same as beatsin8(9);
  uint8_t wave2 = beatsin8(8, 0, 255);
  uint8_t wave3 = beatsin8(7, 0, 255);
  uint8_t wave4 = beatsin8(6, 0, 255);

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette( currentPalette, i + wave1 + wave2 + wave3 + wave4, 255, LINEARBLEND);
  }

}

/********************************** change Pallete effect********************************************************/
void FillLEDsFromPaletteColors(uint8_t colorIndex) {

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette(currentPalette, colorIndex + sin8(i * 16), 255);
    colorIndex += 3;
  }

} // FillLEDsFromPaletteColors()


void ChangePalettePeriodically() {

  uint8_t secondHand = (millis() / 1000) % 80;
  static uint8_t lastSecond = 99;

  if (lastSecond != secondHand) {
    lastSecond = secondHand;
    CRGB p = CHSV(HUE_PURPLE, 255, 255);
    CRGB g = CHSV(HUE_GREEN, 255, 255);
    CRGB b = CRGB::Black;
    CRGB w = CRGB::White;
    if (secondHand ==  0)  {
      targetPalette = RainbowColors_p;
      Serial.println("Rainbow");
    }
    if (secondHand == 10)  {
      targetPalette = CRGBPalette16(g, g, b, b, p, p, b, b, g, g, b, b, p, p, b, b);
      Serial.println("1");
    }
    if (secondHand == 20)  {
      targetPalette = CRGBPalette16(b, b, b, w, b, b, b, w, b, b, b, w, b, b, b, w);
      Serial.println("2");
    }
    if (secondHand == 30)  {
      targetPalette = LavaColors_p;
      Serial.println("Lava");
    }
    if (secondHand == 40)  {
      targetPalette = CloudColors_p;
      Serial.println("Cloud");
    }
    if (secondHand == 50)  {
      targetPalette = PartyColors_p;
      Serial.println("Party");
    }
    if (secondHand == 60)  {
      targetPalette = OceanColors_p;
      Serial.println("Ocean");
    }
    if (secondHand == 70)  {
      targetPalette = ForestColors_p;
      Serial.println("Forest");
    }
  }

}

/********************************** START DEMO mode (playAll) ***********************************************/
void demoMode() {

  unsigned long now;
  now = millis();
  //change mode
  if (now - last_change_auto > TIMER_MS_AUTO) {
    fxmode = ((ws2812fx.getMode() + 1) % ws2812fx.getModeCount());
    Serial.println(ws2812fx.getModeName((ws2812fx.getMode() + 1) % ws2812fx.getModeCount()));
    last_change_auto = now;
  }
}

/**********************************Button check ***********************************************/
void buttonCheck() {
  //Serial.println("ButtonCheck");
  DEBUG_MSG("ButtonCheck\n");
  unsigned long now;
  //check if button was pressed
  if (digitalRead(BUTTON_PIN) == LOW) {
    buttonPressed = true;
    //sendButtonState();
    sendToMqtt("button", buttonPressed, light_notify_topic);
  }

  //Wait and reset Button State when button was pressed
  if (buttonPressed && !motionOn) {                                     //muss geaendert werden, da untergeordnetes while sinnlos!
    now = millis();
    //turn on LED
    digitalWrite(BUTTON_LED_PIN, HIGH);
    Serial.println("STOP active");
    yield();
    if (now - last_change_button > TIMER_MS_BUTTON) {
      //wait until motionstate = 0, blink LED
      while (motionState == 1) {
        motionState = digitalRead(PIR_PIN);
        digitalWrite(BUTTON_LED_PIN, HIGH);
        delay(100);
        digitalWrite(BUTTON_LED_PIN, LOW);
        delay(100);
      }
      buttonPressed = false;
      //turn off LED
      digitalWrite(BUTTON_LED_PIN, LOW);
      Serial.println("STOP disabeld");

      //send buttonstate
      //sendButtonState();
      sendToMqtt("button", buttonPressed, light_notify_topic);
      last_change_button = now;
    }
  }
}

/*
void sendButtonState() {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  //button state
  Serial.print("buttonPressed:");
  Serial.println(buttonPressed);
  root["button"] = buttonPressed;

  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));
  yield();

  client.publish(light_notify_topic, buffer, true);
  Serial.println("Button state sent");
}*/

/**********************************Send to MQTT ***********************************************/
void sendToMqtt(String path, bool val, const char* destinationTopic){
   StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  //button state
  Serial.print("Sending Mesage: ");
  Serial.print(path);
  Serial.print(" -> ");
  Serial.println(val);
  root[path] = val;

  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));
  yield();

  client.publish(destinationTopic, buffer, true);
  Serial.println("Message sent");
}

/**********************************Temperature check ***********************************************/
void gettemperature() {
  //Serial.println("getTemperature");
  DEBUG_MSG("getTemperature\n");
  //yield();
  unsigned long now;
  now = millis();

  if (now - last_change_temp > TIMER_MS_TEMP) {
    //Serial.print("Read dht...");
    DEBUG_MSG("read DHT...");
    //Read Humidity
    humidity = dht.readHumidity();
    // Read temperature as Celsius (the default)
    temperature = dht.readTemperature();
    // Compute heat index in Celsius (isFahreheit = false)
    heatIndex = dht.computeHeatIndex(temperature, humidity, false);
    //Serial.println("Done");
    DEBUG_MSG("Done\n");

    //Serial.println("Send DHT Data...");
    DEBUG_MSG("Send DHT Data...");
    //send informations
    StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

    JsonObject& root = jsonBuffer.createObject();

    //temp & humidity
    root["temperature"] = temperature;
    root["humidity"] = humidity;
    root["heatIndex"] = heatIndex;

    char buffer[root.measureLength() + 1];
    root.printTo(buffer, sizeof(buffer));

    client.publish(temp_state_topic, buffer, true);
    //Serial.println("Temperatur gesendet");
    DEBUG_MSG("Done\n");
    last_change_temp = now;

  }
}

/********************************** PIR motion check ***********************************************/
void motionCheck() {
  //Serial.println("motionCheck");
  DEBUG_MSG("motionCheck\n");
  //check for motion
  motionState = digitalRead(PIR_PIN);
  //change motionOn if autoMode is on and it's dark or motionOn is true
  if (autoMode && (lightState == 1 ) || motionOn) {
    if (motionState == 1  && !buttonPressed) {
      if(!motionOn){
      motionOn = true;
      sendToMqtt("motion", motionOn, light_set_topic);
      }

    } else {
      if(motionOn){
      motionOn = false;
       sendToMqtt("motion", motionOn, light_set_topic);
       }
    }
  }
  yield();
}

/********************************** LDR check ***********************************************/
void ldrCheck() {
  //Serial.println("LDRcheck");
  DEBUG_MSG("LDR check\n");
  //check for light
  lightState = digitalRead(LDR_PIN);
  yield();

}

/********************************** load from Config ***********************************************/
bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    Serial.println("Failed to parse config file");
    return false;
  }
 
  Serial.println("SPIFFS: ");
  Serial.print("stateOn: ");
  stateOn = json["stateOn"];
  Serial.println(stateOn);
  Serial.print("motionOn: ");
  motionOn = json["motionOn"];
  Serial.println(motionOn);
  Serial.print("effectString: ");
  
  effectsave = json["effectString"];
  effectString = effectsave;
  Serial.println(effectString);
  Serial.print("automode: ");
  autoMode= json["autoMode"];
  Serial.println(autoMode);
  Serial.print("brightness: ");
  brightness= json["brightness"];
  Serial.println(brightness);

  Serial.print("CA0: ");
  colorArray[0]= json["color0"];
  Serial.println(colorArray[0]);
  Serial.print("CA1: ");
  colorArray[1]= json["color1"];
  Serial.println(colorArray[1]);
  Serial.print("CA2: ");
  colorArray[2]= json["color2"];
  Serial.println(colorArray[2]);
  
  return true;
}

/********************************** write to Config***********************************************/
bool saveConfig() {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["stateOn"] = stateOn;
  json["motionOn"] = motionOn;
  //effectsave = effectString
  if(effectString != ""){
    json["effectString"] = effectString.c_str();
  }else{
   json["effectString"] = oldeffectString.c_str();
  }
  json["autoMode"] = autoMode;
  json["brightness"] = brightness;

  json["color0"] = colorArray[0];
  json["color1"] = colorArray[1];
  json["color2"] = colorArray[2];
  Serial.print("CAR1: ");
  Serial.println(colorArray[1],HEX);
  

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  json.printTo(configFile);
  return true;
}