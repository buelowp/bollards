#include <ArduinoJson.h>
#include <ArduinoJson.hpp>
#include <painlessMesh.h>

#define   MESH_PREFIX     "bollards"
#define   MESH_PASSWORD   "bollards4thewin"
#define   MESH_PORT       5555

#define   STATION_SSID     "bridge"
#define   STATION_PASSWORD "bollardbridge"
#define   STATION_PORT     5555
uint8_t   station_ip[4] =  {192,168,1,1}; // IP of the server

// prototypes
void receivedCallback( uint32_t from, String &msg );

painlessMesh  mesh;

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

void setup() {
  Serial.begin(115200);
  mesh.setDebugMsgTypes( ERROR | STARTUP | CONNECTION );  // set before init() so that you can see startup messages


  // Channel set to 6. Make sure to use the same channel for your mesh and for you other
  // network (STATION_SSID)
  mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT, WIFI_AP_STA, 6 );
  // Setup over the air update support
  mesh.initOTAReceive("bridge");

  mesh.stationManual(STATION_SSID, STATION_PASSWORD, STATION_PORT, station_ip);
  // Bridge node, should (in most cases) be a root node. See [the wiki](https://gitlab.com/painlessMesh/painlessMesh/wikis/Possible-challenges-in-mesh-formation) for some background
  mesh.setRoot(true);
  // This node and all other nodes should ideally know the mesh contains a root, so call this on all nodes
  mesh.setContainsRoot(true);


  mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);

    IPAddress apip = mesh.getAPIP();
    IPAddress staip = mesh.getStationIP();
    Serial.printf("APIP: %s, Station IP: %s", apip.toString().c_str(), staip.toString().c_str());
}

void loop() {
  mesh.update();
}

void receivedCallback( uint32_t from, String &msg ) 
{
  Serial.printf("bridge: Received from %u msg=%s\n", from, msg.c_str());
}

