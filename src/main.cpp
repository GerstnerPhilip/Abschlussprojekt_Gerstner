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
// - TB6600 4 A 9-42 V Schrittmotorsteuerung 1,5A, 1,7A Spitze, 24V, 800 Schritte/Umdrehung 
// - 10 Relays (9x Flüssigkeiten, 1x Luft zum Entlüften)
// - Rotary Encoder (mit 1 Button) für Benutzernavigation
//
// ================================================ INCLUDES ================================================

#include <Arduino.h>      // Arduino Basis-Library
#include "HX711.h"        // Wägezelle-Verstärker Library
#include <TimerOne.h>     // Timer-Library für periodische Aufgaben
#include <Wire.h>         // I2C Kommunikations-Library
#include <LiquidCrystal_I2C.h>  // LCD Display Library (I2C Modus)



// ================================================ LCD DISPLAY ================================================
// Initialisiert ein 4x20 LCD Display über I2C Schnittstelle
// Häufige Adressen: 0x27, 0x3F, 0x20, 0x3B - versuche diese wenn nichts angezeigt wird
LiquidCrystal_I2C lcd(0x27, 20, 4);  // Adresse 0x27 (standard)

// LCD Pins:
// SDA -> GPIO 8
// SCL -> GPIO 9



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
  // Alkoholische Getränke - Spirituosen (Indizes 0-7)
  // Sortierung nach Häufigkeit: Vodka, Rum, Gin, Tequila sind am populärsten
  "Vodka","Weisser_Rum","Gin","Tequila","Triple_Sec",
  "Brandy","Whiskey","Dunckler_Rum",
  
  // Säfte - Zitrusfrüchte & Beeren (Indizes 8-15)
  // Zitrus-Säfte sind in fast jedem Cocktail vorhanden
  "Limettensaft","Zitronensaft","Orangensaft","Cranberrysaft",
  "Ananassaft","Grapefruitsaft","Preiselbeersaft","Pfirsichsaft",

  // Sirups & Liköre (Indizes 16-23)
  // Diese geben den Cocktails ihre charakteristischen Geschmäcke
  "Zuckersirup","Grenadinesirup","Kokoslikoer","Minzsirup",
  "Kaffeelikoer","Amaretto","Vanillesirup","Ingwersirup",

  // Bitters & Sonstige Getränke (Indizes 24-31)
  // Für Würze und Geschmackstiefen sowie Mixer
  "Angostura_Bitter","Soda_Wasser","Tonic_Water","Ginger_Ale",
  "Cola","Wasser","Bitterzitronen_Limonade","Kamille_Tee"
};



// ================================================ FLÜSSIGKEITS-ARRAY ================================================
// Dieses Array speichert die 9 konfigurieren Flüssigkeiten für den Automaten
// Der Benutzer wählt 9 Flüssigkeiten aus FLUID_NAMES aus
// Der Kalibrierungsfaktor (10.0 ml) und Korrekturfaktor (10 ml) sind Standardwerte
// Diese sollten für jede Flüssigkeit einzeln kalibriert werden
Fluid fluids[9] = 
{
  {FLUID_NAMES[29], 10.0, 10, 0},  // Flüssigkeit 1 (Standard: Wasser)
  {FLUID_NAMES[29], 10.0, 10, 0},  // Flüssigkeit 2 (Standard: Wasser)
  {FLUID_NAMES[29], 10.0, 10, 0},  // Flüssigkeit 3 (Standard: Wasser)
  {FLUID_NAMES[1], 10.0, 10, 0},  // Flüssigkeit 4 (Standard: Wasser)
  {FLUID_NAMES[1], 10.0, 10, 0},  // Flüssigkeit 5 (Standard: Wasser)
  {FLUID_NAMES[1], 10.0, 10, 0},  // Flüssigkeit 6 (Standard: Wasser)
  {FLUID_NAMES[1], 10.0, 10, 0},  // Flüssigkeit 7 (Standard: Wasser)
  {FLUID_NAMES[1], 10.0, 10, 0},  // Flüssigkeit 8 (Standard: Wasser)
  {FLUID_NAMES[1], 10.0, 10, 0}   // Flüssigkeit 9 (Standard: Wasser)
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
// FLUID_NAMES Indizes (Alkoholische): 0=Vodka, 1=Weisser_Rum, 2=Gin, 3=Tequila, 4=Triple_Sec,
//                                      5=Brandy, 6=Whiskey, 7=Dunckler_Rum
// FLUID_NAMES Indizes (Säfte): 8=Limettensaft, 9=Zitronensaft, 10=Orangensaft, 11=Cranberrysaft,
//                              12=Ananassaft, 13=Grapefruitsaft, 14=Preiselbeersaft, 15=Pfirsichsaft
// FLUID_NAMES Indizes (Sirups/Liköre): 16=Zuckersirup, 17=Grenadinesirup, 18=Kokoslikoer, 19=Minzsirup,
//                                       20=Kaffeelikoer, 21=Amaretto, 22=Vanillesirup, 23=Ingwersirup
// FLUID_NAMES Indizes (Bitters/Mixer): 24=Angostura_Bitter, 25=Soda_Wasser, 26=Tonic_Water, 27=Ginger_Ale,
//                                       28=Cola, 29=Wasser, 30=Bitterzitronen_Limonade, 31=Kamille_Tee

Recipe recipes[30] =
{
  {
    "Mojito",
    {1, 8, 19, 25, -1, -1, -1, -1, -1},     // Weisser_Rum, Limettensaft, Minzsirup, Soda_Wasser
    {60, 30, 20, 100, 0, 0, 0, 0, 0},       // Mengen in ml
    4
  },
  {
    "Cosmopolitan",
    {0, 4, 11, 8, -1, -1, -1, -1, -1},      // Vodka, Triple_Sec, Cranberrysaft, Limettensaft
    {40, 20, 30, 30, 0, 0, 0, 0, 0},
    4
  },
  {
    "Pina Colada",
    {1, 12, 18, 16, -1, -1, -1, -1, -1},    // Weisser_Rum, Ananassaft, Kokoslikoer, Zuckersirup
    {60, 90, 30, 10, 0, 0, 0, 0, 0},
    4
  },
  {
    "Margarita",
    {3, 4, 8, -1, -1, -1, -1, -1, -1},      // Tequila, Triple_Sec, Limettensaft
    {50, 20, 30, 0, 0, 0, 0, 0, 0},
    3
  },
  {
    "Daiquiri",
    {1, 8, 16, -1, -1, -1, -1, -1, -1},     // Weisser_Rum, Limettensaft, Zuckersirup
    {60, 30, 15, 0, 0, 0, 0, 0, 0},
    3
  },
  {
    "Mai Tai",
    {1, 8, 21, 16, -1, -1, -1, -1, -1},     // Weisser_Rum, Limettensaft, Amaretto, Zuckersirup
    {60, 30, 20, 10, 0, 0, 0, 0, 0},
    4
  },
  {
    "Screwdriver",
    {0, 10, -1, -1, -1, -1, -1, -1, -1},    // Vodka, Orangensaft
    {50, 150, 0, 0, 0, 0, 0, 0, 0},
    2
  },
  {
    "Sex on the Beach",
    {0, 11, 10, 8, -1, -1, -1, -1, -1},     // Vodka, Cranberrysaft, Orangensaft, Limettensaft
    {40, 40, 100, 30, 0, 0, 0, 0, 0},
    4
  },
  {
    "Long Island Iced Tea",
    {1, 0, 2, 3, 4, 9, 16, 28, -1},         // Rum, Vodka, Gin, Tequila, Triple_Sec, Zitronensaft, Zuckersirup, Cola
    {15, 15, 15, 15, 15, 30, 30, 100, 0},
    8
  },
  {
    "Tom Collins",
    {2, 9, 16, 25, -1, -1, -1, -1, -1},     // Gin, Zitronensaft, Zuckersirup, Soda_Wasser
    {50, 40, 20, 100, 0, 0, 0, 0, 0},
    4
  },
  {
    "Gimlet",
    {2, 8, 16, -1, -1, -1, -1, -1, -1},     // Gin, Limettensaft, Zuckersirup
    {50, 30, 20, 0, 0, 0, 0, 0, 0},
    3
  },
  {
    "Old Fashioned",
    {6, 16, 24, -1, -1, -1, -1, -1, -1},    // Whiskey, Zuckersirup, Angostura_Bitter
    {50, 10, 5, 0, 0, 0, 0, 0, 0},
    3
  },
  {
    "Espresso Martini",
    {0, 20, 16, -1, -1, -1, -1, -1, -1},    // Vodka, Kaffeelikoer, Zuckersirup
    {50, 30, 10, 0, 0, 0, 0, 0, 0},
    3
  },
  {
    "Caipirinha",
    {1, 8, 16, -1, -1, -1, -1, -1, -1},     // Weisser_Rum, Limettensaft, Zuckersirup
    {60, 30, 20, 0, 0, 0, 0, 0, 0},
    3
  },
  {
    "Hurricane",
    {1, 8, 10, 16, -1, -1, -1, -1, -1},     // Weisser_Rum, Limettensaft, Orangensaft, Zuckersirup
    {70, 30, 30, 20, 0, 0, 0, 0, 0},
    4
  },
  {
    "Dark & Stormy",
    {7, 26, 27, -1, -1, -1, -1, -1, -1},    // Dunckler_Rum, Tonic_Water, Ginger_Ale
    {60, 60, 90, 0, 0, 0, 0, 0, 0},
    3
  },

  // Test-Rezepte (können gelöscht werden)
  {
    "Test Cocktail 1",
    {29, -1, -1, -1, -1, -1, -1, -1, -1},
    {100, 0, 0, 0, 0, 0, 0, 0, 0},
    1
  },
  {
    "Test Cocktail 2",
    {29, 29, -1, -1, -1, -1, -1, -1, -1},
    {100, 100, 0, 0, 0, 0, 0, 0, 0},
    2
  },
  {
    "Test Cocktail 3",
    {29, 29, 29, -1, -1, -1, -1, -1, -1},
    {50, 50, 50, 0, 0, 0, 0, 0, 0},
    3
   }
};

// Aktuelle Anzahl der Rezepte
int recipeCount = 19;  // Wir haben 19 vordefinierte Rezepte



// ================================================ MENÜSTATUS UND MENÜZUSTÄNDE ================================================
// Das System funktioniert nach einem Menü-basierten Zustandsautomat (State Machine)
// In jedem Zustand wird ein anderes Menü angezeigt und andere Befehle werden akzeptiert

enum MenuState 
{
  MAIN_MENU,                     // Startmenü mit 4 Optionen: Rezepte, Manuelle Eingabe, Flüssigkeiten, Reinigung
  RECIPES_MENU,                  // Rezepte auswählen und anzeigen
  RECIPE_DETAILS,                // Details des gewählten Rezepts anzeigen (nur Name)
  MANUAL_INPUT,                  // Benutzer gibt Mengen für jede Flüssigkeit manuell ein
  MANUAL_INPUT_EDITING,          // Benutzer ändert Menge einer Flüssigkeit mit Encoder (neu!)
  FLUID_SELECTION,               // Benutzer wählt aus, welche Flüssigkeiten geladen sind (1-9)
  FLUID_CATEGORY_SELECTION,      // Benutzer wählt eine Kategorie aus (Alkoholische, Säfte, Sirups, Sonstige)
  FLUID_FROM_CATEGORY_SELECTION, // Benutzer wählt eine Flüssigkeit aus der Kategorie
  FLUID_INPUT,                   // Kalibrierung einer Flüssigkeit (ml/1000 Schritte, Schlauchkorrektur)
  CLEANING_MODE,                 // Reinigungsmodus (Motor läuft kontinuierlich)
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
void displayMainMenu();        // Hauptmenü mit 4 Optionen
void displayRecipesMenu();     // Rezeptliste anzeigen
void displayRecipeDetails();   // Details eines Rezepts
void displayManualInput();     // Manuelle Eingabemaßnahmen
void displayManualInputEditing();  // Bearbeite Menge einer Flüssigkeit mit Encoder
void displayFluidSelection();  // Wähle Flüssigkeiten aus
void displayFluidCategories(); // Wähle Kategorie (Alkoholische, Säfte, Sirups, Sonstige)
void displayFluidFromCategory();  // Wähle Flüssigkeit aus Kategorie
void displayFluidInput();      // Kalibrierung einer Flüssigkeit
void displayCleaningMode();    // Reinigungsmodus
void displayManualControl();   // Manuelle Bedienung: Relays und Motor direkt ansteuern
void displayManualMotorSteps(); // Schrittanzahl einstellen
void displayRunningRecipe();   // Fortschritt während Rezepterzeugung

// NAVIGATIONSFUNKTIONEN
void handleMenuNavigation();   // Verarbeite Encoder-Eingaben und wechsle Menüstaaten

// REZEPTAUSFÜHRUNG
void executeRecipe(int recipeIndex);  // Führe ein Rezept aus (dosiere alle Zutaten)
boolean isRecipeCompatible(int recipeIndex);  // Prüfe, ob alle Rezeptzutaten verfügbar sind

// REINIGUNGSFUNKTIONEN
void startCleaning();  // Starte Reinigungsmodus
void stopCleaning();   // Beende Reinigungsmodus



// ================================================ GLOBALE FLAGS ================================================
boolean isCleaningMode = false;  // Sind wir gerade im Reinigungsmodus?
bool manualRelayState[10] = {false,false,false,false,false,false,false,false,false,false};  // Relay-Zustände im Manuellen Bedienungs-Menü
long manualMotorSteps = 500;   // Schrittzahl für Motor Vor/Zur im Manuellen Menü
bool manualMotorFast  = true;  // true = NORMAL_SPEED (schnell), false = SLOW_SPEED (langsam)

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// >>>  SEKTION 1: SYSTEM & HARDWARE
// setup(), loop(), Interrupts, Sensoren, Schrittmotor, Relay-Ventile
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
  Serial.begin(115200);
  delay(2000);

  // I2C Pull-up Widerstände aktivieren (GPIO 8=SDA, 9=SCL)
  // Ohne Pull-ups antwortet das LCD nicht auf I2C
  pinMode(8, INPUT_PULLUP);
  pinMode(9, INPUT_PULLUP);
  delay(10);
  Wire.begin(8, 9);
  Wire.setClock(100000);
  delay(100);

  lcd.init();
  lcd.noCursor();
  lcd.noBlink();
  lcd.backlight();
  lcd.clear();
  lcd.print("Booting...");
  Serial.println("1. LCD OK");
  delay(1000);

  // LOW auf ENABLE-Pin aktiviert den TB6600-Treiber
  pinMode(STEPPER_STEP_PIN, OUTPUT);
  pinMode(STEPPER_DIR_PIN, OUTPUT);
  pinMode(STEPPER_ENABLE_PIN, OUTPUT);
  digitalWrite(STEPPER_ENABLE_PIN, LOW);
  Serial.println("2. Motor OK");

  // Alle 10 Relays als Ausgänge, sofort AN (LOW = Relay erregt)
  // Idle-Zustand: alle Relays AN. Nur beim Pumpen gehen alle bis auf das Ziel-Relay aus.
  for (int i = 0; i < NUM_RELAYS; i++) 
  {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);  // LOW = Relay AN (Idle-Zustand)
  }
  Serial.println("3. Relays OK");

  // INPUT_PULLUP: Pin liest HIGH wenn nichts angeschlossen, bei Bewegung LOW
  // CLK und DT brauchen Pull-ups damit sie nicht frei floaten (= Zufallswerte)
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT,  INPUT_PULLUP);
  pinMode(ENCODER_SW,  INPUT_PULLUP);
  pinMode(BACK_BUTTON, INPUT_PULLUP);
  Serial.println("4. Encoder OK");

  setupInterrupts();
  Serial.println("5. Interrupts OK");

  // tare() setzt den aktuellen Wert als Nullpunkt (leeres Glas)
  scale.begin(LOADCELL_DT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare();
  Serial.println("6. Scale OK");

  // Keine Timer-ISR: readWeight() wird in loop() per millis() aufgerufen.
  // Scale.get_units() blockiert bis zu 100ms (HX711 @10Hz) – viel zu lange für eine ISR.
  Serial.println("7. Timer OK (millis-basiert)");

  delay(1000);
  lcd.clear();
  lcd.print("System bereit!");
  delay(1000);
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
  // Waage alle 100ms lesen (nicht als ISR, da get_units() blockierend ist)
  static unsigned long lastWeightTime = 0;
  if (millis() - lastWeightTime >= 100)
  {
    lastWeightTime = millis();
    readWeight();
  }

  handleMenuNavigation();
  
  if (currentMenu == RUNNING_RECIPE)
  {
    // Gewicht wird oben alle 100ms aktualisiert, Display zeigt aktuellen Wert
    displayRunningRecipe();
  }
  else if (displayNeedsUpdate) 
  {
    switch (currentMenu) 
    {
      case MAIN_MENU:                      displayMainMenu();           break;
      case RECIPES_MENU:                   displayRecipesMenu();        break;
      case RECIPE_DETAILS:                 displayRecipeDetails();      break;
      case MANUAL_INPUT:                   displayManualInput();        break;
      case MANUAL_INPUT_EDITING:           displayManualInputEditing(); break;
      case FLUID_SELECTION:                displayFluidSelection();     break;
      case FLUID_CATEGORY_SELECTION:       displayFluidCategories();    break;
      case FLUID_FROM_CATEGORY_SELECTION:  displayFluidFromCategory();  break;
      case FLUID_INPUT:                    displayFluidInput();         break;
      case CLEANING_MODE:                  displayCleaningMode();       break;
      case MANUAL_CONTROL:                 displayManualControl();      break;
      case MANUAL_MOTOR_STEPS:             displayManualMotorSteps();   break;
    }
    displayNeedsUpdate = false;
  }
  
  delay(50);
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



// Dreht den Schrittmotor um die angegebene Anzahl Schritte.
// Pro Schritt: HIGH/LOW-Puls an STEP-Pin → TB6600-Treiber macht einen Schritt.
// speed = Wartezeit in Mikrosekunden pro Puls (kleiner = schneller).
// Rückgabe: true wenn Back-Button während des Laufs gedrückt wurde (Abbruch).
// Alle 500 Schritte (~60ms bei NORMAL_SPEED) wird der Back-Button direkt gelesen.
bool motorStep(long steps, boolean forward, int speed)
{
  static bool lastBackState = HIGH;  // Flanken-Erkennung für Back-Button
  digitalWrite(STEPPER_DIR_PIN, forward ? LOW : HIGH);  // Richtung invertiert (NO-Ventil Logik)

  // Anlauframpe: Motor startet bei START_SPEED (langsam/hohes Drehmoment) und
  // beschleunigt linear auf die Zielgeschwindigkeit über ACCEL_STEPS Schritte.
  // Ohne Rampe überfordert ein zu schneller Startpuls den Motor → er bleibt stecken.
  const int  START_SPEED  = 800;  // Anlaufgeschwindigkeit µs (sicheres Anlaufdrehmoment)
  const long ACCEL_STEPS  = 400;  // Schritte bis Zielgeschwindigkeit erreicht ist

  for (long i = 0; i < steps; i++) 
  {
    // Aktuelle Geschwindigkeit berechnen
    int currentSpeed;
    if (speed >= START_SPEED || steps <= ACCEL_STEPS)
    {
      // Ziel ist bereits langsam genug, oder Fahrt zu kurz → keine Rampe nötig
      currentSpeed = speed;
    }
    else if (i < ACCEL_STEPS)
    {
      // Linear von START_SPEED auf speed interpolieren
      currentSpeed = START_SPEED - (int)((long)(START_SPEED - speed) * i / ACCEL_STEPS);
    }
    else
    {
      currentSpeed = speed;
    }

    digitalWrite(STEPPER_STEP_PIN, HIGH);
    delayMicroseconds(currentSpeed);
    digitalWrite(STEPPER_STEP_PIN, LOW);
    delayMicroseconds(currentSpeed);

    // Alle 500 Schritte Back-Button direkt lesen (Polling, da loop() nicht läuft)
    if (i % 500 == 0)
    {
      bool backState = digitalRead(BACK_BUTTON);
      if (backState == LOW && lastBackState == HIGH)  // Flanke HIGH→LOW = Tastendruck
      {
        lastBackState = backState;
        return true;  // Abbruch signalisieren
      }
      lastBackState = backState;
    }
  }
  return false;  // Normal beendet, kein Abbruch
}



// Schaltet ein einzelnes Relay an (true) oder aus (false). Index 0–9.
void activateRelay(int relayIndex, boolean state)
{
  // LOW-Level Trigger Modul: LOW = Relay erregt (AN), HIGH = Relay stromlos (AUS)
  // NC-Ventil am NO-Kontakt des Relays:
  //   state=true  → Relay AN  (LOW)  → NO-Kontakt schließt → Ventilspule an  → Ventil öffnet
  //   state=false → Relay AUS (HIGH) → NO-Kontakt offen    → Ventilspule aus → Feder schließt Ventil
  if (relayIndex >= 0 && relayIndex < NUM_RELAYS) 
    digitalWrite(RELAY_PINS[relayIndex], state ? LOW : HIGH);
}

// Pumpt durch Fluid fluidIndex (state=true) oder stoppt (state=false).
// Idle-Zustand: alle Relays AN. Beim Pumpen: alle AUS außer Ziel-Relay bleibt AN.
void activateFluid(int fluidIndex, boolean state)
{
  if (fluidIndex < 0 || fluidIndex >= 9) return;
  if (state)
  {
    // Alle Relays AUS, dann nur Ziel-Relay AN lassen
    for (int i = 0; i < NUM_RELAYS; i++) activateRelay(i, false);
    activateRelay(fluidIndex, true);
  }
  else
  {
    // Pumpen fertig → alle Relays wieder AN (Idle-Zustand)
    for (int i = 0; i < NUM_RELAYS; i++) activateRelay(i, true);
  }
}

// Öffnet das Luftventil (Relay 9) für Spülung (state=true)
// oder schließt es (state=false).
void activateAir(boolean state)
{
  if (state)
  {
    // Alle Relays AUS, nur Luft-Relay 9 AN
    for (int i = 0; i < NUM_RELAYS; i++) activateRelay(i, false);
    activateRelay(9, true);
  }
  else
  {
    // Luft-Phase fertig → alle Relays wieder AN (Idle-Zustand)
    for (int i = 0; i < NUM_RELAYS; i++) activateRelay(i, true);
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
        // Menge in 5ml-Schritten erhöhen
        manualAmounts[selectedFluidSlot] += 5;
      }
      else if (currentMenu == MANUAL_MOTOR_STEPS)
      {
        // Schritte in 800er-Schritten erhöhen (max. 50000)
        manualMotorSteps = min(50000L, manualMotorSteps + 800);
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
        // Menge in 5ml-Schritten verringern (aber nicht unter 0)
        manualAmounts[selectedFluidSlot] = max(0, manualAmounts[selectedFluidSlot] - 5);
      }
      else if (currentMenu == MANUAL_MOTOR_STEPS)
      {
        // Schritte in 800er-Schritten verringern (min. 0)
        manualMotorSteps = max(0L, manualMotorSteps - 800);
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
          // Option 1: Rezepte -> wechsle zu RECIPES_MENU
          currentMenu = RECIPES_MENU;
          selectedIndex = 0;  // Starte bei erstem Rezept
        } 
        else if (selectedIndex == 1) 
        {
          // Option 2: Manuelle Eingabe -> wechsle zu MANUAL_INPUT
          currentMenu = MANUAL_INPUT;
          selectedIndex = 0;  // Starte bei erster Flüssigkeit
          // Setze alle manuellen Mengen auf 0
          for (int i = 0; i < 9; i++) manualAmounts[i] = 0;
        } 
        else if (selectedIndex == 2) 
        {
          // Option 3: Flüssigkeiten -> wechsle zu FLUID_SELECTION
          currentMenu = FLUID_SELECTION;
          selectedIndex = 0;  // Starte bei erster Flüssigkeit
        } 
        else if (selectedIndex == 3) 
        {
          // Option 4: Reinigung -> wechsle zu CLEANING_MODE
          currentMenu = CLEANING_MODE;
          selectedIndex = 0;  // Starte bei "Reinigung Starten"
          stopCleaning();     // Stelle sicher dass es mit Inaktiv startet
        } 
        else if (selectedIndex == 4) 
        {
          // Option 5: Manuelle Bedienung -> wechsle zu MANUAL_CONTROL
          currentMenu = MANUAL_CONTROL;
          selectedIndex = 0;  // Starte bei Relay 1
          // Alle Relays sicher ausschalten beim Betreten
          for (int i = 0; i < NUM_RELAYS; i++)
          {
            manualRelayState[i] = false;
            activateRelay(i, false);
          }
        }
        break;
        


      // ===== REZEPTE-MENÜ =====
      case RECIPES_MENU:
        // Prüfe ob das Rezept einen Namen hat und kompatibel ist
        if (selectedIndex >= 0 && selectedIndex < recipeCount && recipes[selectedIndex].name != "")  // name="" = leerer Slot, überspringen
        {
          // Nur wenn das Rezept kompatibel ist, erlaube Auswahl
          if (isRecipeCompatible(selectedIndex))  // Alle Zutaten in fluids[] vorhanden?
          {
            // Speichere den Index und wechsle zu RECIPE_DETAILS
            selectedRecipe = selectedIndex;
            currentMenu = RECIPE_DETAILS;
            selectedIndex = 0;  // Starte bei erster Zutat
          }
          // Falls nicht kompatibel, ignoriere Knopfdruck (Benutzer sieht "X" neben dem Rezept)
        }
        break;
        


      // ===== REZEPT-DETAILS =====
      case RECIPE_DETAILS:
        // selectedIndex 0 = STARTEN, 1 = STOP, 2+ = Zutaten
        if (selectedIndex == 0)
        {
          // Benutzer hat "STARTEN" ausgewählt -> führe Rezept aus
          currentMenu = RUNNING_RECIPE;
          executeRecipe(selectedRecipe);
        }
        else if (selectedIndex == 1)
        {
          // Benutzer hat "STOP" ausgewählt -> stoppe Rezept
          stopCleaning();  // Stoppe alle Pumpen/Motoren
          // Bleibe im RECIPE_DETAILS Menü
        }
        break;
        


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
        int categoryStart[4] = {0, 5, 8, 14};  // Erster FLUID_NAMES-Index pro Kategorie
        int fluidNameIndex = categoryStart[selectedCategory] + selectedIndex;  // globaler Index in FLUID_NAMES
        
        if (fluidNameIndex < 100)  // Sicherheit: nicht über FLUID_NAMES Array-Ende hinaus
        {
          fluids[selectedFluidSlot].name = FLUID_NAMES[fluidNameIndex];  // Neue Flüssigkeit zuweisen
          fluids[selectedFluidSlot].calibration_ml = 10.0;  // Kalibrierung auf Standardwert zurücksetzen (muss danach neu kalibriert werden!)
          fluids[selectedFluidSlot].korrektur_faktor = 10;  // Schlauchkorrektur auf Standardwert (ebenfalls re-kalibrieren)
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
      // Wenn in Menge-Bearbeitung, gehe zurück zur Flüssigkeitsliste
      currentMenu = MANUAL_INPUT;
      selectedIndex = selectedFluidSlot;
    }
    else if (currentMenu == FLUID_CATEGORY_SELECTION)
    {
      // Wenn in Kategorieauswahl, gehe zurück zur Flüssigkeitsauswahl
      currentMenu = FLUID_SELECTION;
      selectedIndex = selectedFluidSlot;
    }
    else if (currentMenu == FLUID_FROM_CATEGORY_SELECTION)
    {
      // Wenn in Flüssigkeit-Auswahl aus Kategorie, gehe zurück zur Kategorieauswahl
      currentMenu = FLUID_CATEGORY_SELECTION;
      selectedIndex = 0;
    }
    else if (currentMenu == RECIPE_DETAILS)
    {
      // Wenn in Rezept-Details, gehe zurück zur Rezeptliste
      currentMenu = RECIPES_MENU;
      selectedIndex = 0;  // Zurücksetzen für nächstes Mal
    }
    else if (currentMenu == MANUAL_CONTROL)
    {
      // Alle Relays AN beim Verlassen → Idle-Zustand
      for (int i = 0; i < NUM_RELAYS; i++)
      {
        manualRelayState[i] = false;
        activateRelay(i, true);
      }
      currentMenu = MAIN_MENU;
      selectedIndex = 4;  // Cursor auf "5.Man.Bedienung" zeigen
    }
    else if (currentMenu == MANUAL_MOTOR_STEPS)
    {
      // Abbrechen: zurück zu Manuelle Bedienung ohne Wert zu ändern
      currentMenu = MANUAL_CONTROL;
      selectedIndex = 12;  // Cursor auf "Schritte"
    }
    else
    {
      // Sonst: gehe zum Hauptmenü
      currentMenu = MAIN_MENU;
      selectedIndex = 0;
      stopCleaning();                // Stoppe Reinigungsmodus falls aktiv
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
  lcd.print("==== HAUPTMENU ====");
  
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
  if (fluidIndex < 0 || fluidIndex >= 9) return;

  // LCD-Anzeige aufbauen
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("= WIRD AUSGEFUEHRT =");
  lcd.setCursor(0, 1); lcd.print("Ist:  0.0ml         ");
  lcd.setCursor(0, 2); lcd.print("Ziel: ");
  lcd.print(targetAmount);
  lcd.print("ml");
  lcd.setCursor(0, 3);
  String fname = fluids[fluidIndex].name;
  fname.replace("_", " ");
  lcd.print(fname.substring(0, 20));

  // Waage für DIESE Zutat nullen. current_weight wird manuell auf 0 gesetzt,
  // damit ein stagnierender HX711 (is_ready=false direkt nach tare) keinen
  // Altwert einschleppt. Nach tare() liefert die nächste Messung ~0.
  scale.tare();
  current_weight = 0.0;

  // Hilfsfunktion: Waage lesen und LCD-Zeile 2 aktualisieren.
  // Negative Werte werden auf 0 geclampt (HX711-Rauschen / Motor-EMI).
  // Wird NUR aufgerufen, wenn der Motor kurz pausiert → sauberere Messung.
  auto updateDisplay = [&]() {
    if (scale.is_ready()) {
      double r = scale.get_units(1);
      if (r >= -5.0) current_weight = (r < 0.0 ? 0.0 : r);
    }
    lcd.setCursor(0, 1);
    lcd.print("Ist:  ");
    lcd.print(String(current_weight, 1));
    lcd.print("ml      ");
  };

  // Gesamtschritte nur als Sicherheitslimit (falls Waage ausfällt)
  int steps = (targetAmount * 1000) / fluids[fluidIndex].calibration_ml;

  // Phase 1: schnell pumpen bis Waage slowStopWeight erreicht.
  // 4000-Schritt-Blöcke (5 Umdrehungen à 800 Schritte): nach jedem Block Waage prüfen.
  // Sicherheitslimit: 3× berechnete Schritte, damit kein Endlos-Lauf bei Waagen-Ausfall.
  activateFluid(fluidIndex, true);
  double slowStopWeight = targetAmount - fluids[fluidIndex].korrektur_faktor - WEIGHT_TOLERANCE;
  if (slowStopWeight < 0) slowStopWeight = 0;
  int fastStepsDone = 0;
  int maxFastSteps = steps * 3;
  // Mindestens einen Block pumpen (Anlauframpe), dann waagegesteuert weiter
  motorStep(4000, true, NORMAL_SPEED);
  fastStepsDone += 4000;
  delay(150);
  updateDisplay();
  while (current_weight < slowStopWeight && fastStepsDone < maxFastSteps)
  {
    motorStep(4000, true, NORMAL_SPEED);
    fastStepsDone += 4000;
    delay(150);
    updateDisplay();
  }

  // Phase 2: langsam in 50-Schritt-Blöcken bis slowStopWeight.
  int maxSlowBlocks = (steps / 100) + 100;  // Safety-Limit mit Puffer
  int slowBlocks = 0;
  while (current_weight < slowStopWeight && slowBlocks < maxSlowBlocks)
  {
    motorStep(50, true, SLOW_SPEED);
    delay(30);
    updateDisplay();
    slowBlocks++;
  }

  // Ventil schließen: korrektur_faktor ml noch im Schlauch
  activateFluid(fluidIndex, false);

  // Stabilisierungspause vor Phase 3
  delay(150);
  updateDisplay();

  // Phase 3: Luft drückt den Schlauchrest ins Glas.
  activateAir(true);
  int korrektionSchritte = (fluids[fluidIndex].korrektur_faktor * 1000) / fluids[fluidIndex].calibration_ml;
  double purgeTarget = targetAmount - WEIGHT_TOLERANCE;
  for (int i = 0; i < korrektionSchritte; i += 50)
  {
    delay(30);
    updateDisplay();
    if (current_weight >= purgeTarget) break;
    int chunk = min(50, korrektionSchritte - i);
    motorStep(chunk, true, NORMAL_SPEED);
  }

  activateAir(false);
  delay(30);
  updateDisplay();
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
  lcd.setCursor(0, 3); lcd.print("Dreh: +-5ml Back: OK");
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
//   0=Alkoholische (0–4), 1=Saefte (5–7), 2=Sirups (8–13), 3=Sonstige (14–18)
// Unterstriche in Namen werden für die Anzeige durch Leerzeichen ersetzt.
void displayFluidFromCategory()
{
  lcd.clear();
  
  String categoryNames[4] = {"Alkoholische", "Saefte", "Sirups", "Sonstige"};
  int categoryStart[4] = {0, 5, 8, 14};   // Erster FLUID_NAMES-Index pro Kategorie
  int categoryEnd[4]   = {4, 7, 13, 18};  // Letzter FLUID_NAMES-Index pro Kategorie
  
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
      // Schrittzahl: max 5 Zeichen
      String sStr = String((long)manualMotorSteps);
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
  lcd.print("== SCHRITTE EINST ==");

  lcd.setCursor(0, 1);
  lcd.print("Encoder drehen:");

  lcd.setCursor(0, 2);
  String sStr = String((long)manualMotorSteps);
  // zentrieren auf 20 Zeichen
  int pad = (20 - sStr.length()) / 2;
  for (int i = 0; i < pad; i++) lcd.print(" ");
  lcd.print(sStr);
  lcd.print(" Schritte");

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
  // Kein scale.tare() hier: pumpFluid() tart die Waage vor jeder Zutat selbst,
  // damit keine Altwerte (current_weight aus loop()) die Phasenbedingungen verfälschen.

  Recipe recipe = recipes[recipeIndex];
  
  for (int i = 0; i < recipe.fluid_count; i++)  // Jede Zutat der Reihe nach dosieren
  {
    int fluidNameIdx = recipe.fluid_index[i];  // Index in FLUID_NAMES (z.B. 1 = "Weisser_Rum")
    int amount = recipe.amounts[i];            // Menge in ml für diese Zutat
    int fluidSlotIdx = -1;                     // Index in fluids[] (0–8), wird unten gesucht
    
    // Suche welcher fluids[]-Slot diesen Namen hat
    // (Rezept kennt FLUID_NAMES-Index, pumpFluid() braucht aber den fluids[]-Index)
    for (int j = 0; j < 9; j++)
    {
      if (fluids[j].name == FLUID_NAMES[fluidNameIdx])  // Namen vergleichen
      {
        fluidSlotIdx = j;  // Gefunden: Slot j hat die benötigte Flüssigkeit
        break;
      }
    }
    
    if (fluidSlotIdx == -1) continue;  // Zutat nicht in den 9 Slots konfiguriert → überspringen
    
    pumpFluid(fluidSlotIdx, amount);   // Flüssigkeit dosieren
    delay(500);  // 500ms Pause: Waage stabilisiert sich bevor nächste Zutat beginnt
  }

  // ===== ABSCHLUSSANZEIGE =====
  // Scrollbare Liste: welche Flüssigkeit wurde wie viel gepumpt?
  // Knopf oder Back beendet die Anzeige.
  {
    int summaryIndex = 0;
    bool done = false;
    encoderButtonPressed = false;

    while (!done)
    {
      // Anzeige aufbauen
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("== FERTIG! Ergebnis");

      // Maximal 3 Einträge sichtbar (Zeilen 1-3), scrollbar
      int startIdx = (summaryIndex > 2) ? summaryIndex - 2 : 0;
      for (int row = 0; row < 3; row++)
      {
        int idx = startIdx + row;
        if (idx >= recipe.fluid_count) break;

        lcd.setCursor(0, row + 1);
        if (idx == summaryIndex) lcd.print(">"); else lcd.print(" ");

        // Flüssigkeitsname (gekürzt)
        String n = FLUID_NAMES[recipe.fluid_index[idx]];
        n.replace("_", " ");
        // Kürzen damit Menge noch Platz hat: max 12 Zeichen
        if (n.length() > 12) n = n.substring(0, 12);
        lcd.print(n);
        // Leerzeichen auffüllen
        for (int s = n.length(); s < 12; s++) lcd.print(" ");
        // Menge rechtsbündig
        String amtStr = String(recipe.amounts[idx]) + "ml";
        // Auf 7 Zeichen rechtsbündig
        for (int s = amtStr.length(); s < 7; s++) lcd.print(" ");
        lcd.print(amtStr);
      }

      // Auf Eingabe warten
      while (true)
      {
        // Encoder drehen
        if (encoderCounter != 0)
        {
          summaryIndex += (encoderCounter > 0) ? 1 : -1;
          encoderCounter = 0;
          summaryIndex = constrain(summaryIndex, 0, recipe.fluid_count - 1);
          break;  // Anzeige neu aufbauen
        }
        // Knopf oder Back: beenden
        if (encoderButtonPressed || digitalRead(BACK_BUTTON) == LOW)
        {
          done = true;
          break;
        }
        delay(20);
      }
      encoderButtonPressed = false;
    }
  }

  currentMenu = MAIN_MENU;  // Zurück zum Hauptmenü nach Fertigstellung
  selectedIndex = 0;        // Cursor auf erste Menüoption
}

// Führt alle Reinigungszyklen blockierend aus (loop() läuft währenddessen nicht).
// Abbruch ist jederzeit per Back-Button möglich (zwischen Relay-Schritten oder in der Wartezeit).
// Nach Abbruch werden alle Relays sicher geschlossen.
void startCleaning() 
{
  isCleaningMode = true;
  bool aborted = false;  // wird true wenn Back-Button während Reinigung gedrückt

  // Schritte berechnen: pro Schritt = 2 Pulse × NORMAL_SPEED µs → Sekunden × 1.000.000 / (2 × NORMAL_SPEED)
  long cleaningSteps = ((long)CLEANING_DURATION_SECONDS * 1000000L) / (2L * NORMAL_SPEED);

  for (int cycle = 0; cycle < CLEANING_CYCLES && !aborted; cycle++)
  {
    // Letzter Zyklus: Benutzer soll Schläuche 1-9 entfernen
    if (cycle == CLEANING_CYCLES - 1)
    {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("= Letzter Zyklus! =");
      lcd.setCursor(0, 1); lcd.print("Schlaeuche 1-9 raus");
      lcd.setCursor(0, 2); lcd.print("Motor Schlauch nicht");
      lcd.setCursor(0, 3); lcd.print("OK:Taste Back:Stop");

      encoderButtonPressed = false;
      // Warten bis OK-Taste oder Back-Taste gedrückt
      // Back-Button direkt lesen (loop() läuft nicht)
      while (!encoderButtonPressed && digitalRead(BACK_BUTTON) == HIGH) delay(50);
      if (digitalRead(BACK_BUTTON) == LOW) { aborted = true; }
      encoderButtonPressed = false;
    }

    // Alle 9 Flüssigkeits-Relays (0–8) nacheinander spülen
    for (int i = 0; i < 9 && !aborted; i++)
    {
      // Abbruch-Check VOR jedem Relay (zwischen Schritten)
      if (digitalRead(BACK_BUTTON) == LOW)
      {
        aborted = true;
        break;
      }

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Zyklus "); lcd.print(cycle + 1); lcd.print("/"); lcd.print(CLEANING_CYCLES);
      lcd.setCursor(0, 1);
      lcd.print("Relay "); lcd.print(i + 1); lcd.print("/9 laeuft...");
      lcd.setCursor(0, 2); lcd.print("Motor laeuft...");
      lcd.setCursor(0, 3); lcd.print("Back = Abbrechen");

      // NO-Ventile: alle schließen, nur Ventil i öffnen
      for (int j = 0; j < NUM_RELAYS; j++) activateRelay(j, true);
      activateRelay(i, false);  // Ventil i öffnen (Relay aus → NO = offen)
      bool stopped = motorStep(cleaningSteps, true, NORMAL_SPEED);
      activateRelay(i, true);   // Ventil i wieder schließen
      if (stopped) aborted = true;
      delay(500);
    }
  }

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("==== REINIGUNG ====");
  if (aborted)
  {
    lcd.setCursor(0, 1); lcd.print("Abgebrochen!");
    lcd.setCursor(0, 2); lcd.print("Alle Ventile zu.");
  }
  else
  {
    lcd.setCursor(0, 1); lcd.print("Fertig!");
    lcd.setCursor(0, 2); lcd.print("Alle 9 Relays OK");
  }
  delay(2000);

  stopCleaning();
  currentMenu = CLEANING_MODE;
  selectedIndex = 0;
  displayNeedsUpdate = true;
}

// Setzt isCleaningMode auf false und schließt alle 10 Relays.
// Wird auch beim Navigieren zurück ins Hauptmenü aufgerufen als Sicherheitsmaßnahme.
void stopCleaning() 
{
  isCleaningMode = false;
  // Alle Relays AN → Idle-Zustand
  for (int i = 0; i < NUM_RELAYS; i++)
    activateRelay(i, true);
}
