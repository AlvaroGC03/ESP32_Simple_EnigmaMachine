#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

// --- HARDWARE ---
LiquidCrystal_I2C lcd(0x27, 16, 2); 
#define RXD2 16
#define TXD2 17

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

// --- VARIABLES VISUALIZACIÓN ---
String currentMsgUp = "RECEPTOR LISTO"; 
String currentMsgDown = "Esperando Master";
bool mostrarPrefix = false; // Controla si mostramos RX/MS

unsigned long lastScrollTime = 0;      
const int scrollInterval = 500;        
int scrollIndexUp = 0;                 
int scrollIndexDown = 0; 
String serialBuffer = "";

// PROTOTIPOS
void actualizarPantallaScroll();
String padString(String texto);
void procesarPaquete(String paq);
void moverRotores();
char encriptarLetra(char c);
int pasoRotor(int indiceEntrada, int posRotor, const char* cableado, bool ida);

void setup() {
  Serial.begin(115200); 
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  lcd.init(); lcd.backlight();
}

void loop() {
  
  // 1. ESCUCHAR AL MASTER
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        procesarPaquete(serialBuffer);
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }

  // 2. ACTUALIZAR PANTALLA (CARRUSEL)
  actualizarPantallaScroll();
}

void procesarPaquete(String paq) {
  paq.trim();
  
  // --- MODO CONFIGURACIÓN ---
  if (paq.startsWith("!")) {
    mostrarPrefix = false; 

    idRotorL = constrain(paq.charAt(1) - '1', 0, 4);
    idRotorM = constrain(paq.charAt(2) - '1', 0, 4);
    idRotorR = constrain(paq.charAt(3) - '1', 0, 4);
    posL = paq.charAt(4) - 'A';
    posM = paq.charAt(5) - 'A';
    posR = paq.charAt(6) - 'A';
    
    currentMsgUp = "SYNC CONFIG OK!";
    String estado = "R:" + String(idRotorL+1) + String(idRotorM+1) + String(idRotorR+1) + 
                    " P:" + String(ALFABETO[posL]) + String(ALFABETO[posM]) + String(ALFABETO[posR]);
    currentMsgDown = estado;
    
    scrollIndexUp = 0; scrollIndexDown = 0;
  }
  // --- MODO TEXTO ---
  else {
    mostrarPrefix = true; 

    currentMsgUp = paq; 
    String mensajeClaro = "";
    
    // Desencriptamos
    for (int i=0; i<paq.length(); i++) {
      char c = paq.charAt(i);
      if (c == ' ') {
        mensajeClaro += " ";
      }
      else if (isalpha(c)) {
        moverRotores(); 
        char claro = encriptarLetra(c); 
        mensajeClaro += claro;
      }
    }
    
    currentMsgDown = mensajeClaro; 
    scrollIndexUp = 0; scrollIndexDown = 0;
  }
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

    String finalUp, finalDown;
    
    // LÓGICA DE PREFIJOS RECEPTOR
    if (mostrarPrefix) {
      finalUp = "RX: " + currentMsgUp;     // Cifrado recibido
      finalDown = "MS: " + currentMsgDown; // Mensaje (Message) Claro
    } else {
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
