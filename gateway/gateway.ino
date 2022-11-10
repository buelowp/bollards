#include <MQTT.h>
#include <MQTTClient.h>
#include <Ethernet.h>
#include <painlessMesh.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>

#define   MESH_PREFIX     "bollards"
#define   MESH_PASSWORD   "bollards4thewin"
#define   MESH_PORT       5555
#define   APP_ID          1

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Scheduler userScheduler; // to control your personal task
painlessMesh  mesh;
EthernetClient ethClient;
MQTTClient mqttClient(1024);
IPAddress mqttServer(172, 24, 1, 12);
uint32_t g_heartbeat = 0;
uint32_t g_mqttMillis = 0;
char g_version[16];

// User stub
void sendHeartbeat();
void updateDisplay();

Task taskSendHeartbeat(TASK_SECOND * 5 , TASK_FOREVER, &sendHeartbeat);
Task taskUpdateDisplay(TASK_SECOND, TASK_FOREVER, &updateDisplay);

void updateDisplay() 
{
    auto nodes = mesh.getNodeList();
    tft.setCursor(0, 0);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.printf("Version: %s\n", g_version);
    tft.printf("Active nodes: %d\n", nodes.size());
    tft.printf("IP:   %s\n", Ethernet.localIP().toString().c_str());
    if (mqttClient.connected())
        tft.printf("MQTT: %s\n", mqttServer.toString().c_str());
    else
        tft.printf("MQTT disconnected\n");
}

/**
 * Just a message relay. Send an MQTT back if the payload is bogus
 */
void messageReceived(String &topic, String &payload) 
{
    String t("pathlights/error/");
    topic += mesh.getNodeId();
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        char buffer[256];
        doc.clear();
        doc["response"] = "error";
        doc["reason"] = "json";
        doc["string"] = error.f_str();
        doc["code"] = error.code();
        int n = serializeJson(doc, buffer);
        mqttClient.publish(t.c_str(), buffer, n);
        return;
    }

    sendMessage(payload);
}

void sendMessage(String &payload)
{
    mesh.sendBroadcast(payload);
}

void sendHeartbeat() 
{
    String topic("pathlights/heartbeat/");
    topic += mesh.getNodeId();
    StaticJsonDocument<256> doc;
    char buffer[256];
    doc["role"] = "gateway";
    doc["heartbeat"] = g_heartbeat++;
    doc["memory"] = ESP.getFreeHeap();
    doc["node"] = mesh.getNodeId();
    int n = serializeJson(doc, buffer);
    mqttClient.publish(topic.c_str(), buffer, n);
}

// Needed for painless library
void receivedCallback(uint32_t from, String &msg) 
{
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, msg);
    if (error) {
        String payload;
        String topic("pathlights/error/");
        topic += mesh.getNodeId();
        doc.clear();
        JsonObject obj;
        obj["response"] = "error";
        obj["reason"] = "json";
        obj["string"] = error.f_str();
        obj["code"] = error.code();
        obj["node"] = from;
        doc["error"] = obj;
        serializeJson(doc, payload);
        mqttClient.publish(topic, payload);
        return;
    }

    if (!doc.isNull()) {
        if (doc.is<JsonObject>()) {
            if (doc.containsKey("heartbeat")) {
                String topic("pathlights/heartbeat/");
                topic += from;
                mqttClient.publish(topic.c_str(), msg);
            }
        }
    }
}

void newConnectionCallback(uint32_t nodeId) 
{
    // Send an MQTT note that a new node has joined
    StaticJsonDocument<256> doc;
    String topic("pathlights/new/");
    char buffer[256];
    topic += nodeId;
    JsonObject obj;
    obj["node"] = nodeId;
    doc["device"] = obj;
    int n = serializeJson(doc, buffer);
    mqttClient.publish(topic.c_str(), buffer, n);

    // Tell the new node who the MQTT gateway is
    String payload("{ \"gateway\" : ");
    payload += mesh.getNodeId();
    payload += "}";
    mesh.sendSingle(nodeId, payload);
}

/**
 * Not sure what to use this for now, but will keep it for the future
 */
void changedConnectionCallback() 
{
    String topic("pathlights/topology");
    String payload = mesh.subConnectionJson();
    mqttClient.publish(topic, payload);
}

/**
 * Need to figure out what Node time is and see if I can use it in animations
 */
void nodeTimeAdjustedCallback(int32_t offset) 
{
}

void connect() 
{
    updateDisplay();
    while (!mqttClient.connect("mqtt-gw")) {
        Serial.print("Last Error: ");
        Serial.println(mqttClient.lastError());
        delay(500);
    }
    mqttClient.subscribe("pathlights/actions/#");
    updateDisplay();
}

void setup() 
{
    sprintf(g_version, "%d.%d.%d", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, APP_ID);
    Serial.begin(115200);
    // turn on backlite
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, HIGH);

    // turn on the TFT / I2C power supply
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    delay(10);

    Ethernet.init(10);
    
    if (Ethernet.begin(mac) == 0) {
        Serial.println("Failed to start ethernet wing");
        if (Ethernet.hardwareStatus() == EthernetNoHardware) {
            Serial.println("Shield not found");
            while (1)
                sleep(1);
        }
    }
    Serial.print("Ethernet started: HW = ");
    Serial.println(Ethernet.hardwareStatus());
    
    // initialize TFT
    tft.init(135, 240); // Init ST7789 240x135
    tft.setRotation(3);
    tft.fillScreen(ST77XX_BLACK);

    mqttClient.begin(mqttServer, 1883, ethClient);
    mqttClient.onMessage(messageReceived);   //mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE ); // all types on
    
    Serial.println("mqtt started");
    mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before init() so that you can see startup messages

    mesh.setRoot(true);
    mesh.setHostname("gateway");   
    mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

    Serial.println("Mesh started");
    userScheduler.addTask(taskSendHeartbeat);
    taskSendHeartbeat.enable();
}

void loop() 
{
    mesh.update();
    Ethernet.maintain();

    if (millis() - g_mqttMillis > 1000) {
        if (!mqttClient.connected()) {
            connect();
        }
        else
            mqttClient.loop();
    
        g_mqttMillis = millis();
    }
}

