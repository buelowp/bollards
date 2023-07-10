#include <SPIFFS.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <painlessMesh.h>

#define NEOPIXEL_PIN    5
#define NUMPIXELS       24
#define APP_ID          39

#define MESH_PREFIX         "bollards"
#define MESH_PASSWORD       "bollards4thewin"
#define MESH_PORT           5555

Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRBW + NEO_KHZ800);
void sendHeartbeat();

Scheduler     userScheduler; // to control your personal task
painlessMesh  mesh;
Task taskSendHeartbeat(TASK_SECOND * 60, TASK_FOREVER, &sendHeartbeat);

bool calc_delay = false;
SimpleList<uint32_t> nodes;
uint32_t g_gatewayNode = 0;
uint32_t g_heartbeat = 0;
char g_version[16];
uint32_t g_node;
TSTRING g_role = "bollard-xiao-c3";

void sendHeartbeat() 
{
    if (g_gatewayNode > 0) {
        StaticJsonDocument<512> doc;
        char buffer[512];
        JsonObject contents = doc.createNestedObject("heartbeat");
        contents["role"] = g_role;
        contents["heap"] = ESP.getFreeHeap();
        contents["node"] = mesh.getNodeId();
        contents["model"] = ESP.getChipModel();
        contents["uptime"] = esp_timer_get_time() / 1000000;
        contents["version"] = g_version;
        int n = serializeJson(doc, buffer);
        mesh.sendSingle(g_gatewayNode, buffer);
    }
}

void setColor(uint32_t color, uint8_t bri)
{
    pixels.setBrightness(bri);
    pixels.fill(color);
    pixels.show();
}

void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t bri)
{
    pixels.setBrightness(bri);
    pixels.fill(pixels.Color(r,g,b,w));
    pixels.show();
}

/**
 * { change : [ { target : <uint32_t>, state : <0|1>, color : 0xFFFFFFFF, bri : <0-255> }, { target : <int>, state <0|1>, color : 0xFFFFFFFF, bri : <0-255> } ] }
 * { change : [{target : <uint32_t>, state : <0|1>, color : { r: <uint8_t>, g: <uint8_t>, b: <uint8_t>, w: <uint8_t>, bri: <uint8_t>}}}, {target : <uint32_t>, state : <0|1>, color : { r: <uint8_t>, g: <uint8_t>, b: <uint8_t>, w: <uint8_t>, bri: <uint8_t>}}]}
 * { identify : <uint32_t>, color : <0xFFFFFFF>, bri: <uint8_t> }
 * { identify: <uint32_t>, color: { r: <uint8_t>, g: <uint8_t>, b: <uint8_t>, w: <uint8_t>, bri: <uint8_t>}}
 * { gateway: <uint32_t> }
 */
void receivedCallback(uint32_t from, String &msg) 
{
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, msg);
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    Serial.println(msg);
    if (!doc.isNull()) {
        if (doc.is<JsonObject>()) {
            JsonObject obj = doc.as<JsonObject>();
            if (obj.containsKey("identify")) {
                uint32_t id =  obj["identify"].as<unsigned int>();
                if (id == g_node) {
                    if (obj.containsKey("color")) {
                        if (obj["color"].is<JsonObject>()) {
                            uint8_t r = obj["color"]["r"].as<unsigned int>();
                            uint8_t g = obj["color"]["g"].as<unsigned int>();
                            uint8_t b = obj["color"]["b"].as<unsigned int>();
                            uint8_t w = obj["color"]["w"].as<unsigned int>();
                            uint8_t bri = obj["color"]["bri"].as<unsigned int>();
                            setColor(r,b,g,w,bri);
                        }
                        else {
                            uint32_t c = obj["color"].as<unsigned int>();
                            uint8_t bri = obj["bri"].as<unsigned int>();
                            setColor(c, bri);
                        }
                    }
                }
                return;
            }
            
            if (obj.containsKey("change")) {
                if (obj["change"].is<JsonArray>()) {
                    JsonArray array = obj["change"].as<JsonArray>();
                    for (JsonVariant v : array) {
                        if (v.is<JsonObject>()) {
                            JsonObject o = v.as<JsonObject>();
                            uint32_t node = o["target"].as<unsigned int>();
                            if (node == g_node || node == 0) {
                                if (o["state"].as<unsigned int>() == 0) {
                                    pixels.clear();
                                    pixels.show();
                                    return;
                                }
                                else {
                                    if (o.containsKey("color")) {
                                        if (o["color"].is<JsonObject>()) {
                                            uint8_t r = o["color"]["r"].as<unsigned int>();
                                            uint8_t g = o["color"]["g"].as<unsigned int>();
                                            uint8_t b = o["color"]["b"].as<unsigned int>();
                                            uint8_t w = o["color"]["w"].as<unsigned int>();
                                            uint8_t bri = o["color"]["bri"].as<unsigned int>();
                                            setColor(r,g,b,w,bri);
                                        }
                                        else {
                                            uint32_t c = o["color"].as<unsigned int>();
                                            uint8_t b = o["bri"].as<unsigned int>();
                                            setColor(c, b);                                        
                                        }
                                    }
                                    return;
                                }
                            }                      
                        }
                    }                  
                }
            }

            if (obj.containsKey("gateway")) {
                g_gatewayNode = obj["gateway"].as<unsigned int>();
                sendHeartbeat();
                pixels.clear();
                pixels.show();
            }
        }
    }
}

void newConnectionCallback(uint32_t nodeId) 
{
}

void droppedConnectionCallback(uint32_t nodeId) 
{
    if (nodeId == g_gatewayNode) {
        g_gatewayNode = 0;
        setColor(255,0,0,0,100);
    }
}

void nodeTimeAdjustedCallback(int32_t offset) 
{
}

void setup() 
{
    Serial.begin(115200);
    sprintf(g_version, "%d.%d.%d", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, APP_ID);

    pixels.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
    pixels.clear();
    pixels.show();

    //mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE ); // all types on
    mesh.setDebugMsgTypes( ERROR | STARTUP | DEBUG);  // set before init() so that you can see startup messages

    mesh.init( MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT );
    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onDroppedConnection(&droppedConnectionCallback);
    mesh.initOTAReceive(g_role);
    g_node = mesh.getNodeId();

    userScheduler.addTask(taskSendHeartbeat);
    taskSendHeartbeat.enable();
    setColor(255,0,0,0,50);
    Serial.printf("Setup complete: version %s, node ID %u\n", g_version, g_node);
}

void loop() 
{
    mesh.update();
}
