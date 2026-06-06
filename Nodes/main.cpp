#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "painlessMesh.h"

#define MESH_PREFIX     "Mesh@ESP"
#define MESH_PASSWORD   "12345678"
#define MESH_PORT       5555

// Hardware/Sensor Settings
MAX30105 particleSensor;
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute;
int beatAvg = 0;
double oxi = 0.0;
bool sensorActive = false;

// painlessMesh globals
painlessMesh mesh;
Scheduler userScheduler;
String msg = "Node Initialize";

// FreeRTOS Task Handles
TaskHandle_t xSensorTaskHandle = NULL;
TaskHandle_t xMeshTaskHandle = NULL;

// Mutex for shared data (beatAvg, oxi, msg)
SemaphoreHandle_t xDataMutex;

// Function Prototypes
void vSensorTask(void *pvParameters);
void vMeshTask(void *pvParameters);
void sendMessage();

Task taskSendMessage(TASK_SECOND * 1, TASK_FOREVER, &sendMessage);

void sendMessage() {
  // Safe read of the shared message using Mutex
  if (xSemaphoreTake(xDataMutex, portMAX_DELAY) == pdTRUE) {
    String messageToSend = msg;
    xSemaphoreGive(xDataMutex);
    
    if (mesh.getNodeList().size() > 0) {
      mesh.sendBroadcast(messageToSend);
    }
  }
}

void receivedCallback(const uint32_t &from, const String &msg) {
  Serial.printf("bridge: Received from %u msg=%s\n", from, msg.c_str());
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("--> Start: New Connection, nodeId = %u\n", nodeId);
}

void setup() {
  Serial.begin(115200);

  // Initialize Mutex
  xDataMutex = xSemaphoreCreateMutex();

  // Initialize painlessMesh
  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.setContainsRoot(true);

  userScheduler.addTask(taskSendMessage);
  taskSendMessage.enable();

  // Initialize MAX30102 Sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Warning: MAX30102 not found! Relay-only mode active.");
    sensorActive = false;
  } else {
    sensorActive = true;
    byte ledBrightness = 70;
    byte sampleAverage = 4;
    byte ledMode = 2; // Red + IR
    int sampleRate = 200;
    int pulseWidth = 411;
    int adcRange = 4096;
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
    Serial.println("MAX30102 initialized successfully.");
  }

  // Create FreeRTOS Tasks
  // Task 1: Sensor Reading (Core 1 - App Core)
  xTaskCreatePinnedToCore(
    vSensorTask,        // Task function
    "SensorTask",       // Task name
    4096,               // Stack size (bytes)
    NULL,               // Parameter
    2,                  // Priority (higher than mesh task to prevent jitter in sensor readings)
    &xSensorTaskHandle, // Handle
    1                   // Core 1
  );

  // Task 2: painlessMesh and Scheduler (Core 0 - Protocol Core, sharing with WiFi stack)
  xTaskCreatePinnedToCore(
    vMeshTask,
    "MeshTask",
    8192,
    NULL,
    1,                  // Priority (lower)
    &xMeshTaskHandle,
    0                   // Core 0 (WiFi core)
  );
}

void loop() {
  // Empty! Everything is handled by FreeRTOS tasks.
  vTaskDelete(NULL); // Delete the default Arduino loop task to save RAM
}

// FreeRTOS Task for reading sensor and processing SpO2 / HR
void vSensorTask(void *pvParameters) {
  if (!sensorActive) {
    vTaskDelete(NULL); // Delete this task if sensor is missing
  }

  double avered = 0;
  double aveir = 0;
  double sumirrms = 0;
  double sumredrms = 0;
  int sampleIndex = 0;
  const int Num = 100;
  double ESpO2 = 93.0;
  const double FSpO2 = 0.7;
  const double frate = 0.95;

  #define TIMETOBOOT 2000
  #define FINGER_ON 66000
  #define MINIMUM_SPO2 0.0

  for (;;) {
    particleSensor.check();

    while (particleSensor.available()) {
      uint32_t red = particleSensor.getFIFOIR();
      uint32_t ir = particleSensor.getFIFORed();

      sampleIndex++;
      double fred = (double)red;
      double fir = (double)ir;

      avered = avered * frate + fred * (1.0 - frate);
      aveir = aveir * frate + fir * (1.0 - frate);

      sumredrms += (fred - avered) * (fred - avered);
      sumirrms += (fir - aveir) * (fir - aveir);

      if (sampleIndex >= Num) {
        if (millis() > TIMETOBOOT) {
          if (ir < FINGER_ON) {
            ESpO2 = MINIMUM_SPO2;
          } else {
            // Added check to prevent division by zero (avered or sumirrms equal to 0)
            if (avered > 0.0 && sumirrms > 0.0 && aveir > 0.0) {
              double R = (sqrt(sumredrms) / avered) / (sqrt(sumirrms) / aveir);
              double SpO2 = -23.3 * (R - 0.4) + 100.0;
              if (SpO2 > 100.0) SpO2 = 100.0;
              if (SpO2 < 0.0) SpO2 = 0.0;
              ESpO2 = FSpO2 * ESpO2 + (1.0 - FSpO2) * SpO2;
            }
          }
          
          // Thread-safe update of oxi
          xSemaphoreTake(xDataMutex, portMAX_DELAY);
          oxi = ESpO2;
          xSemaphoreGive(xDataMutex);
        }
        sumredrms = 0.0;
        sumirrms = 0.0;
        sampleIndex = 0;
      }

      long irHR = particleSensor.getIR();
      if (irHR > FINGER_ON) {
        if (checkForBeat(irHR) == true) {
          long delta = millis() - lastBeat;
          lastBeat = millis();
          beatsPerMinute = 60.0 / (delta / 1000.0);

          if (beatsPerMinute < 255.0 && beatsPerMinute > 20.0) {
            rates[rateSpot++] = (byte)beatsPerMinute;
            rateSpot %= RATE_SIZE;

            int tempSum = 0;
            for (byte x = 0; x < RATE_SIZE; x++) {
              tempSum += rates[x];
            }
            
            // Thread-safe update of beatAvg and message String
            xSemaphoreTake(xDataMutex, portMAX_DELAY);
            beatAvg = tempSum / RATE_SIZE;
            uint32_t myNodeId = mesh.getNodeId();
            msg = "Node " + String(myNodeId) + " -> HR: " + String(beatAvg) + " bpm, SpO2: " + String((int)oxi) + " %";
            xSemaphoreGive(xDataMutex);
          }
        }
      } else {
        // Finger detached
        xSemaphoreTake(xDataMutex, portMAX_DELAY);
        oxi = MINIMUM_SPO2;
        beatAvg = 0;
        uint32_t myNodeId = mesh.getNodeId();
        msg = "Node " + String(myNodeId) + " -> No Finger Detected";
        xSemaphoreGive(xDataMutex);
      }

      particleSensor.nextSample();
    }

    // Delay task for 10ms to let the CPU yield and run other threads
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// FreeRTOS Task for running painlessMesh background thread on Core 0
void vMeshTask(void *pvParameters) {
  for (;;) {
    mesh.update();
    // Yield execution to allow WiFi Stack tasks on Core 0 to run
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
