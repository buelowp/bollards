#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <painlessMesh.h>

#define NEOPIXEL_POWER  8
#define NEOPIXEL_PIN    SCK
#define NUMPIXELS       24

#define MESH_PREFIX         "bollards"
#define MESH_PASSWORD       "bollards4thewin"
#define MESH_PORT           5555

Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_RGBW + NEO_KHZ800);
void sendHeartbeat();

Scheduler     userScheduler; // to control your personal task
painlessMesh  mesh;
Task taskSendMessage(TASK_SECOND * 5, TASK_FOREVER, &sendHeartbeat);

uint32_t g_gatewayNode = 0;
uint32_t g_heartbeat = 1;

void sendToGateway(String &payload)
{
    if (g_gatewayNode > 0) {
        mesh.sendSingle(g_gatewayNode, payload);
    }
    else {
        mesh.sendBroadcast(payload);
    }
}

void sendHeartbeat() 
{
    StaticJsonDocument<512> doc;
    JsonObject obj;
    String payload;

    obj["role"] = "light";
    obj["node"] = mesh.getNodeId();
    obj["memory"] = ESP.getFreeHeap();
    obj["model"] = ESP.getChipModel();
    obj["counter"] = g_heartbeat++;
    doc["heartbeat"] = obj;
    doc["gateway"] = g_gatewayNode;
    serializeJson(doc, payload);

    sendToGateway(payload);
}

void identify(unsigned long color)
{
    pixels.setBrightness(127);
    pixels.fill(color);
    pixels.show();
}

/**
 * { change : [ { target : <int>, state : <0|1>, color : 0xFFFFFFFF, bri : <0-255> }, { target : <int>, state <0|1>, color : 0xFFFFFFFF, bri : <0-255> } ] }
 * { identify : nodeid, color : <0xFFFFFFF> }
 */
void receivedCallback(uint32_t from, String &msg) 
{
    StaticJsonDocument<1024> doc;
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
        sendToGateway(payload);
        return;
    }

    Serial.println(msg);
    if (!doc.isNull()) {
        if (doc.is<JsonObject>()) {
            JsonObject obj = doc.as<JsonObject>();
            for (JsonPair p : obj) {
                if (p.value().is<JsonArray>() && p.key() == "change") {
                    JsonArray arr = p.value().as<JsonArray>();
                    for (JsonVariant v : arr) {
                        JsonObject o = v.as<JsonObject>();
                        if (o.containsKey("target")) {
                            Serial.println("Found a target message");
                            if (o["target"] == mesh.getNodeId() || o["target"] == 0) {
                                int state = o["state"];
                                int bright = o["bri"];
                                unsigned long color = strtoul(o["color"].as<const char*>(), 0, 16);
                                pixels.setBrightness(bright);
                                pixels.fill(color);
                                if (state == 0)
                                    pixels.clear();
                                    
                                pixels.show();
                            }
                        }
                    }
                }
                else if (p.key() == "identify") {
                    Serial.print("I've been asked to identify myself by color ");
                    Serial.println(p.value().as<unsigned int>());
                    identify(strtoul(p.value().as<const char*>(), 0, 16));
                }
            }
        }
    }
}

void newConnectionCallback(uint32_t nodeId) 
{
}

void changedConnectionCallback() 
{
}

void nodeTimeAdjustedCallback(int32_t offset) 
{
}

void setup() 
{
    Serial.begin(115200);

    pinMode(NEOPIXEL_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_POWER, LOW);

    pixels.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
    pixels.clear();
    pixels.show();

    //mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE ); // all types on
    mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before init() so that you can see startup messages

    mesh.init( MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT );
    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

    Serial.println("Setup complete");
}

void loop() 
{
    mesh.update();
}
