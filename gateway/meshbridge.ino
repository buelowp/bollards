#include <sunset.h>
#include <NTPClient.h>
#include <ESP32Time.h>
#include <MQTT.h>
#include <MQTTClient.h>
#include <Ethernet.h>
#include <painlessMesh.h>
#include <SPI.h>
#include "esp_sntp.h"
#include "esp_timer.h"
#include "time.h"

#define   MESH_PREFIX     "bollards"
#define   MESH_PASSWORD   "bollards4thewin"
#define   MESH_PORT       5555
#define   APP_ID          24

#define ONE_MS            1000
#define ONE_SECOND        (1000 * ONE_MS)
#define ONE_MINUTE        (ONE_SECOND * 60)
#define ONE_HOUR          (ONE_MINUTE * 60)

#define LATITUDE        42.0039
#define LONGITUDE       -87.9703
#define TZ_OFFSET      -6

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

Scheduler userScheduler; // to control your personal task
painlessMesh  mesh;
EthernetClient ethClient;
EthernetUDP ntpClient;
MQTTClient mqttClient(1024);
IPAddress mqttServer(172, 24, 1, 12);
uint32_t g_heartbeat = 0;
uint32_t g_mqttMillis = 0;
char g_version[16];
SunSet sun;
bool g_standardDisplay;
bool g_afterSunset;
bool g_afterEleven;
bool g_afterSunrise;

NTPClient timeClient(ntpClient, "pool.ntp.org", 0, ONE_MINUTE);

const char* ntpServer = "pool.ntp.org";

IPAddress ip(172, 24, 1, 12);

// User stub
void sendHeartbeat();

Task taskSendHeartbeat(TASK_SECOND * 60 , TASK_FOREVER, &sendHeartbeat);

void sunsetDisplay()
{
    String payload("{\"change\":[{\"target\":0,\"state\":1,\"color\":{\"r\":0,\"g\":0,\"b\":0,\"w\":255,\"bri\":255}}]}");
    mesh.sendBroadcast(payload);
    Serial.println("sundown");
}

void dimAtEleven()
{
    String payload("{\"change\":[{\"target\":0,\"state\":1,\"color\":{\"r\":0,\"g\":0,\"b\":0,\"w\":255,\"bri\":100}}]}");
    mesh.sendBroadcast(payload);
    Serial.println("Eleven");
}

void sunriseDisplay()
{
    String payload("{\"change\":[{\"target\":0,\"state\":0,\"color\":{\"r\":0,\"g\":0,\"b\":0,\"w\":255,\"bri\":100}}]}");
    mesh.sendBroadcast(payload);
    Serial.println("Sunrise");
}

/**
 * Just a message relay. Send an MQTT back if the payload is bogus
 */
void messageReceived(String &topic, String &payload) 
{
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        String t("pathlights/error");
        char buffer[256];
        doc.clear();
        doc["response"] = "error";
        doc["reason"] = "json";
        doc["string"] = error.f_str();
        doc["code"] = error.code();
        doc["node"] = mesh.getNodeId();
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
    String topic("pathlights/heartbeat");
    StaticJsonDocument<512> doc;
    char buffer[512];
    JsonObject contents = doc.createNestedObject("heartbeat");
    contents["role"] = "gateway";
    contents["heap"] = ESP.getFreeHeap();
    contents["node"] = mesh.getNodeId();
    contents["model"] = ESP.getChipModel();
    contents["uptime"] = esp_timer_get_time() / 1000000;
    contents["version"] = g_version;
    contents["nodeid"] = mesh.getNodeId();
    int n = serializeJson(doc, buffer);
    mqttClient.publish(topic.c_str(), buffer, n);

    String payload("{\"gateway\":");
    payload += mesh.getNodeId();
    payload += "}";
    mesh.sendBroadcast(payload);
}

void droppedConnectionCallback(uint32_t nodeId)
{
    StaticJsonDocument<256> doc;
    String topic("pathlights/state");
    char buffer[256];
    JsonObject obj = doc.createNestedObject("statechange");
    obj["node"] = nodeId;
    obj["action"] = "dropped";
    int n = serializeJson(doc, buffer);
    mqttClient.publish(topic.c_str(), buffer, n);
}

// Needed for painless library
void receivedCallback(uint32_t from, String &msg) 
{
    Serial.printf("Message from %u: %s\n", from, msg.c_str());
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, msg);
    if (error) {
        Serial.printf("Invalid JSON: %s\n", msg.c_str());
        String payload;
        String topic("pathlights/error");
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
                String topic("pathlights/heartbeat");
                mqttClient.publish(topic.c_str(), msg);
            }
        }
    }
}

void newConnectionCallback(uint32_t nodeId) 
{
    // Send an MQTT note that a new node has joined
    StaticJsonDocument<256> doc;
    String topic("pathlights/state");
    char buffer[256];
    JsonObject obj = doc.createNestedObject("statechange");
    obj["node"] = nodeId;
    obj["action"] = "new";
    int n = serializeJson(doc, buffer);
    mqttClient.publish(topic.c_str(), buffer, n);

    // Tell the new node who the MQTT gateway is
    String payload("{\"gateway\":");
    payload += mesh.getNodeId();
    payload += "}";
    mesh.sendSingle(nodeId, payload);
    Serial.printf("New node: %u\n", nodeId);
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
    while (!mqttClient.connect("mesh-mqtt-gw")) {
        Serial.print("Last Error: ");
        Serial.println(mqttClient.lastError());
        delay(500);
    }
    mqttClient.subscribe("pathlights/actions/#");
}

int getMPM()
{
    int mpm = 0;

    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        return -1;
    }

    return timeinfo.tm_hour * 60 + timeinfo.tm_min;
}

void setDSTOffset()
{
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        return;
    }
    sun.setTZOffset(TZ_OFFSET + timeinfo.tm_isdst);
    sun.setCurrentDate(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
}

void setup() 
{
    sprintf(g_version, "%d.%d.%d", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, APP_ID);
    Serial.begin(115200);

    setenv("TZ", "CST6CDT,M3.2.0/2,M11.1.0", 1);
    tzset();
    
    g_standardDisplay = true;
    g_afterSunset = false;
    g_afterEleven = false;
    g_afterSunrise = true;

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

    mqttClient.begin(mqttServer, 1883, ethClient);
    mqttClient.onMessage(messageReceived);

    Serial.println("mqtt started");
    mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before init() so that you can see startup messages

    mesh.setRoot(true);
    mesh.setHostname("meshgateway");   
    mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
    mesh.onDroppedConnection(&droppedConnectionCallback);

    Serial.println("Mesh started");
    userScheduler.addTask(taskSendHeartbeat);
    taskSendHeartbeat.enable();

    IPAddress apip = mesh.getAPIP();
    IPAddress localip = Ethernet.localIP();
    IPAddress staip = mesh.getStationIP();
    Serial.printf("Node: %u: APIP: %s, Ethernet IP: %s\n", mesh.getNodeId(), apip.toString().c_str(), localip.toString().c_str());

    sun.setPosition(LATITUDE, LONGITUDE, TZ_OFFSET);

    timeClient.begin();
}

void loop() 
{
    int mpm = getMPM();

    mesh.update();
    Ethernet.maintain();
    if (timeClient.update()) {
        timeval epoch = {timeClient.getEpochTime(), 0};
        settimeofday((const timeval*)&epoch, 0);
        setDSTOffset();
    }

    if (millis() - g_mqttMillis > 1000) {
        if (!mqttClient.connected()) {
            connect();
        }
        else
            mqttClient.loop();
    
        g_mqttMillis = millis();
    }

    if (mpm >= 0 && g_standardDisplay) {
        int sunset = static_cast<int>(sun.calcSunset());
        int sunrise = static_cast<int>(sun.calcSunrise());
        if (mpm >= sunrise && mpm < sunset && !g_afterSunrise) {
            sunriseDisplay();
            g_afterSunrise = true;
            g_afterSunset = false;
            g_afterEleven = false;
            return;
        } 
        if (mpm >= sunset && mpm < 1380 && !g_afterSunset) {
            sunsetDisplay();
            g_afterSunset = true;
            return;
        }
        if (mpm >= 1380 && !g_afterEleven) {
            dimAtEleven();
            g_afterEleven = true;
        }
    }
}

