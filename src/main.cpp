// ======================================== COCKTAIL AUTOMATEN SYSTEM ========================================
// 
// Dieses Programm kontrolliert einen automatisierten Cocktail-Mischer mit folgenden Funktionen:
// - Rezeptverwaltung (Laden aus Datei oder manuell)
// - Automatische Flüssigkeitsdosierung nach Gewicht (Wägezelle)
// - Benutzerinterface über LCD Display und Rotary Encoder
// - Motorsteuerung für Pumpen
// - Mehrere Betriebsmodi: Rezepte, manuelle Eingabe, Flüssigkeitskalibrierung, Reinigung
//
// Hardware:
// - ESP32 Microcontroller
// - 4x20 LCD Display (I2C)
// - HX711 Wägezelle-Verstärker (für genaue Dosierung)
// - Schrittmotor (Nemar 23) für Pumpenantrieb Motorstrom 1,8A, 1,8° Schrittwinkel
// - TB6600 4 A 9-42 V Schrittmotorsteuerung 2A, 2,2A Spitze, 24V, 800 Schritte/Umdrehung 
// - 10 Relays (9x Flüssigkeiten, 1x Luft zum Entlüften)
// - Rotary Encoder (mit 1 Button) für Benutzernavigation
//
// ================================================ INCLUDES ================================================

#include <Arduino.h>           // Arduino Basis-Library
#include "HX711.h"             // Wägezelle-Verstärker Library
#include <TimerOne.h>          // Timer-Library für periodische Aufgaben
#include <Wire.h>              // I2C Kommunikations-Library
#include <LiquidCrystal_I2C.h> // LCD Display Library (I2C Modus)
#include <WiFi.h>              // ESP32 WiFi
#include <PubSubClient.h>      // MQTT Client
#include <ArduinoJson.h>       // JSON Serialisierung / Parsing
#include <LittleFS.h>          // Flash-Dateisystem für Web-Dateien
#include <HTTPClient.h>        // HTTP GET für Online-Rezepte
#include <ESPAsyncWebServer.h> // Async Web-Server für LittleFS



// ================================================ LCD DISPLAY ================================================
// Initialisiert ein 4x20 LCD Display über I2C Schnittstelle
// Häufige Adressen: 0x27, 0x3F, 0x20, 0x3B - versuche diese wenn nichts angezeigt wird
const uint8_t LCD_I2C_ADDRESS  = 0x27;  // I2C Adresse des LCD (0x27 oder 0x3F)
const int     LCD_COLUMNS      = 20;    // Anzahl Spalten
const int     LCD_ROWS         = 4;     // Anzahl Zeilen
const int     LCD_SDA_PIN      = 8;     // SDA Pin (GPIO 8)
const int     LCD_SCL_PIN      = 9;     // SCL Pin (GPIO 9)
const long    LCD_I2C_CLOCK_HZ = 100000; // I2C Taktfrequenz in Hz

LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);



// ================================================ WÄGEZELLE HX711 ================================================
// Die Wägezelle misst das Gewicht der Flüssigkeit für genaue Dosierung
// Sie verwendet 2 digitale Pins: DT (Data) und SCK (Serial Clock)
  
const int LOADCELL_DT_PIN = 1;    // Daten-Pin der Wägezelle (GPIO 1)
const int LOADCELL_SCK_PIN = 2;   // Takt-Pin der Wägezelle (GPIO 2)

HX711 scale;                      // Instanz des HX711 Wägezelle-Verstärkers
double calibration_factor = 408;  // Kalibrierungsfaktor (muss experimentell bestimmt werden)
double current_weight = 0;        // Aktuelles Gewicht in Gramm/ml



// ================================================ SCHRITTMOTOR (NEMAR 23) ================================================
// Der Schrittmotor pumpt die Flüssigkeiten durch Peristaltik-Pumpen
// Er wird mit Step/Direction Signalen gesteuert

const int STEPPER_STEP_PIN = 5;       // Pin für Step-Pulse (GPIO 5)
const int STEPPER_DIR_PIN = 6;        // Pin für Drehrichtung (GPIO 6)
const int STEPPER_ENABLE_PIN = 7;     // Pin zum Aktivieren des Motors (GPIO 7)
const int STEPS_PER_REVOLUTION = 800; // Schritte pro komplette Drehung

// Geschwindigkeiten in Mikrosekunden (µs) - niedrigere Werte = schneller
// Die Zeit gibt an, wie lange zwischen Step-Pulsen gewartet wird
// 150 µs pro Schritt + 800 Schritte pro Umdrehung = 240.000 µs pro Umdrehung = 0,52 Umdrehungen pro Sekunde (schnell)
// 400 µs pro Schritt + 800 Schritte pro Umdrehung = 640.000 µs pro Umdrehung = 0,19 Umdrehungen pro Sekunde (langsam/genau)
const int NORMAL_SPEED = 150;     // Normale Geschwindigkeit in µs
const int SLOW_SPEED = 400;       // Langsame Geschwindigkeit in µs (genauer)
const int CLEANING_DURATION_SECONDS = 10;  // Wie lange der Motor pro Relay beim Reinigen läuft (Sekunden)
const int CLEANING_CYCLES = 3;             // Anzahl der Reinigungszyklen gesamt
const double WEIGHT_TOLERANCE = 5.0;       // Toleranz in Gramm: Pumpe stoppt WEIGHT_TOLERANCE g vor Ziel (verhindert Überlaufen durch Sensorverzögerung)

// ================================================ MOTOR ANLAUFRAMPE ================================================
const int  MOTOR_START_SPEED = 800;  // Anlaufgeschwindigkeit µs (langsam = hohes Drehmoment beim Start)
const long MOTOR_ACCEL_STEPS = 400;  // Schritte bis Zielgeschwindigkeit erreicht ist

// ================================================ PUMPEN-EINSTELLUNGEN ================================================
const int    PUMP_FAST_BLOCK_STEPS   = 4000; // Schritte pro Block in Phase 1 (5 Umdrehungen à 800 = 4000)
const int    PUMP_SLOW_BLOCK_STEPS   = 50;   // Schritte pro Block in Phase 2 (langsam, waagegesteuert)
const int    PUMP_PURGE_BLOCK_STEPS  = 50;   // Schritte pro Block in Phase 3 (Luft)
const int    PUMP_FAST_SAFETY_FACTOR = 3;    // Sicherheitsmultiplikator: max. 3x berechnete Schritte in Phase 1
const int    PUMP_SLOW_SAFETY_EXTRA  = 100;  // Zusätzliche Blöcke Sicherheitspuffer in Phase 2
const int    PUMP_EMI_DELAY_MS       = 150;  // Pause nach Motorlauf (ms) - EMI abklingen lassen vor Waagemessung
const int    PUMP_SLOW_DELAY_MS      = 30;   // Pause zwischen Phase-2/3-Blöcken (ms)
const double PUMP_SCALE_NOISE_FLOOR  = -5.0; // Untergrenze für gültige Waagemessung (g) - filtert EMI-Rauschen
const int    INGREDIENT_PAUSE_MS     = 500;  // Pause zwischen zwei Zutaten (Waage stabilisiert sich) in ms
const int    CLEANING_RELAY_PAUSE_MS = 500;  // Pause zwischen Reinigungszyklen in ms

// ================================================ ALLGEMEIN ================================================
const int LOOP_DELAY_MS          = 50;   // Haupt-Loop Verzögerung in ms (Entprellung / Reaktionszeit)
const int SETUP_BOOT_DELAY_MS    = 2000; // Wartezeit am Anfang von setup() damit Serial Monitor verbinden kann
const int SETUP_I2C_SETTLE_MS    = 10;   // Wartezeit nach I2C Pull-up Aktivierung (µs, Leitungen stabilisieren)
const int SETUP_I2C_INIT_MS      = 100;  // Wartezeit nach Wire.begin() bevor LCD angesprochen wird
const int SETUP_COMPONENT_MS     = 1000; // Pause zwischen Initialisierungsschritten in setup()
const int UI_DONE_DISPLAY_MS     = 2000; // Wie lange Abschluss-/Ergebnismeldungen angezeigt werden (ms)
const int UI_SUMMARY_POLL_MS     = 20;   // Polling-Intervall in der Zusammenfassungsanzeige (ms)
const int MANUAL_MOTOR_MAX_REVOLUTIONS = 60;  // Maximale Umdrehungen im manuellen Motormenü
const int GLASS_SIZE_ML[5] = {200, 400, 500, 750, -1};  // Glasgröße in ml: 0=Klein(200), 1=Mittel(400), 2=Normal(500), 3=Groß(750), 4=Original(exakt)

// ================================================ GLASERKENNUNG (WAAGE) ================================================
const double GLASS_JUMP_MIN_G   = 100.0;   // Mindest-Gewichtssprung um Glas zu erkennen (g)
const unsigned long GLASS_STABLE_MS = 3000; // Gewicht muss 3s stabil über dem Sprung bleiben
const double GLASS_REMOVE_HYST_G = 50.0;   // Hysterese: Glas gilt als weg wenn unter (glassWeight - 50g)

// ================================================ WIFI & MQTT ================================================
// WiFi-Zugangsdaten (Handy-Hotspot)
const char* WIFI_SSID       = "Philip";      // ← SSID des Handy-Hotspots anpassen
const char* WIFI_PASSWORD   = "123456789";  // ← Passwort des Handy-Hotspots anpassen
const int   WIFI_TIMEOUT_MS = 10000;  // Max. Wartezeit bei WiFi-Verbindung (10s), dann Offline-Modus

// Web-Server OK (IP: 192.168.31.17)
// Wie komme ich auf die Web-Oberfläche? → IP-Adresse im Browser eingeben (z.B. 192.168.31.17)
// Anderer Esp32 192.168.31.88

// MQTT Broker: HiveMQ öffentlicher Broker (kein Account nötig)
const char* MQTT_BROKER    = "broker.hivemq.com";
const int   MQTT_PORT      = 1883;
// Client-ID muss eindeutig sein – wird in connectToMQTT() mit zufälligem Suffix generiert
const char* MQTT_CLIENT_ID_PREFIX = "cocktailbot_";
const int   MQTT_RECONNECT_INTERVAL_MS = 5000;  // Reconnect-Versuch alle 5s

// MQTT-Topics (publish)
const char* TOPIC_STATUS_WEIGHT   = "cocktailbot/status/weight";
const char* TOPIC_STATUS_RELAYS   = "cocktailbot/status/relays";
const char* TOPIC_STATUS_PUMP     = "cocktailbot/status/pump";
const char* TOPIC_STATUS_FLUIDS   = "cocktailbot/status/fluids";
const char* TOPIC_STATUS_MENU     = "cocktailbot/status/menu";
const char* TOPIC_RECIPES_OFFLINE = "cocktailbot/recipes/offline";
const char* TOPIC_RECIPES_ONLINE  = "cocktailbot/recipes/online";
const char* TOPIC_STATUS_RECIPE_RESULT = "cocktailbot/status/recipe_result";  // Ergebnis nach Rezeptende

// MQTT-Topics (subscribe / commands)
const char* TOPIC_CMD_RECIPE_OFFLINE = "cocktailbot/command/recipe/offline";
const char* TOPIC_CMD_RECIPE_ONLINE  = "cocktailbot/command/recipe/online";
const char* TOPIC_CMD_FLUID_SET      = "cocktailbot/command/fluid/set";
const char* TOPIC_CMD_RELAY          = "cocktailbot/command/relay";
const char* TOPIC_CMD_MOTOR          = "cocktailbot/command/motor";
const char* TOPIC_CMD_CLEANING_START = "cocktailbot/command/cleaning/start";
const char* TOPIC_CMD_CLEANING_STOP    = "cocktailbot/command/cleaning/stop";
const char* TOPIC_CMD_RECIPES_REFRESH  = "cocktailbot/command/recipes/refresh";
const char* TOPIC_CMD_CALIBRATE_FLUID  = "cocktailbot/command/calibrate/fluid";   // {"slot":0,"calibration_ml":10.5,"korrektur":8}
const char* TOPIC_CMD_CALIBRATE_SCALE  = "cocktailbot/command/calibrate/scale";   // {"factor":408}

// URL der GitHub Gist Raw-Datei mit Online-Rezepten (JSON-Array)
// Format: [{"name":"TestCocktail","ingredients":[{"fluid":"Vodka","amount":60}]}]
const char* ONLINE_RECIPES_URL = "https://raw.githubusercontent.com/GerstnerPhilip/Abschlussprojekt_Gerstner/main/recipes.json";

const int MQTT_STATUS_INTERVAL_MS = 2000;  // Wie oft Status-Topics publiziert werden (ms)

// ================================================ RELAYS (VENTILE) ================================================
// 10 Relays steuern die elektromagnetischen Ventile
// Relays 0-8: Flüssigkeitsventile (für 9 verschiedene Flüssigkeiten)
// Relay 9: Luftventil (zum Leeren der Schläuche nach dem Pumpen)
// ESP32-S3 verfügbare Pins: 0-21, 26-48

const int RELAY_PINS[10] = {10, 16, 13, 11, 17, 14, 12, 18, 15 , 42};  // GPIO Pins für die Relays (verfügbar auf ESP32-S3)
const int NUM_RELAYS = 10;  // Gesamtanzahl der Relays

// Relays sind normalerweise LOW (Ventile geschlossen), HIGH = Ventil geöffnet
// Flussigkeit 1 = Relay 1 = GPIO 10 = Violette Leitung
// Flussigkeit 2 = Relay 7 = GPIO 16 = Gelbe Leitung
// Flussigkeit 3 = Relay 4 = GPIO 13 = Graue Leitung
// Flussigkeit 4 = Relay 2 = GPIO 11 = Blaue Leitung
// Flussigkeit 5 = Relay 8 = GPIO 17 = Rote Leitung
// Flussigkeit 6 = Relay 5 = GPIO 14 = Weiße Leitung
// Flussigkeit 7 = Relay 3 = GPIO 12 = Grüne Leitung
// Flussigkeit 8 = Relay 9 = GPIO 18 = Orange Leitung
// Flussigkeit 9 = Relay 6 = GPIO 15 = Schwarze Leitung
// Luftventil = Relay 10 = GPIO 42 = Braune Leitung


// ================================================ ROTARY ENCODER & BUTTONS ================================================
// Der Rotary Encoder ermöglicht Benutzernavigation im Menü
// Mit Drehung navigiert man durch Menüeinträge
// Der Button bestätigt Auswahlen
// Der Back-Button kehrt zum Hauptmenü zurück

const int ENCODER_CLK = 35;   // Clock Pin des Encoders (GPIO 35)
const int ENCODER_DT = 36;    // Data Pin des Encoders (GPIO 36)
const int ENCODER_SW = 37;    // Button des Encoders (GPIO 37)

// ACHTUNG: GPIO 46 auf ESP32-S3 ist ein input-only Pin OHNE internen Pull-up.
// Ein floatender Pin löst Interrupts zufällig aus → auf GPIO 47 geändert (hat internen Pull-up).
// Hardware: Back-Button-Kabel von GPIO 46 auf GPIO 47 umstecken!
const int BACK_BUTTON = 47;   // Separat button Zurück (GPIO 47)

// encoderCounter bleibt volatile (wird in ISR geschrieben)
volatile int encoderCounter = 0;           // Zähler für Encoder-Drehung
// Buttons werden nun per Polling gesetzt (kein ISR mehr) → kein volatile nötig
bool encoderButtonPressed = false;
bool backButtonPressed    = false;
// lastEncoderTime entfernt: millis()-Debouncing nicht mehr nötig (Zustandsmaschine filtert Preller)

// Display-Update Flag (nur Updates bei Eingaben)
volatile boolean displayNeedsUpdate = true;  // True: Display muss aktualisiert werden



// ================================================ FLÜSSIGKEITSVERWALTUNG ================================================

const int calibration_ml = 10;  // Standard-Kalibrierungswert: 10 ml pro 1000 Motorschritte (muss für jede Flüssigkeit individuell kalibriert werden)
const int korrektur_faktor = 10; // Standard-Korrekturfaktor: 10 ml (Menge, die im Schlauch verbleibt und mitgepumpt wird)
const int manual_input_increment = 5; // Schrittgröße bei manueller Eingabe (ml)

// Struct definiert die Eigenschaften jeder Flüssigkeit
struct Fluid 
{
  String name;              // Name der Flüssigkeit (z.B. "Weißer Rum")
  double calibration_ml;    // Durchsatzrate: ml pro 1000 Motorschritte
  int korrektur_faktor;     // Menge in ml, die noch im Schlauch ist (muss leergepumpt werden)
  int amount;               // Aktuelle Menge zum Pumpen in ml (wird vom Rezept oder manuelle Eingabe gesetzt)
};



// ================================================ VERFÜGBARE FLÜSSIGKEITEN ================================================
// Liste aller Flüssigkeiten, die das System kennt
// Diese werden durch Underscores statt Leerzeichen separiert
// Der Benutzer wählt 9 dieser Flüssigkeiten für die automatische Dosierung aus
// Sortierung: Nach Häufigkeit der Verwendung in populären Cocktails
const String FLUID_NAMES[100] = 
{
  // Alkoholische Getränke - Spirituosen (Indizes 0-11)
  // Sortierung nach Häufigkeit: Vodka, Rum, Gin, Tequila sind am populärsten
  "Vodka","Weisser_Rum","Gin","Tequila","Triple_Sec",        // 0-4
  "Brandy","Whiskey","Dunkler_Rum",                          // 5-7
  "Prosecco","Champagner","Bier","Aperol",                   // 8-11

  // Säfte - Zitrusfrüchte & Beeren (Indizes 12-23)
  // Zitrus-Säfte sind in fast jedem Cocktail vorhanden
  "Limettensaft","Zitronensaft","Orangensaft","Cranberrysaft",// 12-15
  "Ananassaft","Grapefruitsaft","Preiselbeersaft","Pfirsichsaft",// 16-19
  "Mangosaft","Maracujasaft","Apfelsaft","Tomatensaft",      // 20-23

  // Sirups & Liköre (Indizes 24-35)
  // Diese geben den Cocktails ihre charakteristischen Geschmäcke
  "Zuckersirup","Grenadinesirup","Kokoslikoer","Minzsirup",  // 24-27
  "Kaffeelikoer","Amaretto","Vanillesirup","Ingwersirup",    // 28-31
  "Erdbeersirup","Himbeersirup","Litschigeist","Maracujasirup",// 32-35

  // Bitters & Sonstige Getränke / Softdrinks (Indizes 36-55)
  // Für Würze, Geschmackstiefen sowie alkoholfreie Mixer
  "Angostura_Bitter","Soda_Wasser","Tonic_Water","Ginger_Ale",// 36-39
  "Cola","Wasser","Bitterzitronen_Limonade","Kamille_Tee",   // 40-43
  "Sprite","Eistee_Zitrone","Eistee_Pfirsich","Kokoswasser", // 44-47
  "Orangenlimonade","Zitronenlimonade","Mineralwasser","Ingwerbier",// 48-51
  "Multivitaminsaft","Pfanta","Gruener_Tee","Hibiskustee" // 52-55
};



// ================================================ FLÜSSIGKEITS-ARRAY ================================================
// Dieses Array speichert die 9 konfigurieren Flüssigkeiten für den Automaten
// Der Benutzer wählt 9 Flüssigkeiten aus FLUID_NAMES aus
// Der Kalibrierungsfaktor (10.0 ml) und Korrekturfaktor (10 ml) sind Standardwerte
// Diese sollten für jede Flüssigkeit einzeln kalibriert werden
Fluid fluids[9] = 
{
  {FLUID_NAMES[41], 10.0, 10, 0},  // Flüssigkeit 1 (Standard: Wasser)
  {FLUID_NAMES[41], 10.0, 10, 0},  // Flüssigkeit 2 (Standard: Wasser)
  {FLUID_NAMES[41], 10.0, 10, 0},  // Flüssigkeit 3 (Standard: Wasser)
  {FLUID_NAMES[1],  10.0, 10, 0},  // Flüssigkeit 4 (Standard: Weisser_Rum)
  {FLUID_NAMES[1],  10.0, 10, 0},  // Flüssigkeit 5 (Standard: Weisser_Rum)
  {FLUID_NAMES[1],  10.0, 10, 0},  // Flüssigkeit 6 (Standard: Weisser_Rum)
  {FLUID_NAMES[1],  10.0, 10, 0},  // Flüssigkeit 7 (Standard: Weisser_Rum)
  {FLUID_NAMES[1],  10.0, 10, 0},  // Flüssigkeit 8 (Standard: Weisser_Rum)
  {FLUID_NAMES[1],  10.0, 10, 0}   // Flüssigkeit 9 (Standard: Weisser_Rum)
};

// ================================================ REZEPTVERWALTUNG ================================================
// Ein Rezept definiert einen Cocktail mit seinen Zutaten und Mengen
struct Recipe 
{
  String name;           // Name des Cocktails (z.B. "Mojito")
  int fluid_index[9];    // Indizes der Flüssigkeiten im Rezept (Referenz zu fluids[] Array)
  int amounts[9];        // Mengen in ml für jede Flüssigkeit
  int fluid_count;       // Wie viele verschiedene Flüssigkeiten enthält dieses Rezept?
};



// ================================================ REZEPTE DEFINIEREN (HARDCODED) ================================================
// Die Rezepte sind direkt im Programm definiert
// Format: Name = Flüssigkeit1_Name menge1, Flüssigkeit2_Name menge2, ...
// FLUID_NAMES Indizes (Alkoholische):   0=Vodka, 1=Weisser_Rum, 2=Gin, 3=Tequila, 4=Triple_Sec,
//                                        5=Brandy, 6=Whiskey, 7=Dunkler_Rum,
//                                        8=Prosecco, 9=Champagner, 10=Bier, 11=Aperol
// FLUID_NAMES Indizes (Säfte):           12=Limettensaft, 13=Zitronensaft, 14=Orangensaft, 15=Cranberrysaft,
//                                        16=Ananassaft, 17=Grapefruitsaft, 18=Preiselbeersaft, 19=Pfirsichsaft,
//                                        20=Mangosaft, 21=Maracujasaft, 22=Apfelsaft, 23=Tomatensaft
// FLUID_NAMES Indizes (Sirups/Liköre):   24=Zuckersirup, 25=Grenadinesirup, 26=Kokoslikoer, 27=Minzsirup,
//                                        28=Kaffeelikoer, 29=Amaretto, 30=Vanillesirup, 31=Ingwersirup,
//                                        32=Erdbeersirup, 33=Himbeersirup, 34=Litschigeist, 35=Maracujasirup
// FLUID_NAMES Indizes (Bitters/Mixer):   36=Angostura_Bitter, 37=Soda_Wasser, 38=Tonic_Water, 39=Ginger_Ale,
//                                        40=Cola, 41=Wasser, 42=Bitterzitronen_Limonade, 43=Kamille_Tee,
//                                        44=Sprite, 45=Eistee_Zitrone, 46=Eistee_Pfirsich, 47=Kokoswasser
// FLUID_NAMES Indizes (Cremig/Milchig):  48=Kokosmilch, 49=Sahne, 50=Kondensmilch, 51=Mandelmilch

Recipe recipes[30] =
{
  {
    "Mojito",
    {1, 12, 27, 37, -1, -1, -1, -1, -1},     // Weisser_Rum, Limettensaft, Minzsirup, Soda_Wasser
    {45, 20, 20, 100, 0, 0, 0, 0, 0},       // Mengen in ml (IBA: 45ml Rum, 20ml Lime)
    4
  },
  {
    "Cosmopolitan",
    {0, 4, 15, 12, -1, -1, -1, -1, -1},      // Vodka, Triple_Sec, Cranberrysaft, Limettensaft
    {40, 15, 30, 15, 0, 0, 0, 0, 0},        // IBA: 40/15/30/15ml
    4
  },
  {
    "Pina Colada",
    {1, 16, 26, -1, -1, -1, -1, -1, -1},    // Weisser_Rum, Ananassaft, Kokoslikoer
    {50, 50, 30, 0, 0, 0, 0, 0, 0},         // IBA: 50/50/30ml (kein Zuckersirup)
    3
  },
  {
    "Margarita",
    {3, 4, 12, -1, -1, -1, -1, -1, -1},      // Tequila, Triple_Sec, Limettensaft
    {50, 20, 15, 0, 0, 0, 0, 0, 0},         // IBA: 50/20/15ml
    3
  },
  {
    "Daiquiri",
    {1, 12, 24, -1, -1, -1, -1, -1, -1},     // Weisser_Rum, Limettensaft, Zuckersirup
    {60, 20, 15, 0, 0, 0, 0, 0, 0},         // IBA: 60/20/15ml
    3
  },
  {
    "Mai Tai",
    {1, 12, 29, 24, -1, -1, -1, -1, -1},     // Weisser_Rum, Limettensaft, Amaretto, Zuckersirup
    {60, 30, 20, 10, 0, 0, 0, 0, 0},
    4
  },
  {
    "Screwdriver",
    {0, 14, -1, -1, -1, -1, -1, -1, -1},    // Vodka, Orangensaft
    {50, 150, 0, 0, 0, 0, 0, 0, 0},
    2
  },
  {
    "Sex on the Beach",
    {0, 19, 14, 15, -1, -1, -1, -1, -1},    // Vodka, Pfirsichsaft, Orangensaft, Cranberrysaft
    {40, 20, 40, 40, 0, 0, 0, 0, 0},        // IBA: 40/20/40/40ml
    4
  },
  {
    "Long Island Iced Tea",
    {1, 0, 2, 3, 4, 13, 24, 40, -1},        // Rum, Vodka, Gin, Tequila, Triple_Sec, Zitronensaft, Zuckersirup, Cola
    {15, 15, 15, 15, 15, 25, 30, 100, 0},   // IBA: 25ml Lemon
    8
  },
  {
    "Tom Collins",
    {2, 13, 24, 37, -1, -1, -1, -1, -1},     // Gin, Zitronensaft, Zuckersirup, Soda_Wasser
    {50, 40, 20, 100, 0, 0, 0, 0, 0},
    4
  },
  {
    "Gimlet",
    {2, 12, 24, -1, -1, -1, -1, -1, -1},     // Gin, Limettensaft, Zuckersirup
    {50, 30, 20, 0, 0, 0, 0, 0, 0},
    3
  },
  {
    "Old Fashioned",
    {6, 24, 36, -1, -1, -1, -1, -1, -1},    // Whiskey, Zuckersirup, Angostura_Bitter
    {50, 10, 5, 0, 0, 0, 0, 0, 0},
    3
  },
  {
    "Espresso Martini",
    {0, 28, 24, -1, -1, -1, -1, -1, -1},    // Vodka, Kaffeelikoer, Zuckersirup
    {50, 30, 10, 0, 0, 0, 0, 0, 0},
    3
  },
  {
    "Caipirinha",
    {1, 12, 24, -1, -1, -1, -1, -1, -1},     // Weisser_Rum, Limettensaft, Zuckersirup
    {60, 30, 20, 0, 0, 0, 0, 0, 0},
    3
  },
  {
    "Hurricane",
    {1, 12, 14, 24, -1, -1, -1, -1, -1},     // Weisser_Rum, Limettensaft, Orangensaft, Zuckersirup
    {70, 30, 30, 20, 0, 0, 0, 0, 0},
    4
  },
  {
    "Dark & Stormy",
    {7, 39, -1, -1, -1, -1, -1, -1, -1},    // Dunkler_Rum, Ginger_Ale
    {60, 120, 0, 0, 0, 0, 0, 0, 0},         // IBA: 60ml Rum + 120ml Ginger Beer
    2
  },

  // Neue Rezepte
  {
    "Tequila Sunrise",
    {3, 14, 25, -1, -1, -1, -1, -1, -1},    // Tequila, Orangensaft, Grenadinesirup
    {45, 90, 15, 0, 0, 0, 0, 0, 0},         // IBA: 45/90/15ml
    3
  },
  {
    "Moscow Mule",
    {0, 39, 12, -1, -1, -1, -1, -1, -1},     // Vodka, Ginger_Ale, Limettensaft
    {45, 120, 10, 0, 0, 0, 0, 0, 0},        // IBA: 45/120/10ml
    3
  },
  {
    "Whiskey Sour",
    {6, 13, 24, -1, -1, -1, -1, -1, -1},     // Whiskey, Zitronensaft, Zuckersirup
    {45, 25, 20, 0, 0, 0, 0, 0, 0},         // IBA: 45/25/20ml
    3
  },
  {
    "Gin Tonic",
    {2, 38, -1, -1, -1, -1, -1, -1, -1},    // Gin, Tonic_Water
    {50, 150, 0, 0, 0, 0, 0, 0, 0},
    2
  },
  {
    "Paloma",
    {3, 17, 12, 37, -1, -1, -1, -1, -1},     // Tequila, Grapefruitsaft, Limettensaft, Soda_Wasser
    {50, 100, 15, 30, 0, 0, 0, 0, 0},
    4
  },
  {
    "Amaretto Sour",
    {29, 13, 24, -1, -1, -1, -1, -1, -1},    // Amaretto, Zitronensaft, Zuckersirup
    {50, 25, 10, 0, 0, 0, 0, 0, 0},
    3
  },
  {
    "Sidecar",
    {5, 4, 13, -1, -1, -1, -1, -1, -1},      // Brandy, Triple_Sec, Zitronensaft
    {50, 20, 20, 0, 0, 0, 0, 0, 0},         // IBA: 50/20/20ml
    3
  },

  // Test-Rezepte (können gelöscht werden)
  {
    "Test Cocktail 1",
    {41, -1, -1, -1, -1, -1, -1, -1, -1},
    {100, 0, 0, 0, 0, 0, 0, 0, 0},
    1
  },
  {
    "Test Cocktail 2",
    {41, 41, -1, -1, -1, -1, -1, -1, -1},
    {100, 100, 0, 0, 0, 0, 0, 0, 0},
    2
  },
  {
    "Test Cocktail 3",
    {41, 41, 41, -1, -1, -1, -1, -1, -1},
    {50, 50, 50, 0, 0, 0, 0, 0, 0},
    3
   }
};

// Aktuelle Anzahl der Rezepte
int recipeCount = 26;  // 16 korrigierte + 7 neue + 3 Test-Rezepte



// ================================================ MENÜSTATUS UND MENÜZUSTÄNDE ================================================
// Das System funktioniert nach einem Menü-basierten Zustandsautomat (State Machine)
// In jedem Zustand wird ein anderes Menü angezeigt und andere Befehle werden akzeptiert

enum MenuState 
{
  MAIN_MENU,                     // Startmenü mit 4 Optionen: Rezepte, Manuelle Eingabe, Flüssigkeiten, Reinigung
  RECIPES_TYPE_MENU,             // Auswahl: "Offline Rezepte" oder "Online Rezepte"
  RECIPES_MENU,                  // Offline-Rezepte auswählen und anzeigen
  RECIPE_DETAILS,                // Details des gewählten Offline-Rezepts anzeigen
  RECIPES_ONLINE_MENU,           // Online-Rezepte auswählen (vom Gist geladen)
  RECIPE_ONLINE_DETAILS,         // Details eines Online-Rezepts anzeigen
  MANUAL_INPUT,                  // Benutzer gibt Mengen für jede Flüssigkeit manuell ein
  MANUAL_INPUT_EDITING,          // Benutzer ändert Menge einer Flüssigkeit mit Encoder (neu!)
  FLUID_SELECTION,               // Benutzer wählt aus, welche Flüssigkeiten geladen sind (1-9)
  FLUID_CATEGORY_SELECTION,      // Benutzer wählt eine Kategorie aus (Alkoholische, Säfte, Sirups, Sonstige)
  FLUID_FROM_CATEGORY_SELECTION, // Benutzer wählt eine Flüssigkeit aus der Kategorie
  FLUID_INPUT,                   // Kalibrierung einer Flüssigkeit (ml/1000 Schritte, Schlauchkorrektur)
  CLEANING_MODE,                 // Reinigungsmodus (Motor läuft kontinuierlich)
  GLASS_SIZE_SELECT,             // Glasgröße auswählen (Klein/Normal/Groß) bevor Rezept startet
  RUNNING_RECIPE,                // Rezept wird gerade ausgeführt (Gewicht anzeigen, nicht unterbrechen!)
  MANUAL_CONTROL,                // Manuelle Bedienung: alle Relays und Motor direkt ansteuern
  MANUAL_MOTOR_STEPS             // Schrittanzahl im Manuellen Bedienungs-Menü einstellen
};



// ================================================ GLOBALE VARIABLEN FÜR MENÜVERWALTUNG ================================================
MenuState currentMenu = FLUID_SELECTION;       // Aktueller Menüzustand (startet im Flüssigkeiten-Menü)
int selectedIndex = 0;                        // Welcher Menüeintrag ist ausgewählt? (0 = erste Option)
int selectedFluidSlot = 0;                    // Welche der 9 Positionen wird gerade bearbeitet? (0-8)
int selectedCategory = 0;                     // Welche Kategorie ist ausgewählt? (0-3)
int manualAmounts[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};  // Mengen für jede Flüssigkeit in manueller Eingabe
boolean fluidActive[9] = {false, false, false, false, false, false, false, false, false};  // Ist Flüssigkeit aktiv?
int selectedRecipe = 0;                       // Index des aktuell ausgewählten Rezepts
int compatibleRecipes[20] = {-1};             // Indizes der kompatiblen Rezepte (im recipes[] Array)
int compatibleCount = 0;                      // Anzahl der kompatiblen Rezepte



// ================================================ FUNKTIONSDEKLARATIONEN ================================================
// Dies sind Vordeklarationen aller Funktionen. Das Programm definiert sie später im Code

// INTERRUPT-HANDLERFUNKTIONEN
void setupInterrupts();      // Richte Interrupt-Handler ein
void encoderISR();           // Interrupt Service Routine für Encoder-Drehung
void buttonISR();            // ISR für Encoder-Button
void backButtonISR();        // ISR für Back-Button

// MOTORSTEUERUNGSFUNKTIONEN
void pumpFluid(int fluidIndex, int amount);           // Pumpe eine bestimmte Menge aus einer Flüssigkeit
void pumpFluidWithWeight(int fluidIndex, int targetWeight);  // Alternative: nach Gewicht pumpen
bool motorStep(long steps, boolean forward, int speed);  // Steuere Schrittmotor direkt an; gibt true zurück wenn Back-Button abgebrochen hat

// RELAYSTEUERUNG (VENTILE)
void activateRelay(int relayIndex, boolean state);   // Schalte ein Relay an/aus
void activateFluid(int fluidIndex, boolean state);   // Öffne/schließe ein Flüssigkeitsventil
void activateAir(boolean state);                     // Öffne/schließe das Luftventil

// SENSORLESUNG
void readWeight();  // Lese aktuelle Wägezellen-Gewicht und speichere in current_weight

// DISPLAYFUNKTIONEN (MENÜANZEIGE)
void displayMainMenu();        // Hauptmenü mit 5 Optionen
void displayRecipesMenu();     // Rezeptliste anzeigen
void displayRecipeDetails();   // Details eines Rezepts
void displayManualInput();     // Manuelle Eingabemaßnahmen
void displayManualInputEditing();  // Bearbeite Menge einer Flüssigkeit mit Encoder
void displayFluidSelection();  // Wähle Flüssigkeiten aus
void displayFluidCategories(); // Wähle Kategorie (Alkoholische, Säfte, Sirups, Sonstige)
void displayFluidFromCategory();  // Wähle Flüssigkeit aus Kategorie
void displayFluidInput();      // Kalibrierung einer Flüssigkeit
void displayGlassSizeSelect(); // Glasgröße auswählen
void displayCleaningMode();    // Reinigungsmodus
void displayManualControl();   // Manuelle Bedienung: Relays und Motor direkt ansteuern
void displayManualMotorSteps(); // Schrittanzahl einstellen
void displayRunningRecipe();   // Fortschritt während Rezepterzeugung

// NAVIGATIONSFUNKTIONEN
void handleMenuNavigation();   // Verarbeite Encoder-Eingaben und wechsle Menüstaaten

// REZEPTAUSFÜHRUNG
void executeRecipe(int recipeIndex);  // Führe ein Rezept aus (dosiere alle Zutaten)
boolean isRecipeCompatible(int recipeIndex);  // Prüfe, ob alle Rezeptzutaten verfügbar sind

// GLASERKENNUNG
void checkGlassDetection();           // Glaserkennung via Gewichtssprung (loop()-Aufruf)

// REINIGUNGSFUNKTIONEN
void startCleaning();  // Starte Reinigungsmodus
void stopCleaning();   // Beende Reinigungsmodus

// WIFI & MQTT
void connectToWiFi();                                               // WiFi-Verbindung aufbauen (blockiert bis verbunden oder Timeout)
void connectToMQTT();                                               // MQTT-Verbindung aufbauen und Topics subscriben
void mqttCallback(char* topic, byte* payload, unsigned int length); // Eingehende MQTT-Nachrichten verarbeiten
void publishStatus();                                               // Alle Status-Topics publizieren
void publishRecipeResult(Recipe& recipe, float* actualAmounts);     // Ergebnis nach Rezeptende publizieren
void publishOfflineRecipes();                                       // Offline-Rezepte als JSON auf MQTT publishen
void publishFluids();                                               // Flüssigkeits-Array als JSON auf MQTT publishen
void publishRelays();                                               // Relay-Zustände als JSON auf MQTT publishen

// ONLINE-REZEPTE
void fetchOnlineRecipes();          // HTTP GET → JSON parsen → onlineRecipes[] füllen
void executeOnlineRecipe(Recipe&);  // Online-Rezept ausführen (analog executeRecipe)
void displayRecipesTypeMenu();      // Anzeige: Offline / Online Auswahl
void displayOnlineRecipesMenu();    // Online-Rezeptliste anzeigen
void displayOnlineRecipeDetails();  // Online-Rezept-Details anzeigen



// ================================================ GLOBALE FLAGS ================================================
boolean isCleaningMode = false;  // Sind wir gerade im Reinigungsmodus?
bool manualRelayState[10] = {false,false,false,false,false,false,false,false,false,false};  // Relay-Zustände im Manuellen Bedienungs-Menü
long manualMotorSteps = STEPS_PER_REVOLUTION;  // Schrittzahl für Motor Vor/Zur im Manuellen Menü (1 Umdrehung)
bool manualMotorFast  = true;  // true = NORMAL_SPEED (schnell), false = SLOW_SPEED (langsam)
bool isRecipeRunning  = false;  // true wenn executeRecipe() oder executeOnlineRecipe() läuft
bool glassRemovedAbort = false; // true wenn Glas während Rezept entfernt wurde → Abbruch
float glassScaleFactor = 1.0f;  // Skalierungsfaktor für Glasgröße (0.75=Klein, 1.0=Normal, 1.5=Groß)
bool glassSizeForOnline = false; // true=Online-Rezept nach Glasauswahl, false=Offline
float glassSizeBaseMl  = 0.0f;  // Summe der Originalmengen des gewählten Rezepts (für ml-Anzeige)
float recipeStartWeight = 0.0f; // Glasgewicht zu Beginn des Rezepts (für Fortschrittsberechnung)
float recipeTotalMl     = 0.0f; // Gesamtmenge des aktuellen Rezepts in ml

// ── Glaserkennung ──
bool glassDetected    = false;  // true = Glas steht auf der Waage (bestätigt)
double glassWeight    = 0.0;    // Gewicht des erkannten Glases (g)
double baselineWeight = 0.0;    // Ruhgewicht vor dem Sprung (Tara)
bool glassDetecting   = false;  // Sprung erkannt, Stabilisierung läuft noch
unsigned long glassJumpTime = 0; // Zeitpunkt des Sprungs

// ================================================ WIFI & MQTT GLOBALS ================================================
WiFiClient   wifiClient;    // TCP-Client für MQTT
PubSubClient mqttClient(wifiClient);  // MQTT-Client (nutzt wifiClient)
AsyncWebServer webServer(80);  // HTTP Web-Server auf Port 80 (serviert LittleFS-Dateien)
bool wifiConnected  = false;   // true wenn WiFi verbunden
bool mqttConnected  = false;   // true wenn MQTT-Broker verbunden
char mqttClientId[30];         // Eindeutige Client-ID (Prefix + zufällige HEX-Bytes)

// ---- MQTT Pending-Flags (werden im Callback gesetzt, in loop() abgearbeitet) ----
// Wichtig: NIE blockierende Funktionen direkt im MQTT-Callback aufrufen!
volatile int  pendingRecipeIndex   = -1;   // >= 0 → Offline-Rezept mit diesem Index starten
volatile bool pendingOnlineRecipe  = false; // true → onlinePendingRecipe ausführen
volatile bool pendingCleaningStart = false; // true → startCleaning() aufrufen
volatile bool pendingCleaningStop  = false; // true → stopCleaning() aufrufen
volatile bool pendingOnlineRecipeFetch = false; // true → fetchOnlineRecipes() aufrufen
volatile int  pendingRelayIndex    = -1;    // >= 0 → Relay schalten
volatile bool pendingRelayState    = false; // gewünschter Relay-Zustand
volatile int  pendingMotorSteps    = 0;     // Motor-Schritte aus Command
volatile bool pendingMotorForward  = true;  // Motor-Richtung aus Command
volatile bool pendingMotorCmd      = false; // true → Motor-Command ausstehend
volatile int  pendingFluidSlot     = -1;    // >= 0 → Flüssigkeit ändern
String        pendingFluidName     = "";    // neuer Flüssigkeitsname für pendingFluidSlot
Recipe        onlinePendingRecipe;          // Rezept das aus MQTT-Command geparsed wurde

// Kalibrierung (per-Slot Fluid)
volatile int    pendingCalibrateSlot   = -1;    // >= 0 → Fluid-Kalibrierung speichern
volatile double pendingCalibrateFluidMl = 0.0;  // neuer calibration_ml Wert
volatile int    pendingCalibrateKorrektur = 0;  // neuer korrektur_faktor Wert

// Kalibrierung (Waage / Scale-Faktor)
volatile bool   pendingScaleCalibrate  = false; // true → neuen calibration_factor setzen
volatile double pendingScaleFactor     = 0.0;   // neuer Wägezellen-Kalibrierungsfaktor

// ---- Online-Rezepte Array ----
Recipe onlineRecipes[20];        // Bis zu 20 online geladene Rezepte
int    onlineRecipeCount  = 0;   // Aktuell geladene Anzahl
int    selectedOnlineRecipe = 0; // Ausgewählter Online-Rezept-Index

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// >>>  SEKTION 0: WIFI & MQTT
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Verbindet den ESP32 mit dem WLAN-Hotspot. Blockiert bis verbunden oder WIFI_TIMEOUT_MS
// abgelaufen ist. Bei Timeout wird im Offline-Modus weitergemacht (wifiConnected = false).
void connectToWiFi()
{
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("== WiFi Verbinde ==");
  lcd.setCursor(0, 1); lcd.print("SSID: ");
  lcd.print(WIFI_SSID);

  Serial.print("WiFi verbinde mit: ");
  Serial.println(WIFI_SSID);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS)
  {
    delay(250);
    Serial.print(".");
    lcd.setCursor(0, 2); lcd.print("Verbinde");
    for (int i = 0; i < (millis() - t0) / 500 % 4; i++) lcd.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    wifiConnected = true;
    String ip = WiFi.localIP().toString();
    Serial.println("\nWiFi verbunden! IP: " + ip);
    lcd.setCursor(0, 2); lcd.print("Verbunden!          ");
    lcd.setCursor(0, 3); lcd.print(ip);
    delay(2000);
  }
  else
  {
    wifiConnected = false;
    Serial.println("\nWiFi Timeout – Offline-Modus");
    lcd.setCursor(0, 2); lcd.print("Timeout! Offline...");
    delay(2000);
  }
}

// Publiziert eine einzelne MQTT-Nachricht (Helper um Wiederholungen zu vermeiden).
// retained=true: Broker speichert letzte Nachricht → neuer Client bekommt sofort aktuellen Wert.
static void mqttPublish(const char* topic, const String& payload, bool retained = false)
{
  if (mqttClient.connected())
    mqttClient.publish(topic, payload.c_str(), retained);
}

// Verarbeitet alle eingehenden MQTT-Nachrichten. Wird von PubSubClient aufgerufen.
// WICHTIG: Kein blocking Code hier! Nur Flags setzen, die in loop() abgearbeitet werden.
void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  // Payload in String kopieren
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  String t(topic);
  JsonDocument doc;
  deserializeJson(doc, msg);  // Fehler ignorieren – doc bleibt leer bei ungültigem JSON

  if (t == TOPIC_CMD_RECIPE_OFFLINE)
  {
    // {"name": "Mojito", "glass": "normal"}  oder {"index": 3, "glass": "normal"}
    // Name-Suche hat Vorrang (verhindert Index-Verschiebung)
    int idx = -1;
    String recipeName = doc["name"] | String("");
    if (recipeName.length() > 0) {
      for (int i = 0; i < recipeCount; i++) {
        if (String(recipes[i].name) == recipeName) { idx = i; break; }
      }
    }
    if (idx < 0) idx = doc["index"] | -1;  // Fallback auf Index
    if (idx >= 0 && idx < recipeCount && !isRecipeRunning)
    {
      String g = doc["glass"] | String("normal");
      float baseMl = 0;
      for (int i = 0; i < recipes[idx].fluid_count; i++) baseMl += recipes[idx].amounts[i];
      float targetMl;
      if      (g == "klein")    targetMl = 200.0f;
      else if (g == "mittel")   targetMl = 400.0f;
      else if (g == "gross")    targetMl = 750.0f;
      else if (g == "original") targetMl = baseMl;   // kein Skalieren
      else                      targetMl = 500.0f;   // normal
      glassScaleFactor = (baseMl > 0) ? targetMl / baseMl : 1.0f;
      pendingRecipeIndex = idx;
    }
  }
  else if (t == TOPIC_CMD_RECIPE_ONLINE)
  {
    // {"name":"...", "ingredients":[{"fluid":"Vodka","amount":60},...]}
    if (!isRecipeRunning)
    {
      onlinePendingRecipe.name = doc["name"] | String("Unbekannt");
      onlinePendingRecipe.fluid_count = 0;
      JsonArray ingredients = doc["ingredients"].as<JsonArray>();
      for (JsonObject ing : ingredients)
      {
        if (onlinePendingRecipe.fluid_count >= 9) break;
        int i = onlinePendingRecipe.fluid_count;
        String fname = ing["fluid"] | String("");
        // FLUID_NAMES-Index suchen
        onlinePendingRecipe.fluid_index[i] = 0;
        for (int j = 0; j < 100; j++)
        {
          if (FLUID_NAMES[j] == fname) { onlinePendingRecipe.fluid_index[i] = j; break; }
        }
        onlinePendingRecipe.amounts[i] = ing["amount"] | 0;
        onlinePendingRecipe.fluid_count++;
      }
      String g = doc["glass"] | String("normal");
      float baseMl = 0;
      for (int i = 0; i < onlinePendingRecipe.fluid_count; i++) baseMl += onlinePendingRecipe.amounts[i];
      float targetMl;
      if      (g == "klein")    targetMl = 200.0f;
      else if (g == "mittel")   targetMl = 400.0f;
      else if (g == "gross")    targetMl = 750.0f;
      else if (g == "original") targetMl = baseMl;
      else                      targetMl = 500.0f;
      glassScaleFactor = (baseMl > 0) ? targetMl / baseMl : 1.0f;
      pendingOnlineRecipe = true;
    }
  }
  else if (t == TOPIC_CMD_FLUID_SET)
  {
    // {"slot": 2, "name": "Gin"}
    int slot = doc["slot"] | -1;
    String name = doc["name"] | String("");
    if (slot >= 0 && slot < 9 && name.length() > 0)
    {
      pendingFluidSlot = slot;
      pendingFluidName = name;
    }
  }
  else if (t == TOPIC_CMD_RELAY)
  {
    // {"index": 5, "state": true}
    int idx   = doc["index"] | -1;
    bool state = doc["state"] | false;
    if (idx >= 0 && idx < NUM_RELAYS)
    {
      pendingRelayIndex = idx;
      pendingRelayState = state;
    }
  }
  else if (t == TOPIC_CMD_MOTOR)
  {
    // {"action": "forward", "steps": 800}
    String action = doc["action"] | String("forward");
    int steps = doc["steps"] | 0;
    if (steps > 0 && !isRecipeRunning)
    {
      pendingMotorForward = (action == "forward");
      pendingMotorSteps   = steps;
      pendingMotorCmd     = true;
    }
  }
  else if (t == TOPIC_CMD_CLEANING_START)
  {
    if (!isRecipeRunning) pendingCleaningStart = true;
  }
  else if (t == TOPIC_CMD_CLEANING_STOP)
  {
    pendingCleaningStop = true;
  }
  else if (t == TOPIC_CMD_RECIPES_REFRESH)
  {
    // Browser fordert Online-Rezept-Neuladen an – fetchOnlineRecipes() in loop() ist blockierend,
    // daher pendingOnlineRecipeFetch-Flag setzen
    if (!isRecipeRunning) pendingOnlineRecipeFetch = true;
  }
  else if (t == TOPIC_CMD_CALIBRATE_FLUID)
  {
    // {"slot":0,"calibration_ml":10.5,"korrektur":8}
    int slot         = doc["slot"] | -1;
    double cal_ml    = doc["calibration_ml"] | -1.0;
    int korrektur    = doc["korrektur"] | -1;
    if (slot >= 0 && slot < 9 && cal_ml > 0)
    {
      pendingCalibrateSlot      = slot;
      pendingCalibrateFluidMl   = cal_ml;
      pendingCalibrateKorrektur = (korrektur >= 0) ? korrektur : fluids[slot].korrektur_faktor;
    }
  }
  else if (t == TOPIC_CMD_CALIBRATE_SCALE)
  {
    // {"factor":408}
    double f = doc["factor"] | -1.0;
    if (f > 0)
    {
      pendingScaleFactor    = f;
      pendingScaleCalibrate = true;
    }
  }
}

// Verbindet mit dem MQTT-Broker. Generiert eindeutige Client-ID, setzt Callback,
// subscribed auf alle Command-Topics und publiziert initiale retained-Status-Nachrichten.
void connectToMQTT()
{
  if (!wifiConnected) return;

  // Eindeutige Client-ID: Prefix + 3 zufällige Bytes als HEX
  uint8_t mac[6]; WiFi.macAddress(mac);
  snprintf(mqttClientId, sizeof(mqttClientId), "%s%02X%02X%02X",
           MQTT_CLIENT_ID_PREFIX, mac[3], mac[4], mac[5]);

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(4096);  // Größerer Buffer für Rezept-JSONs

  Serial.print("MQTT verbinde: ");
  Serial.println(mqttClientId);

  if (mqttClient.connect(mqttClientId))
  {
    mqttConnected = true;
    Serial.println("MQTT verbunden!");

    // Alle Command-Topics subscriben
    mqttClient.subscribe(TOPIC_CMD_RECIPE_OFFLINE);
    mqttClient.subscribe(TOPIC_CMD_RECIPE_ONLINE);
    mqttClient.subscribe(TOPIC_CMD_FLUID_SET);
    mqttClient.subscribe(TOPIC_CMD_RELAY);
    mqttClient.subscribe(TOPIC_CMD_MOTOR);
    mqttClient.subscribe(TOPIC_CMD_CLEANING_START);
    mqttClient.subscribe(TOPIC_CMD_CLEANING_STOP);
    mqttClient.subscribe(TOPIC_CMD_RECIPES_REFRESH);
    mqttClient.subscribe(TOPIC_CMD_CALIBRATE_FLUID);
    mqttClient.subscribe(TOPIC_CMD_CALIBRATE_SCALE);

    // Initiale Status-Nachrichten publizieren (retained → Browser bekommt sie sofort)
    publishOfflineRecipes();
    publishFluids();
    publishRelays();
  }
  else
  {
    mqttConnected = false;
    Serial.print("MQTT Verbindung fehlgeschlagen, rc=");
    Serial.println(mqttClient.state());
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// >>>  SEKTION 1: SYSTEM & HARDWARE
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// SETUP - wird einmal beim Start aufgerufen
//
// Initialisiert alle Hardware in dieser Reihenfolge:
//   1. LCD  (I2C über GPIO 8=SDA / 9=SCL, Pull-ups nötig damit das Display antwortet)
//   2. Motor (LOW auf ENABLE aktiviert den TB6600-Treiber)
//   3. Relays (alle 10 Ventile sicher auf LOW = geschlossen)
//   4. Encoder (SW und BACK mit Pull-Up: Pin = HIGH normal, LOW = Taste gedrückt)
//   5. Wägezelle (calibration_factor kalibriert, tare() = aktuelles Gewicht als Nullpunkt)
//   6. Timer (ruft readWeight() alle 100ms automatisch auf, nicht-blockierend)
///////////////////////////////////////////////////////////////////////////////
void setup() 
{
  Serial.begin(115200);       // Serielle Verbindung starten – 115200 Baud ist der ESP32-Standard
  delay(SETUP_BOOT_DELAY_MS); // 2 Sekunden warten damit der USB-Serial-Treiber am PC sich verbinden kann

  // ---- I2C für das LCD initialisieren ----
  // ESP32-S3 hat keinen Hardware-I2C auf GPIO 8/9, deshalb GPIO-Pins manuell als INPUT_PULLUP
  // konfigurieren bevor Wire.begin() aufgerufen wird. Ohne Pull-up floaten SDA/SCL → LCD antwortet nicht.
  pinMode(LCD_SDA_PIN, INPUT_PULLUP);  // SDA (Datenleitung) auf HIGH ziehen: interner ~45kΩ nach 3,3V
  pinMode(LCD_SCL_PIN, INPUT_PULLUP);  // SCL (Taktleitung) auf HIGH ziehen: I2C-Bus-Ruhepegel = HIGH
  delay(SETUP_I2C_SETTLE_MS);          // 10ms warten bis die Leitungspegel stabil sind
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN); // I2C-Bus starten: SDA=GPIO8, SCL=GPIO9
  Wire.setClock(LCD_I2C_CLOCK_HZ);     // Takt auf 100 kHz: Standard-I2C, den das LCD-Modul versteht
  delay(SETUP_I2C_INIT_MS);            // 100ms warten bis der PCF8574-Chip auf dem LCD-Modul bereit ist

  lcd.init();        // LCD-Treiber initialisieren: setzt Zeilen/Spalten-Modus und löscht RAM
  lcd.noCursor();    // Blinkenden Unterstrich-Cursor verstecken (sieht professioneller aus)
  lcd.noBlink();     // Block-Cursor-Blinken deaktivieren
  lcd.backlight();   // Hintergrundbeleuchtung einschalten (über PCF8574 Port-Expander auf I2C)
  lcd.clear();       // Alle 80 Zeichenpositionen im LCD-RAM löschen
  lcd.print("Booting...");    // Erste sichtbare Meldung: Benutzer sieht dass das System startet
  Serial.println("1. LCD OK"); // Bestätigung im Serial Monitor ausgeben
  delay(SETUP_COMPONENT_MS);  // 1 Sekunde warten damit der Text auf dem LCD gelesen werden kann

  // ---- Schrittmotor-Treiber TB6600 aktivieren ----
  // TB6600 hat einen Active-Low ENABLE-Pin: LOW = Treiber aktiv, HIGH = Motor stromlos (frei drehbar)
  pinMode(STEPPER_STEP_PIN, OUTPUT);     // Step-Pin als Ausgang: wir senden Pulse zum TB6600
  pinMode(STEPPER_DIR_PIN, OUTPUT);      // Dir-Pin als Ausgang: bestimmt die Drehrichtung
  pinMode(STEPPER_ENABLE_PIN, OUTPUT);   // Enable-Pin als Ausgang: schaltet Treiber an/aus
  digitalWrite(STEPPER_ENABLE_PIN, LOW); // LOW = Treiber sofort aktivieren (Motor hält seine Position)
  Serial.println("2. Motor OK");

  // ---- Alle 10 Relays initialisieren ----
  // Idle-Zustand: alle Relays AN (LOW). Beim Pumpen: alle Relays AUS außer dem Ziel-Relay.
  // Hintergrund: LOW-Level-Trigger-Modul → LOW erregt die Relayspule → NC-Kontakt bleibt zu.
  for (int i = 0; i < NUM_RELAYS; i++)  // Alle 10 Relay-Pins nacheinander einrichten
  {
    pinMode(RELAY_PINS[i], OUTPUT);          // Pin als Ausgang konfigurieren
    digitalWrite(RELAY_PINS[i], LOW);        // LOW = Relay AN = Idle-Zustand (Ventil geschlossen)
  }
  Serial.println("3. Relays OK");

  // ---- Encoder und Buttons mit Pull-up einrichten ----
  // INPUT_PULLUP: interner ~45kΩ zieht Pin auf HIGH wenn nichts angeschlossen.
  // Encoder dreht → CLK oder DT geht kurz auf LOW. Button gedrückt → SW/BACK geht auf LOW.
  // Ohne Pull-up würden die Pins frei "floaten" und zufällige Signale erzeugen.
  pinMode(ENCODER_CLK, INPUT_PULLUP);  // Encoder Takt-Signal
  pinMode(ENCODER_DT,  INPUT_PULLUP);  // Encoder Daten-Signal
  pinMode(ENCODER_SW,  INPUT_PULLUP);  // Encoder-Taster
  pinMode(BACK_BUTTON, INPUT_PULLUP);  // Zurück-Taster (GPIO 47)
  Serial.println("4. Encoder OK");

  setupInterrupts();               // Encoder-Interrupts registrieren (CLK+DT lösen ISR aus bei Flanke)
  Serial.println("5. Interrupts OK");

  // ---- Wägezelle HX711 initialisieren ----
  // scale.begin() verbindet mit dem Chip. set_scale() stellt den Kalibrierungsfaktor ein
  // (roher ADC-Wert / calibration_factor = Gramm). tare() setzt den Nullpunkt auf das aktuelle Gewicht.
  scale.begin(LOADCELL_DT_PIN, LOADCELL_SCK_PIN); // HX711 mit den beiden Pins verbinden
  scale.set_scale(calibration_factor);             // Kalibrierung: ADC-Rohwert / factor = Gramm
  scale.tare();                                    // Aktuelles Gewicht als 0g definieren (leeres Glas)
  Serial.println("6. Scale OK");

  // Keine Timer-ISR: readWeight() wird in loop() per millis() aufgerufen.
  // Scale.get_units() blockiert bis zu 100ms (HX711 @10Hz) – viel zu lange für eine ISR.
  Serial.println("7. Timer OK (millis-basiert)");

  delay(SETUP_COMPONENT_MS);
  lcd.clear();
  lcd.print("System bereit!");
  delay(SETUP_COMPONENT_MS);

  // ---- LittleFS für Web-Dateien starten ----
  if (!LittleFS.begin(true))  // true = autoFormat: leeres Flash wird automatisch formatiert
  {
    Serial.println("LittleFS Fehler!");
  }
  else
  {
    Serial.println("8. LittleFS OK");

    // Web-Server Handler registrieren (begin() erst nach WiFi-Connect)
    // Cache-Control: no-cache → Browser holt immer die aktuelle Version vom ESP32
    webServer.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");
    Serial.println("9. Web-Server Handler OK");
  }

  // ---- WiFi verbinden ----
  connectToWiFi();  // Anzeige auf LCD, blockiert bis Verbunden oder Timeout

  // ---- Web-Server starten (erst nach WiFi, damit LWIP-Stack bereit ist) ----
  if (wifiConnected)
  {
    webServer.begin();
    Serial.println("10. Web-Server OK (IP: " + WiFi.localIP().toString() + ")");
  }

  // ---- MQTT verbinden (nur wenn WiFi ok) ----
  if (wifiConnected)
  {
    connectToMQTT();
    lcd.clear();
    if (mqttConnected)
    {
      lcd.setCursor(0, 0); lcd.print("MQTT: verbunden");
      lcd.setCursor(0, 1); lcd.print(WiFi.localIP().toString());
    }
    else
    {
      lcd.setCursor(0, 0); lcd.print("MQTT: FEHLER");
      lcd.setCursor(0, 1); lcd.print("Weiter ohne MQTT");
    }
    delay(1500);
  }

  currentMenu = FLUID_SELECTION;  // Starte im Flüssigkeiten-Menü zur Konfiguration
  displayNeedsUpdate = true;
}



///////////////////////////////////////////////////////////////////////////////
// LOOP - Hauptschleife, läuft ständig
//
// Verarbeitet Eingaben und zeigt das richtige Menü.
// Das Display wird nur neu gezeichnet wenn displayNeedsUpdate = true (nach Eingaben),
// ausser bei RUNNING_RECIPE — dort muss das Gewicht live mitlaufen.
///////////////////////////////////////////////////////////////////////////////
void loop() 
{
  // ---- MQTT Keep-Alive und Auto-Reconnect ----
  if (wifiConnected)
  {
    if (!mqttClient.connected())
    {
      static unsigned long lastReconnectAttempt = 0;
      if (millis() - lastReconnectAttempt >= MQTT_RECONNECT_INTERVAL_MS)
      {
        lastReconnectAttempt = millis();
        connectToMQTT();  // Reconnect-Versuch
      }
    }
    mqttClient.loop();  // Eingehende MQTT-Nachrichten weiterleiten → ruft mqttCallback() auf
  }

  // ---- Pending-Flags aus MQTT-Callback abarbeiten ----
  // (blockierende Aktionen nie im Callback selbst, sondern hier in loop())
  if (pendingRelayIndex >= 0)
  {
    activateRelay(pendingRelayIndex, pendingRelayState);
    manualRelayState[pendingRelayIndex] = pendingRelayState;
    publishRelays();
    pendingRelayIndex = -1;
    displayNeedsUpdate = true;
  }
  if (pendingFluidSlot >= 0 && pendingFluidName.length() > 0)
  {
    fluids[pendingFluidSlot].name = pendingFluidName;
    publishFluids();
    pendingFluidSlot = -1;
    pendingFluidName = "";
    displayNeedsUpdate = true;
  }
  if (pendingMotorCmd && !isRecipeRunning)
  {
    pendingMotorCmd = false;
    motorStep(pendingMotorSteps, pendingMotorForward, manualMotorFast ? NORMAL_SPEED : SLOW_SPEED);
  }
  if (pendingCleaningStart && !isRecipeRunning)
  {
    pendingCleaningStart = false;
    currentMenu = CLEANING_MODE;
    displayNeedsUpdate = true;
    startCleaning();
  }
  if (pendingCleaningStop)
  {
    pendingCleaningStop = false;
    stopCleaning();
    displayNeedsUpdate = true;
  }
  if (pendingOnlineRecipeFetch && !isRecipeRunning)
  {
    pendingOnlineRecipeFetch = false;
    fetchOnlineRecipes();
    displayNeedsUpdate = true;
  }
  if (pendingCalibrateSlot >= 0)
  {
    int s = pendingCalibrateSlot;
    pendingCalibrateSlot = -1;
    fluids[s].calibration_ml    = pendingCalibrateFluidMl;
    fluids[s].korrektur_faktor  = pendingCalibrateKorrektur;
    publishFluids();
    displayNeedsUpdate = true;
  }
  if (pendingScaleCalibrate)
  {
    pendingScaleCalibrate = false;
    calibration_factor = pendingScaleFactor;
    scale.set_scale(calibration_factor);
  }
  if (pendingRecipeIndex >= 0 && !isRecipeRunning)
  {
    int idx = pendingRecipeIndex;
    pendingRecipeIndex = -1;
    if (!glassDetected)
    {
      if (mqttClient.connected())
        mqttClient.publish("cocktailbot/status/error", "{\"error\":\"Kein Glas erkannt\"}");
      currentMenu = MAIN_MENU;
      displayNeedsUpdate = true;
    }
    else
    {
    currentMenu = RUNNING_RECIPE;
    displayNeedsUpdate = true;
    isRecipeRunning = true;
    executeRecipe(idx);
    isRecipeRunning = false;
    glassScaleFactor = 1.0f;
    currentMenu = MAIN_MENU;
    displayNeedsUpdate = true;
    } // end glass check
  }
  if (pendingOnlineRecipe && !isRecipeRunning)
  {
    pendingOnlineRecipe = false;
    if (!glassDetected)
    {
      if (mqttClient.connected())
        mqttClient.publish("cocktailbot/status/error", "{\"error\":\"Kein Glas erkannt\"}");
      currentMenu = MAIN_MENU;
      displayNeedsUpdate = true;
    }
    else
    {
    currentMenu = RUNNING_RECIPE;
    displayNeedsUpdate = true;
    isRecipeRunning = true;
    executeOnlineRecipe(onlinePendingRecipe);
    isRecipeRunning = false;
    glassScaleFactor = 1.0f;
    currentMenu = MAIN_MENU;
    displayNeedsUpdate = true;
    } // end glass check
  }

  // ---- Statusdaten periodisch über MQTT publizieren ----
  static unsigned long lastStatusPublish = 0;
  if (wifiConnected && mqttClient.connected() && millis() - lastStatusPublish >= MQTT_STATUS_INTERVAL_MS)
  {
    lastStatusPublish = millis();
    publishStatus();  // Waage, Relays, Pump-Status, Menü
  }

  // ---- WiFi-Status alle 2s prüfen und bei Änderung Display aktualisieren ----
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck >= 2000)
  {
    lastWifiCheck = millis();
    bool nowConnected = (WiFi.status() == WL_CONNECTED);
    if (nowConnected != wifiConnected)
    {
      wifiConnected = nowConnected;
      displayNeedsUpdate = true;  // LCD neu zeichnen (z.B. WiFi:OK → WiFi:NICHT verbunden)
    }
  }

  // ---- Wägezelle alle 100ms lesen ----
  static unsigned long lastWeightTime = 0;
  if (millis() - lastWeightTime >= 100)
  {
    lastWeightTime = millis();
    readWeight();
    checkGlassDetection();
  }

  handleMenuNavigation();

  if (currentMenu == RUNNING_RECIPE)
  {
    displayRunningRecipe();
  }
  else if (displayNeedsUpdate)
  {
    switch (currentMenu)
    {
      case MAIN_MENU:                      displayMainMenu();           break;
      case RECIPES_TYPE_MENU:              displayRecipesTypeMenu();    break;
      case RECIPES_MENU:                   displayRecipesMenu();        break;
      case RECIPE_DETAILS:                 displayRecipeDetails();      break;
      case RECIPES_ONLINE_MENU:            displayOnlineRecipesMenu();  break;
      case RECIPE_ONLINE_DETAILS:          displayOnlineRecipeDetails(); break;
      case MANUAL_INPUT:                   displayManualInput();        break;
      case MANUAL_INPUT_EDITING:           displayManualInputEditing(); break;
      case FLUID_SELECTION:                displayFluidSelection();     break;
      case FLUID_CATEGORY_SELECTION:       displayFluidCategories();    break;
      case FLUID_FROM_CATEGORY_SELECTION:  displayFluidFromCategory();  break;
      case FLUID_INPUT:                    displayFluidInput();         break;
      case GLASS_SIZE_SELECT:               displayGlassSizeSelect();    break;
      case CLEANING_MODE:                  displayCleaningMode();       break;
      case MANUAL_CONTROL:                 displayManualControl();      break;
      case MANUAL_MOTOR_STEPS:             displayManualMotorSteps();   break;
    }
    displayNeedsUpdate = false;
  }

  delay(LOOP_DELAY_MS);
}



///////////////////////////////////////////////////////////////////////////////
// INTERRUPTS - SCHNELLE REAKTION AUF EINGABEN
///////////////////////////////////////////////////////////////////////////////
// Interrupts ermöglichen schnelle Reaktion auf Encoder und Button
// Diese Funktionen unterbrechen den normalen Code-Fluss um Eingaben zu verarbeiten

// Registriert die Interrupt-Handler.
// NUR der Encoder benutzt Interrupts (braucht genaues Timing).
// Buttons werden in handleMenuNavigation() per Polling gelesen (zuverlässiger, kein ISR-Prellproblem).
void setupInterrupts() 
{
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_DT),  encoderISR, CHANGE);
  // ENCODER_SW und BACK_BUTTON: kein Interrupt, wird in handleMenuNavigation() gepollt
}



// Vollständiges Quadratur-Decoding mit Lookup-Tabelle und Akkumulator.
//
// Jede Rastung des Encoders erzeugt genau 4 gültige Zustandsübergänge:
//   Rechtsdrehung (CW):  3→1→0→2→3  (Zustand = CLK<<1 | DT)
//   Linksdrehung (CCW): 3→2→0→1→3
//
// enc_table: für jeden (prev<<2 | curr) gibt die Tabelle +1 (CW), -1 (CCW) oder 0 (ungültig) zurück.
// Der Akkumulator zählt Viertelschritte - erst bei ±4 wird ein voller Schritt gemeldet.
//
// Preller-Immunimtät: Ein Preller erzeugt nur 1-2 Viertelschritte die sich im Akkumulator
// umgekehrt aufheben (z.B. 3→1→3 = +1 + -1 = 0). Kein millis()-Debounce nötig.
void IRAM_ATTR encoderISR() 
{
  static const int8_t enc_table[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
  static uint8_t prev = 0b11;  // Ruhezustand: CLK=HIGH, DT=HIGH
  static int8_t  acc  = 0;     // Akkumulator für Viertelschritte

  // Aktuellen Zustand lesen: Bit1 = CLK, Bit0 = DT
  uint8_t curr = (digitalRead(ENCODER_CLK) << 1) | digitalRead(ENCODER_DT);

  acc += enc_table[(prev << 2) | curr];  // Tabelle nachschlagen, Viertelschritt aufaddieren
  prev = curr;

  // Erst nach 4 gültigen Viertelschritten in eine Richtung einen Schritt zählen
  if      (acc >=  4) { encoderCounter++; acc = 0; }
  else if (acc <= -4) { encoderCounter--; acc = 0; }
}



// Buttons werden nicht mehr per ISR gelesen sondern in handleMenuNavigation() gepollt.
// Polling reicht für Buttons völlig aus (Mensch drückt langsam, loop() läuft alle ~50ms).
// Vorteil: kein ISR-Prellproblem, keine Race Conditions möglich.



// Liest das aktuelle Gewicht vom HX711 und speichert es in current_weight.
// Wird aus loop() heraus aufgerufen (ca. alle 100ms) – NICHT als ISR!
// get_units(1) = 1 Sample: reicht hier, da wir bereits alle 100ms lesen.
// Mehr Samples würden blockieren (HX711 @10Hz = 100ms/Sample).
void readWeight() 
{
  if (scale.is_ready()) 
  {
    current_weight = scale.get_units(1);  // 1 Sample statt 5: kein blockierendes Warten
  }
}

// Glaserkennung: erkennt anhand eines Gewichtssprung (+100g) der 3s stabil bleibt, ob ein Glas aufgestellt wurde.
// Wird nur außerhalb von Rezeptausführung aufgerufen (isRecipeRunning == false).
// Setzt glassDetected / glassWeight und zeigt Feedback auf dem LCD.
void checkGlassDetection()
{
  if (isRecipeRunning || isCleaningMode) return;  // Während Rezept/Reinigung nicht detektieren

  double w = current_weight;

  // --- Glas entfernen ---
  if (glassDetected)
  {
    if (w < glassWeight - GLASS_REMOVE_HYST_G)
    {
      glassDetected    = false;
      glassDetecting   = false;
      baselineWeight   = w;
      glassWeight      = 0.0;
      displayNeedsUpdate = true;  // LCD aktualisieren
    }
    return;
  }

  // --- Sprung erkennen ---
  if (!glassDetecting)
  {
    if (w > baselineWeight + GLASS_JUMP_MIN_G)
    {
      glassDetecting = true;
      glassJumpTime  = millis();
    }
    else
    {
      // Baseline langsam nachführen (leichte Drift der Waage)
      baselineWeight = baselineWeight * 0.98 + w * 0.02;
    }
    return;
  }

  // --- Stabilisierung abwarten ---
  // Wenn Gewicht wieder unter Schwelle fällt → kein Glas, war nur Erschütterung
  if (w < baselineWeight + GLASS_JUMP_MIN_G)
  {
    glassDetecting = false;
    return;
  }

  // 3 Sekunden stabil → Glas bestätigt
  if (millis() - glassJumpTime >= GLASS_STABLE_MS)
  {
    glassDetected    = true;
    glassDetecting   = false;
    glassWeight      = w;
    displayNeedsUpdate = true;  // LCD aktualisieren
  }
}



// Dreht den Schrittmotor um die angegebene Anzahl Schritte.
// Pro Schritt: HIGH/LOW-Puls an STEP-Pin → TB6600-Treiber macht einen Schritt.
// speed = Wartezeit in Mikrosekunden pro Puls (kleiner = schneller).
// Rückgabe: true wenn Back-Button während des Laufs gedrückt wurde (Abbruch).
// Alle 500 Schritte (~60ms bei NORMAL_SPEED) wird der Back-Button direkt gelesen.
bool motorStep(long steps, boolean forward, int speed)
{
  static bool lastBackState = HIGH;  // Letzter Zustand des Back-Buttons für Flankenerkennung (HIGH = nicht gedrückt)

  // Drehrichtung setzen: TB6600 wertet den DIR-Pin VOR dem ersten Step-Puls aus
  // forward=true  → LOW  → TB6600 dreht vorwärts (Pumpe fördert Flüssigkeit)
  // forward=false → HIGH → TB6600 dreht rückwärts (Pumpe saugt zurück / manuelles Rückwärtsdrehen)
  digitalWrite(STEPPER_DIR_PIN, forward ? LOW : HIGH);

  // ---- Anlauframpe ----
  // Problem: Wenn wir sofort mit NORMAL_SPEED (150µs) starten, ist der Puls zu schnell –
  // der Motor hat keine Zeit Drehmoment aufzubauen und bleibt stecken (Schrittverlust).
  // Lösung: In den ersten MOTOR_ACCEL_STEPS Schritten die Verzögerung von MOTOR_START_SPEED
  // (800µs = sehr langsam = maximales Drehmoment) linear auf speed interpolieren.
  for (long i = 0; i < steps; i++)  // Jeden einzelnen Schritt ausführen
  {
    int currentSpeed;  // Pulsbreite in µs für diesen Schritt

    if (speed >= MOTOR_START_SPEED || steps <= MOTOR_ACCEL_STEPS)
    {
      // Zwei Fälle wo keine Rampe nötig ist:
      // 1. Zielgeschwindigkeit ist schon langsamer als Startgeschwindigkeit (z.B. SLOW_SPEED=400 < 800)
      // 2. Strecke zu kurz für eine sinnvolle Rampe (würde nie voll beschleunigen)
      currentSpeed = speed;
    }
    else if (i < MOTOR_ACCEL_STEPS)  // Wir sind noch in der Beschleunigungsphase
    {
      // Lineare Interpolation: Schritt 0 → MOTOR_START_SPEED, Schritt MOTOR_ACCEL_STEPS → speed
      // Formel: start - (start - target) * (i / accel_steps)
      currentSpeed = MOTOR_START_SPEED - (int)((long)(MOTOR_START_SPEED - speed) * i / MOTOR_ACCEL_STEPS);
    }
    else
    {
      currentSpeed = speed;  // Rampe abgeschlossen: ab jetzt konstant mit Zielgeschwindigkeit fahren
    }

    // Einen einzelnen Motorschritt ausführen:
    // Der TB6600 reagiert auf die steigende Flanke (LOW→HIGH). Das HIGH muss mindestens 2µs anliegen.
    // Nach dem LOW wartet der Motor die gleiche Zeit bevor er den nächsten Schritt annimmt.
    digitalWrite(STEPPER_STEP_PIN, HIGH);  // Steigende Flanke: TB6600 führt einen Schritt aus
    delayMicroseconds(currentSpeed);        // HIGH für currentSpeed µs halten (Pulsbreite)
    digitalWrite(STEPPER_STEP_PIN, LOW);   // Fallende Flanke: Puls abschließen
    delayMicroseconds(currentSpeed);        // LOW für currentSpeed µs: Mindestwartezeit bis zum nächsten Schritt

    // ---- Back-Button prüfen (alle 500 Schritte) ----
    // Da diese Funktion blockierend ist (loop() läuft nicht), müssen wir den Button selbst pollen.
    // 500 Schritte bei NORMAL_SPEED = 500 * 2 * 150µs = 150ms zwischen Checks → schnell genug für Nutzer.
    if (i % 500 == 0)
    {
      bool backState = digitalRead(BACK_BUTTON);           // Aktuellen Pin-Zustand direkt lesen
      if (backState == LOW && lastBackState == HIGH)        // Flanke HIGH→LOW = Taste wurde frisch gedrückt
      {
        lastBackState = backState;  // Zustand speichern damit wir nicht doppelt reagieren
        return true;                // Abbruch: Aufrufer (pumpFluid/startCleaning) soll sofort stoppen
      }
      lastBackState = backState;    // Zustand merken für nächste Flankenerkennung
    }
  }
  return false;  // Alle Schritte normal abgeschlossen, kein Abbruch durch Back-Button
}



// Schaltet ein einzelnes Relay an (true) oder aus (false). Index 0–9.
void activateRelay(int relayIndex, boolean state)
{
  // LOW-Level Trigger Modul: LOW = Relay erregt (AN), HIGH = Relay stromlos (AUS)
  // NC-Ventil am NO-Kontakt des Relays:
  //   state=true  → Relay AN  (LOW)  → NO-Kontakt schließt → Ventilspule an  → Ventil öffnet
  //   state=false → Relay AUS (HIGH) → NO-Kontakt offen    → Ventilspule aus → Feder schließt Ventil
  if (relayIndex >= 0 && relayIndex < NUM_RELAYS)  // Index-Prüfung: verhindert Schreiben außerhalb des Arrays
  {
    digitalWrite(RELAY_PINS[relayIndex], state ? LOW : HIGH);  // LOW=AN, HIGH=AUS (invertierte Logik!)
    manualRelayState[relayIndex] = state;  // Zustand mitführen damit publishRelays() korrekte Werte hat
  }
}

// Pumpt durch Fluid fluidIndex (state=true) oder stoppt (state=false).
// Idle-Zustand: alle Relays AN. Beim Pumpen: alle AUS außer Ziel-Relay bleibt AN.
void activateFluid(int fluidIndex, boolean state)
{
  if (fluidIndex < 0 || fluidIndex >= 9) return;  // Ungültiger Index: sofort abbrechen, nichts tun
  if (state)  // Pumpen starten: Flüssigkeitsventil öffnen
  {
    // Zuerst alle 10 Ventile schließen (alle Relays AUS), damit wirklich nur eine Flüssigkeit fließt
    for (int i = 0; i < NUM_RELAYS; i++) activateRelay(i, false);  // Alle Relays ausschalten
    activateRelay(fluidIndex, true);  // Dann nur das gewünschte Ventil öffnen (Relay AN)
  }
  else  // Pumpen stoppen: alle Ventile schließen
  {
    // Alle Relays wieder AN → Idle-Zustand (NC-Ventile halten alle Flüssigkeitswege geschlossen)
    for (int i = 0; i < NUM_RELAYS; i++) activateRelay(i, true);  // Alle 10 Relays einschalten
  }
}

// Öffnet das Luftventil (Relay 9) für das Leerblasen der Schläuche (state=true)
// oder schließt es und stellt den Idle-Zustand wieder her (state=false).
void activateAir(boolean state)
{
  if (state)  // Luft-Phase starten: Luftventil öffnen
  {
    // Alle anderen Ventile zuerst schließen, dann nur Luft-Relay öffnen
    for (int i = 0; i < NUM_RELAYS; i++) activateRelay(i, false);  // Alle 10 Relays ausschalten
    activateRelay(9, true);  // Relay 9 (Luftventil, GPIO 42) öffnen → Luft drückt Schlauchrest ins Glas
  }
  else  // Luft-Phase beenden
  {
    // Luftventil schließen und alle Relays wieder AN → Idle-Zustand
    for (int i = 0; i < NUM_RELAYS; i++) activateRelay(i, true);  // Alle 10 Relays einschalten
  }
}



////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// >>>  SEKTION 2: MENÜ NAVIGATION
// handleMenuNavigation() - Encoder-Eingaben verarbeiten und Menüzustände wechseln
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// MENÜ NAVIGATION
//
// Verarbeitet alle Benutzereingaben und steuert Menüübergänge.
// Wird in jeder loop()-Iteration aufgerufen.
//   - Encoder drehen:  selectedIndex / selectedCategory / manualAmounts anpassen
//   - Encoder Knopf:   passende Aktion je nach currentMenu ausführen
//   - Back-Knopf:      zum übergeordneten Menü zurückkehren
///////////////////////////////////////////////////////////////////////////////
void handleMenuNavigation() 
{
  static int lastEncoderCounter = 0;
  static unsigned long lastButtonPress = 0;   // Debounce für Encoder-Button
  static unsigned long lastBackPress   = 0;   // Debounce für Back-Button
  static bool lastSwState   = HIGH;           // letzter gelesen Zustand ENCODER_SW
  static bool lastBackState = HIGH;           // letzter gelesen Zustand BACK_BUTTON

  // ===== ENCODER-BUTTON POLLING =====
  // Pin lesen, bei Flanke HIGH→LOW + 50ms Debounce als gedrückt werten
  bool swState = digitalRead(ENCODER_SW);
  if (swState == LOW && lastSwState == HIGH && millis() - lastButtonPress > 50)
  {
    encoderButtonPressed = true;
    lastButtonPress = millis();
  }
  lastSwState = swState;

  // ===== BACK-BUTTON POLLING =====
  bool backState = digitalRead(BACK_BUTTON);
  if (backState == LOW && lastBackState == HIGH && millis() - lastBackPress > 50)
  {
    backButtonPressed = true;
    lastBackPress = millis();
  }
  lastBackState = backState;
  
  // Encoder wurde gedreht: selectedIndex, selectedCategory oder manualAmounts erhöhen/erniedrigen
  if (encoderCounter != lastEncoderCounter) 
  {
    if (encoderCounter > lastEncoderCounter) 
    {
      // Rechtsdrehung
      if (currentMenu == FLUID_CATEGORY_SELECTION)
      {
        selectedCategory++;  // Erhöhe Kategorieauswahl
      }
      else if (currentMenu == MANUAL_INPUT_EDITING)
      {
        // Menge in manual_input_increment ml-Schritten erhöhen
        manualAmounts[selectedFluidSlot] += manual_input_increment;
      }
      else if (currentMenu == MANUAL_MOTOR_STEPS)
      {
        // Umdrehungen erhöhen (max. MANUAL_MOTOR_MAX_REVOLUTIONS)
        manualMotorSteps = min((long)MANUAL_MOTOR_MAX_REVOLUTIONS * STEPS_PER_REVOLUTION, manualMotorSteps + STEPS_PER_REVOLUTION);
      }
      else
      {
        selectedIndex++;     // Erhöhe Standard-Auswahl
      }
    } 
    else 
    {
      // Linksdrehung
      if (currentMenu == FLUID_CATEGORY_SELECTION)
      {
        selectedCategory--;  // Erniedrige Kategorieauswahl
      }
      else if (currentMenu == MANUAL_INPUT_EDITING)
      {
        // Menge in manual_input_increment ml-Schritten verringern (aber nicht unter 0)
        manualAmounts[selectedFluidSlot] = max(0, manualAmounts[selectedFluidSlot] - manual_input_increment);
      }
      else if (currentMenu == MANUAL_MOTOR_STEPS)
      {
        // Umdrehungen verringern (min. 0)
        manualMotorSteps = max(0L, manualMotorSteps - STEPS_PER_REVOLUTION);
      }
      else
      {
        selectedIndex--;     // Erniedrige Standard-Auswahl
      }
    }
    lastEncoderCounter = encoderCounter;
    displayNeedsUpdate = true;
  }

  // Knopf gedrückt: Flag wurde im Polling oben gesetzt, 50ms Debounce bereits erledigt
  if (encoderButtonPressed) 
  {
    encoderButtonPressed = false;
    displayNeedsUpdate = true;

    switch (currentMenu) 
    {
      // ===== HAUPTMENÜ =====
      case MAIN_MENU:
        if (selectedIndex == 0) 
        {
          // Option 1: Rezepte → RECIPES_TYPE_MENU (Offline / Online Auswahl)
          currentMenu = RECIPES_TYPE_MENU;
          selectedIndex = 0;
        } 
        else if (selectedIndex == 1) 
        {
          currentMenu = MANUAL_INPUT;
          selectedIndex = 0;
          for (int i = 0; i < 9; i++) manualAmounts[i] = 0;
        } 
        else if (selectedIndex == 2) 
        {
          currentMenu = FLUID_SELECTION;
          selectedIndex = 0;
        } 
        else if (selectedIndex == 3) 
        {
          currentMenu = CLEANING_MODE;
          selectedIndex = 0;
          stopCleaning();
        } 
        else if (selectedIndex == 4) 
        {
          currentMenu = MANUAL_CONTROL;
          selectedIndex = 0;
          for (int i = 0; i < NUM_RELAYS; i++)
          {
            manualRelayState[i] = false;
            activateRelay(i, false);
          }
        }
        break;



      // ===== REZEPTTYP-MENÜ (Offline / Online) =====
      case RECIPES_TYPE_MENU:
        if (selectedIndex == 0)
        {
          // Offline-Rezepte
          currentMenu = RECIPES_MENU;
          selectedIndex = 0;
        }
        else if (selectedIndex == 1)
        {
          // Online-Rezepte: Bei Bedarf WiFi/MQTT neu verbinden, dann laden
          if (!wifiConnected)
          {
            connectToWiFi();
          }
          if (wifiConnected && !mqttClient.connected())
          {
            connectToMQTT();
          }
          fetchOnlineRecipes();
          currentMenu = RECIPES_ONLINE_MENU;
          selectedIndex = 0;
        }
        break;



      // ===== REZEPTE-MENÜ (Offline) =====
      case RECIPES_MENU:
        if (selectedIndex >= 0 && selectedIndex < recipeCount && recipes[selectedIndex].name != "")
        {
          if (isRecipeCompatible(selectedIndex))
          {
            selectedRecipe = selectedIndex;
            currentMenu = RECIPE_DETAILS;
            selectedIndex = 0;
          }
        }
        break;



      // ===== ONLINE-REZEPTE-MENÜ =====
      case RECIPES_ONLINE_MENU:
        if (selectedIndex >= 0 && selectedIndex < onlineRecipeCount)
        {
          selectedOnlineRecipe = selectedIndex;
          currentMenu = RECIPE_ONLINE_DETAILS;
          selectedIndex = 0;
        }
        break;



      // ===== ONLINE-REZEPT-DETAILS =====
      case RECIPE_ONLINE_DETAILS:
        if (selectedIndex == 0)
        {
          if (!glassDetected)
          {
            lcd.clear();
            lcd.setCursor(0, 0); lcd.print("!! KEIN GLAS !!");
            lcd.setCursor(0, 1); lcd.print("Bitte Glas");
            lcd.setCursor(0, 2); lcd.print("aufstellen!");
            delay(2500);
            displayNeedsUpdate = true;
            break;
          }
          glassSizeForOnline = true;
          glassSizeBaseMl = 0;
          for (int i = 0; i < onlineRecipes[selectedOnlineRecipe].fluid_count; i++)
            glassSizeBaseMl += onlineRecipes[selectedOnlineRecipe].amounts[i];
          currentMenu = GLASS_SIZE_SELECT;
          selectedIndex = 1;  // Vorauswahl: Normal
          displayNeedsUpdate = true;
        }
        else if (selectedIndex == 1)
        {
          stopCleaning();
        }
        break;



      // ===== REZEPT-DETAILS (Offline) =====
      case RECIPE_DETAILS:
        if (selectedIndex == 0)
        {
          if (!glassDetected)
          {
            lcd.clear();
            lcd.setCursor(0, 0); lcd.print("!! KEIN GLAS !!");
            lcd.setCursor(0, 1); lcd.print("Bitte Glas");
            lcd.setCursor(0, 2); lcd.print("aufstellen!");
            delay(2500);
            displayNeedsUpdate = true;
            break;
          }
          glassSizeForOnline = false;
          glassSizeBaseMl = 0;
          for (int i = 0; i < recipes[selectedRecipe].fluid_count; i++)
            glassSizeBaseMl += recipes[selectedRecipe].amounts[i];
          currentMenu = GLASS_SIZE_SELECT;
          selectedIndex = 1;  // Vorauswahl: Normal
          displayNeedsUpdate = true;
        }
        else if (selectedIndex == 1)
        {
          // Benutzer hat "STOP" ausgewählt -> stoppe Rezept
          stopCleaning();  // Stoppe alle Pumpen/Motoren
          // Bleibe im RECIPE_DETAILS Menü
        }
        break;
        


      // ===== GLASGROESSE-AUSWAHL =====
      case GLASS_SIZE_SELECT:
      {
        int selIdx = constrain(selectedIndex, 0, 4);
        float targetMl;
        if (GLASS_SIZE_ML[selIdx] > 0)
          targetMl = (float)GLASS_SIZE_ML[selIdx];
        else
          targetMl = glassSizeBaseMl;  // Original: kein Skalieren
        glassScaleFactor = (glassSizeBaseMl > 0) ? targetMl / glassSizeBaseMl : 1.0f;
        currentMenu = RUNNING_RECIPE;
        isRecipeRunning = true;
        displayNeedsUpdate = true;
        if (glassSizeForOnline)
        {
          executeOnlineRecipe(onlineRecipes[selectedOnlineRecipe]);
        }
        else
        {
          executeRecipe(selectedRecipe);
        }
        isRecipeRunning = false;
        glassScaleFactor = 1.0f;  // Zurücksetzen nach Rezept
        currentMenu = MAIN_MENU;
        displayNeedsUpdate = true;
        break;
      }

      // ===== MANUELLE EINGABE =====
      case MANUAL_INPUT:
        if (selectedIndex < 9) 
        {
          // Benutzer hat auf eine Flüssigkeit gedrückt
          // Gehe in den Edit-Modus für diese Flüssigkeit
          selectedFluidSlot = selectedIndex;
          currentMenu = MANUAL_INPUT_EDITING;
          encoderCounter = 0;  // Reset encoder für neue Eingabe
        } 
        else if (selectedIndex == 9) 
        {
          // Benutzer hat alle Flüssigkeiten eingegeben und drückt "Start"
          currentMenu = RUNNING_RECIPE;
          // HINWEIS: executeManualRecipe() Funktion müsste hier aufgerufen werden
        }
        break;
        


      // ===== FLÜSSIGKEITSAUSWAHL =====
      case FLUID_SELECTION:
        // Benutzer wählt eine Position (0-8) aus
        // Speichere diese Position und gehe zur Kategorieauswahl
        selectedFluidSlot = selectedIndex;  // Merken welche der 9 Positionen tauscht wird
        selectedCategory = 0;               // Immer bei Kategorie 0 (Alkoholische) starten
        selectedIndex = 0;                  // Erste Kategorie auswählen
        currentMenu = FLUID_CATEGORY_SELECTION;
        break;
        


      // ===== FLÜSSIGKEIT KATEGORIE AUSWAHL =====
      case FLUID_CATEGORY_SELECTION:
        // Benutzer hat eine Kategorie gewählt
        // Wechsle zur Flüssigkeitsauswahl in dieser Kategorie
        selectedIndex = 0;  // Starte bei erster Flüssigkeit der Kategorie
        currentMenu = FLUID_FROM_CATEGORY_SELECTION;
        break;
        


      // ===== FLUSSIGKEIT AUS KATEGORIE AUSWAHL =====
      case FLUID_FROM_CATEGORY_SELECTION:
      {
        // categoryStart[] gibt den Startindex jeder Kategorie in FLUID_NAMES an
        int categoryStart[4] = {0, 12, 24, 36};  // Erster FLUID_NAMES-Index pro Kategorie
        int fluidNameIndex = categoryStart[selectedCategory] + selectedIndex;  // globaler Index in FLUID_NAMES
        
        if (fluidNameIndex < 100)  // Sicherheit: nicht über FLUID_NAMES Array-Ende hinaus
        {
          fluids[selectedFluidSlot].name = FLUID_NAMES[fluidNameIndex];  // Neue Flüssigkeit zuweisen
          fluids[selectedFluidSlot].calibration_ml;  // Kalibrierung auf Standardwert zurücksetzen (muss danach neu kalibriert werden!)
          fluids[selectedFluidSlot].korrektur_faktor;  // Schlauchkorrektur auf Standardwert (ebenfalls re-kalibrieren)
        }
        
        currentMenu = FLUID_SELECTION;
        selectedIndex = selectedFluidSlot;  // Cursor auf die gerade geänderte Position springen
        break;
      }
        


      // ===== REINIGUNGSMODUS =====
      case CLEANING_MODE:
        // Benutzer wählt zwischen "Stop" (index 0) und "Starten" (index 1)
        if (selectedIndex == 0) 
        {
          // "Reinigung Stop" gewählt
          stopCleaning();
        }
        else if (selectedIndex == 1)
        {
          // "Reinigung Starten" gewählt
          startCleaning();
        }
        break;



      // ===== MANUELLE BEDIENUNG =====
      case MANUAL_CONTROL:
        if (selectedIndex >= 0 && selectedIndex <= 9)
        {
          // Relay togglen: AN -> AUS, AUS -> AN
          manualRelayState[selectedIndex] = !manualRelayState[selectedIndex];
          activateRelay(selectedIndex, manualRelayState[selectedIndex]);
        }
        else if (selectedIndex == 10)
        {
          // Motor vorwärts: manualMotorSteps Schritte mit gewählter Geschwindigkeit
          motorStep(manualMotorSteps, true, manualMotorFast ? NORMAL_SPEED : SLOW_SPEED);
        }
        else if (selectedIndex == 11)
        {
          // Motor rückwärts: manualMotorSteps Schritte mit gewählter Geschwindigkeit
          motorStep(manualMotorSteps, false, manualMotorFast ? NORMAL_SPEED : SLOW_SPEED);
        }
        else if (selectedIndex == 12)
        {
          // Schritte einstellen: wechsle in den Schritt-Editier-Modus
          currentMenu = MANUAL_MOTOR_STEPS;
        }
        else if (selectedIndex == 13)
        {
          // Geschwindigkeit umschalten: schnell <-> langsam
          manualMotorFast = !manualMotorFast;
        }
        break;


      // ===== MOTORSCHRITTE EINSTELLEN =====
      case MANUAL_MOTOR_STEPS:
        // Encoder-Knopf bestätigt den Wert und kehrt zurück
        currentMenu = MANUAL_CONTROL;
        selectedIndex = 12;  // Cursor bleibt auf "Schritte"
        break;
    }
  }
  


  // Back-Knopf: zum übergeordneten Menü zurückkehren
  if (backButtonPressed) 
  {
    backButtonPressed = false;
    displayNeedsUpdate = true;
    
    // Spezial-Handling für neue States
    if (currentMenu == MANUAL_INPUT_EDITING)
    {
      currentMenu = MANUAL_INPUT;
      selectedIndex = selectedFluidSlot;
    }
    else if (currentMenu == FLUID_CATEGORY_SELECTION)
    {
      currentMenu = FLUID_SELECTION;
      selectedIndex = selectedFluidSlot;
    }
    else if (currentMenu == FLUID_FROM_CATEGORY_SELECTION)
    {
      currentMenu = FLUID_CATEGORY_SELECTION;
      selectedIndex = 0;
    }
    else if (currentMenu == RECIPE_DETAILS)
    {
      currentMenu = RECIPES_MENU;
      selectedIndex = selectedRecipe;
    }
    else if (currentMenu == RECIPES_MENU || currentMenu == RECIPES_ONLINE_MENU)
    {
      // Zurück zu Rezepttyp-Auswahl
      currentMenu = RECIPES_TYPE_MENU;
      selectedIndex = (currentMenu == RECIPES_ONLINE_MENU) ? 1 : 0;
    }
    else if (currentMenu == RECIPES_TYPE_MENU)
    {
      currentMenu = MAIN_MENU;
      selectedIndex = 0;
    }
    else if (currentMenu == RECIPE_ONLINE_DETAILS)
    {
      currentMenu = RECIPES_ONLINE_MENU;
      selectedIndex = selectedOnlineRecipe;
    }
    else if (currentMenu == MANUAL_CONTROL)
    {
      for (int i = 0; i < NUM_RELAYS; i++)
      {
        manualRelayState[i] = false;
        activateRelay(i, true);
      }
      currentMenu = MAIN_MENU;
      selectedIndex = 4;
    }
    else if (currentMenu == MANUAL_MOTOR_STEPS)
    {
      currentMenu = MANUAL_CONTROL;
      selectedIndex = 12;
    }
    else
    {
      currentMenu = MAIN_MENU;
      selectedIndex = 0;
      stopCleaning();
    }
  }
}



////////////////////////////////////////////////////////////////////////////////
// >>>  SEKTION 3: REZEPTE
////////////////////////////////////////////////////////////////////////////////

// Zeigt das Hauptmenü mit 4 Optionen: Rezepte, Manuelle Eingabe, Flüssigkeiten, Reinigung.
// Das Menü scrollt wenn selectedIndex > 2, damit immer 3 Einträge sichtbar sind.
void displayMainMenu() 
{
  lcd.clear();
  lcd.setCursor(0, 0);
  // Zeile 0: Titel + Glasstatus rechts
  lcd.print("=== HAUPTMENU ===");
  lcd.setCursor(17, 0);
  if (glassDetected)        lcd.print(" \x07\x07");   // Glas erkannt (zwei gefüllte Blöcke)
  else if (glassDetecting)  lcd.print(" ..");         // Erkennung läuft (Punkte)
  else                      lcd.print("   ");
  
  selectedIndex = constrain(selectedIndex, 0, 4);
  
  String menuItems[5] = 
  {
    "1.Rezepte",
    "2.Manuelle Eingabe",
    "3.Fluessigkeiten",
    "4.Reinigung",
    "5.Man.Bedienung"
  };
  
  // Scroll-Logik: ausgewählter Eintrag bleibt immer in den 3 sichtbaren Zeilen
  int startScroll = constrain(selectedIndex - 1, 0, 2);
  for (int i = 0; i < 3; i++) 
  {
    int itemIndex = startScroll + i;
    lcd.setCursor(0, i + 1);
    lcd.print(itemIndex == selectedIndex ? "> " : "  ");
    lcd.print(menuItems[itemIndex]);
  }
}



// Zeigt die scrollbare Rezeptliste.
// Am rechten Rand erscheint ein Symbol: Häkchen (0xFE) = alle Zutaten vorhanden,
// X = mindestens eine Zutat fehlt → Rezept kann nicht gemacht werden.
void displayRecipesMenu() 
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("==== REZEPTE ====");
  
  if (recipeCount == 0)
  {
    lcd.setCursor(0, 1); lcd.print("Keine Rezepte");
    lcd.setCursor(0, 2); lcd.print("vorhanden");
    return;
  }
  
  selectedIndex = constrain(selectedIndex, 0, recipeCount - 1);
  
  // Scroll-Offset: die ausgewählte Zeile soll immer sichtbar bleiben
  int startIdx = 0;
  if (selectedIndex >= 2)
  {
    startIdx = selectedIndex - 2;        // Auswahl ist unten, Liste scrollt mit
    if (startIdx + 2 >= recipeCount)     // Sicherstellen dass wir nicht nach Ende scrollen
      startIdx = recipeCount - 3;        // Letzten 3 Einträge anzeigen wenn am Ende angekommen
  }
  
  for (int row = 0; row < 3 && startIdx + row < recipeCount; row++) 
  {
    int recipeIdx = startIdx + row;
    lcd.setCursor(0, row + 1);
    lcd.print(recipeIdx == selectedIndex ? "> " : "  ");
    
    Recipe recipe = recipes[recipeIdx];
    if (recipe.name != "")
    {
      // Name auf 16 Zeichen bringen (auffüllen damit der Status rechts klar ist)
      String shortName = recipe.name.substring(0, 16);
      while (shortName.length() < 16) shortName += " ";
      lcd.print(shortName);
      
      // Spalte 19 = letztes Zeichen der Zeile: Häkchen oder X
      lcd.setCursor(19, row + 1);
      lcd.print(isRecipeCompatible(recipeIdx) ? "\xFE" : "X");
    }
    else
    {
      lcd.print("(leer)");
    }
  }
}



// Prüft ob ein Rezept mit den aktuell eingestellten 9 Flüssigkeiten gemacht werden kann.
// Jede Rezept-Zutat (als fluid_index aus FLUID_NAMES) wird mit den Namen in fluids[0..8]
// verglichen. Fehlt nur eine Zutat → false. Alle vorhanden → true.
// Beispiel:
// - Eingesetzte Flüssigkeiten: Vodka, Orangensaft, Rum, ...
// - Rezept-Zutaten: Vodka + Orangensaft
//   -> beide Zutaten werden in fluids[] gefunden => Rezept ist kompatibel
// - Rezept-Zutaten: Vodka + Limettensaft
//   -> Limettensaft fehlt in fluids[] => Rezept ist nicht kompatibel
boolean isRecipeCompatible(int recipeIndex)
{
  Recipe recipe = recipes[recipeIndex];
  
  for (int i = 0; i < recipe.fluid_count; i++)
  {
    int recipeFluidIdx = recipe.fluid_index[i];
    boolean found = false;
    
    for (int j = 0; j < 9; j++)
    {
      if (fluids[j].name == FLUID_NAMES[recipeFluidIdx])
      {
        found = true;
        break;
      }
    }
    
    if (!found) return false;  // Zutat nicht vorhanden → nicht herstellbar
  }
  
  return true;
}



// Zeigt den Bestätigungsbildschirm für ein Rezept.
// Zeile 0: Rezeptname zentriert.
// Zeilen 1–3: scrollbare Liste mit idx 0 = STARTEN, idx 1 = STOP, idx 2+ = Zutaten.
// Zutaten werden als "Name: XXml" formatiert (rechtsbündig auf 18 Zeichen).
void displayRecipeDetails()
{
  lcd.clear();
  
  // Rezeptname zentriert in Zeile 0
  lcd.setCursor(0, 0);
  String recipeName = recipes[selectedRecipe].name;
  if (recipeName.length() < 20)
  {
    int padding = (20 - recipeName.length()) / 2;  // Anzahl der Leerzeichen links berechnen
    String paddedName = "";
    for (int i = 0; i < padding; i++) paddedName += " ";  // Leerzeichen links einfügen
    paddedName += recipeName;                              // Name in der Mitte
    while (paddedName.length() < 20) paddedName += " ";   // Rest rechts auffüllen
    lcd.print(paddedName);
  }
  else
  {
    lcd.print(recipeName.substring(0, 20));  // Langer Name: einfach auf 20 Zeichen kürzen
  }
  
  Recipe recipe = recipes[selectedRecipe];
  int totalOptions = recipe.fluid_count + 2;  // +2 für die festen Einträge STARTEN und STOP
  selectedIndex = constrain(selectedIndex, 0, totalOptions - 1);
  
  // Scroll-Offset: ausgewählte Option soll immer sichtbar bleiben
  int startIdx = 0;
  if (selectedIndex >= 2)
  {
    startIdx = selectedIndex - 2;          // Auswahl ans untere Drittel bringen
    if (startIdx + 2 >= totalOptions)      // Nicht über das letzte Element hinaus
      startIdx = totalOptions - 3;         // Die letzten 3 Optionen zeigen
  }
  
  for (int row = 0; row < 3 && startIdx + row < totalOptions; row++)
  {
    int idx = startIdx + row;
    lcd.setCursor(0, row + 1);
    lcd.print(idx == selectedIndex ? "> " : "  ");
    
    if (idx == 0)
    {
      lcd.print("STARTEN          ");
    }
    else if (idx == 1)
    {
      lcd.print("STOP             ");
    }
    else
    {
      // Zutat anzeigen: idx 2 = erste Zutat, idx 3 = zweite usw.
      int ingredientIdx = idx - 2;
      int fluidNameIdx = recipe.fluid_index[ingredientIdx];
      String fluidName = FLUID_NAMES[fluidNameIdx];
      int amount = recipe.amounts[ingredientIdx];
      
      // Format: "FluidName   :  50ml" (18 Zeichen nach dem "> ")
      String line = fluidName.substring(0, 12);  // Name auf 12 Zeichen begrenzen
      while (line.length() < 12) line += " ";    // Mit Leerzeichen auf genau 12 auffüllen
      line += ": ";
      if (amount < 10)  line += "  ";      // 1-stellig: 2 Leerzeichen für Rechtsbündigkeit
      else if (amount < 100) line += " "; // 2-stellig: 1 Leerzeichen
      line += amount;                     // 3-stellig: kein Leerzeichen nötig
      line += "ml";
      while (line.length() < 18) line += " ";    // Auf exakt 18 Zeichen auffüllen
      lcd.print(line.substring(0, 18));
    }
  }
}



// Zeigt den Live-Fortschritt während ein Rezept läuft.
// Die static-Variable lastMenu verhindert dass lcd.clear() bei jedem Aufruf
// (alle 50ms) ausgeführt wird — das würde flackern. Der Titel wird nur
// einmal beim ersten Aufruf gezeichnet, danach nur noch das Gewicht.
void displayRunningRecipe() 
{
  static MenuState lastMenu = MAIN_MENU;
  
  if (currentMenu != lastMenu || lastMenu != RUNNING_RECIPE)
  {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("= WIRD AUSGEFUEHRT =");
    lcd.setCursor(0, 2); lcd.print("Bitte warten...");
    lastMenu = RUNNING_RECIPE;
  }
  
  // Gewicht in Zeile 1 live aktualisieren (alle 100ms durch Timer-ISR)
  lcd.setCursor(0, 1);
  lcd.print("Gewicht: ");
  lcd.print(String(current_weight, 1));
  lcd.print("ml      ");  // Leerzeichen löschen alte Ziffern wenn Wert kürzer wird
}



// Dosiert eine genaue Menge einer Flüssigkeit. Ablauf in 3 Phasen:
//
// Phase 1 – Schnell (80% der berechneten Schritte mit NORMAL_SPEED):
//   Grobe Annäherung ans Ziel. Formel: steps = (ml * 1000) / calibration_ml
//   calibration_ml = wie viele ml bei 1000 Motorschritten gefördert werden.
//
// Phase 2 – Langsam (Schritt für Schritt, Waage überwacht):
//   Läuft bis current_weight >= slowStopWeight.
//   slowStopWeight = targetAmount - korrektur_faktor - WEIGHT_TOLERANCE
//   Die Pumpe stoppt früher als das Ziel, damit der Schlauchrest die fehlende
//   Menge genau ausgleicht (korrektur_faktor = bekannte ml die im Schlauch bleiben).
//
// Phase 3 – Luft drückt Schlauchrest ins Glas:
//   Ventil zu, Luftventil auf. Luft drückt den Schlauchrest ins Glas.
//   Stoppt sobald purgeTarget (targetAmount - WEIGHT_TOLERANCE) erreicht
//   oder korrektionSchritte aufgebraucht sind (Sicherheitslimit).
void pumpFluid(int fluidIndex, int targetAmount)
{
  if (fluidIndex < 0 || fluidIndex >= 9) return;  // Ungültiger Slot: sofort abbrechen

  // ---- LCD-Startanzeige aufbauen ----
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("= WIRD AUSGEFUEHRT =");  // Titelzeile: System ist beschäftigt
  lcd.setCursor(0, 1); lcd.print("Ist:  0.0ml         ");  // Zeile 1: aktuelles Gewicht (wird live aktualisiert)
  lcd.setCursor(0, 2); lcd.print("Ziel: ");                 // Zeile 2: Zielgewicht (bleibt konstant)
  lcd.print(targetAmount);                                   // Zahl der Zielmenge einfügen (z.B. "60")
  lcd.print("ml");                                           // Einheit anfügen
  lcd.setCursor(0, 3);
  String fname = fluids[fluidIndex].name;  // Name der Flüssigkeit holen (z.B. "Weisser_Rum")
  fname.replace("_", " ");                 // Unterstriche durch Leerzeichen ersetzen für bessere Lesbarkeit
  lcd.print(fname.substring(0, 20));       // Name in Zeile 3 ausgeben (max. 20 Zeichen)

  // ---- Waage für diese Zutat nullen ----
  // scale.tare() setzt den aktuellen Messwert als neuen Nullpunkt.
  // current_weight wird manuell auf 0 gesetzt damit kein alter Wert aus loop() die Phase-1-Bedingung
  // fälschlicherweise als bereits erreicht bewertet (HX711 liefert nach tare() kurz keinen frischen Wert).
  scale.tare();         // Nullpunkt auf aktuelles Gewicht setzen (leeres Glas = 0g)
  current_weight = 0.0; // Variable sofort auf 0 setzen (HX711 noch nicht bereit nach tare)

  // ---- Hilfsfunktion: Waage lesen + LCD-Zeile 1 aktualisieren ----
  // Lambda mit [&]: kann alle lokalen Variablen direkt nutzen (current_weight, lcd, etc.)
  // Wird NUR aufgerufen wenn der Motor steht → sauberere Messung ohne EMI-Störungen
  auto updateDisplay = [&]() {
    if (scale.is_ready()) {                        // Nur lesen wenn HX711 eine neue Messung bereit hat
      double r = scale.get_units(1);               // 1 Sample lesen: Rohwert / calibration_factor = Gramm
      if (r < -(double)GLASS_JUMP_MIN_G * 0.5) {
        current_weight = r;                        // Großes Negativ → Glas entfernt, direkt durchlassen
      } else if (r >= PUMP_SCALE_NOISE_FLOOR) {
        current_weight = (r < 0.0 ? 0.0 : r);     // Normaler Bereich: leicht negative Werte auf 0 klemmen
      }
      // Bereich -5g … -50g = Motor-EMI-Rauschen → ignorieren (current_weight unverändert)
    }
    lcd.setCursor(0, 1);                    // Zeile 1 neu beschreiben
    lcd.print("Ist:  ");                    // Beschriftung
    lcd.print(String(current_weight, 1));   // Gewicht mit 1 Nachkommastelle (z.B. "42.3")
    lcd.print("ml      ");                  // Einheit + Leerzeichen (überschreiben alte längere Zahlen)
  };

  // ---- Schrittzahl berechnen ----
  // Formel: Ziel-ml × 1000 / calibration_ml = Schritte
  // calibration_ml = wie viele ml bei genau 1000 Motorschritten gepumpt werden (experimentell gemessen)
  // Beispiel: 60ml × 1000 / 10.0 ml/1000 = 6000 Schritte
  // WICHTIG: Dieser Wert ist nur das Sicherheitslimit – die Waage steuert den tatsächlichen Stopp!
  int steps = (targetAmount * 1000) / fluids[fluidIndex].calibration_ml;

  // ============================
  // PHASE 1: Schnell pumpen
  // ============================
  // In großen 4000-Schritt-Blöcken (= 5 Umdrehungen) pumpen, nach jedem Block Waage prüfen.
  // Pumpt bis current_weight >= slowStopWeight (= Ziel minus Schlauchinhalt minus Toleranz).
  // Sicherheitslimit: PUMP_FAST_SAFETY_FACTOR × berechnete Schritte, falls Waage ausfällt.
  activateFluid(fluidIndex, true);  // Flüssigkeitsventil öffnen (nur dieses eine Ventil)

  // slowStopWeight = Gewicht bei dem Phase 1 endet und Phase 2 (langsam) beginnt
  // Wir stoppen FRÜHER als das Ziel, weil noch korrektur_faktor ml im Schlauch sind
  // die separat durch Luft (Phase 3) ins Glas kommen
  double slowStopWeight = targetAmount - fluids[fluidIndex].korrektur_faktor - WEIGHT_TOLERANCE;
  if (slowStopWeight < 0) slowStopWeight = 0;  // Sicherheit bei sehr kleinen Mengen

  int fastStepsDone = 0;                              // Zähler: wie viele Schritte in Phase 1 gelaufen
  int maxFastSteps  = steps * PUMP_FAST_SAFETY_FACTOR; // Sicherheitslimit: max. 3× berechnete Schritte

  // Mindestens einen Block laufen lassen (Anlauframpe muss durchlaufen werden)
  motorStep(PUMP_FAST_BLOCK_STEPS, true, NORMAL_SPEED); // 4000 Schritte mit Normalgeschwindigkeit
  fastStepsDone += PUMP_FAST_BLOCK_STEPS;                // Schrittanzahl aufaddieren
  delay(PUMP_EMI_DELAY_MS);                              // 150ms warten: Motor-EMI klingt ab bevor Waage gelesen wird
  updateDisplay();                                        // Waage lesen und auf LCD zeigen
  if (current_weight < -(double)GLASS_JUMP_MIN_G) { glassRemovedAbort = true; activateFluid(fluidIndex, false); return; }

  static unsigned long lastFastStatus = 0;
  while (current_weight < slowStopWeight && fastStepsDone < maxFastSteps)  // Noch nicht am Ziel?
  {
    motorStep(PUMP_FAST_BLOCK_STEPS, true, NORMAL_SPEED); // Nächsten 4000-Schritt-Block pumpen
    fastStepsDone += PUMP_FAST_BLOCK_STEPS;                // Gesamtschritte aktualisieren
    delay(PUMP_EMI_DELAY_MS);                              // EMI-Pause: Waage stabilisieren lassen
    updateDisplay();                                        // Neues Gewicht lesen und anzeigen
    if (current_weight < -(double)GLASS_JUMP_MIN_G) { glassRemovedAbort = true; break; }
    if (wifiConnected && millis() - lastFastStatus >= 500) { mqttClient.loop(); publishStatus(); lastFastStatus = millis(); }
  }
  if (glassRemovedAbort) { activateFluid(fluidIndex, false); return; }

  // ============================
  // PHASE 2: Langsam pumpen
  // ============================
  // In kleinen 50-Schritt-Blöcken (= 1/16 Umdrehung) mit langsamer Geschwindigkeit.
  // Nach jedem Block Waage lesen → viel feiner dosieren als Phase 1.
  int maxSlowBlocks = (steps / 100) + PUMP_SLOW_SAFETY_EXTRA;  // Sicherheitslimit mit Puffer
  int slowBlocks = 0;                                            // Zähler für bisherige langsame Blöcke
  static unsigned long lastPumpStatus = 0;
  while (current_weight < slowStopWeight && slowBlocks < maxSlowBlocks)  // Ziel noch nicht erreicht?
  {
    motorStep(PUMP_SLOW_BLOCK_STEPS, true, SLOW_SPEED);  // 50 Schritte bei 400µs/Schritt (sehr langsam)
    delay(PUMP_SLOW_DELAY_MS);                            // 30ms Pause: Waage nach dem Miniblock einschwingen
    if (wifiConnected) { mqttClient.loop(); if (millis() - lastPumpStatus >= 500) { publishStatus(); lastPumpStatus = millis(); } }
    updateDisplay();                                       // Gewicht lesen und anzeigen
    if (current_weight < -(double)GLASS_JUMP_MIN_G) { glassRemovedAbort = true; break; }
    slowBlocks++;                                          // Block-Zähler erhöhen
  }

  activateFluid(fluidIndex, false);  // Flüssigkeitsventil schließen: korrektur_faktor ml noch im Schlauch
  if (glassRemovedAbort) { return; }

  // ============================
  // PHASE 3: Luft bläst Schlauch leer
  // ============================
  // Der Schlauch enthält noch korrektur_faktor ml Flüssigkeit.
  // Luftventil öffnen → Luft schiebt den Rest ins Glas.
  // korrektionSchritte = wie viele Motorschritte nötig sind um den Schlauchinhalt zu fördern.
  delay(PUMP_EMI_DELAY_MS);           // 150ms Stabilisierungspause
  if (wifiConnected) mqttClient.loop(); // MQTT keep-alive
  updateDisplay();                      // Gewicht nach Phase 2 auf LCD zeigen

  activateAir(true);  // Luftventil öffnen (Relay 9): Luft fließt durch Schlauch in Richtung Glas
  int korrektionSchritte = (fluids[fluidIndex].korrektur_faktor * 1000) / fluids[fluidIndex].calibration_ml;
  double purgeTarget = targetAmount - WEIGHT_TOLERANCE;  // Abbruchziel: Gesamtmenge minus kleine Toleranz

  for (int i = 0; i < korrektionSchritte; i += PUMP_PURGE_BLOCK_STEPS)  // Schlauch schrittweise entleeren
  {
    delay(PUMP_SLOW_DELAY_MS);                            // 30ms: Waage nach jedem Block stabilisieren
    if (wifiConnected) { mqttClient.loop(); if (millis() - lastPumpStatus >= 500) { publishStatus(); lastPumpStatus = millis(); } }
    updateDisplay();                                       // Aktuelles Gewicht anzeigen
    if (current_weight >= purgeTarget) break;  // Ziel erreicht: Phase 3 früh beenden
    if (current_weight < -(double)GLASS_JUMP_MIN_G) { glassRemovedAbort = true; break; }
    int chunk = min(PUMP_PURGE_BLOCK_STEPS, korrektionSchritte - i);  // Letzten Block nicht über das Ende
    motorStep(chunk, true, NORMAL_SPEED);  // Motor pumpt Luft durch den Schlauch
  }

  activateAir(false);         // Luftventil schließen: alle Relays wieder AN (Idle)
  if (glassRemovedAbort) { return; }
  delay(PUMP_SLOW_DELAY_MS);  // Letzte 30ms Pause damit Waage den letzten Tropfen noch registriert
  updateDisplay();             // Endgewicht nach vollständiger Dosierung auf LCD zeigen
}

////////////////////////////////////////////////////////////////////////////////
// >>>  SEKTION 4: MANUELLE EINGABE
////////////////////////////////////////////////////////////////////////////////

// Zeigt alle 9 Flüssigkeiten mit ihren eingestellten Mengen (scrollbar).
// Index 9 = START MANUELL Button.
// Mengen werden in handleMenuNavigation() per Encoder in 5ml-Schritten angepasst.

void displayManualInput() 
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("= Manuelle Eingabe =");

  // Begrenzung: 0-8 sind Flüssigkeiten, 9 ist der Start-Button
  selectedIndex = constrain(selectedIndex, 0, 9);

  // Wir zeigen 3 Zeilen an
  for (int i = 0; i < 3; i++) 
  {
    int itemIndex = i + (selectedIndex > 2 ? selectedIndex - 2 : 0);
    if (itemIndex > 9) continue; // Nicht über "Start Manuell" hinausgehen

    lcd.setCursor(0, i + 1);

    if (itemIndex == selectedIndex) 
    {
      lcd.print(">"); // Auswahl-Cursor
    } 
    else 
    {
      lcd.print(" ");
    }

    if (itemIndex < 9) 
    {
      // Anzeige für Flüssigkeiten 1-9
      if ( itemIndex == selectedIndex) 
      {
        // EDIT-MODUS: Zeige Nummer und Namen
        lcd.print(itemIndex + 1);
        lcd.print(".");
        // Namen kürzen damit es passt (max 12 Zeichen)
        String shortName = fluids[itemIndex].name.substring(0, 12);
        lcd.print(shortName);
        lcd.setCursor(15, i + 1); // Reserviere letzte 5 Spalten für ml (Platz für 999ml)
        if (manualAmounts[itemIndex] < 10) lcd.print("  ");
        else if (manualAmounts[itemIndex] < 100) lcd.print(" ");
        lcd.print(manualAmounts[itemIndex]);
        lcd.print("ml");
      } 
      else 
      {
        // LISTEN-MODUS: Name und Menge
        lcd.print(itemIndex + 1);
        lcd.print(".");
        // Namen kürzen (max 12 Zeichen)
        String shortName = fluids[itemIndex].name.substring(0, 12);
        lcd.print(shortName);
        
        lcd.setCursor(15, i + 1); // Reserviere letzte 5 Spalten für ml (Platz für 999ml)
        if (manualAmounts[itemIndex] < 10) lcd.print("  ");
        else if (manualAmounts[itemIndex] < 100) lcd.print(" ");
        lcd.print(manualAmounts[itemIndex]);
        lcd.print("ml");
      }
    } 
    else if (itemIndex == 9) 
    {
      // Anzeige für den Start-Button
      lcd.print("START MANUELL");
    }
  }
}

// Bearbeitungsbildschirm für eine einzelne Flüssigkeit.
// Encoder dreht die Menge in 5ml-Schritten (Logik in handleMenuNavigation).
// Back-Knopf speichert und kehrt zur Liste zurück.
void displayManualInputEditing() 
{
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("= Menge justieren = ");
  lcd.setCursor(0, 1); lcd.print(fluids[selectedFluidSlot].name);
  lcd.setCursor(0, 2);
  lcd.print("Menge: ");
  lcd.print(manualAmounts[selectedFluidSlot]);
  lcd.print(" ml");
  lcd.setCursor(0, 3); lcd.print("Dreh: +- "); lcd.print(manual_input_increment); lcd.print(" ml Back: OK");
}

////////////////////////////////////////////////////////////////////////////////
// >>>  SEKTION 5: FLÜSSIGKEITEN & KALIBRIERUNG
////////////////////////////////////////////////////////////////////////////////

// Zeigt die 9 konfigurierten Flüssigkeiten (scrollbar).
// Knopfdruck öffnet die Kategorieauswahl um eine andere Flüssigkeit zuzuweisen.
void displayFluidSelection() 
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("== FLUESSIGKEITEN ==");
  
  selectedIndex = constrain(selectedIndex, 0, 8);
  
  for (int i = 0; i < 3; i++) 
  {
    lcd.setCursor(0, i + 1);
    int itemIndex = i + (selectedIndex > 2 ? selectedIndex - 2 : 0);
    lcd.print(itemIndex == selectedIndex ? "> " : "  ");
    if (itemIndex < 9)
    {
      lcd.print(itemIndex + 1);
      lcd.print(".");
      lcd.print(fluids[itemIndex].name);
    }
  }
}

// Zeigt die 4 Flüssigkeitskategorien: Alkoholische, Saefte, Sirups, Sonstige.
// Knopfdruck wechselt zu displayFluidFromCategory() um eine Flüssigkeit auszuwählen.
void displayFluidCategories()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("==== KATEGORIE ====");
  
  selectedCategory = constrain(selectedCategory, 0, 3);
  
  String categories[4] = {"Alkoholische", "Saefte", "Sirups", "Sonstige"};
  
  for (int i = 0; i < 3; i++) 
  {
    lcd.setCursor(0, i + 1);
    int itemIndex = i + (selectedCategory > 2 ? selectedCategory - 2 : 0);
    lcd.print(itemIndex == selectedCategory ? "> " : "  ");
    if (itemIndex < 4) lcd.print(categories[itemIndex]);
  }
}

// Zeigt alle Flüssigkeiten einer Kategorie aus FLUID_NAMES.
// categoryStart[] gibt an bei welchem FLUID_NAMES-Index jede Kategorie beginnt:
//   0=Alkoholische (0–11), 1=Saefte (12–23), 2=Sirups (24–35), 3=Sonstige (36–51)
// Unterstriche in Namen werden für die Anzeige durch Leerzeichen ersetzt.
void displayFluidFromCategory()
{
  lcd.clear();
  
  String categoryNames[4] = {"Alkoholische", "Saefte", "Sirups", "Sonstige"};
  int categoryStart[4] = {0, 12, 24, 36};   // Erster FLUID_NAMES-Index pro Kategorie
  int categoryEnd[4]   = {11, 23, 35, 51};  // Letzter FLUID_NAMES-Index pro Kategorie
  
  lcd.setCursor(0, 0);
  lcd.print(categoryNames[selectedCategory]);  // Kategoriename als Titel
  
  int start = categoryStart[selectedCategory];
  int end   = categoryEnd[selectedCategory];
  int categoryFluidCount = end - start + 1;  // Anzahl der Flüssigkeiten in dieser Kategorie
  
  selectedIndex = constrain(selectedIndex, 0, categoryFluidCount - 1);
  
  for (int i = 0; i < 3 && i < categoryFluidCount; i++) 
  {
    lcd.setCursor(0, i + 1);
    int itemIndex = i + (selectedIndex > 2 ? selectedIndex - 2 : 0);
    
    if (itemIndex < categoryFluidCount)
    {
      lcd.print(itemIndex == selectedIndex ? "> " : "  ");
      int fluidNameIndex = start + itemIndex;
      if (fluidNameIndex <= end)
      {
        String fluidName = FLUID_NAMES[fluidNameIndex];
        fluidName.replace("_", " ");  // "Weisser_Rum" → "Weisser Rum"
        lcd.print(fluidName);
      }
    }
  }
}

// Zeigt die Kalibrierungswerte der ausgewählten Flüssigkeit:
//   calibration_ml  = wie viele ml bei 1000 Motorschritten gepumpt werden.
//                     Beispiel: 10.5 → 1000 Schritte = 10.5 ml
//   korrektur_faktor = bekannte ml die nach dem Pumpen noch im Schlauch bleiben.
//                     Dieser Wert wird in pumpFluid() für die Schlauchrest-Logik genutzt.
void displayFluidInput() 
{
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(fluids[selectedIndex].name);
  lcd.setCursor(0, 1); lcd.print("Kalibrierung (ml/1000s)");
  lcd.setCursor(0, 2);
  lcd.print("Wert: ");
  lcd.print(fluids[selectedIndex].calibration_ml);
  lcd.setCursor(0, 3);
  lcd.print("Korrektur: ");
  lcd.print(fluids[selectedIndex].korrektur_faktor);
  lcd.print("ml");
}

////////////////////////////////////////////////////////////////////////////////
// >>>  SEKTION 6: REINIGUNG
////////////////////////////////////////////////////////////////////////////////

// Zeigt die Glasgröße-Auswahl (Klein/Normal/Groß) vor dem Rezeptstart.
// selectedIndex 0=Klein(250ml), 1=Normal(500ml), 2=Groß(750ml)
void displayGlassSizeSelect()
{
  const char* names[5]   = { "Klein ", "Mittel", "Normal", "Gross ", "Orig. " };
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("=== GLASGROESSE ===");
  selectedIndex = constrain(selectedIndex, 0, 4);
  // Zeige die 3 Einträge rund um selectedIndex (Scroll)
  int startRow = constrain(selectedIndex - 1, 0, 2);
  for (int row = 0; row < 3; row++)
  {
    int idx = startRow + row;
    lcd.setCursor(0, row + 1);
    lcd.print(idx == selectedIndex ? "> " : "  ");
    char buf[19];
    if (GLASS_SIZE_ML[idx] > 0)
      snprintf(buf, sizeof(buf), "%-6s       %4dml", names[idx], GLASS_SIZE_ML[idx]);
    else
      snprintf(buf, sizeof(buf), "%-6s  Original  ", names[idx]);
    lcd.print(buf);
  }
}

// Zeigt das Reinigungsmenü mit zwei Optionen: "Stop" (Index 0) oben,
// "Starten" (Index 1) unten. Zeile 3 zeigt den aktuellen Status (Aktiv/Inaktiv).
// Stop steht absichtlich oben damit man nicht aus Versehen startet.
void displayCleaningMode() 
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("==== REINIGUNG ====");
  
  selectedIndex = constrain(selectedIndex, 0, 1);
  
  lcd.setCursor(0, 1);
  lcd.print(selectedIndex == 0 ? "> Reinigung Stop" : "  Reinigung Stop");
  
  lcd.setCursor(0, 2);
  lcd.print(selectedIndex == 1 ? "> Reinigung Starten" : "  Reinigung Starten");
  
  lcd.setCursor(0, 3);
  lcd.print(isCleaningMode ? " Aktiv" : " Inaktiv");
}



// Zeigt das Manuelle Bedienungs-Menü mit 12 Einträgen (Relay 1-10, Motor Vor, Motor Zur).
// Jeder Relay-Eintrag zeigt seinen AN/AUS-Zustand. Motor-Einträge zeigen Richtungspfeile.
// Encoder drehen = scrollen, Knopf drücken = Relay togglen / Motor kurz fahren.
void displayManualControl()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("== MAN. BEDIENUNG ==");

  // 14 Einträge: 0-9 = Relays, 10 = Motor Vor, 11 = Motor Zur, 12 = Schritte, 13 = Geschwindigkeit
  selectedIndex = constrain(selectedIndex, 0, 13);

  // Scroll: ausgewählter Eintrag bleibt immer sichtbar
  int startIdx = constrain(selectedIndex - 1, 0, 11);

  for (int i = 0; i < 3; i++)
  {
    int itemIdx = startIdx + i;
    if (itemIdx > 13) break;

    lcd.setCursor(0, i + 1);
    lcd.print(itemIdx == selectedIndex ? "> " : "  ");

    String label;

    if (itemIdx < 9)
    {
      label = fluids[itemIdx].name;
      label.replace("_", " ");
      if (label.length() == 0) label = "Relay " + String(itemIdx + 1);
    }
    else if (itemIdx == 9)  { label = "Luft"; }
    else if (itemIdx == 10) { label = "Motor Vor"; }
    else if (itemIdx == 11) { label = "Motor Zur"; }
    else if (itemIdx == 12) { label = "Schritte"; }
    else                    { label = "Geschw."; }

    // Name: 13 Zeichen
    if (label.length() > 13) label = label.substring(0, 13);
    while (label.length() < 13) label += " ";
    lcd.print(label);

    // Status / Wert (5 Zeichen)
    if (itemIdx <= 9)
    {
      lcd.print(manualRelayState[itemIdx] ? "[AN] " : "[AUS]");
    }
    else if (itemIdx == 10) { lcd.print(" >>> "); }
    else if (itemIdx == 11) { lcd.print(" <<< "); }
    else if (itemIdx == 12)
    {
      // Umdrehungen: max 4 Zeichen + "U"
      String sStr = String((long)(manualMotorSteps / STEPS_PER_REVOLUTION)) + "U";
      while (sStr.length() < 5) sStr = " " + sStr;
      lcd.print(sStr);
    }
    else
    {
      lcd.print(manualMotorFast ? " SCHN" : " LANG");
    }
  }
}



// Zeigt den Schritte-Editier-Modus.
// Encoder drehen = Schritte in 100er-Schritten ändern (100 – 50000).
// Encoder-Knopf oder Back = bestätigen und zurück zur Manuellen Bedienung.
void displayManualMotorSteps()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("== MOTOR EINSTELL ==");

  lcd.setCursor(0, 1);
  lcd.print("Encoder drehen:");

  lcd.setCursor(0, 2);
  long revolutions = manualMotorSteps / STEPS_PER_REVOLUTION;
  String sStr = String(revolutions);
  // zentrieren auf 20 Zeichen
  int pad = (20 - sStr.length() - 12) / 2;  // 12 = len(" Umdrehungen")
  for (int i = 0; i < pad; i++) lcd.print(" ");
  lcd.print(sStr);
  lcd.print(" Umdrehungen");

  lcd.setCursor(0, 3);
  lcd.print("Knopf=OK  Back=Abbr.");
}
//
// Wichtiges Detail: Das Rezept speichert fluid_index als Index in FLUID_NAMES
// (globale Namensliste). pumpFluid() braucht aber den Index in fluids[] (die 9
// konfigurierten Positionen). Deshalb wird für jede Zutat zuerst der passende
// fluids[]-Slot gesucht (Namensvergleich). Nicht gefundene Zutaten werden übersprungen.
void executeRecipe(int recipeIndex)
{
  Recipe recipe = recipes[recipeIndex];  // Rezept-Kopie holen (Änderungen bleiben lokal)
  glassRemovedAbort = false;             // Abbruch-Flag zurücksetzen

  // Glasgrößen-Skalierung: Mengen mit glassScaleFactor multiplizieren (min. 1 ml)
  for (int i = 0; i < recipe.fluid_count; i++)
    recipe.amounts[i] = max(1, (int)round(recipe.amounts[i] * glassScaleFactor));

  // Gesamtmenge und Startgewicht für Fortschrittsanzeige setzen
  recipeTotalMl = 0.0f;
  for (int i = 0; i < recipe.fluid_count; i++) recipeTotalMl += recipe.amounts[i];
  recipeStartWeight = current_weight;

  float actualAmounts[20] = {0};  // Tatsächlich gepumpte Mengen pro Zutat (Gewichtsdifferenz)

  for (int i = 0; i < recipe.fluid_count; i++)  // Jede Zutat der Reihe nach dosieren
  {
    int fluidNameIdx = recipe.fluid_index[i];  // Index in FLUID_NAMES (z.B. 1 = "Weisser_Rum")
    int amount       = recipe.amounts[i];      // Ziel-Menge in ml für diese Zutat (z.B. 60)
    int fluidSlotIdx = -1;                     // Index in fluids[] (0–8) wird unten gesucht, -1 = nicht gefunden
    
    // Das Rezept kennt nur den FLUID_NAMES-Index.
    // pumpFluid() braucht aber den fluids[]-Index (welcher der 9 Schläuche diese Flüssigkeit hat).
    // → Alle 9 Slots durchsuchen und den übereinstimmenden Namen finden.
    for (int j = 0; j < 9; j++)
    {
      if (fluids[j].name == FLUID_NAMES[fluidNameIdx])  // Stimmt der Name überein?
      {
        fluidSlotIdx = j;  // Gefunden: Slot j hat den richtigen Schlauch
        break;             // Suche beenden
      }
    }
    
    if (fluidSlotIdx == -1) continue;  // Nicht gefunden: Zutat überspringen (isRecipeCompatible hätte dies verhindern sollen)
    
    float weightBefore = current_weight;
    pumpFluid(fluidSlotIdx, amount);  // Alle 3 Phasen (schnell, langsam, Luft) für diese Zutat ausführen
    if (glassRemovedAbort) break;     // Glas entfernt → Rezept abbrechen
    actualAmounts[i] = current_weight - weightBefore;  // Tatsächlich gepumpte Menge messen
    if (actualAmounts[i] < 0) actualAmounts[i] = 0;
    delay(INGREDIENT_PAUSE_MS);       // 500ms Pause: Waage einpendeln lassen bevor nächste Zutat beginnt
  }

  // Glas entfernt während Rezept → Fehlermeldung, kein Summary
  if (glassRemovedAbort)
  {
    glassDetected  = false;
    baselineWeight = 0.0;
    glassWeight    = 0.0;
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("!! ABGEBROCHEN !!");
    lcd.setCursor(0, 1); lcd.print("Glas entfernt!");
    lcd.setCursor(0, 2); lcd.print("Rezept gestoppt.");
    delay(3000);
    currentMenu = MAIN_MENU;
    selectedIndex = 0;
    displayNeedsUpdate = true;
    return;
  }

  // ===== ABSCHLUSSANZEIGE =====
  // Nach allen Zutaten: scrollbare Zusammenfassung was gepumpt wurde.
  // Encoder dreht zum Scrollen, Knopf oder Back schließt die Anzeige.
  {
    int summaryIndex = 0;          // Welche Zeile ist gerade ausgewählt (für den Cursor-Pfeil)
    bool done = false;             // Schleife läuft bis Benutzer Knopf drückt
    encoderButtonPressed = false;  // Altes Flag löschen damit kein versehentliches Sofortbeenden

    while (!done)  // Schleife: Display neu aufbauen nach jeder Encoder-Drehung
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("== FERTIG! Ergebnis");  // Überschrift: signalisiert dass der Cocktail fertig ist

      // Scroll-Offset: summaryIndex soll immer in den 3 sichtbaren Zeilen bleiben
      int startIdx = (summaryIndex > 2) ? summaryIndex - 2 : 0;  // Erste sichtbare Zeile
      for (int row = 0; row < 3; row++)   // Zeilen 1-3 des LCD befüllen
      {
        int idx = startIdx + row;                          // Absoluter Zutaten-Index
        if (idx >= recipe.fluid_count) break;              // Nicht mehr Zutaten als vorhanden zeigen

        lcd.setCursor(0, row + 1);                         // LCD-Zeile anspringen (1, 2 oder 3)
        if (idx == summaryIndex) lcd.print(">"); else lcd.print(" ");  // Cursor-Pfeil für ausgewählte Zeile

        String n = FLUID_NAMES[recipe.fluid_index[idx]];  // Rohname holen (z.B. "Weisser_Rum")
        n.replace("_", " ");                               // Unterstrich → Leerzeichen für bessere Lesbarkeit
        if (n.length() > 10) n = n.substring(0, 10);      // Auf 10 Zeichen kürzen
        lcd.print(n);
        for (int s = n.length(); s < 10; s++) lcd.print(" ");  // Auf 10 Zeichen auffüllen

        // Ist/Soll rechtsbündig: z.B. "48/60ml" (9 Zeichen)
        String actualStr = String((int)(actualAmounts[idx] + 0.5f));
        String targetStr = String(recipe.amounts[idx]);
        String amtStr = actualStr + "/" + targetStr + "ml";
        for (int s = amtStr.length(); s < 9; s++) lcd.print(" ");  // Rechtsbündig auffüllen
        lcd.print(amtStr);
      }

      // Auf Eingabe warten: Encoder drehen scrolt, Knopf/Back schlieSSt die Anzeige
      // Auto-Close nach 10s damit Web-Interface nicht blockiert bleibt
      unsigned long summaryTimeout = millis() + 10000UL;
      while (true)
      {
        if (wifiConnected) mqttClient.loop();  // MQTT am Leben halten
        // Countdown in Zeile 0 rechts anzeigen
        int secsLeft = (int)((summaryTimeout - millis()) / 1000) + 1;
        if (secsLeft < 0) secsLeft = 0;
        lcd.setCursor(16, 0); lcd.print(" "); lcd.print(secsLeft < 10 ? " " : ""); lcd.print(secsLeft);
        if (encoderCounter != 0)  // Encoder wurde gedreht seit letztem Prüfen?
        {
          summaryIndex += (encoderCounter > 0) ? 1 : -1;
          encoderCounter = 0;
          summaryIndex = constrain(summaryIndex, 0, recipe.fluid_count - 1);
          summaryTimeout = millis() + 10000UL;  // Timeout zurücksetzen bei Interaktion
          break;
        }
        if (encoderButtonPressed || digitalRead(BACK_BUTTON) == LOW)
        {
          done = true;
          break;
        }
        if (millis() >= summaryTimeout) { done = true; break; }  // Auto-Close
        delay(UI_SUMMARY_POLL_MS);
      }
      encoderButtonPressed = false;  // Flag löschen damit nächster Knopfdruck frisch erkannt wird
    }
  }

  if (wifiConnected) publishRecipeResult(recipe, actualAmounts);  // Ergebnis via MQTT senden

  currentMenu = MAIN_MENU;  // Zurück ins Hauptmenü nach dem Cocktail
  selectedIndex = 0;        // Cursor auf den ersten Hauptmenü-Eintrag
}

// Führt alle Reinigungszyklen blockierend aus (loop() läuft währenddessen nicht).
// Abbruch ist jederzeit per Back-Button möglich (zwischen Relay-Schritten oder in der Wartezeit).
// Nach Abbruch werden alle Relays sicher geschlossen.
void startCleaning() 
{
  isCleaningMode = true;   // Flag setzen: System ist jetzt im Reinigungsmodus
  bool aborted = false;    // Wird true wenn Back-Button während der Reinigung gedrückt wird

  // Schrittzahl für CLEANING_DURATION_SECONDS Sekunden Motor-Laufzeit berechnen.
  // Formel: Sekunden × 1.000.000µs/s  ÷  (2 × NORMAL_SPEED µs/Halbpuls) = Schritte
  // Der Faktor 2: jeder Schritt = HIGH (speed µs) + LOW (speed µs) = 2 × speed µs gesamt.
  long cleaningSteps = ((long)CLEANING_DURATION_SECONDS * 1000000L) / (2L * NORMAL_SPEED);

  for (int cycle = 0; cycle < CLEANING_CYCLES && !aborted; cycle++)  // Jeden Zyklus durchlaufen
  {
    // Vor dem letzten Zyklus warnen: Benutzer soll Einlass-Schläuche herausziehen.
    // Letzter Zyklus = Schläuche leerblasen (kein Wasser mehr ansaugen).
    if (cycle == CLEANING_CYCLES - 1)  // Ist das der letzte Zyklus?
    {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("= Letzter Zyklus! =");  // Warnung: letzter Durchlauf
      lcd.setCursor(0, 1); lcd.print("Schlaeuche 1-9 raus");   // Anweisung: Schläuche aus dem Behälter
      lcd.setCursor(0, 2); lcd.print("Motor Schlauch nicht");  // Motorschlauch bleibt drin (Pumpenmechanismus)
      lcd.setCursor(0, 3); lcd.print("OK:Taste Back:Stop");    // Bedienanleitung für den Benutzer

      encoderButtonPressed = false;  // Altes Knopf-Flag löschen
      // Blockierend warten bis Benutzer OK (Encoder-Knopf) oder Back drückt.
      // loop() läuft hier nicht → Back-Button direkt per digitalRead pollen.
      while (!encoderButtonPressed && digitalRead(BACK_BUTTON) == HIGH) delay(LOOP_DELAY_MS);
      if (digitalRead(BACK_BUTTON) == LOW) { aborted = true; }  // Back = Reinigung abbrechen
      encoderButtonPressed = false;  // Knopf-Flag zurücksetzen
    }

    // Alle 9 Flüssigkeits-Schläuche (Relay 0–8) + Luftventil (Relay 9) nacheinander durchspülen.
    for (int i = 0; i < 10 && !aborted; i++)  // Jeden der 10 Schläuche der Reihe nach reinigen
    {
      // Abbruch-Check am Anfang jedes neuen Relays: sauberster Punkt zum Stoppen
      if (digitalRead(BACK_BUTTON) == LOW)
      {
        aborted = true;  // Abbruch-Flag setzen
        break;           // Innere Schleife verlassen
      }

      // Fortschritt auf LCD anzeigen: welcher Zyklus, welches Relay gerade läuft
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Zyklus "); lcd.print(cycle + 1); lcd.print("/"); lcd.print(CLEANING_CYCLES);  // z.B. "Zyklus 2/3"
      lcd.setCursor(0, 1);
      lcd.print("Relay "); lcd.print(i + 1); lcd.print("/10 laeuft...");  // z.B. "Relay 5/10 laeuft..."
      lcd.setCursor(0, 2); lcd.print("Motor laeuft...");    // Statusmeldung während Motor dreht
      lcd.setCursor(0, 3); lcd.print("Back = Abbrechen");   // Abbruchhilfe für Benutzer

      // Nur Ventil i öffnen: alle anderen bleiben zu damit wirklich nur ein Schlauch gespült wird
      for (int j = 0; j < NUM_RELAYS; j++) activateRelay(j, true);   // Alle Relays AN (alle Ventile geschlossen)
      activateRelay(i, false);  // Relay i AUS → NC-Kontakt öffnet → Wasser kann durch Schlauch i fließen
      bool stopped = motorStep(cleaningSteps, true, NORMAL_SPEED);    // Motor für cleaningSteps laufen lassen
      activateRelay(i, true);   // Relay i wieder AN → Ventil i schließt sich
      if (stopped) aborted = true;      // motorStep() gab true zurück = Back wurde gedrückt → Abbruch
      delay(CLEANING_RELAY_PAUSE_MS);   // 500ms Pause: Wasser aus dem Schlauch ablaufen lassen
    }
  }

  // ---- Abschlussmeldung ----
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("==== REINIGUNG ====");  // Titelzeile
  if (aborted)  // Wurde die Reinigung abgebrochen?
  {
    lcd.setCursor(0, 1); lcd.print("Abgebrochen!");       // Fehlermeldung
    lcd.setCursor(0, 2); lcd.print("Alle Ventile zu.");   // Beruhigung: kein Ventil steht offen
  }
  else  // Normale Beendigung aller Zyklen
  {
    lcd.setCursor(0, 1); lcd.print("Fertig!");            // Erfolgsmeldung
    lcd.setCursor(0, 2); lcd.print("Alle 10 Relays OK");  // Bestätigung dass alle Schläuche gespült wurden
  }
  delay(UI_DONE_DISPLAY_MS);  // 2 Sekunden anzeigen damit Benutzer die Meldung lesen kann

  stopCleaning();              // Alle Relays sicher in Idle-Zustand bringen (alle AN)
  currentMenu = CLEANING_MODE; // Zurück zum Reinigungsmenü-Zustand
  selectedIndex = 0;           // Cursor auf erste Option ("Reinigung Stop")
  displayNeedsUpdate = true;   // Display-Update anfordern damit Menü neu gezeichnet wird

  lcd.setCursor(0, 0); lcd.print("==== REINIGUNG ====");
  if (aborted)
  {
    lcd.setCursor(0, 1); lcd.print("Abgebrochen!");
    lcd.setCursor(0, 2); lcd.print("Alle Ventile zu.");
  }
  else
  {
    lcd.setCursor(0, 1); lcd.print("Fertig!");
    lcd.setCursor(0, 2); lcd.print("Alle 10 Relays OK");
  }
  delay(UI_DONE_DISPLAY_MS);

  stopCleaning();
  currentMenu = CLEANING_MODE;
  selectedIndex = 0;
  displayNeedsUpdate = true;
}

// Setzt isCleaningMode auf false und schließt alle 10 Relays.
// Wird auch beim Navigieren zurück ins Hauptmenü aufgerufen als Sicherheitsmaßnahme.
// Sicherheitsfunktion: setzt alle Relays in Idle-Zustand.
// Wird sowohl nach normalem Abschluss als auch nach Abbruch aufgerufen.
// Wird außerdem beim Zurücknavigieren ins Hauptmenü aufgerufen damit
// kein Ventil versehentlich offen bleibt.
void stopCleaning() 
{
  isCleaningMode = false;
  for (int i = 0; i < NUM_RELAYS; i++)
    activateRelay(i, true);
}

////////////////////////////////////////////////////////////////////////////////
// >>>  SEKTION 7: MQTT PUBLISH-FUNKTIONEN
////////////////////////////////////////////////////////////////////////////////

// Serialisiert die 9 Flüssigkeiten als JSON und publiziert retained.
// Format: [{"slot":0,"name":"Vodka","calibration_ml":10.0,"korrektur_faktor":10}]
void publishFluids()
{
  if (!mqttClient.connected()) return;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < 9; i++)
  {
    JsonObject o = arr.add<JsonObject>();
    o["slot"]            = i;
    o["name"]            = fluids[i].name;
    o["calibration_ml"]  = fluids[i].calibration_ml;
    o["korrektur_faktor"] = fluids[i].korrektur_faktor;
  }
  String out; serializeJson(doc, out);
  mqttPublish(TOPIC_STATUS_FLUIDS, out, true);  // retained = Browser bekommt sofort aktuellen Wert
}

// Serialisiert alle Relay-Zustände als JSON-Array und publiziert retained.
// Format: [false,false,...] — Index 0–8 = Flüssigkeit, Index 9 = Luft
void publishRelays()
{
  if (!mqttClient.connected()) return;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < NUM_RELAYS; i++)
    arr.add(manualRelayState[i]);
  String out; serializeJson(doc, out);
  mqttPublish(TOPIC_STATUS_RELAYS, out, true);
}

// Publiziert alle offline Rezepte als JSON (einmalig bei Connect, retained).
// Format: [{"name":"Mojito","fluid_count":3,"fluid_index":[...],"amounts":[...]}]
void publishOfflineRecipes()
{
  if (!mqttClient.connected()) return;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < recipeCount; i++)
  {
    JsonObject r = arr.add<JsonObject>();
    r["index"]       = i;
    r["name"]        = recipes[i].name;
    r["fluid_count"] = recipes[i].fluid_count;
    r["compatible"]  = isRecipeCompatible(i);
    JsonArray fi = r["fluid_index"].to<JsonArray>();
    JsonArray am = r["amounts"].to<JsonArray>();
    JsonArray fn = r["fluid_names"].to<JsonArray>();
    for (int j = 0; j < recipes[i].fluid_count; j++)
    {
      fi.add(recipes[i].fluid_index[j]);
      am.add(recipes[i].amounts[j]);
      fn.add(FLUID_NAMES[recipes[i].fluid_index[j]]);
    }
  }
  String out; serializeJson(doc, out);
  mqttPublish(TOPIC_RECIPES_OFFLINE, out, true);
}

// Publiziert Waage, Pump-Status und Menü-Zustand periodisch.
void publishStatus()
{
  if (!mqttClient.connected()) return;

  // Waage: nur wenn Wert sich signifikant geändert hat (vermeidet Nachrichten-Flood)
  static double lastPublishedWeight = -999.0;
  if (fabs(current_weight - lastPublishedWeight) >= 0.5)
  {
    lastPublishedWeight = current_weight;
    mqttPublish(TOPIC_STATUS_WEIGHT, String(current_weight, 1));
  }

  // Pump-Status
  {
    JsonDocument doc;
    doc["running"]        = isRecipeRunning;
    doc["cleaning"]       = isCleaningMode;
    doc["glass_detected"] = glassDetected;
    doc["glass_detecting"]= glassDetecting;
    doc["glass_weight"]   = (int)round(glassWeight);
    // Fortschritt 0-100: wie viel ml seit Rezeptstart ins Glas gepumpt wurden
    int progress = 0;
    if (isRecipeRunning && recipeTotalMl > 0.0f)
    {
      float poured = current_weight - recipeStartWeight;
      if (poured < 0.0f) poured = 0.0f;
      progress = (int)((poured / recipeTotalMl) * 100.0f);
      if (progress > 100) progress = 100;
    }
    doc["progress"] = progress;
    String out; serializeJson(doc, out);
    mqttPublish(TOPIC_STATUS_PUMP, out);
  }

  // Relay-Zustände (jede 2s aktualisieren damit Web-UI immer aktuell ist)
  publishRelays();

  // Menü-Zustand (für Debugging / Skin-Anpassung im Browser)
  {
    JsonDocument doc;
    doc["menu"]          = (int)currentMenu;
    doc["selectedIndex"] = selectedIndex;
    String out; serializeJson(doc, out);
    mqttPublish(TOPIC_STATUS_MENU, out);
  }
}

////////////////////////////////////////////////////////////////////////////////
// >>>  SEKTION 8: ONLINE-REZEPTE
////////////////////////////////////////////////////////////////////////////////

// Lädt Rezepte von ONLINE_RECIPES_URL via HTTP GET und füllt onlineRecipes[].
// Zeigt während des Requests "Lade..." auf dem LCD.
// JSON-Format der Gist-Datei:
//   [{"name":"Testcocktail","ingredients":[{"fluid":"Vodka","amount":60}]}]
void fetchOnlineRecipes()
{
  if (!wifiConnected)
  {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Kein WiFi!");
    lcd.setCursor(0, 1); lcd.print("Online Rezepte n/a");
    delay(2000);
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("=  Online Rezepte =");
  lcd.setCursor(0, 1); lcd.print("Lade...");

  HTTPClient http;
  // Cache-Busting: millis() als Query-Parameter verhindert dass GitHub CDN die alte Version liefert
  String url = String(ONLINE_RECIPES_URL) + "?t=" + String(millis());
  http.begin(url);
  http.addHeader("Cache-Control", "no-cache");
  int code = http.GET();

  if (code != 200)
  {
    lcd.setCursor(0, 1); lcd.print("Fehler: HTTP ");
    lcd.print(code);
    lcd.setCursor(0, 2); lcd.print("Prüfe URL/Internet");
    http.end();
    delay(2500);
    return;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err)
  {
    lcd.setCursor(0, 1); lcd.print("JSON-Fehler!");
    lcd.print(err.c_str());
    delay(2500);
    return;
  }

  onlineRecipeCount = 0;
  for (JsonObject rj : doc.as<JsonArray>())
  {
    if (onlineRecipeCount >= 20) break;
    Recipe& r = onlineRecipes[onlineRecipeCount];
    r.name = rj["name"] | String("Unbekannt");
    r.fluid_count = 0;
    for (JsonObject ing : rj["ingredients"].as<JsonArray>())
    {
      if (r.fluid_count >= 9) break;
      int i = r.fluid_count;
      String fname = ing["fluid"] | String("");
      r.fluid_index[i] = 0;
      for (int j = 0; j < 100; j++)
      {
        if (FLUID_NAMES[j] == fname) { r.fluid_index[i] = j; break; }
      }
      r.amounts[i] = ing["amount"] | 0;
      r.fluid_count++;
    }
    onlineRecipeCount++;
  }

  // Online-Rezepte über MQTT publizieren damit Browser sie auch sieht
  if (mqttClient.connected())
  {
    JsonDocument pubDoc;
    JsonArray arr = pubDoc.to<JsonArray>();
    for (int i = 0; i < onlineRecipeCount; i++)
    {
      JsonObject ro = arr.add<JsonObject>();
      ro["name"] = onlineRecipes[i].name;
      ro["fluid_count"] = onlineRecipes[i].fluid_count;
      JsonArray fn = ro["fluid_names"].to<JsonArray>();
      JsonArray am = ro["amounts"].to<JsonArray>();
      for (int j = 0; j < onlineRecipes[i].fluid_count; j++)
      {
        fn.add(FLUID_NAMES[onlineRecipes[i].fluid_index[j]]);
        am.add(onlineRecipes[i].amounts[j]);
      }
    }
    String out; serializeJson(pubDoc, out);
    mqttPublish(TOPIC_RECIPES_ONLINE, out, true);
  }

  lcd.setCursor(0, 1); lcd.print(onlineRecipeCount);
  lcd.print(" Rezepte geladen   ");
  delay(1500);
  displayNeedsUpdate = true;
}

// Führt ein beliebiges Recipe-Objekt aus (analog zu executeRecipe, aber per Referenz).
// Wird für Online-Rezepte und MQTT-Command-Rezepte genutzt.
void executeOnlineRecipe(Recipe& recipeRef)
{
  Recipe recipe = recipeRef;  // Lokale Kopie damit Skalierung das Original nicht verändert
  glassRemovedAbort = false;  // Abbruch-Flag zurücksetzen

  // Glasgrößen-Skalierung
  for (int i = 0; i < recipe.fluid_count; i++)
    recipe.amounts[i] = max(1, (int)round(recipe.amounts[i] * glassScaleFactor));

  // Gesamtmenge und Startgewicht für Fortschrittsanzeige setzen
  recipeTotalMl = 0.0f;
  for (int i = 0; i < recipe.fluid_count; i++) recipeTotalMl += recipe.amounts[i];
  recipeStartWeight = current_weight;

  float actualAmounts[20] = {0};

  for (int i = 0; i < recipe.fluid_count; i++)
  {
    int fluidNameIdx = recipe.fluid_index[i];
    int amount       = recipe.amounts[i];
    int fluidSlotIdx = -1;
    for (int j = 0; j < 9; j++)
    {
      if (fluids[j].name == FLUID_NAMES[fluidNameIdx]) { fluidSlotIdx = j; break; }
    }
    if (fluidSlotIdx == -1) continue;  // Zutat nicht eingelegt → überspringen
    float weightBefore = current_weight;
    pumpFluid(fluidSlotIdx, amount);
    if (glassRemovedAbort) break;     // Glas entfernt → Rezept abbrechen
    actualAmounts[i] = current_weight - weightBefore;
    if (actualAmounts[i] < 0) actualAmounts[i] = 0;
    delay(INGREDIENT_PAUSE_MS);
  }

  // Glas entfernt während Rezept → Fehlermeldung, kein Summary
  if (glassRemovedAbort)
  {
    glassDetected  = false;
    baselineWeight = 0.0;
    glassWeight    = 0.0;
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("!! ABGEBROCHEN !!");
    lcd.setCursor(0, 1); lcd.print("Glas entfernt!");
    lcd.setCursor(0, 2); lcd.print("Rezept gestoppt.");
    delay(3000);
    currentMenu = MAIN_MENU;
    selectedIndex = 0;
    displayNeedsUpdate = true;
    return;
  }

  // Abschlussanzeige (gleiche Logik wie in executeRecipe)
  {
    int summaryIndex = 0;
    bool done = false;
    encoderButtonPressed = false;
    unsigned long summaryTimeout2 = millis() + 10000UL;
    while (!done)
    {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("== FERTIG! Ergebnis");
      int startIdx = (summaryIndex > 2) ? summaryIndex - 2 : 0;
      for (int row = 0; row < 3; row++)
      {
        int idx = startIdx + row;
        if (idx >= recipe.fluid_count) break;
        lcd.setCursor(0, row + 1);
        if (idx == summaryIndex) lcd.print(">"); else lcd.print(" ");
        String n = FLUID_NAMES[recipe.fluid_index[idx]];
        n.replace("_", " ");
        if (n.length() > 10) n = n.substring(0, 10);
        lcd.print(n);
        for (int s = n.length(); s < 10; s++) lcd.print(" ");
        String actualStr = String((int)(actualAmounts[idx] + 0.5f));
        String targetStr = String(recipe.amounts[idx]);
        String amtStr = actualStr + "/" + targetStr + "ml";
        for (int s = amtStr.length(); s < 9; s++) lcd.print(" ");
        lcd.print(amtStr);
      }
      while (true)
      {
        if (encoderCounter != 0)
        {
          summaryIndex += (encoderCounter > 0) ? 1 : -1;
          encoderCounter = 0;
          summaryIndex = constrain(summaryIndex, 0, recipe.fluid_count - 1);
          summaryTimeout2 = millis() + 10000UL;
          break;
        }
        if (encoderButtonPressed || digitalRead(BACK_BUTTON) == LOW) { done = true; break; }
        if (wifiConnected) mqttClient.loop();
        int secsLeft2 = (int)((summaryTimeout2 - millis()) / 1000) + 1;
        if (secsLeft2 < 0) secsLeft2 = 0;
        lcd.setCursor(16, 0); lcd.print(" "); lcd.print(secsLeft2 < 10 ? " " : ""); lcd.print(secsLeft2);
        if (millis() >= summaryTimeout2) { done = true; break; }
        delay(UI_SUMMARY_POLL_MS);
      }
      encoderButtonPressed = false;
    }
  }
  if (wifiConnected) publishRecipeResult(recipe, actualAmounts);  // Ergebnis via MQTT senden
  currentMenu = MAIN_MENU;
  selectedIndex = 0;
}

// Publiziert das Rezeptergebnis: Ziel- vs. tatsächliche Menge pro Zutat.
void publishRecipeResult(Recipe& recipe, float* actualAmounts)
{
  if (!mqttClient.connected()) return;
  JsonDocument doc;
  doc["name"] = recipe.name;
  float totalTarget = 0, totalActual = 0;
  JsonArray arr = doc["ingredients"].to<JsonArray>();
  for (int i = 0; i < recipe.fluid_count; i++)
  {
    JsonObject ing = arr.add<JsonObject>();
    String n = FLUID_NAMES[recipe.fluid_index[i]];
    n.replace("_", " ");
    ing["name"]   = n;
    ing["target"] = recipe.amounts[i];
    ing["actual"] = (int)(actualAmounts[i] + 0.5f);  // gerundet
    totalTarget += recipe.amounts[i];
    totalActual += actualAmounts[i];
  }
  doc["total_target"] = (int)totalTarget;
  doc["total_actual"]  = (int)(totalActual + 0.5f);
  String out; serializeJson(doc, out);
  mqttPublish(TOPIC_STATUS_RECIPE_RESULT, out);
}

// Zeigt das Rezepttyp-Menü: "Offline Rezepte" / "Online Rezepte"
void displayRecipesTypeMenu()
{
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("==== REZEPTE ====");
  selectedIndex = constrain(selectedIndex, 0, 1);
  lcd.setCursor(0, 1); lcd.print(selectedIndex == 0 ? "> Offline Rezepte " : "  Offline Rezepte ");
  lcd.setCursor(0, 2); lcd.print(selectedIndex == 1 ? "> Online Rezepte  " : "  Online Rezepte  ");
  if (wifiConnected) { lcd.setCursor(0, 3); lcd.print("WiFi:OK "); }
  else               { lcd.setCursor(0, 3); lcd.print("WiFi:NICHT verbunden"); }
}

// Zeigt die Online-Rezeptliste (scrollbar, analog displayRecipesMenu)
void displayOnlineRecipesMenu()
{
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("= ONLINE REZEPTE =");
  if (onlineRecipeCount == 0)
  {
    lcd.setCursor(0, 1); lcd.print("Keine geladen");
    lcd.setCursor(0, 2); lcd.print("Bitte Online-Menue");
    lcd.setCursor(0, 3); lcd.print("betreten zum Laden");
    return;
  }
  selectedIndex = constrain(selectedIndex, 0, onlineRecipeCount - 1);
  int startIdx = (selectedIndex > 2) ? selectedIndex - 2 : 0;
  for (int row = 0; row < 3 && startIdx + row < onlineRecipeCount; row++)
  {
    int idx = startIdx + row;
    lcd.setCursor(0, row + 1);
    lcd.print(idx == selectedIndex ? "> " : "  ");
    String shortName = onlineRecipes[idx].name.substring(0, 16);
    while (shortName.length() < 16) shortName += " ";
    lcd.print(shortName);
    lcd.setCursor(19, row + 1);
    lcd.print(isRecipeCompatible(idx) ? "\xFE" : "X");  // Kompatibilität-Symbol
  }
}

// Zeigt Details eines Online-Rezepts (scrollbar, analog displayRecipeDetails)
void displayOnlineRecipeDetails()
{
  if (selectedOnlineRecipe < 0 || selectedOnlineRecipe >= onlineRecipeCount) return;
  lcd.clear();
  Recipe& recipe = onlineRecipes[selectedOnlineRecipe];

  lcd.setCursor(0, 0);
  String rn = recipe.name;
  if ((int)rn.length() < 20) { int p = (20 - rn.length()) / 2; for (int i=0;i<p;i++) rn=" "+rn; while((int)rn.length()<20) rn+=" "; }
  lcd.print(rn.substring(0, 20));

  int totalOptions = recipe.fluid_count + 2;
  selectedIndex = constrain(selectedIndex, 0, totalOptions - 1);
  int startIdx = (selectedIndex > 2) ? constrain(selectedIndex - 2, 0, totalOptions - 3) : 0;

  for (int row = 0; row < 3 && startIdx + row < totalOptions; row++)
  {
    int idx = startIdx + row;
    lcd.setCursor(0, row + 1);
    lcd.print(idx == selectedIndex ? "> " : "  ");
    if (idx == 0) lcd.print("STARTEN          ");
    else if (idx == 1) lcd.print("STOP             ");
    else
    {
      int ii = idx - 2;
      String fn = FLUID_NAMES[recipe.fluid_index[ii]];
      int amt = recipe.amounts[ii];
      String line = fn.substring(0, 12);
      while ((int)line.length() < 12) line += " ";
      line += ": ";
      if (amt < 10) line += "  ";
      else if (amt < 100) line += " ";
      line += amt; line += "ml";
      while ((int)line.length() < 18) line += " ";
      lcd.print(line.substring(0, 18));
    }
  }
}
