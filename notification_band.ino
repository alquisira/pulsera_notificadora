#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <NimBLEDevice.h>

// Cambia este pin si conectaste DIN de los WS2812B-B a otro GPIO.
static const uint8_t LED_DATA_PIN = 4;
static const uint8_t LED_COUNT = 2;
static const uint8_t LED_BRIGHTNESS = 40; // 0-255. Bajo ayuda a cuidar la bateria.

// UUIDs propios de la pulsera. La app Android debe conectarse a este servicio
// y escribir en la caracteristica NOTIFICATION_CHAR_UUID.
static const char* DEVICE_NAME = "NotificationBand";
static const char* NOTIFICATION_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* NOTIFICATION_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char* STATUS_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

Adafruit_NeoPixel pixels(LED_COUNT, LED_DATA_PIN, NEO_GRB + NEO_KHZ800);

static NimBLEServer* server = nullptr;
static NimBLECharacteristic* statusCharacteristic = nullptr;

enum NotificationApp {
  APP_NONE,
  APP_FACEBOOK,
  APP_YOUTUBE,
  APP_INSTAGRAM,
  APP_WHATSAPP
};

struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static NotificationApp activeApp = APP_NONE;
static NotificationApp pendingApp = APP_NONE;
static bool hasPendingApp = false;
static bool clientConnected = false;
static volatile bool hasPendingConnectionLight = false;

static bool animationRunning = false;
static bool blinkOn = false;
static uint8_t blinkStep = 0;
static unsigned long lastBlinkMs = 0;

String normalizeCommand(std::string value) {
  String command = String(value.c_str());
  command.trim();
  command.toLowerCase();
  return command;
}

const char* appName(NotificationApp app) {
  switch (app) {
    case APP_FACEBOOK:
      return "Facebook";
    case APP_YOUTUBE:
      return "YouTube";
    case APP_INSTAGRAM:
      return "Instagram";
    case APP_WHATSAPP:
      return "WhatsApp";
    default:
      return "Off";
  }
}

Color appColor(NotificationApp app) {
  switch (app) {
    case APP_FACEBOOK:
      return {0, 0, 255};       // Azul
    case APP_YOUTUBE:
      return {255, 0, 0};       // Rojo
    case APP_INSTAGRAM:
      return {255, 0, 90};      // Rosa/magenta
    case APP_WHATSAPP:
      return {0, 255, 0};       // Verde
    default:
      return {0, 0, 0};
  }
}

bool parseNotificationApp(const String& command, NotificationApp& app) {
  if (command == "f" || command == "fb" || command == "facebook") {
    app = APP_FACEBOOK;
    return true;
  }

  if (command == "y" || command == "yt" || command == "youtube") {
    app = APP_YOUTUBE;
    return true;
  }

  if (command == "i" || command == "ig" || command == "instagram") {
    app = APP_INSTAGRAM;
    return true;
  }

  if (command == "w" || command == "wa" || command == "whatsapp") {
    app = APP_WHATSAPP;
    return true;
  }

  if (command == "o" || command == "0" || command == "off" || command == "apagar") {
    app = APP_NONE;
    return true;
  }

  return false;
}

void showColor(Color color) {
  for (uint8_t i = 0; i < LED_COUNT; i++) {
    pixels.setPixelColor(i, pixels.Color(color.r, color.g, color.b));
  }
  pixels.show();
}

void turnOffLeds() {
  showColor({0, 0, 0});
}

void publishStatus(NotificationApp app) {
  if (statusCharacteristic == nullptr) {
    return;
  }

  statusCharacteristic->setValue(appName(app));
  if (clientConnected) {
    statusCharacteristic->notify();
  }
}

void startNotification(NotificationApp app) {
  activeApp = app;
  animationRunning = app != APP_NONE;
  blinkOn = false;
  blinkStep = 0;
  lastBlinkMs = 0;

  if (app == APP_NONE) {
    turnOffLeds();
  }

  publishStatus(app);
  Serial.printf("Notification: %s\n", appName(app));
}

void showBluetoothConnected() {
  activeApp = APP_NONE;
  animationRunning = false;
  showColor({0, 0, 255});
  Serial.println("Bluetooth connected: LEDs blue.");
}

void updateAnimation() {
  if (!animationRunning) {
    return;
  }

  const unsigned long now = millis();
  const unsigned long intervalMs = blinkOn ? 220 : 160;

  if (lastBlinkMs != 0 && now - lastBlinkMs < intervalMs) {
    return;
  }

  lastBlinkMs = now;
  blinkOn = !blinkOn;

  if (blinkOn) {
    showColor(appColor(activeApp));
  } else {
    turnOffLeds();
    blinkStep++;
  }

  if (blinkStep >= 3) {
    animationRunning = false;
    blinkOn = true;
    showColor(appColor(activeApp));
  }
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    clientConnected = true;
    hasPendingConnectionLight = true;
    Serial.printf("Client connected: %s\n", connInfo.getAddress().toString().c_str());
    server->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    clientConnected = false;
    Serial.printf("Client disconnected. Advertising again.\n");
    NimBLEDevice::startAdvertising();
  }
};

class NotificationCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
    String command = normalizeCommand(characteristic->getValue());
    NotificationApp app = APP_NONE;

    if (parseNotificationApp(command, app)) {
      pendingApp = app;
      hasPendingApp = true;
    } else {
      Serial.printf("Unknown BLE command ignored: %s\n", command.c_str());
    }

    Serial.printf("Received BLE command: %s\n", command.c_str());
  }
};

ServerCallbacks serverCallbacks;
NotificationCallbacks notificationCallbacks;

void setup() {
  Serial.begin(115200);
  delay(200);

  pixels.begin();
  pixels.setBrightness(LED_BRIGHTNESS);
  turnOffLeds();

  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);

  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);

  NimBLEService* notificationService = server->createService(NOTIFICATION_SERVICE_UUID);

  NimBLECharacteristic* notificationCharacteristic =
      notificationService->createCharacteristic(
          NOTIFICATION_CHAR_UUID,
          NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  notificationCharacteristic->setCallbacks(&notificationCallbacks);
  notificationCharacteristic->setValue("F,Y,I,W,off");

  statusCharacteristic =
      notificationService->createCharacteristic(
          STATUS_CHAR_UUID,
          NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  statusCharacteristic->setValue(appName(APP_NONE));

  notificationService->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(DEVICE_NAME);
  advertising->addServiceUUID(notificationService->getUUID());
  advertising->enableScanResponse(true);
  advertising->start();

  Serial.println("NotificationBand lista por BLE.");
  Serial.println("Comandos: F/Facebook, Y/YouTube, I/Instagram, W/WhatsApp, off.");
}


void handleSerialCommands() {
  if (!Serial.available()) {
    return;
  }

  String command = Serial.readStringUntil('\n');
  command.trim();
  command.toLowerCase();

  NotificationApp app = APP_NONE;

  if (parseNotificationApp(command, app)) {
    startNotification(app);
  } else {
    Serial.printf("Comando serial desconocido: %s\n", command.c_str());
  }
}

void loop() {
  handleSerialCommands();

  if (hasPendingConnectionLight) {
    noInterrupts();
    hasPendingConnectionLight = false;
    interrupts();

    showBluetoothConnected();
  }

  if (hasPendingApp) {
    noInterrupts();
    NotificationApp app = pendingApp;
    hasPendingApp = false;
    interrupts();

    startNotification(app);
  }

  updateAnimation();
}
