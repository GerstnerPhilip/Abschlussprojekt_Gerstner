# Gefahrenanalyse – Getränkeautomat (Cocktailautomat)
**Abschlussprojekt Gerstner Philip**

| | |
|---|---|
| **Projekt** | Getränkeautomat / Cocktailautomat |
| **Autor** | Philip Gerstner |
| **Klasse** | FSTEL24 |
| **Betreuer** | Herr Benz |
| **Schule** | Meisterschule für Handwerker, Kaiserslautern |
| **Datum** | 13.04.2026 |
| **Version** | 1.0 |

---

## 1. Systembeschreibung

Der Getränkeautomat dosiert bis zu 9 verschiedene Flüssigkeiten automatisch.
Das System besteht aus folgenden sicherheitsrelevanten Komponenten:

| Modul | Komponenten | Spannung |
|---|---|---|
| Spannung / Steuerung | BRIMETI Schaltnetzteil 230V AC → 24V DC, 150W, IP67 | 230V AC / 24V DC |
| Mikrokontroller | XTVTX ESP32-S3 (16MB Flash, WiFi, BT 5.0) | 5V DC |
| Pumpensteuerung | COVVY TB6600 Schrittmotorcontroller (9–42V, 4A max.) | 24V DC |
| Pumpe | Kamoer KDS3000-ST Peristaltikpumpe (Schrittmotor, 0–1800 ml/min) | 24V DC |
| Ventile | 10× Micro-Magnetventil, Normally Open (12V DC) | 12V DC |
| Relais | 10× ICQUANZX 1-Kanal Relaismodul (5V Trigger, 230V/10A max. Kontakte) | 5V Signal |
| Wägezelle | 2× ALAMSCN 5 kg + HX711 ADC-Modul | 3,3V / 5V |
| Schlauch | Silikonschlauch 5 mm Innen-Ø, lebensmittelecht (–40°C bis +220°C) | – |
| Bedienung | AZDelivery 4×20 LCD, GIAK KY-040 Drehgeber, 5× Drucktaster | 5V DC |
| Gehäuse | 6 Module, 3D-gedruckt aus PP-Filament (250°C Schmelzpunkt) | – |

**Betriebsumgebung:** Innenbereich, Küche/Gastronomie, bestimmungsgemäß durch eingewiesene Person zu bedienen.

---

## 2. Methode der Risikobewertung

**RPZ = E × S**

| Kürzel | Bedeutung | Skala |
|---|---|---|
| E | Eintrittswahrscheinlichkeit | 1 (sehr unwahrscheinlich) … 5 (sehr wahrscheinlich) |
| S | Schadensausmaß | 1 (vernachlässigbar) … 5 (katastrophal) |
| RPZ | Risikoprioritätszahl | 1–25 |

**Ampelschema:**
- 1–4 → Gering (akzeptabel)
- 5–9 → Mittel (Maßnahmen wünschenswert)
- 10–14 → Erhöht (Maßnahmen erforderlich)
- 15–25 → Hoch (sofortige Maßnahmen erforderlich)

Die Schutzmaßnahmen folgen dem **STOP-Prinzip**
(Substitution → Technisch → Organisatorisch → Persönlich).

---

## 3. Risikotabelle

### Kategorie A – Elektrische Gefährdungen
| Nr. | Gefährdung |
|---|---|
| A1 | Berühren spannungsführender 230V-Teile |
| A2 | Kurzschluss durch Flüssigkeitseintritt |
| A3 | Überstrom / Überlastung Zuleitungen |
| A4 | Elektromagnetische Störung durch WiFi/BT |

### Kategorie B – Mechanische Gefährdungen
| Nr. | Gefährdung |
|---|---|
| B1 | Erfassen durch rotierende Pumpe |
| B2 | Platzen / Lösen des Silikonschlauchs |
| B3 | Herunterfallen / Kippen des Geräts |

### Kategorie C – Thermische Gefährdungen
| Nr. | Gefährdung |
|---|---|
| C1 | Überhitzung TB6600 Motorcontroller |
| C2 | Überhitzung Schaltnetzteil |
| C3 | Verbrennung durch heiße Gehäuseteile |

### Kategorie D – Flüssigkeits- / Feuchtigkeitsgefährdungen
| Nr. | Gefährdung |
|---|---|
| D1 | Rutschgefahr durch ausgelaufene Flüssigkeit |
| D2 | Flüssigkeitseintritt in Elektronikmodule |
| D3 | Schäden am 3D-Druck-Gehäuse durch Feuchtigkeit |

### Kategorie E – Brand- und Explosionsgefahr
| Nr. | Gefährdung |
|---|---|
| E1 | Entzündung von Alkohol durch heiße Bauteile |
| E2 | Schwelbrand durch Überlastung von Relais |

### Kategorie F – Lebensmittelhygiene / Biostoffe
| Nr. | Gefährdung |
|---|---|
| F1 | Keimbildung / Schimmel im Schlauch |
| F2 | Kontamination durch nicht lebensmittelgerechte Materialien |
| F3 | Kreuzkontamination zwischen Getränken |

### Kategorie G – Softwarefehler / Fehlfunktionen
| Nr. | Gefährdung |
|---|---|
| G1 | Überlauf: Pumpe läuft zu lange |
| G2 | Falsches Ventil öffnet |
| G3 | Unbeabsichtigtes Betätigen über WiFi |
| G4 | Absturz / Freeze des ESP32 |

### Kategorie H – Strukturversagen / Mechanische Integrität
| Nr. | Gefährdung |
|---|---|
| H1 | Bruch von 3D-Druck-Modulen unter Gewichtslast |
| H2 | Ermüdungsbruch der Schraubenverbindungen |

---

## 4. Schutzmaßnahmen nach STOP-Prinzip

**S – Substitution**
- Silikonschlauch statt PVC
- 12V Magnetventile statt 230V

**T – Technische Maßnahmen**
- IP67 Netzteil
- Geschlossene Module mit Schraubenverriegelung
- Räumliche Trennung Elektronik / Flüssigkeit
- Tropfschutz-Trennwand
- Schlauchklemmen
- Hardware-Watchdog ESP32
- Timeout + Gewichtssensor

**O – Organisatorische Maßnahmen**
- Netzteil vor Wartung trennen
- Reinigungsmodus nach jedem Betriebstag
- Sichtprüfung vor Betrieb
- Flüssigkeitskonfiguration bestätigen
- Aufstellung auf stabilem Untergrund
- Keine offenen Flammen
- Nur empfohlene Ersatzteile

**P – Persönliche Maßnahmen**
- Einweisung Bedienpersonal
- Wartung nur durch eingewiesene Personen

---

## 5. Funktionale Sicherheit nach ISO 13849 und EN 62061

### 5.1 ISO 13849-1 – Sicherheitsbezogene Teile von Steuerungen

Die Norm ISO 13849-1 bewertet sicherheitsbezogene Steuerungsfunktionen anhand eines **Performance Level (PL a–e)**.

#### Sicherheitsfunktionen des Cocktailautomaten

| Sicherheitsfunktion | Beschreibung | Bewertung |
|---|---|---|
| **SF1 – Dosierstopp** | Pumpe und Ventile stoppen, sobald die Zielgewicht-Schwelle erreicht ist (Wägezelle + HX711 → ESP32) | PL b (Kat. 1) |
| **SF2 – Timeout-Abschaltung** | Hardware-Watchdog und Software-Timeout schalten alle Ausgänge ab, wenn der Dosierprozess eine Maximalzeit überschreitet | PL b (Kat. 1) |
| **SF3 – Ventilüberwachung** | Plausibilitätsprüfung: Gewicht steigt trotz geschlossenem Ventil → Fehlerzustand und Pumpenabschaltung | PL a (Kat. B) |

**Einstufungsbegründung:**
- **Kategorie B / PL a:** Einkanalige Steuerung ohne Diagnose. Gilt für SF3, da die Logik rein softwareseitig im ESP32 liegt und keine redundante Hardware vorgesehen ist.
- **Kategorie 1 / PL b:** Bewährte Bauelemente und Prinzipien. Gilt für SF1 und SF2, da der Hardware-Watchdog des ESP32 (unabhängig vom Anwendungsprogramm) als bewährtes Sicherheitselement wirkt und die Wägezelle ein robuster, kalibrierter Sensor ist.

> **Hinweis:** Da es sich um einen nicht-industriellen Prototyp für Gastronomie / Privat handelt und kein Personenschutz im Sinne einer Maschinenrichtlinie 2006/42/EG zwingend erforderlich ist, ist PL b für die vorliegenden Sicherheitsfunktionen als ausreichend anzusehen.

---

### 5.2 EN 62061 – Funktionale Sicherheit elektrischer Steuerungen (SRECS)

Die Norm EN 62061 (IEC 62061) legt Anforderungen an **sicherheitsbezogene elektrische Steuerungssysteme (SRECS)** fest und verwendet **Safety Integrity Level (SIL 1–3)** als Bewertungsmaßstab.

#### SIL-Einstufung für den Cocktailautomaten

| SRECS-Funktion | Gefährdung | SIL-Ziel | Begründung |
|---|---|---|---|
| **Dosierstopp** (SF1) | Überlauf → Rutschgefahr / Kurzschluss (D1, A2) | SIL 1 | Geringe Schwere (keine Personengefährdung bei bestimmungsgemäßem Betrieb), Eintrittswahrscheinlichkeit gering |
| **Watchdog / Timeout** (SF2) | Dauerbetrieb Pumpe → Überhitzung / Brand (E2, C1) | SIL 1 | Hardware-Watchdog des ESP32 + Software-Timeout bilden redundante Ebene |
| **Ventilsteuerung** | Falsches Ventil → Falschdosierung (G2) | SIL 1 | Ausfall hat keine unmittelbare Personengefährdung |

**Architektur (nach EN 62061, Anhang K):**
- **Subsystem-Typ:** Single-Channel mit Diagnose (1oo1D)
- **Hardware Fault Tolerance (HFT):** 0 – ein Einzelfehler kann zur Fehlfunktion führen
- **Safe Failure Fraction (SFF):** > 60 % durch Watchdog und Timeouts

**Maßnahmen zur Erfüllung von SIL 1:**
1. Hardware-Watchdog des ESP32 unabhängig vom Anwendungscode aktiviert
2. Software-Timeout begrenzt jeden Dosiervorgang auf max. 30 s
3. Wägezellen-Plausibilitätsprüfung erkennt Sensor- und Schlauchfehler
4. MQTT-Steuerbefehle werden nur aus dem lokalen Netz akzeptiert (kein öffentlicher Zugriff)

> **Hinweis:** Eine vollständige SIL-Verifizierung nach EN 62061 (FMEA, PFH-Berechnung) ist für den vorliegenden Prototyp nicht durchgeführt worden. Die Einstufung dient der orientierenden Beurteilung im Rahmen des Abschlussprojekts.

---

## 6. Restrisikobewertung

| Nr. | Gefährdung | RPZ | Bewertung |
|---|---|---|---|
| A1 | Berühren 230V nach Öffnen | 5 | tolerierbar |
| E1 | Alkoholentzündung bei Fehlbedienung | 5 | tolerierbar |
| F1 | Hygiene bei fehlender Reinigung | 6 | tolerierbar |

---

## 7. Fazit

Durch technische, organisatorische und softwareseitige Maßnahmen konnten alle Risiken auf ein akzeptables Maß reduziert werden.

Die Inbetriebnahme ist freigegeben unter folgenden Bedingungen:
- Alle Abdeckungen vorhanden
- Netzteil-Leitung nicht beschädigt
- Module verschraubt
- Schläuche angeschlossen
- Bediener eingewiesen

---

*Gefahrenanalyse erstellt nach dem BAuA-Handbuch zur Gefährdungsbeurteilung (Bundesanstalt für Arbeitsschutz und Arbeitsmedizin) sowie dem STOP-Prinzip gemäß DIN EN ISO 12100, ergänzt durch funktionale Sicherheitsbewertung nach ISO 13849-1 und EN 62061.*
