#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#define GET_CHIPID()  (ESP.getChipId())
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#define GET_CHIPID()  ((uint16_t)(ESP.getEfuseMac()>>32))
#endif
#include <FS.h>
#include <PubSubClient.h>
#include <AutoConnect.h>
#include <EasyButton.h>

#define PARAM_FILE      "/param.json"
#define AUX_SETTING_URI "/mqtt_setting"
#define AUX_SAVE_URI    "/mqtt_save"
//#define AUX_CLEAR_URI   "/mqtt_clear"

// JSON definition of AutoConnectAux.
// Multiple AutoConnectAux can be defined in the JSON array.
// In this example, JSON is hard-coded to make it easier to understand
// the AutoConnectAux API. In practice, it will be an external content
// which separated from the sketch, as the mqtt_RSSI_FS example shows.
static const char AUX_mqtt_setting[] PROGMEM = R"raw(
[
  {
    "title": "MQTT Setting",
    "uri": "/mqtt_setting",
    "menu": true,
    "element": [
      {
        "name": "header",
        "type": "ACText",
        "value": "<h2>MQTT broker settings</h2>",
        "style": "text-align:center;color:#2f4f4f;padding:10px;"
      },
      {
        "name": "caption",
        "type": "ACText",
        "value": "Publishing the WiFi signal strength to MQTT channel. RSSI value of ESP8266 to the channel created on ThingSpeak",
        "style": "font-family:serif;color:#4682b4;"
      },
      {
        "name": "mqttserver",
        "type": "ACInput",
        "value": "",
        "label": "Server",
        "pattern": "^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]*[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\\-]*[A-Za-z0-9])$",
        "placeholder": "MQTT broker server"
      },
      {
        "name": "newline",
        "type": "ACElement",
        "value": "<hr>"
      },
      {
        "name": "newline",
        "type": "ACElement",
        "value": "<hr>"
      },
      {
        "name": "hostname",
        "type": "ACInput",
        "value": "",
        "label": "ESP host name",
        "pattern": "^([a-zA-Z0-9]([a-zA-Z0-9-])*[a-zA-Z0-9]){1,24}$"
      },
      {
        "name": "save",
        "type": "ACSubmit",
        "value": "Save&amp;Start",
        "uri": "/mqtt_save"
      },
      {
        "name": "discard",
        "type": "ACSubmit",
        "value": "Discard",
        "uri": "/_ac"
      }
    ]
  },
  {
    "title": "MQTT Setting",
    "uri": "/mqtt_save",
    "menu": false,
    "element": [
      {
        "name": "caption",
        "type": "ACText",
        "value": "<h4>Parameters saved as:</h4>",
        "style": "text-align:center;color:#2f4f4f;padding:10px;"
      },
      {
        "name": "parameters",
        "type": "ACText"
      },
      {
        "name": "newline",
        "type": "ACElement",
        "value": "<hr>"
      },
      {
        "name": "OK",
        "type": "ACSubmit",
        "value": "OK",
        "uri": "/_ac"
      },
      {
        "name": "newline",
        "type": "ACElement",
        "value": "<hr>"
      },
    ]
  }
]
)raw";

// Adjusting WebServer class with between ESP8266 and ESP32.
#if defined(ARDUINO_ARCH_ESP8266)
typedef ESP8266WebServer  WiFiWebServer;
#elif defined(ARDUINO_ARCH_ESP32)
typedef WebServer WiFiWebServer;
#endif

AutoConnect  portal;
AutoConnectConfig config;
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);
String  serverName;
String  hostName;
unsigned int  updateInterval = 10;
unsigned long lastPub = 0;

#define TIMEZONE    (3600 * 0)    // Dublin
#define NTPServer1  "ntp.nict.jp" // NICT japan.
#define NTPServer2  "time1.google.com"

// Arduino pin where the button is connected to.

#if defined(ARDUINO_ARCH_ESP32)
#define BUTTON_PIN_RED 14
#define BUTTON_PIN_YELLOW 15
#define BUTTON_PIN_GREEN 25
//#define BUTTON_PIN_BLUE 27

#elif defined(ARDUINO_ARCH_ESP8266)
#define BUTTON_PIN_YELLOW 0
#define BUTTON_PIN_RED 2
//#define BUTTON_PIN_BLUE 4
#define BUTTON_PIN_GREEN 5
#endif

// Instance of the button.
EasyButton button_yellow(BUTTON_PIN_YELLOW);
EasyButton button_red(BUTTON_PIN_RED);
//EasyButton button_blue(BUTTON_PIN_BLUE);
EasyButton button_green(BUTTON_PIN_GREEN);

// Callback function to be called when the button is pressed.
void onPressed_red() {
  Serial.println("Red (Radio) Button has been pressed!");

  mqttClient.publish("/magicchair/music", "off");
  mqttClient.publish("/magicchair/books", "off");
  mqttClient.publish("/magicchair/radio", "on");
}

void onPressed_yellow() {
  Serial.println("Yellow (Music) Button has been pressed!");

  mqttClient.publish("/magicchair/radio", "off");
  mqttClient.publish("/magicchair/books", "off");
  mqttClient.publish("/magicchair/music", "on");
}

void onPressed_blue() {
  Serial.println("Blue (Books) Button has been pressed!");

  mqttClient.publish("/magicchair/radio", "off");
  mqttClient.publish("/magicchair/music", "off");
  mqttClient.publish("/magicchair/books", "on");
}

void onPressed_green() {
  Serial.println("Green (Stop) Button has been pressed!");

  mqttClient.publish("/magicchair/radio", "off");
  mqttClient.publish("/magicchair/music", "off");
  mqttClient.publish("/magicchair/books", "off");
}


// Callback function to be called when the button is pressed.
void onPressedForDuration() {
  Serial.println("on duration:Button has been pressed!");
  mqttClient.publish("/magicchair/radio", "off");
  mqttClient.publish("/magicchair/music", "off");
  mqttClient.publish("/magicchair/books", "off");
  mqttClient.publish("/magicchair/goodbye", "on");
}

bool mqttConnect() {
  static const char alphanum[] = "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";  // For random generation of client ID.
  char    clientId[9];

  uint8_t retry = 3;
  while (!mqttClient.connected()) {
    if (serverName.length() <= 0)
      break;

    mqttClient.setServer(serverName.c_str(), 1883);
    Serial.println(String("Attempting MQTT broker:") + serverName);

    for (uint8_t i = 0; i < 8; i++) {
      clientId[i] = alphanum[random(62)];
    }
    clientId[8] = '\0';

    if (mqttClient.connect(clientId)) {
      Serial.println("Established:" + String(clientId));
      return true;
    }
    else {
      Serial.println("Connection failed:" + String(mqttClient.state()));
      if (!--retry)
        break;
      delay(3000);
    }
  }
  return false;
}

// Load parameters saved with  saveParams from SPIFFS into the
// elements defined in /mqtt_setting JSON.
String loadParams(AutoConnectAux& aux, PageArgument& args) {
  (void)(args);
  File param = SPIFFS.open(PARAM_FILE, "r");
  if (param) {
    if (aux.loadElement(param))
      Serial.println(PARAM_FILE " loaded");
    else
      Serial.println(PARAM_FILE " failed to load");
    param.close();
  }
  else {
    Serial.println(PARAM_FILE " open failed");
#ifdef ARDUINO_ARCH_ESP32
    Serial.println("If you get error as 'SPIFFS: mount failed, -10025', Please modify with 'SPIFFS.begin(true)'.");
#endif
  }
  return String("");
}

// Save the value of each element entered by '/mqtt_setting' to the
// parameter file. The saveParams as below is a callback function of
// /mqtt_save. When invoking this handler, the input value of each
// element is already stored in '/mqtt_setting'.
// In Sketch, you can output to stream its elements specified by name.
String saveParams(AutoConnectAux& aux, PageArgument& args) {
  // The 'where()' function returns the AutoConnectAux that caused
  // the transition to this page.
  AutoConnectAux*   mqtt_setting = portal.where();

  AutoConnectInput& mqttserver = mqtt_setting->getElement<AutoConnectInput>("mqttserver");
  serverName = mqttserver.value;
  serverName.trim();

  AutoConnectInput& hostname = mqtt_setting->getElement<AutoConnectInput>("hostname");
  hostName = hostname.value;
  hostName.trim();

  // The entered value is owned by AutoConnectAux of /mqtt_setting.
  // To retrieve the elements of /mqtt_setting, it is necessary to get
  // the AutoConnectAux object of /mqtt_setting.
  File param = SPIFFS.open(PARAM_FILE, "w");
  mqtt_setting->saveElement(param, { "mqttserver", "hostname" });
  param.close();

  // Echo back saved parameters to AutoConnectAux page.
  AutoConnectText&  echo = aux.getElement<AutoConnectText>("parameters");
  echo.value = "Server: " + serverName;
  echo.value += mqttserver.isValid() ? String(" (OK)") : String(" (ERR)");
  echo.value += "<br>ESP host name: " + hostName + "<br>";

  return String("");
}

void handleRoot() {

	String  content =
	    "<html>"
	    "<head>"
	    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
	    "</head>"
	    "<body>"
	    "<h2 align=\"center\" style=\"color:blue;margin:20px;\">Magic Chair</h2>"
	    "<h3 align=\"center\" style=\"color:gray;margin:10px;\">{{DateTime}}</h3>"
	    "<p style=\"padding-top:10px;text-align:center\">" AUTOCONNECT_LINK(COG_32) "</p>"
	    "</body>"
	    "</html>";
	  static const char *wd[7] = { "Sun", "Mon", "Tue", "Wed", "Thr", "Fri", "Sat" };
	  struct tm *tm;
	  time_t  t;
	  char    dateTime[26];

	  t = time(NULL);
	  tm = localtime(&t);
	  sprintf(dateTime, "%04d/%02d/%02d(%s) %02d:%02d:%02d.",
		  tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		  wd[tm->tm_wday],
		  tm->tm_hour, tm->tm_min, tm->tm_sec);
	  content.replace("{{DateTime}}", String(dateTime));
	  WiFiWebServer&  webServer = portal.host();
	  webServer.send(200, "text/html", content);
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println();
  SPIFFS.begin();

  AutoConnectAux configParams;

  File cfgParamsFile = SPIFFS.open(PARAM_FILE, "r");
  if (cfgParamsFile) {
    if (configParams.loadElement(cfgParamsFile))
      Serial.println(PARAM_FILE " loaded");
    else
      Serial.println(PARAM_FILE " failed to load");
    cfgParamsFile.close();
  }
  else {
    Serial.println(PARAM_FILE " open failed");
#ifdef ARDUINO_ARCH_ESP32
    Serial.println("If you get error as 'SPIFFS: mount failed, -10025', Please modify with 'SPIFFS.begin(true)'.");
#endif
  }

  AutoConnectInput&  mqttserver = configParams.getElement<AutoConnectInput>("mqttserver");

  if (mqttserver.value.length()) {
     Serial.println("retrieved mqttserver " + mqttserver.value);
     serverName = mqttserver.value;
  }

  button_red.begin();
  button_yellow.begin();
//  button_blue.begin();
  button_green.begin();

  // Add the callback function to be called when the button is pressed.
  button_red.onPressed(onPressed_red);
  button_yellow.onPressed(onPressed_yellow);
//  button_blue.onPressed(onPressed_blue);
  button_green.onPressed(onPressed_green);

  // Add the callback function to be called when the button is pressed for at least the given time.
  button_red.onPressedFor(2000, onPressedForDuration);
  button_yellow.onPressedFor(2000, onPressedForDuration);
//  button_blue.onPressedFor(2000, onPressedForDuration);
  button_green.onPressedFor(2000, onPressedForDuration);


  if (portal.load(FPSTR(AUX_mqtt_setting))) {
    AutoConnectAux* mqtt_setting = portal.aux(AUX_SETTING_URI);
    AutoConnectInput&     hostnameElm = mqtt_setting->getElement<AutoConnectInput>("hostname");
    if (hostnameElm.value.length()) {
      config.hostName = hostnameElm.value;
      Serial.println("hostname set to " + config.hostName);
    }

    config.title = "MagicChair Config";
    config.apid = "MagicChair";
    config.psk = "password";
    config.bootUri = AC_ONBOOTURI_HOME;
    config.homeUri = "/";
    portal.config(config);

    portal.on(AUX_SETTING_URI, loadParams);
    portal.on(AUX_SAVE_URI, saveParams);
  }
  else
    Serial.println("load error");

  Serial.print("WiFi ");
  if (portal.begin()) {
    Serial.println("connected:" + WiFi.SSID());
    Serial.println("IP:" + WiFi.localIP().toString());
    configTime(TIMEZONE, 0, NTPServer1, NTPServer2);
  }
  else {
    Serial.println("connection failed:" + String(WiFi.status()));
    while (1) {
      delay(100);
      yield();
    }
  }

  WiFiWebServer&  webServer = portal.host();
  webServer.on("/", handleRoot);
  //webServer.on(AUX_CLEAR_URI, handleClearChannel);
}

void loop() {
  portal.handleClient();
  if (updateInterval > 0) {
    if (millis() - lastPub > updateInterval) {
      if (!mqttClient.connected()) {
        mqttConnect();
      }
      mqttClient.loop();
      lastPub = millis();
    }
  button_red.read();
  button_yellow.read();
//  button_blue.read();
  button_green.read();

  }
}
