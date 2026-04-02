# Bedienungsanleitung - Cocktailautomat
Abschlussprojekt Gerstner Philip

---

## 1. Uebersicht
Der Cocktailautomat dosiert bis zu 9 verschiedene Fluessigkeiten automatisch per Peristaltikpumpe.
Die Mengen werden durch eine Waagezelle praezise gemessen.
Bedienung: 4x20 LCD-Display und Drehgeber (Encoder) mit zwei Tasten.

Betriebsmodi:
- Rezept automatisch zubereiten
- Manuelle Mengen eingeben
- Reinigungsmodus
- Manuelle Bedienung (Diagnose)

---

## 2. Bedienelemente

| Bedienelement           | Funktion                           |
|-------------------------|------------------------------------|
| Encoder rechts drehen   | Menue runter / Wert erhoehen       |
| Encoder links drehen    | Menue hoch / Wert verringern       |
| Encoder-Knopf druecken  | OK / Auswahl bestaetigen           |
| Back-Taste druecken     | Zurueck / Abbrechen                |

---

## 3. Einschalten und Startvorgang

1. Strom einschalten - LCD zeigt "Booting..."
2. Danach erscheint das Menue "Fluessigkeiten konfigurieren"
3. Zuerst die eingebauten Fluessigkeiten einstellen (Abschnitt 5)
4. Mit Back zum Hauptmenue

WICHTIG: Vor dem ersten Einsatz immer die Fluessigkeiten konfigurieren,
damit der Automat weiss, welche Leitung welche Fluessigkeit fuehrt.

---

## 4. Hauptmenue

  == Cocktail Automat ==
  >1. Rezepte
   2. Manuelle Eingabe
   3. Fluessigkeiten
   4. Reinigung
   5. Man. Bedienung

Navigation: Encoder drehen -> Option auswaehlen -> Encoder-Knopf druecken

---

## 5. Fluessigkeiten konfigurieren

1. Hauptmenue -> "3. Fluessigkeiten"
2. Slot 1-9 auswaehlen und bestaetigen
3. Kategorie waehlen:
   - Alkoholische Getraenke (Vodka, Rum, Gin, Tequila ...)
   - Saefte (Limettensaft, Orangensaft, Cola ...)
   - Sirups und Likoere (Zuckersirup, Grenadine ...)
   - Sonstige (Wasser, Soda, Tonic Water ...)
4. Fluessigkeit auswaehlen und bestaetigen

Kalibrierungswerte (optional anpassen):
- ml/1000 Schritte: Durchsatz des Motors (Standard: 10 ml)
- Schlauchkorrektur: ml die im Schlauch bleiben (Standard: 10 ml)

---

## 6. Rezept auswaehlen und starten

1. Hauptmenue -> "1. Rezepte"
2. Encoder drehen um Rezept auszuwaehlen
3. Encoder-Knopf -> Details anzeigen
4. "STARTEN" auswaehlen und bestaetigen

Waehrend der Zubereitung:
  = WIRD AUSGEFUEHRT =
  Ist:  45.3ml
  Ziel: 60ml
  Weisser Rum

  Zeile 2: Aktuelles Gewicht (live)
  Zeile 3: Zielgewicht dieser Zutat
  Zeile 4: Name der aktuellen Zutat

Nach Fertigstellung: Zusammenfassung aller gepumpten Mengen
Mit Encoder-Knopf oder Back zurueck zum Hauptmenue.

Hinweis: Nur Rezepte mit konfigurierten Zutaten koennen gestartet werden.

---

## 7. Manuelle Eingabe

1. Hauptmenue -> "2. Manuelle Eingabe"
2. Fluessigkeit auswaehlen -> Encoder-Knopf
3. Menge in 5ml-Schritten einstellen -> Bestaetigen
4. Alle Mengen einstellen -> "Start Manuell" -> Bestaetigen

---

## 8. Reinigungsmodus

1. Hauptmenue -> "4. Reinigung"
2. Wasserschlauch in alle 9 Anschluesse stecken
3. Encoder-Knopf -> Start
4. 3 Reinigungszyklen x 10 Sekunden pro Leitung
5. Letzter Zyklus: Schlaeuche entfernen (nur Luftspuelung)
Back-Taste: jederzeit abbrechen

---

## 9. Manuelle Bedienung

1. Hauptmenue -> "5. Man. Bedienung"
2. Eintrag auswaehlen:

| Eintrag       | Funktion                                    |
|---------------|---------------------------------------------|
| Relay 1-9     | Fluessigkeitsventile oeffnen/schliessen      |
| Relay 10      | Luftventil oeffnen/schliessen               |
| Motor Vor     | Motor vorwaerts laufen lassen               |
| Motor Zur     | Motor rueckwaerts laufen lassen             |
| Schritte      | Schrittanzahl einstellen (100 bis 50.000)   |
| Geschwind.    | Geschwindigkeit umschalten (Schnell/Langsam)|

3. Encoder-Knopf: Relay umschalten oder Motor starten
4. Back: alle Relays in Ruhestellung, zurueck zum Hauptmenue

---

## 10. Verfuegbare Rezepte (19 gespeichert)

|  # | Rezept               | Zutaten                                                         |
|----|----------------------|-----------------------------------------------------------------|
|  1 | Mojito               | Weisser Rum, Limettensaft, Minzsirup, Soda Wasser               |
|  2 | Cosmopolitan         | Vodka, Triple Sec, Cranberrysaft, Limettensaft                  |
|  3 | Pina Colada          | Weisser Rum, Ananassaft, Kokoslikoer, Zuckersirup               |
|  4 | Margarita            | Tequila, Triple Sec, Limettensaft                               |
|  5 | Daiquiri             | Weisser Rum, Limettensaft, Zuckersirup                          |
|  6 | Mai Tai              | Weisser Rum, Limettensaft, Amaretto, Zuckersirup                |
|  7 | Screwdriver          | Vodka, Orangensaft                                              |
|  8 | Sex on the Beach     | Vodka, Cranberrysaft, Orangensaft, Limettensaft                 |
|  9 | Long Island Iced Tea | Rum, Vodka, Gin, Tequila, Triple Sec, Zitronensaft, Zucker, Cola|
| 10 | Tom Collins          | Gin, Zitronensaft, Zuckersirup, Soda Wasser                    |
| 11 | Gimlet               | Gin, Limettensaft, Zuckersirup                                  |
| 12 | Old Fashioned        | Whiskey, Zuckersirup, Angostura Bitter                          |
| 13 | Espresso Martini     | Vodka, Kaffeelikoer, Zuckersirup                                |
| 14 | Caipirinha           | Weisser Rum, Limettensaft, Zuckersirup                          |
| 15 | Hurricane            | Weisser Rum, Limettensaft, Orangensaft, Zuckersirup             |
| 16 | Dark and Stormy      | Dunkler Rum, Tonic Water, Ginger Ale                            |
| 17 | Test Cocktail 1      | Wasser 100 ml                                                   |
| 18 | Test Cocktail 2      | Wasser 100 ml + 100 ml                                          |
| 19 | Test Cocktail 3      | Wasser 50 ml + 50 ml + 50 ml                                    |

---

## 11. Kalibrierung

ml pro 1000 Schritte (Durchsatzrate):
1. Leitung anschliessen, Behaelter unter Ausgabe stellen
2. Fluessigkeiten-Menue -> Slot -> Kalibrierung aufrufen
3. Motor 1000 Schritte laufen lassen, Ausgabe abmessen
4. Gemessenen Wert eintragen

Schlauchkorrektur:
- Menge die nach Pumpstopp noch im Schlauch bleibt
- Wird automatisch per Luft ins Glas gedrueckt
- Standardwert: 10 ml - bei Bedarf anpassen

---

## 12. Technische Daten

| Komponente               | Details                               |
|--------------------------|---------------------------------------|
| Mikrocontroller          | ESP32-S3 DevKitM-1                    |
| Display                  | 4x20 LCD I2C (Adresse 0x27)           |
| Waagezelle               | HX711, Kalibrierungsfaktor 408        |
| Motor                    | NEMA 23, 800 Schritte/Umdrehung       |
| Motortreiber             | TB6600, 24V, bis 4A                   |
| Ventile                  | 10 Relays (NC), LOW-Level Trigger     |
| Normale Geschwindigkeit  | 150 us/Schritt (~0,52 U/s)            |
| Langsame Geschwindigkeit | 400 us/Schritt (~0,19 U/s)            |
| Gewichtstoleranz         | +/- 5 g                               |
| Reinigungszeit           | 10 s pro Leitung, 3 Zyklen            |

---

## 13. Schlauch- und Ventilbelegung

| Leitung        | Relay    | GPIO    | Kabelfarbe |
|----------------|----------|---------|------------|
| Fluessigkeit 1 | Relay  1 | GPIO 10 | Violett    |
| Fluessigkeit 2 | Relay  7 | GPIO 16 | Gelb       |
| Fluessigkeit 3 | Relay  4 | GPIO 13 | Grau       |
| Fluessigkeit 4 | Relay  2 | GPIO 11 | Blau       |
| Fluessigkeit 5 | Relay  8 | GPIO 17 | Rot        |
| Fluessigkeit 6 | Relay  5 | GPIO 14 | Weiss      |
| Fluessigkeit 7 | Relay  3 | GPIO 12 | Gruen      |
| Fluessigkeit 8 | Relay  9 | GPIO 18 | Orange     |
| Fluessigkeit 9 | Relay  6 | GPIO 15 | Schwarz    |
| Luftventil     | Relay 10 | GPIO 42 | Braun      |

---

Abschlussprojekt - Gerstner Philip | April 2026
