//Source Code untuk menghitung waktu proses pembentukan SSID dan sinkronisasi jaringan
//edited by Donny Ali Sanjaya

#include "painlessMesh.h"               //Library painlessMesh
#include <AsyncTCP.h>                   //Library AsyncTCP
#include <ESPAsyncWebServer.h>          //Library AsyncWebServer

#define   MESH_PREFIX     "Mesh@ESP"          //Nama SSID dari jaringan mesh yang akan dibentuk
#define   MESH_PASSWORD   "12345678"          //Password SSID dari jaringan mesh yang akan dibentuk
#define   MESH_PORT       5555                //Port dari SSID 

#define   STATION_SSID     "ESPMesh"          //SSID Access Point (router) yang akan dikonekan (bridge antara mesh - internet)
#define   STATION_PASSWORD "12345678"         //Password SSID Access Point (router) yang akan dikonekan (bridge antara mesh - internet)
#define   HOSTNAME         "ESPMesh-Bridge"   //Nama host (user) untuk ESP32 yang akan dikenali oleh Access Point (router)

Scheduler userScheduler;      //Scheduler painlessMesh
SimpleList<uint32_t> nodes;   //variabel untuk jumlah nodes dalam jaringan mesh
painlessMesh  mesh;           //Memanggil library PainlessMesh menjadi "mesh"
WiFiClient wifiClient;        //Memanggil library WifiClinet menjadi "wifiClient"
AsyncWebServer server(80);    //Memanggil library AsyncWebServer menjadi "server" dengan port 80

//Prototype fungsi-------------------------------------------------------------------------------------------------------------------------------------------------------------
void receivedCallback(const uint32_t &from, const String &msg);
unsigned long int CurrentTime = millis();  //perhitungan waktu
IPAddress getlocalIP();                    
IPAddress myIP(0,0,0,0);
IPAddress myAPIP(0,0,0,0);

//Pesan diterima dari mesh network---------------------------------------------------------------------------------------------------------------------------------------------
void receivedCallback(const uint32_t &from, const String &msg) //fungsi callback saat menerima pesan dari node pada jaringan mesh 
  {
    Serial.printf("bridge: Received from %u msg=%s\n", from, msg.c_str());  //menampilkan isi pesan pada serial monitor
  }

//Node join ke mesh network atau ada node baru join ke mesh network------------------------------------------------------------------------------------------------------------
void newConnectionCallback(uint32_t nodeId) //fungsi callback saat node join ke mesh network atau ada koneksi baru dari node baru 
  {
  unsigned long int networkTime = millis(); //deklarasi network time saat ini
  unsigned long int networkTimeTaken = networkTime-CurrentTime; //perhitungan waktu proses ketika node masuk ke jaringan
  Serial.printf("--> Start: New Connection, from nodeId = %u, Time Taken for Syncronizing the Networks: %u milliseconds\n",nodeId ,networkTimeTaken); //informasi waktu proses
  Serial.printf("--> Start: New Connection, %s\n", mesh.subConnectionJson(true).c_str()); //menampilkan routing table dalam format JSON
  }

//Perubahan koneksi pada jaringan (entah ada node keluar atau node masuk)---------------------------------------------------------------------------------------------------------
void changedConnectionCallback() //fungsi callback untuk perubahan koneksi jaringan mesh
  {
  Serial.printf("Changed connections\n");
  nodes = mesh.getNodeList();
  Serial.printf("Num nodes: %d\n", nodes.size());
  Serial.printf("Connection list:");
  SimpleList<uint32_t>::iterator node = nodes.begin();
  while (node != nodes.end()) 
    {
    Serial.printf(" %u", *node);
    node++;
    }
  Serial.println();
  }

IPAddress getlocalIP() //fungsi untuk mendapatkan IP address dari Access Point (router)
  {
  return IPAddress(mesh.getStationIP());
  }

//fungsi untuk mengetahui routing table pada web server
String scanprocessor(const String& var)
{
  if(var == "SCAN")
    return mesh.subConnectionJson(false) ;
  return String();
}

void setup() 
  {
  Serial.begin(115200); //kecepatan serial baud rate
  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);  //pesan pada saat startup untuk painlessmesh
  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT, WIFI_AP_STA);  //initialize jaringan mesh untuk mulai dibangun
  mesh.onReceive(&receivedCallback);  //callback untuk pesan masuk
  mesh.onNewConnection(&newConnectionCallback); //callback untuk koneksi baru
  mesh.onChangedConnections(&changedConnectionCallback);  //callback untuk perubahan koneksi
  mesh.stationManual(STATION_SSID, STATION_PASSWORD); //connect ke access point secara manual dan berfungsi untuk meng-stop pencarian AP ketika sudah terkonek
  mesh.setHostname(HOSTNAME); //set hostname dengan variabel hostname
  mesh.setRoot(true); //set node ini menjadi root node
  mesh.setContainsRoot(true); //memberitahu bahwa dalam jaringan mesh terdapat root node
  //async webserver untuk membaca routing table dari jaringan mesh
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) 
    {
    request->send(200, "application/json", mesh.subConnectionJson(false) );
    });
  server.begin(); //nyalakan asyncwebserver
  
  unsigned long int buildingTime = millis();  //deklarasi variabel waktu proses pembentukan SSID
  Serial.print("Time Taken for Building the Network: ");
  Serial.print(buildingTime - CurrentTime); //hasil waktu proses untuk pembentukan SSID
  Serial.println(" milliseconds");
  }

void loop() 
  {
  mesh.update();    //menjalankan seluruh fungsi painlessmesh
  if(myIP != getlocalIP()) //menampilkan IP Address dari Access Point (router) 
    {
    myIP = getlocalIP();
    Serial.println("My IP is " + myIP.toString());
    }
  }