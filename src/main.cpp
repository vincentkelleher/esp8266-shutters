#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DNSServer.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "memory.h"

Memory* memory;

const int FLASH_BUTTON = 0;
const int UP_BUTTON = D8;
const int MIDDLE_BUTTON = D7;
const int DOWN_BUTTON = D6;

const char* setupSsid = "Shutter Controller";
const char* setupPassword = "shutter123!";
const IPAddress setupIp(192, 168, 1, 1);
const IPAddress setupNetMask(255, 255, 255, 0);

void flash(int ms);
void activate(int button);
void wifiSetup();
void mqttSetup();
void connectMqtt();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void listenForSetupMode();
void enterSetupMode();

WiFiClient espClient;
PubSubClient mqttClient(espClient);

DNSServer dnsServer;
AsyncWebServer server(80);

char discoveryTopic[200];
char discoveryPayload[1500];
char commandTopic[200];
char stateTopic[200];
char availabilityTopic[200];
char haAvailabilityTopic[200];

bool restart = false;
int flashButtonState = HIGH;
unsigned long flashButtonPressStart = -1;

char* hostname;
char* wifiName;
char* wifiPassword;
char* mqttClientId;
char* mqttHost;
char* mqttUsername;
char* mqttPassword;
char* haTopicPrefix;
char* commandTopicPrefix;

void setup() {
  Serial.begin(115200);
  Serial.println(F("Setting up..."));

  memory = new Memory();

  if (memory->ready()) {
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(UP_BUTTON, OUTPUT);
    pinMode(MIDDLE_BUTTON, OUTPUT);
    pinMode(DOWN_BUTTON, OUTPUT);
    pinMode(FLASH_BUTTON, INPUT);

    Serial.println(F("Reading values from EEPROM..."));
    hostname = memory->readNext();
    wifiName = memory->readNext();
    wifiPassword = memory->readNext();
    mqttClientId = memory->readNext();
    mqttHost = memory->readNext();
    mqttUsername = memory->readNext();
    mqttPassword = memory->readNext();
    haTopicPrefix = memory->readNext();
    commandTopicPrefix = memory->readNext();

    sprintf(discoveryTopic, "%s/cover/%s/config", haTopicPrefix, mqttClientId);
    sprintf(commandTopic, "%s/cover/%s/set", commandTopicPrefix, mqttClientId);
    sprintf(stateTopic, "%s/cover/%s/state", commandTopicPrefix, mqttClientId);
    sprintf(availabilityTopic, "%s/cover/%s/availability", commandTopicPrefix, mqttClientId);
    sprintf(haAvailabilityTopic, "%s/status", haTopicPrefix);
    sprintf(discoveryPayload, "{\"name\":null,\"unique_id\":\"%s\",\"device_class\":\"blind\",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"model\":\"Somfy Shutter Controller\",\"manufacturer\":\"Oreo Technology\"},\"command_topic\":\"%s\",\"state_topic\":\"%s\",\"availability\":{\"topic\":\"%s\"},\"qos\":0,\"retain\":false,\"payload_open\":\"UP\",\"payload_close\":\"DOWN\",\"payload_stop\":\"MIDDLE\",\"state_open\":\"open\",\"state_opening\":\"opening\",\"state_closed\":\"closed\",\"state_closing\":\"closing\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\",\"optimistic\":true}", 
      mqttClientId, mqttClientId, mqttClientId, commandTopic, stateTopic, availabilityTopic);

    wifiSetup();
    mqttSetup();
  } else {
    enterSetupMode();
  }
}

void loop() {
  if (restart) {
    Serial.println("Restarting...");
    restart = false;
    delay(3000);
    ESP.restart();
  }

  if (memory->ready()) {
    listenForSetupMode();

    if (!mqttClient.connected()) {
      connectMqtt();
    }
    mqttClient.loop();
  }
}


void wifiSetup() {
  WiFi.hostname(hostname);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiName, wifiPassword);

  Serial.printf("Connecting to %s", wifiName);
  while (WiFi.status() != WL_CONNECTED && !restart)
  {
    flash(250);
    Serial.print(".");
    listenForSetupMode();
  }
  Serial.println();

  Serial.printf("Connected ! IP address: %s\n", WiFi.localIP().toString().c_str());
} 

void mqttSetup() {
  mqttClient.setServer(mqttHost, 1883);
  mqttClient.setBufferSize(1024);
  mqttClient.setCallback(mqttCallback);
}

void connectMqtt() {
  while (!mqttClient.connected()) {
    Serial.printf("Connecting to %s MQTT host...\n", mqttHost);

    String clientId = mqttClientId;
    clientId += String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str(), mqttUsername, mqttPassword)) {
      Serial.println(F("MQTT connected !"));

      Serial.printf("Subscribing to topic %s...\n", haAvailabilityTopic);
      mqttClient.subscribe(haAvailabilityTopic);

      Serial.printf("Subscribing to topic %s...\n", commandTopic);
      mqttClient.subscribe(commandTopic);
    } else {
      Serial.printf("MQTT connection failed: state code %d\n", mqttClient.state());
      flash(2500);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char* message = (char*) payload;
  Serial.printf("Message arrived [%s][%d]: %s", topic, length, message);
  Serial.println();

  if (strcmp(topic, haAvailabilityTopic) == 0 && strncmp(message, "online", length) == 0) {
    Serial.printf("Sending discovery payload to %s\n", discoveryTopic);
    mqttClient.publish(discoveryTopic, discoveryPayload, true);

    Serial.printf("Sending availability payload to %s\n", availabilityTopic);
    mqttClient.publish(availabilityTopic, "online", true);
  }

  if (strcmp(topic, commandTopic) == 0) {
    if (strncmp(message, "UP", length) == 0) {
      Serial.println("Opening...");
      activate(UP_BUTTON);
    } else if (strncmp(message, "MIDDLE", length) == 0) {
      Serial.println("Middle button...");
      activate(MIDDLE_BUTTON);
    } else if (strncmp(message, "DOWN", length) == 0) {
      Serial.println("Closing...");
      activate(DOWN_BUTTON);
    }
  }
}

void listenForSetupMode() {
  int newFlashButtonState = digitalRead(FLASH_BUTTON);

  if (flashButtonPressStart != (unsigned long) -1 && millis() - flashButtonPressStart >= 5000) {
    flashButtonPressStart = -1;
    flashButtonState = HIGH;

    Serial.println("Entering setup mode...");
    flash(500);
    flash(500);
    flash(500);
    
    memory->clear();
    memory->ready(false);
    memory->commit();

    restart = true;
  }

  if (newFlashButtonState != flashButtonState) {
    if (newFlashButtonState == LOW) {
      flashButtonState = newFlashButtonState;
      flashButtonPressStart = millis();
    }

    digitalWrite(LED_BUILTIN, flashButtonState);
  }
}

void enterSetupMode() {
  Serial.println("Setting up Wifi access point...");
  WiFi.softAP(setupSsid, setupPassword);
  WiFi.softAPConfig(setupIp, setupIp, setupNetMask);

  IPAddress ip = WiFi.softAPIP();
  Serial.print("Access point IP address: ");
  Serial.println(ip);

  Serial.print("Setup server address: ");
  Serial.println(WiFi.localIP());

  dnsServer.start(53, "*", WiFi.softAPIP());
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<!DOCTYPE html><html><head>");
    response->print("<title>Shutter Controller</title>");
    response->print("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    response->print("<style>");
    response->print("html { display: flex; justify-content: center; margin: 0px auto; }");
    response->print("form#setup-form { display: flex; flex-direction: column; }");
    response->print("form#setup-form > div { display: flex; justify-content: space-between; margin-top: 12px; }");
    response->print("form#setup-form > div > label { margin-right: 10px; }");
    response->print("form#setup-form > div > input { margin-top: -5px; padding: 5px; }");
    response->print("form#setup-form > div:last-child { justify-content: center; margin-top: 30px;}");
    response->print("</style>");
    response->print("</head><body>");
    response->print("<h1>Shutter Controller</h1>");
    response->print("<form id=\"setup-form\" action=\"\" method=\"post\">");
    response->print("<h2>Network</h2>");
    response->print("<div><label for=\"hostname\">Hostname: </label><input type=\"text\" name=\"hostname\" id=\"hostname\" required /></div>");
    response->print("<div><label for=\"wifiName\">Wifi name: </label><input type=\"text\" name=\"wifiName\" id=\"wifiName\" required /></div>");
    response->print("<div><label for=\"wifiPassword\">Wifi password: </label><input type=\"password\" name=\"wifiPassword\" id=\"wifiPassword\" required /></div>");
    response->print("<h2>MQTT</h2>");
    response->print("<div><label for=\"mqttClientId\">Client ID: </label><input type=\"text\" name=\"mqttClientId\" id=\"mqttClientId\" required /></div>");
    response->print("<div><label for=\"mqttHost\">MQTT host: </label><input type=\"text\" name=\"mqttHost\" id=\"mqttHost\" required /></div>");
    response->print("<div><label for=\"mqttUser\">MQTT username: </label><input type=\"text\" name=\"mqttUser\" id=\"mqttUser\" required /></div>");
    response->print("<div><label for=\"mqttPassword\">MQTT password: </label><input type=\"password\" name=\"mqttPassword\" id=\"mqttPassword\" required /></div>");
    response->print("<div><label for=\"haTopicPrefix\">Home Assistant topic prefix: </label><input type=\"text\" name=\"haTopicPrefix\" id=\"haTopicPrefix\" required /></div>");
    response->print("<div><label for=\"commandTopicPrefix\">Command topic prefix: </label><input type=\"text\" name=\"commandTopicPrefix\" id=\"commandTopicPrefix\" required /></div>");
    response->print("<div><input type=\"submit\" value=\"Save\" /></div>");
    response->print("</form>");
    response->print("</body></html>");
    request->send(response);
  });

  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request){
    int params = request->params();
    for(int i = 0; i < params; i++){
      AsyncWebParameter* p = request->getParam(i);
      const char* value = p->value().c_str();
      
      Serial.printf("Setting EEPROM[%s] to %s", p->name().c_str(), value);
      Serial.println();

      memory->write(value);
    }

    memory->ready(true);
    memory->commit();

    request->send(200, "text/plain", "Settings were saved ! Restarting in 3 seconds...");

    restart = true;
  });

  server.begin();
}

void flash(int ms) {
  digitalWrite(LED_BUILTIN, LOW);
  delay(ms);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(ms);
}

void activate(int button) {
  digitalWrite(button, HIGH);
  delay(100);
  digitalWrite(button, LOW);
}