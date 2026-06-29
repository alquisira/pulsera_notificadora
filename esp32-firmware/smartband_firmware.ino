/*
 * Firmware para Pulsera Inteligente con Notificaciones RGB
 * 
 * Microcontrolador: ESP32-C3 Super Mini
 * LEDs: WS2812B (2 unidades)
 * Comunicación: Bluetooth Low Energy (BLE)
 * 
 * Colores por aplicación:
 * - WhatsApp: Verde (0, 255, 0)
 * - Facebook: Azul (0, 0, 255)
 * - Instagram: Rosa (255, 192, 203)
 * - Mercado Libre: Amarillo (255, 255, 0)
 * - Gmail: Rojo (255, 0, 0)
 * - YouTube: Rojo (255, 0, 0)
 * - Telegram: Cian (0, 255, 255)
 * - Llamada: Blanco (255, 255, 255)
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <NeoPixelBus.h>

// Pin de datos para los LEDs WS2812B (GPIO4 en ESP32-C3)
#define LED_PIN 4
#define LED_COUNT 2

// UUIDs del servicio BLE
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Estados de conexión BLE
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Instancia del NeoPixel (WS2812B)
NeoPixelBus<NeoGrbFeature, NeoEsp32I2s1Ws2812Method> strip(LED_COUNT, LED_PIN);

// Servidor y características BLE
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;

// Callbacks de conexión BLE
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      // Reiniciar publicidad para permitir reconexión
      BLEDevice::startAdvertising();
    }
};

// Convertir nombre de aplicación a color RGB
RgbColor getAppColor(const String& appName) {
  appName.toLowerCase();
  
  if (appName.indexOf("whatsapp") >= 0) {
    return RgbColor(0, 255, 0);        // Verde
  } else if (appName.indexOf("facebook") >= 0 || appName.indexOf("fb") >= 0) {
    return RgbColor(0, 0, 255);        // Azul
  } else if (appName.indexOf("instagram") >= 0 || appName.indexOf("insta") >= 0) {
    return RgbColor(255, 192, 203);    // Rosa
  } else if (appName.indexOf("mercadolibre") >= 0 || appName.indexOf("mercado libre") >= 0) {
    return RgbColor(255, 255, 0);      // Amarillo
  } else if (appName.indexOf("gmail") >= 0) {
    return RgbColor(255, 0, 0);        // Rojo
  } else if (appName.indexOf("youtube") >= 0) {
    return RgbColor(255, 0, 0);        // Rojo
  } else if (appName.indexOf("telegram") >= 0) {
    return RgbColor(0, 255, 255);      // Cian
  } else if (appName.indexOf("call") >= 0 || appName.indexOf("llamada") >= 0) {
    return RgbColor(255, 255, 255);    // Blanco
  }
  
  // Color por defecto (blanco suave)
  return RgbColor(100, 100, 100);
}

// Mostrar notificación con el color correspondiente
void showNotification(const String& appName, int duration = 5000) {
  RgbColor color = getAppColor(appName);
  
  // Establecer color en todos los LEDs
  for (int i = 0; i < LED_COUNT; i++) {
    strip.SetPixelColor(i, color);
  }
  strip.Show();
  
  // Mantener iluminación durante el tiempo especificado
  delay(duration);
  
  // Apagar LEDs
  for (int i = 0; i < LED_COUNT; i++) {
    strip.SetPixelColor(i, RgbColor(0));
  }
  strip.Show();
}

// Callback para recibir datos desde Android
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      
      if (value.length() > 0) {
        String appName = String(value.c_str());
        Serial.print("Notificación recibida: ");
        Serial.println(appName);
        
        // Mostrar notificación con el color correspondiente
        showNotification(appName);
      }
    }
};

void setup() {
  // Inicializar puerto serie para debugging
  Serial.begin(115200);
  Serial.println("Iniciando Pulsera Inteligente...");
  
  // Inicializar LEDs
  strip.Begin();
  strip.Show();
  
  // Parpadeo inicial para indicar que está funcionando
  for (int i = 0; i < LED_COUNT; i++) {
    strip.SetPixelColor(i, RgbColor(255, 255, 255));
  }
  strip.Show();
  delay(200);
  for (int i = 0; i < LED_COUNT; i++) {
    strip.SetPixelColor(i, RgbColor(0));
  }
  strip.Show();
  
  // Crear dispositivo BLE
  BLEDevice::init("SmartBand_Notifications");
  
  // Crear servidor BLE
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  // Crear servicio BLE
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  // Crear característica BLE
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_INDICATE
  );
  
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  
  // Iniciar servicio
  pService->start();
  
  // Iniciar publicidad
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("Esperando conexión BLE...");
}

void loop() {
  // Manejar desconexión/reconexión
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Reiniciando publicidad BLE...");
    oldDeviceConnected = deviceConnected;
  }
  
  // Manejar nueva conexión
  if (deviceConnected && !oldDeviceConnected) {
    Serial.println("Dispositivo conectado!");
    oldDeviceConnected = deviceConnected;
  }
  
  // Pequeño delay para evitar bloqueo del loop
  delay(10);
}
