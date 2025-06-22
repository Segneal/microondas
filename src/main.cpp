#include <EEPROM.h>
#include <Keypad.h>
#include <Adafruit_NeoPixel.h>
#include <LiquidCrystal_I2C.h>

//============PROTOTIPOS DE FUNCIONES===========
// Acá están todas las declaraciones de funciones que vamos a usar después
void handleCurrentState(char key);  // Maneja el estado actual del microondas
void handleOffState();  // Estado apagado (no implementado)
void handleWaitingState(char key);  // Estado de espera (standby)
void handleConfiguringState(char key);  // Estado de configuración
void handleCookingState();  // Estado de cocción activa
void handlePausedState();  // Estado pausado
void handleFinishedState();  // Estado cuando termina el programa
void showInitialScreen();  // Muestra pantalla inicial
void saveUserProgram();  // Guarda programa en EEPROM (no implementado)
void resetConfiguration();  // Resetea la configuración
void startCookingProgram(int index,int cook, int cool, int reps);  // Inicia un programa
void loadProgramsFromEEPROM();  // Carga programas de la memoria
void resetAfterCooking();  // Resetea después de cocinar
void checkCancel(char key);  // Chequea si se cancela la operación
void updateInteriorLight();  // Controla la luz interna
void handleDoorOpenState();  // Maneja cuando la puerta está abierta
void updatePlatePattern();  // Actualiza los patrones del anillo de LEDs
void updateRotatingPattern();  // Patrón giratorio para cocción
void updateBlinkingPattern();  // Patrón de parpadeo para enfriamiento
void updateBuzzer();  // Controla el buzzer
void handleFinishedBeep();  // Sonido de finalización

//========== LCD DISPLAY ===========
// Pantalla LCD con interfaz I2C (dirección 0x27, 16 columnas x 2 filas)
LiquidCrystal_I2C lcd(0x27, 16, 2);

//========== LUZ INTERNA ==========
const byte lightPin = 4;  // Pin para controlar la luz del microondas

//========== TECLADO ==========
// Configuración del keypad matricial 4x4
const int ROWS = 4;
const int COLS = 4;
byte rowPins[ROWS] = {13, 12, 11, 10};  // Pines de filas
byte colPins[COLS] = {9, 8, 7, 6};  // Pines de columnas

// Mapeo de teclas
char keys[ROWS][COLS] = {
  {'1','2','3','A'},  // Fila 1
  {'4','5','6','B'},  // Fila 2
  {'7','8','9','C'},  // Fila 3
  {'*','0','#','D'}   // Fila 4
};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

//========== BUZZER ==========
const int buzzerPin = 2;  // Pin del buzzer

//========== ANILLO NEOPIXEL ==========
const byte ringPin = A5;  // Pin del anillo de LEDs
const int numPixels = 16;  // Cantidad de LEDs en el anillo
Adafruit_NeoPixel ring = Adafruit_NeoPixel(numPixels, ringPin, NEO_GRB + NEO_KHZ800);

//========== SENSOR DE PUERTA ==========
const byte doorPin = A1;  // HIGH = puerta cerrada, LOW = puerta abierta

//========== ENUMS (ENUMERACIONES) ==========
// Estados posibles del microondas
enum MicrowaveState {
  WAITING,      // Esperando instrucciones
  CONFIGURING,  // Configurando programa
  COOKING,      // En proceso de cocción
  PAUSED,       // Pausado (puerta abierta)
  FINISHED,     // Programa completado
  DOOR_OPEN     // Puerta abierta
};

//========== ESTADO DE CONFIGURACIÓN ==========
// Pasos para configurar un programa
enum ConfigStep {
  SET_COOK_TIME,    // Setear tiempo de cocción
  SET_COOL_TIME,    // Setear tiempo de enfriamiento
  SET_REPETITIONS,  // Setear repeticiones
  CONFIG_DONE       // Configuración lista
};

// ======== ESTRUCTURA PARA EEPROM ============
// Estructura para guardar programas en la memoria
struct CookingProgram {
  const char* label;       // Nombre del programa
  int cookTime;            // Tiempo de cocción en segundos
  int coolTime;            // Tiempo de enfriamiento en segundos
  int repetitions;         // Cantidad de repeticiones
};

struct ProgramData {
  int cookTime;
  int coolTime;
  int repetitions;
};

// Array con los 4 programas (A, B, C, D)
CookingProgram cookingPrograms[4];

//=============ESTADO DE COCCIÓN ===============
// Variables globales para el estado de cocción
int currentCookTime = 0;        // Tiempo actual de cocción
int currentCoolTime = 0;        // Tiempo actual de enfriamiento
int currentRepetitions = 0;     // Repeticiones actuales
int currentStep = 0;            // 0 = cocinando, 1 = enfriando
unsigned long lastTimerUpdate = 0;  // Última actualización del timer
const unsigned long timerInterval = 1000; // Intervalo de 1 segundo
int currentProgramIndex = -1;   // Índice del programa actual (-1 = ninguno)

//========== ESTADO GLOBAL ==========
MicrowaveState currentState = WAITING;  // Estado actual
MicrowaveState prevState = WAITING;     // Estado previo (para volver)

// Variables de configuración
int cookTime = 0;          // Tiempo de cocción configurado
int coolTime = 0;          // Tiempo de enfriamiento configurado
int repetitions = 1;       // Repeticiones configuradas
ConfigStep configStep = SET_COOK_TIME;  // Paso actual de configuración
bool configFirstTime = true;  // Flag para primer ingreso a estado
String configInput = "";      // Input del usuario durante configuración
bool programReady = false;    // Flag de programa listo
bool screenInitialized = false;  // Flag de pantalla inicializada

// Variables para controlar el anillo de LEDs
unsigned long lastRingUpdate = 0;       // Última actualización del anillo
const unsigned long ringUpdateInterval = 200; // Intervalo de actualización (ms)
int currentLedPosition = 0;            // Posición actual en patrón giratorio
bool blinkState = false;               // Estado del parpadeo
unsigned long lastBlinkUpdate = 0;     // Último cambio de parpadeo
const unsigned long blinkInterval = 500; // Intervalo de parpadeo (ms)

//========== CONTROL DEL BUZZER ==========
bool buzzerActive = false;             // Estado del buzzer
const int HEATING_TONE = 300;          // Frecuencia para calentamiento
const int COOLING_TONE = 600;          // Frecuencia para enfriamiento
bool finishBeepDone = false;           // Flag de beep finalizado
int beepCounter = 0;                   // Contador de beeps
unsigned long lastBeepTime = 0;        // Último beep

// Variables para manejo de fases
unsigned long phaseStartTime;      // Inicio de fase actual
bool phaseSoundEnabled = false;    // Control de retardo inicial
//========== FUNCIONES PARA EEPROM ==========
// Guarda los programas en la EEPROM
void saveToEEPROM() {
  for (int i = 0; i < 4; i++) {
    ProgramData data = {
      cookingPrograms[i].cookTime,
      cookingPrograms[i].coolTime,
      cookingPrograms[i].repetitions
    };
    EEPROM.put(i * sizeof(ProgramData), data);
  }
}

// Guarda los programas por defecto en la EEPROM
void saveDefaultProgramsToEEPROM() {
  cookingPrograms[0] = {"Calentar        ", 30, 0 , 1};  // Programa A
  cookingPrograms[1] = {"Descongelar     ", 20, 10, 5};  // Programa B
  cookingPrograms[2] = {"Recalentar      ", 15, 3, 3};   // Programa C
  cookingPrograms[3] = {"Personalizado   ", cookTime, coolTime, repetitions}; // Programa D
  saveToEEPROM();
}

// Carga los programas desde la EEPROM
void loadFromEEPROM() {
  const char* labels[4] = {
    "Calentar        ",  // Etiqueta programa A
    "Descongelar     ",  // Etiqueta programa B
    "Recalentar      ",  // Etiqueta programa C
    "Personalizado   "   // Etiqueta programa D
  };

  for (int i = 0; i < 4; i++) {
    ProgramData data;
    EEPROM.get(i * sizeof(ProgramData), data);

    cookingPrograms[i].label = labels[i];
    cookingPrograms[i].cookTime = data.cookTime;
    cookingPrograms[i].coolTime = data.coolTime;
    cookingPrograms[i].repetitions = data.repetitions;
  }
}

//========== SETUP ==========
void setup() {
  Serial.begin(9600);  // Inicia comunicación serial
  lcd.begin(16, 2);    // Inicia LCD
  pinMode(doorPin, INPUT);  // Configura pin de puerta como entrada
  pinMode(lightPin, OUTPUT);  // Configura pin de luz como salida
  pinMode(buzzerPin, OUTPUT);  // Configura pin de buzzer como salida
  ring.begin();       // Inicia anillo de LEDs
  ring.show();        // Muestra estado inicial (apagado)
  
  // Simulación de datos en EEPROM
  saveDefaultProgramsToEEPROM();  // Guarda programas por defecto
  loadFromEEPROM();               // Carga programas desde EEPROM
}

//========== LOOP PRINCIPAL ==========
void loop() {
  // Primero verificamos el estado de la puerta
  bool doorClosed = digitalRead(doorPin) == HIGH;
  
  // Lógica para manejar cambios de estado por apertura/cierre de puerta
  if (!doorClosed && currentState != DOOR_OPEN) {
    prevState = currentState;    // Guarda estado actual antes de cambiar
    currentState = DOOR_OPEN;    // Cambia a estado puerta abierta
    screenInitialized = false;   // Fuerza refresco de pantalla
  }
  else if (doorClosed && currentState == DOOR_OPEN) {
    currentState = prevState;    // Vuelve al estado anterior
    screenInitialized = false;   // Fuerza refresco de pantalla
  }
  
  // Obtiene tecla presionada y maneja estado actual
  char key = keypad.getKey();
  handleCurrentState(key);
  checkCancel(key);            // Verifica si se canceló la operación
  updateInteriorLight();       // Actualiza luz interna
  updatePlatePattern();        // Actualiza patrones del anillo
  updateBuzzer();             // Actualiza estado del buzzer
}

//========== MANEJO DE ESTADOS ==========
void handleCurrentState(char key) {
  // Inicializa pantalla si no está inicializada
  if (!screenInitialized) {
    showInitialScreen();
    screenInitialized = true;
  }
  
  // Máquina de estados principal
  switch(currentState) {
    case WAITING:
      handleWaitingState(key);    // Estado de espera
      break;
    case CONFIGURING:
      handleConfiguringState(key); // Estado de configuración
      break;
    case COOKING:
      handleCookingState();      // Estado de cocción
      break;
    case PAUSED:
      handlePausedState();       // Estado pausado
      break;
    case FINISHED:
      handleFinishedState();     // Estado finalizado
      break;
    case DOOR_OPEN:
      handleDoorOpenState();     // Estado puerta abierta
  }
}

//========== MANEJADORES DE ESTADOS ==========
// Maneja el estado de espera (standby)
void handleWaitingState(char key) {
  if (key == NO_KEY) return;  // Si no hay tecla, no hace nada
  
  // Lógica para teclas especiales
  if (key == '#') {
    // Tecla # entra en modo configuración
    prevState = currentState;
    currentState = CONFIGURING;
  } else if (key >= 'A' && key <= 'D') {
    // Teclas A-D inician programas predefinidos
    int index = key - 'A';  // Convierte a índice (0-3)
    startCookingProgram(
      index,
      cookingPrograms[index].cookTime,
      cookingPrograms[index].coolTime,
      cookingPrograms[index].repetitions
    );
  }
  else if (key >= '1' && key <= '9') {
    // Teclas 1-9 inician cocción rápida
    int cookSeconds = key - '0';  // Convierte char a int
    currentProgramIndex = -1;     // Indica que es programa de usuario
    startCookingProgram(-1, cookSeconds, 0, 1);
  }
}

// Maneja estado finalizado (placeholder)
void handleFinishedState(){}

// Maneja estado de configuración
void handleConfiguringState(char key) {
  // Inicialización de pantalla para cada paso
  if (configFirstTime) {
    lcd.clear();
    switch (configStep) {
      case SET_COOK_TIME:
        lcd.print("Tiemp de Cocc:  ");  // Tiempo de cocción
        break;
      case SET_COOL_TIME:
        lcd.print("Tiempo standby:");   // Tiempo de enfriamiento
        break;
      case SET_REPETITIONS:
        lcd.print("Num repeticion: ");  // Número de repeticiones
        break;
      case CONFIG_DONE:
        lcd.clear();
        lcd.print("Programa listo  ");  // Programa listo
        lcd.setCursor(0,1);
        lcd.print("# para guardar  ");  // Instrucción para guardar
        break;
    }
    lcd.setCursor(0, 1);
    configInput = "";
    configFirstTime = false;
  }

  // Procesamiento de teclas durante configuración
  if (key) {
    if (key >= '0' && key <= '9') {
      // Teclas numéricas (0-9)
      if (configInput.length() < 4) {
        configInput += key;
        lcd.setCursor(2, 1);
        lcd.print("-> " +configInput + " seg");
      }
    } else if (key == '#') {
      // Tecla # para confirmar paso
      if (configStep != CONFIG_DONE) {
        if (configInput.length() == 0) {
          // Validación: input vacío
          lcd.setCursor(0, 1);
          lcd.print("Enter a value   ");
          delay(1000);
          configFirstTime = true;
          return;
        }

        int value = configInput.toInt();

        // Validación adicional para tiempo de cocción
        if (configStep == SET_COOK_TIME && value <= 0) {
          lcd.setCursor(0, 1);
          lcd.print("Debe mas que 0  ");
          delay(1000);
          configFirstTime = true;
          return;
        }

        // Asignación de valores según paso actual
        switch(configStep) {
          case SET_COOK_TIME:
            cookTime = value;
            cookingPrograms[3].cookTime = cookTime;
            break;
          case SET_COOL_TIME:
            coolTime = value;
            cookingPrograms[3].coolTime = coolTime;
            break;
          case SET_REPETITIONS:
            repetitions = max(1, value);
            cookingPrograms[3].repetitions = repetitions;
            break;
          case CONFIG_DONE:
            break;
        }
        
        // Avanza al siguiente paso
        configStep = static_cast<ConfigStep>(configStep + 1);
        configFirstTime = true;
      } else {
        // Finaliza configuración
        lcd.clear();
        lcd.setCursor(0,1);
        lcd.print("Cancelado       ");
        resetConfiguration();
      }
    } 
  }
}

// Maneja estado de cocción activa
void handleCookingState() {
  bool doorClosed = digitalRead(doorPin) == HIGH;

  // Verificación de puerta abierta durante cocción
  if (!doorClosed) {
    prevState = currentState;
    currentState = PAUSED;  // Pausa si la puerta está abierta
    lcd.clear();
    lcd.print("Pausado p.abiert");
    return;
  }

  // Lógica de temporización
  unsigned long now = millis();

  if (now - lastTimerUpdate >= timerInterval) {
    lastTimerUpdate = now;

    // Muestra nombre del programa en LCD
    lcd.setCursor(0, 0);
    if (currentProgramIndex >= 0) {
      lcd.print(cookingPrograms[currentProgramIndex].label);
    } else {
      lcd.print("Coccion Rapida ");
    }

    // Manejo de tiempos según fase (cocción/enfriamiento)
    if (currentStep == 0) {  // Fase de cocción
      if (currentCookTime >= 0) {
        lcd.setCursor(0, 1);
        lcd.print("Calentando:");
        lcd.print(currentCookTime);
        lcd.print(" s  ");
        currentCookTime--;
      } else {
        // Transición a enfriamiento o repetición
        if (currentCoolTime > 0) {
          currentStep = 1;  // Pasa a fase de enfriamiento
        } else {
          currentRepetitions--;
          if (currentRepetitions > 0) {
            // Reinicia para nueva repetición
            currentCookTime = cookTime;
            currentCoolTime = coolTime;
            currentStep = 0;
          } else {
            // Programa completado
            prevState = currentState;
            currentState = FINISHED;
            lcd.clear();
            lcd.print("Completado      "); 
            handleFinishedBeep();
            delay(1000);
            resetAfterCooking();
            return;
          }
        }
      }
    } else if (currentStep == 1) {  // Fase de enfriamiento
      if (currentCoolTime >= 0) {
        lcd.setCursor(0, 1);
        lcd.print("Esperando: ");
        lcd.print(currentCoolTime);
        lcd.print(" s  ");
        currentCoolTime--;
      } else {
        currentRepetitions--;
        if (currentRepetitions > 0) {
          // Reinicia para nueva repetición
          currentCookTime = cookTime;
          currentCoolTime = coolTime;
          currentStep = 0;
        } else {
          // Programa completado
          prevState = currentState;
          currentState = FINISHED;
          lcd.clear();
          lcd.print("Terminado!      ");
          handleFinishedBeep();
          delay(1000);
          resetAfterCooking();
          return;
        }
      }
    }
  }
}

// Maneja estado pausado
void handlePausedState() {
  bool doorClosed = digitalRead(doorPin) == HIGH;

  // Si se cierra la puerta, reanuda la cocción
  if (doorClosed) {
    prevState = currentState;
    currentState = COOKING;
    lcd.clear();
    lcd.print("Reanudando");
    delay(1000);
    lastTimerUpdate = millis();  // Actualiza timer sin reiniciar valores
  }
}

//========== FUNCIONES UTILITARIAS ==========
// Resetea la configuración a valores por defecto
void resetConfiguration() {
  cookTime = 0;
  coolTime = 0;
  repetitions = 1;
  configInput = "";
  configStep = SET_COOK_TIME;
  configFirstTime = true;
  programReady = false;
  screenInitialized = false;
  prevState = currentState;
  currentState = WAITING;
}

// Muestra pantalla inicial con opciones
void showInitialScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("A:Calen B:Descon");  // Programas A y B
  lcd.setCursor(0, 1);
  lcd.print("C:Recal D:Person");  // Programas C y D
}

// (Función no implementada actualmente)
void saveUserProgram() {
  // Futuro: guardar en EEPROM
}

// Inicia un programa de cocción
void startCookingProgram(int programIndex, int cook, int cool, int reps) {
  currentProgramIndex = programIndex;
  cookTime = cook;
  coolTime = cool;
  repetitions = max(1, reps);
  currentCookTime = cookTime;
  currentCoolTime = coolTime;
  currentRepetitions = repetitions;
  currentStep = 0;
  lastTimerUpdate = millis();
  prevState = currentState;
  currentState = COOKING;
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("   Comenzando   ");
  finishBeepDone = false;
}

// (Función no utilizada actualmente)
void loadProgramsFromEEPROM() {
  cookingPrograms[0] = {"Calentar        ", 2, 2, 2};
  cookingPrograms[1] = {"Descongelar     ", 20, 10, 1};
  cookingPrograms[2] = {"Recalentar      ", 3, 5, 5};
  cookingPrograms[3] = {"Personalizado   ", cookTime, coolTime, repetitions};
}

// Resetea el sistema después de cocinar
void resetAfterCooking() {
  cookTime = 0;
  coolTime = 0;
  repetitions = 1;
  configStep = SET_COOK_TIME;
  configFirstTime = true;
  programReady = false;
  screenInitialized = false;
  prevState = WAITING;
  currentState = WAITING;
  noTone(buzzerPin);
  buzzerActive = false;
  finishBeepDone = false;
  buzzerActive = false;
  beepCounter = 0;
  lastBeepTime = millis();
}

// Verifica si se presionó la tecla de cancelar (*)
void checkCancel(char key) {
  if (key == '*') {
    if (currentState == CONFIGURING || currentState == COOKING || currentState == PAUSED) {
      lcd.clear();
      lcd.print("Cancelado       ");
      delay(1000);
      prevState = WAITING;
      currentState = WAITING;
      screenInitialized = false;
      resetAfterCooking();
    }
  }
}

// Actualiza la luz interior según estado
void updateInteriorLight() {
  bool doorOpen = digitalRead(doorPin) == LOW;  // True si puerta abierta
  bool cooking = (currentState == COOKING);     // True si está cocinando

  // Luz se enciende si puerta abierta o durante cocción
  if (doorOpen || cooking) {
    digitalWrite(lightPin, HIGH);
  } else {
    digitalWrite(lightPin, LOW);
  }
}

// Maneja estado de puerta abierta
void handleDoorOpenState() {
  digitalWrite(lightPin, HIGH);  // Enciende luz siempre que la puerta está abierta

  // Muestra mensaje en LCD
  lcd.setCursor(0, 0);
  lcd.print("Cierre la puerta");

  lcd.setCursor(0, 1);
  if (currentState == PAUSED || currentState == COOKING) {
    lcd.print("Para continuar  ");  // Mensaje si estaba en proceso
  } else {
    lcd.print("Para iniciar    ");  // Mensaje si estaba en espera
  }
}

// Variables para patrón giratorio
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 100;  // Intervalo de actualización
int headIndex = 0;  // Posición principal en el patrón
const int tailSize = 3;  // Tamaño de la cola del patrón

// Actualiza patrón giratorio en el anillo de LEDs
void updateRotatingPattern() {
  unsigned long now = millis();
  if (now - lastUpdate >= updateInterval) {
    lastUpdate = now;

    ring.clear();  // Apaga todos los LEDs

    // Dibuja la cola del patrón
    for (int i = 0; i < tailSize; i++) {
      int index = (headIndex - i + ring.numPixels()) % ring.numPixels();
      
      // Calcula brillo con atenuación
      int brightness = 255 - (i * 80); // Brillo decreciente
      brightness = max(0, brightness);
      
      ring.setPixelColor(index, ring.Color(brightness, brightness, brightness));
    }

    ring.show();  // Actualiza LEDs

    // Avanza la posición principal
    headIndex = (headIndex + 1) % ring.numPixels();
  }
}

// Función principal para manejar patrones del anillo
void updatePlatePattern() {
  ring.clear();  // Apaga todos los LEDs
  bool doorClosed = digitalRead(doorPin) == HIGH;
  
  // Si puerta abierta, todos los LEDs en blanco
  if (!doorClosed) {
    for(uint16_t i=0; i<ring.numPixels(); i++) {
      ring.setPixelColor(i, ring.Color(255, 255, 255));
    }
    ring.show();
    return;
  }
  
  // Durante cocción, muestra patrones según fase
  if (currentState == COOKING) {
    if (currentStep == 0) {
      updateRotatingPattern();  // Patrón giratorio para calentamiento
    } else if (currentStep == 1) {
      updateBlinkingPattern();  // Patrón de parpadeo para enfriamiento
    }
  } 
}

// Variables para patrón de parpadeo
bool ledsOn = false;
unsigned long previousMillis = 0;
const unsigned long onDuration = 500;   // Tiempo encendido
const unsigned long offDuration = 250;  // Tiempo apagado

// Actualiza patrón de parpadeo para fase de enfriamiento
void updateBlinkingPattern() {
  unsigned long currentMillis = millis();
  
  if (ledsOn) {
    // Apaga después de tiempo encendido
    if (currentMillis - previousMillis >= onDuration) {
      ledsOn = false;
      previousMillis = currentMillis;
      ring.clear();
      ring.show();
    }
  } else {
    // Enciende después de tiempo apagado
    if (currentMillis - previousMillis >= offDuration) {
      ledsOn = true;
      previousMillis = currentMillis;
      for (unsigned int i = 0; i < ring.numPixels(); i++) {
        ring.setPixelColor(i, ring.Color(255, 255, 255));  // Blanco
      }
      ring.show();
    }
  }
}


// Actualiza estado del buzzer según fase
void updateBuzzer() {
  unsigned long currentMillis = millis();

  // Silencia si la puerta está abierta
  if (digitalRead(doorPin) == LOW) {
    noTone(buzzerPin);
    phaseSoundEnabled = false;
    return;
  }

  // Maneja cambios de fase
  static int lastStep = -1;
  if (currentStep != lastStep) {
    phaseStartTime = currentMillis;
    phaseSoundEnabled = false;
    lastStep = currentStep;
  }

  switch (currentState) {
    case COOKING:
      // Habilita sonido después de 1 segundo
      if (!phaseSoundEnabled && (currentMillis - phaseStartTime >= 1000)) {
        phaseSoundEnabled = true;
      }

      // Sonido continuo durante la fase
      if (phaseSoundEnabled) {
        if (currentStep == 0) { // Tono de Calentamiento
          tone(buzzerPin, HEATING_TONE);
        } else { //Tono de Enfriamiento
          tone(buzzerPin, COOLING_TONE);
        }
      } else {
        noTone(buzzerPin);
      }
      
      beepCounter = 0;  // Resetea contador de beeps
      break;
    default:
      noTone(buzzerPin);  // Silencia en otros estados
      break;
  }
}

// Reproduce sonido de finalización (3 beeps)
void handleFinishedBeep() {
  const int FINISH_BEEP_FREQ = 2000;  // Frecuencia aguda
  const int FINISH_BEEP_DURATION = 100;  // Duración corta
  const int FINISH_PAUSE_DURATION = 500; // Pausa entre beeps

  for (int i = 0; i < 3; i++) {
    tone(buzzerPin, FINISH_BEEP_FREQ);
    delay(FINISH_BEEP_DURATION);
    noTone(buzzerPin);
    delay(FINISH_PAUSE_DURATION);
  }
}