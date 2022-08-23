#include <Wire.h>         //Library wire.h
#include "MAX30105.h"     //Library SparkFun MAX3010x
#include "heartRate.h"    //Library heartRate dari SparkFun MAX3010x
#include "painlessMesh.h" //Library painlessMesh

#define   MESH_PREFIX     "Mesh@ESP"    //Nama SSID dari jaringan mesh yang akan dibentuk
#define   MESH_PASSWORD   "12345678"    //Password SSID dari jaringan mesh yang akan dibentuk
#define   MESH_PORT       5555          //Port dari SSID 

//Variabel untuk Sensor MAX30102--------------------------------------------------------------------
MAX30105 particleSensor;  //Memanggil library SparkFun MAX3010x menjadi "particleSensor" 
const byte RATE_SIZE = 4; //Increase this for more averaging, 4 is good
byte rates[RATE_SIZE];    //Array of heart rates
byte rateSpot = 0;        //initialize ratespot
long lastBeat = 0;        //Initialize lastBeat
float beatsPerMinute;     //Deklarasi variabel
int beatAvg;              //Deklarasi variabel
double oxi;               //Deklarasi variabel
#define USEFIFO           //Deklarasi variabel

Scheduler userScheduler;  //Scheduler painlessMesh
painlessMesh mesh;        //Memanggil library PainlessMesh menjadi "mesh"
String msg;               

//Prototype fungsi-------------------------------------------------------------------------------------------------------------------------------------------------------------
void sendMessage();     //fungsi pengiriman pesan
void SensorReadings();  //fungsi pembacaan sensor
Task taskSendMessage(TASK_SECOND * 1 , TASK_FOREVER, &sendMessage); //task scheduler untuk pengiriman pesan setiap 1 detik pada fungsi sendMessage

//Initialize Variabel Sensor MAX30102------------------------------------------------------------------------------------------------------------------------------------------------------------- 
double avered = 0;
double aveir = 0;
double sumirrms = 0;
double sumredrms = 0;
int i = 0;
int Num = 100;//calculate SpO2 by this sampling interval
double ESpO2 = 93.0;//initial value of estimated SpO2
double FSpO2 = 0.7; //filter factor for estimated SpO2
double frate = 0.95; //low pass filter for IR/red LED value to eliminate AC component
#define TIMETOBOOT 2000 // wait for this time(msec) to output SpO2
#define SCALE 88.0 //adjust to display heart beat and SpO2 in the same scale
#define SAMPLING 5 //if you want to see heart beat more precisely , set SAMPLING to 1
#define FINGER_ON 66000 // if red signal is lower than this , it indicates your finger is not on the sensor
#define MINIMUM_SPO2 0.0
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//Fungsi pembacaan sensor MAX30102////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SensorReadings(){
  uint32_t ir, red;
  double fred, fir;
  double SpO2 = 0; //raw SpO2 before low pass filtered
 
#ifdef USEFIFO
  particleSensor.check(); //Check the sensor, read up to 3 samples
 
  while (particleSensor.available()) {//do we have new data
#ifdef MAX30105
   red = particleSensor.getFIFORed(); //Sparkfun's MAX30105
    ir = particleSensor.getFIFOIR();  //Sparkfun's MAX30105
#else
    red = particleSensor.getFIFOIR(); //why getFOFOIR output Red data by MAX30102 on MH-ET LIVE breakout board
    ir = particleSensor.getFIFORed(); //why getFIFORed output IR data by MAX30102 on MH-ET LIVE breakout board
#endif
    i++;
    fred = (double)red;
    fir = (double)ir;
    avered = avered * frate + (double)red * (1.0 - frate);//average red level by low pass filter
    aveir = aveir * frate + (double)ir * (1.0 - frate); //average IR level by low pass filter
    sumredrms += (fred - avered) * (fred - avered); //square sum of alternate component of red level
    sumirrms += (fir - aveir) * (fir - aveir);//square sum of alternate component of IR level    
    if ((i % SAMPLING) == 0) {//slow down graph plotting speed for arduino Serial plotter by thin out
      if ( millis() > TIMETOBOOT) {
        if (ir < FINGER_ON) ESpO2 = MINIMUM_SPO2; //indicator for finger detached
        if (ESpO2 <= -1)
        {
          ESpO2 = 0;
        }

        if (ESpO2 > 100)
        {
          ESpO2 = 100;
        }

        oxi = ESpO2;
        
        //Serial.print(" Oxygen % = ");
        //Serial.println(ESpO2); //low pass filtered SpO2
        mesh.update();
      }
      mesh.update();
    }
    if ((i % Num) == 0) {
      double R = (sqrt(sumredrms) / avered) / (sqrt(sumirrms) / aveir);
      // Serial.println(R);
      SpO2 = -23.3 * (R - 0.4) + 100; //http://ww1.microchip.com/downloads/jp/AppNotes/00001525B_JP.pdf
      ESpO2 = FSpO2 * ESpO2 + (1.0 - FSpO2) * SpO2;//low pass filter
      //Serial.print(SpO2);Serial.print(",");Serial.println(ESpO2);
      //Serial.print(" Oxygen2 % = ");
      //Serial.println(ESpO2); //low pass filtered SpO2
      sumredrms = 0.0; sumirrms = 0.0; i = 0;
      mesh.update();
      break;
    }
    mesh.update();
    particleSensor.nextSample(); //We're finished with this sample so move to next sample
    //Serial.println(SpO2);

    //////////////////////////////////////////////////////////////////////////////
    long irHR = particleSensor.getIR();
   if(irHR > 66000)//If a finger is detected 
   {                                                           
    //  Serial.print("irHR = ");                 
    //  Serial.println(irHR);
    //  Serial.print(" bpm"); 
    //  Serial.println("SpO2 = ");           
    //  Serial.println(oxi); 
    //  Serial.print(" %");
     mesh.update();
      if (checkForBeat(irHR) == true)                        //If a heart beat is detected
      {                             
        Serial.print("HR = ");                 
        Serial.print(beatAvg);
        Serial.println(" bpm"); 
        Serial.print("SpO2 = ");           
        Serial.print(oxi); 
        Serial.println(" %"); 
        msg = ("Node x --> Heart Rate : " + String(beatAvg) + " bpm, SpO2 : " + String(oxi) + " %"); //Command untuk mengirimkan pesan

        long delta = millis() - lastBeat;                   //Measure duration between two beats
        lastBeat = millis();
        beatsPerMinute = 60 / (delta / 1000.0);           //Calculating the BPM
        if(beatsPerMinute < 255 && beatsPerMinute > 20)               //To calculate the average we strore some values (4) then do some math to calculate the average
        {
          rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
          rateSpot %= RATE_SIZE; //Wrap variable
          //Take average of readings
          beatAvg = 0;
          for(byte x = 0 ; x < RATE_SIZE ; x++){
            beatAvg += rates[x];
          }
          beatAvg /= RATE_SIZE;
          mesh.update();
        }
        mesh.update();
      }
      mesh.update();
    }
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    mesh.update();
  }
mesh.update();
}

//Fungsi pengiriman pesan
void sendMessage()
  {
  mesh.sendBroadcast(msg);  //kirim pesan broadcastly
  taskSendMessage.setInterval(TASK_SECOND * 1); //kirim setiap 1 detik
  }

//Pesan diterima dari mesh network---------------------------------------------------------------------------------------------------------------------------------------------
void receivedCallback(const uint32_t &from, const String &msg) //fungsi callback saat menerima pesan dari node pada jaringan mesh 
  {
  Serial.printf("bridge: Received from %u msg=%s\n", from, msg.c_str());  //menampilkan isi pesan pada serial monitor
  }

//Node join ke mesh network atau ada node baru join ke mesh network------------------------------------------------------------------------------------------------------------
void newConnectionCallback(uint32_t nodeId) //fungsi callback saat node join ke mesh network atau ada koneksi baru dari node baru 
  {
  Serial.printf("--> Start: New Connection, from nodeId = %u",nodeId); //informasi nodeID dari node baru
  }

void setup() {
  Serial.begin(115200); //kecepatan serial baud rate
  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);  //pesan pada saat startup untuk painlessmesh
  mesh.init( MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT );  //initialize jaringan mesh untuk mulai dibangun
  mesh.onReceive(&receivedCallback);  //callback untuk pesan masuk
  mesh.onNewConnection(&newConnectionCallback); //callback untuk koneksi baru
  mesh.setContainsRoot(true); //memberitahu bahwa dalam jaringan mesh terdapat root node
  userScheduler.addTask(taskSendMessage); //initialize scheduler untuk taskSendMessage
  taskSendMessage.enable(); //enable scheduler taskSendMessage
  
  if(!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
  {
    Serial.println("MAX30102 was not found. Please check wiring/power/solder jumper at MH-ET LIVE MAX30102 board. ");
    while (1);
  }

  //Set up variabel untuk sensor MAX30102 (kalibrasi)-------------------------------------------------------------------------------------------------
  //Setup to sense a nice looking saw tooth on the plotter
  //byte ledBrightness = 0x7F; //Options: 0=Off to 255=50mA
  byte ledBrightness = 70; //Options: 0=Off to 255=50mA
  byte sampleAverage = 4; //Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  //Options: 1 = IR only, 2 = Red + IR on MH-ET LIVE MAX30102 board
  int sampleRate = 200; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; //Options: 69, 118, 215, 411
  int adcRange = 4096; //Options: 2048, 4096, 8192, 16384
  // Set up the wanted parameters
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //Configure sensor with these settings
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}

void loop() {
  SensorReadings(); //menjalankan fungsi pembacaan sensor
  mesh.update();    //menjalankan seluruh fungsi painlessmesh
}
#endif