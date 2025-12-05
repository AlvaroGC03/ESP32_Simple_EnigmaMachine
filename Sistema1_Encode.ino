#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- HARDWARE ---
LiquidCrystal_I2C lcd(0x27, 16, 2); 
#define RXD2 16
#define TXD2 17

// --- BLE ---
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL; 
bool deviceConnected = false;
bool oldDeviceConnected = false;
String inputString = "";
bool stringComplete = false;

// --- BASE DE DATOS ENIGMA ---
const char ALFABETO[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const char* ALL_WIRINGS[] = {
  "EKMFLGDQVZNTOWYHXUSPAIBRCJ", // I
  "AJDKSIRUXBLHWTMCQGZNPYFVOE", // II
  "BDFHJLCPRTXVZNYEIWGAKMUSQO", // III
  "ESOVPZJAYQUIRHXLNFTGKDCMWB", // IV
  "VZBRGITYUPSDNHLXAWMJQOFEKC"  // V
};
const char ALL_NOTCHES[] = {'Q', 'E', 'V', 'J', 'Z'};
const char REFLECTOR[] = "YRUHQSLDPXNGOKMIEBFZCWVJAT"; 

// --- ESTADO DEL SISTEMA ---
int idRotorL = 2; int idRotorM = 1; int idRotorR = 0;
int posL = 0; int posM = 0; int posR = 0;
char PLUGBOARD[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"; 

// --- VARIABLES VISUALIZACIÓN (UI) ---
String currentMsgUp = "ENIGMA MASTER"; 
String currentMsgDown = "Esperando App...";
bool mostrarPrefix = false; // Controla si mostramos TX/RX

unsigned long lastScrollTime = 0;      
const int scrollInterval = 500;        
int scrollIndexUp = 0;                 
int scrollIndexDown = 0;               

// PROTOTIPOS
void actualizarPantallaScroll();
String padString(String texto); 
void procesarComando(String cmd);
void moverRotores();
char encriptarLetra(char c);
int pasoRotor(int indiceEntrada, int posRotor, const char* cableado, bool ida);

// CLASES BLE
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; };
    void onDisconnect(BLEServer* pServer) { deviceConnected = false; }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue(); 
      if (rxValue.length() > 0) {
        inputString = rxValue;
        stringComplete = true;
      }
    }
};

void setup() {
  Serial.begin(115200); 
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  lcd.init(); lcd.backlight();
  
  BLEDevice::init("Enigma_Master"); 
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());
  BLECharacteristic * pRx = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  pRx->setCallbacks(new MyCallbacks());
  pService->start();
  pServer->getAdvertising()->start();
}

void loop() {
  // GESTION DE COMANDOS Y TEXTO
  if (stringComplete) {
    inputString.trim();
    inputString.toUpperCase();
    
    // --- MODO CONFIGURACIÓN (Comienza con !) ---
    if (inputString.startsWith("!")) {
      mostrarPrefix = false; 
      
      procesarComando(inputString);
      Serial2.println(inputString); // Sincronizamos Slave
      
      currentMsgUp = "CFG UPDATED!";
      String estado = "R:" + String(idRotorL+1) + String(idRotorM+1) + String(idRotorR+1) + 
                      " P:" + String(ALFABETO[posL]) + String(ALFABETO[posM]) + String(ALFABETO[posR]);
      currentMsgDown = estado;
      
      scrollIndexUp = 0; scrollIndexDown = 0;
    } 
    // --- MODO TEXTO (Validación) ---
    else {
      // 1. VALIDACIÓN DE SEGURIDAD
      bool esValido = true;
      for (int i = 0; i < inputString.length(); i++) {
        char c = inputString.charAt(i);
        // Si NO es letra Y NO es espacio -> Es inválido
        if (!isalpha(c) && c != ' ') {
          esValido = false;
          break; 
        }
      }

      // 2. TOMA DE DECISIÓN
      if (!esValido) {
        // --- CASO ERROR: Texto Inválido ---
        mostrarPrefix = false; 
        
        currentMsgUp = "ERROR DE ENTRADA";
        currentMsgDown = "Texto no valido";
        scrollIndexUp = 0; scrollIndexDown = 0;

        // Reporte Extenso a Terminal Serial y Celular (BLE)
        String errorMsg = "ERROR CRITICO: El mensaje contiene caracteres no validos (Numeros o Simbolos). Proceso abortado.\n";
        Serial.println(errorMsg);

        if (deviceConnected && pTxCharacteristic != NULL) {
           pTxCharacteristic->setValue((uint8_t*)errorMsg.c_str(), errorMsg.length());
           pTxCharacteristic->notify();
        }
      } 
      else {
        // --- CASO ÉXITO: Texto Válido ---
        mostrarPrefix = true; 
        
        String textoCifrado = "";
        currentMsgUp = inputString;
        scrollIndexUp = 0; 

        for (int i = 0; i < inputString.length(); i++) {
          char c = inputString.charAt(i);
          if (c == ' ') {
             textoCifrado += " ";     
             Serial2.print(" ");
          }
          else if (isalpha(c)) {
            moverRotores();
            char cifrado = encriptarLetra(c);
            textoCifrado += cifrado;
            Serial2.print(cifrado);
          }
          delay(50); 
        }
        Serial2.println();
        
        currentMsgDown = textoCifrado;
        scrollIndexDown = 0; 

        if (deviceConnected && pTxCharacteristic != NULL) {
            String reporte = "OUT: " + textoCifrado + "\n";
            pTxCharacteristic->setValue((uint8_t*)reporte.c_str(), reporte.length());
            pTxCharacteristic->notify();
        }
      }
    }
    inputString = "";
    stringComplete = false;
  }
  
  // RECONEXION
  if (!deviceConnected && oldDeviceConnected) {
      delay(500); pServer->startAdvertising(); oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) { oldDeviceConnected = deviceConnected; }

  // ACTUALIZACION DE PANTALLA
  actualizarPantallaScroll();
}

// --- VISUALIZACIÓN INTELIGENTE ---

String padString(String texto) {
  String temp = texto;
  while (temp.length() < 16) {
    temp += " ";
  }
  return temp;
}

void actualizarPantallaScroll() {
  if (millis() - lastScrollTime >= scrollInterval) {
    lastScrollTime = millis(); 

    // LÓGICA DE VISUALIZACIÓN CONDICIONAL
    String finalUp, finalDown;
    
    // Si mostrarPrefix es TRUE (Texto normal), agregamos TX/RX
    if (mostrarPrefix) {
      finalUp = "TX: " + currentMsgUp;
      finalDown = "RX: " + currentMsgDown;
    } else {
      // Si es FALSE (Inicio o Configuración), mostramos el texto LIMPIO
      finalUp = currentMsgUp;
      finalDown = currentMsgDown;
    }

    // --- LINEA ARRIBA ---
    if (finalUp.length() <= 16) {
      lcd.setCursor(0, 0);
      lcd.print(padString(finalUp)); 
    } else {
      String txtUpFull = finalUp + "   "; 
      lcd.setCursor(0, 0);
      String ventana = "";
      for(int i=0; i<16; i++) { 
        int charIndex = (scrollIndexUp + i) % txtUpFull.length();
        ventana += txtUpFull.charAt(charIndex);
      }
      lcd.print(ventana);
      scrollIndexUp = (scrollIndexUp + 1) % txtUpFull.length();
    }

    // --- LINEA ABAJO ---
    if (finalDown.length() <= 16) {
      lcd.setCursor(0, 1);
      lcd.print(padString(finalDown)); 
    } else {
      String txtDownFull = finalDown + "   ";
      lcd.setCursor(0, 1);
      String ventana = "";
      for(int i=0; i<16; i++) {
        int charIndex = (scrollIndexDown + i) % txtDownFull.length();
        ventana += txtDownFull.charAt(charIndex);
      }
      lcd.print(ventana);
      scrollIndexDown = (scrollIndexDown + 1) % txtDownFull.length();
    }
  }
}

void procesarComando(String cmd) {
  if (cmd.length() >= 7) {
    idRotorL = constrain(cmd.charAt(1) - '1', 0, 4);
    idRotorM = constrain(cmd.charAt(2) - '1', 0, 4);
    idRotorR = constrain(cmd.charAt(3) - '1', 0, 4);
    posL = cmd.charAt(4) - 'A';
    posM = cmd.charAt(5) - 'A';
    posR = cmd.charAt(6) - 'A';
  }
}

// --- LOGICA ENIGMA ---
void moverRotores() {
  bool giraM = false; bool giraL = false;
  if (ALFABETO[posR] == ALL_NOTCHES[idRotorR]) { giraM = true; }
  if (ALFABETO[posM] == ALL_NOTCHES[idRotorM]) { giraL = true; }
  posR = (posR + 1) % 26;
  if (giraM) { posM = (posM + 1) % 26; }
  if (giraL) { posL = (posL + 1) % 26; }
}

char encriptarLetra(char c) {
  int idx = c - 'A';
  idx = PLUGBOARD[idx] - 'A';
  idx = pasoRotor(idx, posR, ALL_WIRINGS[idRotorR], true);
  idx = pasoRotor(idx, posM, ALL_WIRINGS[idRotorM], true);
  idx = pasoRotor(idx, posL, ALL_WIRINGS[idRotorL], true);
  char refl = REFLECTOR[idx];
  idx = refl - 'A';
  idx = pasoRotor(idx, posL, ALL_WIRINGS[idRotorL], false);
  idx = pasoRotor(idx, posM, ALL_WIRINGS[idRotorM], false);
  idx = pasoRotor(idx, posR, ALL_WIRINGS[idRotorR], false);
  idx = PLUGBOARD[idx] - 'A';
  return ALFABETO[idx];
}

int pasoRotor(int indiceEntrada, int posRotor, const char* cableado, bool ida) {
  int offset = posRotor;
  if (ida) {
    int indiceEnRotor = (indiceEntrada + offset) % 26;
    char letraSalida = cableado[indiceEnRotor];
    return (letraSalida - 'A' - offset + 26) % 26; 
  } else {
    int indiceBusqueda = (indiceEntrada + offset) % 26;
    char letraBuscada = ALFABETO[indiceBusqueda];
    int posEnWiring = -1;
    for(int i=0; i<26; i++){
      if(cableado[i] == letraBuscada){ posEnWiring = i; break; }
    }
    return (posEnWiring - offset + 26) % 26;
  }
}
