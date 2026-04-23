# Gefahrenanalyse – Getränkeautomat (Cocktailautomat)
Abschlussprojekt Gerstner Philip

---

| | |
|---|---|
| **Projekt:** | Getränkeautomat / Cocktailautomat |
| **Autor:** | Philip Gerstner |
| **Klasse:** | FSTEL24 |
| **Betreuer:** | Herr Benz |
| **Schule:** | Meisterschule für Handwerker, Kaiserslautern |
| **Datum:** | 13.04.2026 |
| **Version:** | 1.0 |

---

## 1. Systembeschreibung

Der Getränkeautomat dosiert bis zu 9 verschiedene Flüssigkeiten automatisch.
Das System besteht aus folgenden sicherheitsrelevanten Komponenten:

| Modul | Komponenten | Spannung |
|-------|------------|----------|
| **Spannung / Steuerung** | BRIMETI Schaltnetzteil 230V AC → 24V DC, 150W, IP67 | 230V AC / 24V DC |
| **Mikrokontroller** | XTVTX ESP32-S3 (16MB Flash, WiFi, BT 5.0) | 5V DC |
| **Pumpensteuerung** | COVVY TB6600 Schrittmotorcontroller (9-42V, 4A max.) | 24V DC |
| **Pumpe** | Kamoer KDS3000-ST Peristaltikpumpe (Schrittmotor, 0–1800 ml/min) | 24V DC |
| **Ventile** | 10× Micro-Magnetventil, Normally Open (12V DC) | 12V DC |
| **Relais** | 10× ICQUANZX 1-Kanal Relaismodul (5V Trigger, 230V/10A max. Kontakte) | 5V Signal |
| **Wägezelle** | 2× ALAMSCN 5kg Wägezellen + HX711 ADC-Modul | 3.3V / 5V |
| **Schlauch** | Silikonschlauch 5mm Innen-Ø, lebensmittelecht (–40°C bis +220°C) | – |
| **Bedienung** | AZDelivery 4×20 LCD, GIAK KY-040 Drehgeber, 5× Drucktaster | 5V DC |
| **Gehäuse** | 6 Module, 3D-gedruckt aus PP-Filament (250°C Schmelzpunkt) | – |

**Betriebsumgebung:** Innenbereich, Küche/Gastronomie, bestimmungsgemäß durch eingewiesene Person zu bedienen.

---

## 2. Methode der Risikobewertung

Die Risikobewertung erfolgt mittels **Risikoprioritätszahl (RPZ)**:

$$RPZ = E \times S$$

| Kürzel | Bedeutung | Stufen |
|--------|-----------|--------|
| **E** | Eintrittswahrscheinlichkeit | 1 = sehr unwahrscheinlich … 5 = sehr wahrscheinlich |
| **S** | Schadensausmaß | 1 = vernachlässigbar … 5 = katastrophal (Todesfolge) |
| **RPZ** | Risikoprioritätszahl | 1–25 |

### Risikobewertung – Ampelschema

| RPZ | Bewertung | Handlungsbedarf |
|-----|-----------|-----------------|
| 1–4 | 🟢 **Gering** | Akzeptabel, keine zwingenden Maßnahmen |
| 5–9 | 🟡 **Mittel** | Maßnahmen wünschenswert |
| 10–14 | 🟠 **Erhöht** | Maßnahmen erforderlich |
| 15–25 | 🔴 **Hoch** | Sofortige Maßnahmen zwingend erforderlich |

Die Schutzmaßnahmen folgen dem **STOP-Prinzip** (Substitution → Technisch → Organisatorisch → Persönlich/Schutzausrüstung), wobei höherrangige Maßnahmen bevorzugt werden.

---

## 3. Risikotabelle

### Kategorie A – Elektrische Gefährdungen

| Nr. | Gefährdung | Ursache / Szenario | E | S | RPZ (vor) | Schutzmaßnahme | E | S | RPZ (nach) |
|-----|-----------|-------------------|---|---|-----------|----------------|---|---|------------|
| A1 | Berühren spannungsführender 230V-Teile | Offenes Gehäuse, Kabel lose, Wartung ohne Abschalten | 2 | 5 | **10** 🟠 | T: Netzteil IP67, Gehäuse vollständig geschlossen, Schraubenverriegelung; O: Netzteil vor Wartung trennen (Hinweis in Anleitung) | 1 | 5 | **5** 🟡 |
| A2 | Kurzschluss durch Flüssigkeitseintritt (24V/12V-Bereich) | Schlauchbruch, Leckage nahe Leiterbahnen | 3 | 3 | **9** 🟡 | T: Elektronik-Module räumlich oben, Flüssigkeitsmodule unten; Tropfschutz-Trennwand (3D-Druck); lebensmittelechte Schläuche mit Schlauchklemmen | 1 | 3 | **3** 🟢 |
| A3 | Überstrom / Überlastung Zuleitungen | Kurzschluss in Motorsteuerung oder Netzteil | 2 | 4 | **8** 🟡 | T: Schmelzsicherung am 24V-Ausgang; TB6600 mit eingebauter Strombegrenzung; Kabelquerschnitt nach Last dimensioniert | 1 | 4 | **4** 🟢 |
| A4 | Elektromagnetische Störung durch WiFi/BT (ESP32-S3) | Fehlfunktion anderer Geräte in unmittelbarer Nähe | 2 | 1 | **2** 🟢 | T: CE-konformes Modul; keine medizinischen Geräte im Umfeld | 1 | 1 | **1** 🟢 |

---

### Kategorie B – Mechanische Gefährdungen

| Nr. | Gefährdung | Ursache / Szenario | E | S | RPZ (vor) | Schutzmaßnahme | E | S | RPZ (nach) |
|-----|-----------|-------------------|---|---|-----------|----------------|---|---|------------|
| B1 | Erfassen durch rotierende Pumpe (Schrittmotor-Welle) | Eingriff in das Pumpenmodul bei laufendem Betrieb | 2 | 3 | **6** 🟡 | T: Pumpenmodul vollständig verkleidet (3D-Druck); kein Zugang im Betrieb; O: Nur Wartung bei ausgeschaltetem System | 1 | 3 | **3** 🟢 |
| B2 | Platzen / Lösen des Silikonschlauchs unter Druck | Kink, Alterung, fehlerhafte Montage der Schlauchklemmen | 3 | 2 | **6** 🟡 | T: Lebensmittelechter Silikonschlauch (geprüft –40°C bis +220°C); Schlauchklemmen an allen Verbindungen; O: visuelle Prüfung vor jedem Betrieb | 2 | 2 | **4** 🟢 |
| B3 | Herunterfallen / Kippen des Geräts | Instabiles 3D-Druck-Gehäuse, Erschütterung | 2 | 3 | **6** 🟡 | T: Modulares Gehäuse mit Verschraubung; Standfläche breit ausgelegt; O: Aufstellung auf ebenem, stabilem Untergrund | 1 | 3 | **3** 🟢 |
| B4 | Quetschgefahr beim Einsetzen / Befüllen der Flaschen | Schwere Flaschen, enge Öffnung | 2 | 2 | **4** 🟢 | O: Einweisungsanleitung; Flaschen einzeln einsetzen | 1 | 2 | **2** 🟢 |

---

### Kategorie C – Thermische Gefährdungen

| Nr. | Gefährdung | Ursache / Szenario | E | S | RPZ (vor) | Schutzmaßnahme | E | S | RPZ (nach) |
|-----|-----------|-------------------|---|---|-----------|----------------|---|---|------------|
| C1 | Überhitzung TB6600 Motorcontroller | Dauerbetrieb, Lüftungsschlitze verdeckt | 3 | 3 | **9** 🟡 | T: TB6600 mit eigenem Kühlkörper; Lüftungsöffnungen im Gehäusemodul einplanen; SW: Betriebsdauer-Begrenzung im Firmware-Code | 1 | 3 | **3** 🟢 |
| C2 | Überhitzung Schaltnetzteil (24V/150W) | Dauerbetrieb bei Volllast, kein Luftstrom | 2 | 3 | **6** 🟡 | T: IP67-Netzteil für Dauerbetrieb ausgelegt; im separaten Modul 0/1 mit Belüftung montiert; Abstand zu brennbaren Materialien halten | 1 | 3 | **3** 🟢 |
| C3 | Verbrennung durch heiße Gehäuseteile | Wärmeabgabe am Netzteil-Modul nach langem Betrieb | 2 | 2 | **4** 🟢 | T: Netzteil innerhalb geschlossenem Modul; keine direkten Berührungsflächen nach außen | 1 | 2 | **2** 🟢 |

---

### Kategorie D – Flüssigkeits- / Feuchtigkeitsgefährdungen

| Nr. | Gefährdung | Ursache / Szenario | E | S | RPZ (vor) | Schutzmaßnahme | E | S | RPZ (nach) |
|-----|-----------|-------------------|---|---|-----------|----------------|---|---|------------|
| D1 | Rutschgefahr durch ausgelaufene Flüssigkeit | Schlauchbruch, überfülltes Glas, Fehlbedienung | 3 | 2 | **6** 🟡 | T: Auffangwanne unter Ausgabebereich (3D-Druck); SW: Gewichtsüberwachung stoppt Pumpe bei Überschreitung; O: regelmäßige Reinigung | 2 | 2 | **4** 🟢 |
| D2 | Flüssigkeitseintritt in Electronic-Module | Direktes Überlaufen, Spritzer | 2 | 4 | **8** 🟡 | T: Elektronikmodule in separaten, nach unten abgedichteten Boxen; Ventilmodul mit Tropfablauf | 1 | 4 | **4** 🟢 |
| D3 | Schäden am 3D-Druck-Gehäuse durch Feuchtigkeit | PP-Filament ist wasserbeständig, jedoch kann Feuchtigkeit Klebverbindungen lösen | 2 | 2 | **4** 🟢 | T: Ausschließlich Schraubverbindungen (M2/M3/M4); kein Kleber strukturell eingesetzt | 1 | 2 | **2** 🟢 |

---

### Kategorie E – Brand- und Explosionsgefahr

| Nr. | Gefährdung | Ursache / Szenario | E | S | RPZ (vor) | Schutzmaßnahme | E | S | RPZ (nach) |
|-----|-----------|-------------------|---|---|-----------|----------------|---|---|------------|
| E1 | Entzündung von Alkohol durch heiße Bauteile | Alkoholdämpfe bei offenem Behälter nahe Netzteil/Motor | 2 | 5 | **10** 🟠 | T: Flaschen/Behälter vollständig verschlossen (Silikonschlauch mit Stopfen im Behälter eingeführt); Elektronik räumlich getrennt; O: Keine offenen Flammen in der Nähe | 1 | 5 | **5** 🟡 |
| E2 | Schwelbrand durch Überlastung von Relais | Relais schaltet Verbraucher außerhalb seiner Spezifikation | 2 | 4 | **8** 🟡 | T: Relais-Kontakte für 230V/10A, tatsächliche Last 12V/max. 1A (Magnetventile) → massive Unterdimensionierung ausgeschlossen; Sicherung im 12V-Zweig | 1 | 4 | **4** 🟢 |
| E3 | Brand durch Kurzschluss in Schlauchverbindungen nahe Wärme | Schmelzen des Schlauchs durch Überhitzung | 1 | 4 | **4** 🟢 | T: Silikonschlauch hitzbeständig bis +220°C; max. Betriebstemperatur aller Bauteile deutlich darunter | 1 | 4 | **4** 🟢 |

---

### Kategorie F – Lebensmittelhygiene / Biostoffe

| Nr. | Gefährdung | Ursache / Szenario | E | S | RPZ (vor) | Schutzmaßnahme | E | S | RPZ (nach) |
|-----|-----------|-------------------|---|---|-----------|----------------|---|---|------------|
| F1 | Keimbildung / Schimmel im Schlauch | Flüssigkeitsreste stehen in Silikonschlauch zwischen Betrieb | 4 | 3 | **12** 🟠 | T: Lebensmittelechter Silikonschlauch; SW: Reinigungsmodus spült alle Leitungen mit Wasser durch; O: Reinigung nach jedem Betriebstag vorgeschrieben (laut Anleitung, Kapitel 10) | 2 | 3 | **6** 🟡 |
| F2 | Kontamination durch nicht lebensmittelgerechte Materialien | Einsatz falscher Schläuche bei Wartung | 2 | 3 | **6** 🟡 | T: Ausschließlich PE/Silikon-Komponenten auf dem Getränkepfad; O: Ersatzteilliste mit Typenbezeichnung in Dokumentation; P: Nur empfohlene Ersatzteile verwenden | 1 | 3 | **3** 🟢 |
| F3 | Kreuzkontamination zwischen Getränken | Rückfluss in Schlauch / Ventil | 2 | 2 | **4** 🟢 | T: Magnetventile Normally Open → Strom unterbindet Fluss; Peristaltikpumpe schließt Schlauch bei Stillstand ab (mechanische Sperre) | 1 | 2 | **2** 🟢 |

---

### Kategorie G – Softwarefehler / Fehlfunktionen

| Nr. | Gefährdung | Ursache / Szenario | E | S | RPZ (vor) | Schutzmaßnahme | E | S | RPZ (nach) |
|-----|-----------|-------------------|---|---|-----------|----------------|---|---|------------|
| G1 | Overrun: Pumpe läuft zu lange, Glas überläuft | Softwarefehler, Gewichtssensor fällt aus (HX711) | 3 | 2 | **6** 🟡 | SW: Doppelte Absicherung: Gewicht-Rückkopplung UND Zeitlimit (Timeout) pro Zutat; Watchdog-Timer im ESP32 aktiv; O: Glas-Höhenbegrenzung max. 20 cm | 1 | 2 | **2** 🟢 |
| G2 | Falsches Ventil öffnet, falsche Flüssigkeit im Glas | Konfigurationsfehler, falsche Slot-Zuordnung | 3 | 1 | **3** 🟢 | SW: Flüssigkeitskonfiguration muss vor jedem Betrieb bestätigt werden (Pflichtschritt beim Start laut Anleitung) | 2 | 1 | **2** 🟢 |
| G3 | Unbeabsichtigtes Betätigen (unbeaufsichtigter Betrieb) | ESP32 WiFi-Schnittstelle empfängt Befehle | 2 | 2 | **4** 🟢 | SW: Keine ungesicherte Remote-Steuerung; lokale Bedienung primär; O: Gerät nach Benutzung ausschalten | 1 | 2 | **2** 🟢 |
| G4 | Absturz / Freeze des ESP32 | Stack-Overflow, Memory-Leak bei Dauerbetrieb | 2 | 2 | **4** 🟢 | SW: Hardware-Watchdog des ESP32-S3 aktiv (Reset bei Freeze); alle Pumpen default OFF bei Systemstart | 1 | 2 | **2** 🟢 |

---

### Kategorie H – Strukturversagen / Mechanische Integrität

| Nr. | Gefährdung | Ursache / Szenario | E | S | RPZ (vor) | Schutzmaßnahme | E | S | RPZ (nach) |
|-----|-----------|-------------------|---|---|-----------|----------------|---|---|------------|
| H1 | Bruch von 3D-Druck-Modulen unter Gewichtslast | Schwere Flaschen (9× ~0,75 l = ~6,75 kg), Schichtentrennung beim Druck | 2 | 3 | **6** 🟡 | T: PP-Filament (höhere Schlagfestigkeit als PLA); Module verschraubt; Lastanalyse in Konstruktionsphase; O: Sichtprüfung auf Risse vor Inbetriebnahme | 1 | 3 | **3** 🟢 |
| H2 | Ermüdungsbruch der Schraubenverbindungen (M2/M3/M4) | Vibration durch Schrittmotor / Peristaltikpumpe | 2 | 2 | **4** 🟢 | T: Schrauben mit Sicherungsring oder Loctite; O: Anzugsmoment nach 10 Betriebsstunden prüfen | 1 | 2 | **2** 🟢 |

---

## 4. Risikomatrix (Übersicht)

Die folgende Matrix zeigt alle identifizierten Risiken **vor** Schutzmaßnahmen:

```
Schadensausmaß S
     5 │ A1              E1
       │
     4 │      A3    D2        E2
       │
     3 │   B1 B2 B3 C1  D1 F1   G1  H1
       │           C2
     2 │      B4   C3  D1  D3 F3 G3 G4 H2
       │
     1 │                        A4
       └─────────────────────────────────
         1    2    3    4    5     E (Eintrittswahrscheinlichkeit)
```

**Vor Schutzmaßnahmen:** 2 Risiken 🟠 Erhöht (A1, E1), 1 Risiko 🟠 (F1)

**Nach Schutzmaßnahmen:** Alle Risiken auf 🟢 Gering bis max. 🟡 Mittel reduziert.

---

## 5. Schutzmaßnahmen nach STOP-Prinzip

### S – Substitution
| Maßnahme | Begründung |
|----------|-----------|
| Silikonschlauch statt PVC-Schlauch | Lebensmittelecht, temperaturbeständig, kein Weichmacher |
| PP-Filament statt PLA für Gehäuse | Höhere Temperatur- und Feuchtigkeitsbeständigkeit |
| Magnetventile 12V (statt 230V) | Niederspannungsseite; Sicherheitstrennung von 230V-Netz |

### T – Technische Schutzmaßnahmen
| Maßnahme | Adressierte Gefährdung |
|----------|----------------------|
| IP67-Schaltnetzteil, vollständig gekapselt | A1, C2 |
| Geschlossene Module mit Schraubenverriegelung | A1, B3, C1 |
| Schmelzsicherung im 24V-Kreis | A3 |
| Räumliche Trennung: Elektronik oben, Flüssigkeit unten | A2, D2 |
| Tropfschutz-Trennwand zwischen Modul 3 (Ventile) und Elektronik | A2, D2 |
| Schlauchklemmen an allen Verbindungen | B2 |
| Separate Auffangwanne im Ausgabebereich | D1 |
| Lüftungsöffnungen am Motorcontroller-Modul | C1 |
| Hardware-Watchdog ESP32-S3 | G1, G4 |
| Doppelte Abbruchbedingung (Gewicht + Timeout) | G1 |
| Pumpe OFF als Default-Zustand | G4 |

### O – Organisatorische Maßnahmen
| Maßnahme | Adressierte Gefährdung |
|----------|----------------------|
| Netzteil vor Wartung / Öffnen des Gehäuses trennen (Anleitung Kap. 8) | A1 |
| Reinigungsmodus nach jedem Betriebstag obligatorisch (Anleitung Kap. 9) | F1 |
| Sichtprüfung Schlauch und Gehäuse vor Inbetriebnahme | B2, H1 |
| Flüssigkeitskonfiguration als Pflichtschritt beim Start | G2 |
| Aufstellung nur auf ebenem, stabilem Untergrund | B3 |
| Keine offenen Flammen oder Zündquellen im Umkreis von 1 m | E1 |
| Nur empfohlene Ersatzteile (Ersatzteilliste in Dokumentation) | F2 |
| Anzugsmoment der Schrauben nach 10 Betriebsstunden prüfen | H2 |

### P – Persönliche Schutzmaßnahmen
| Maßnahme | Adressierte Gefährdung |
|----------|----------------------|
| Einweisung der Bedienperson anhand der Betriebsanleitung | A1, D1, F1 |
| Wartung ausschließlich durch eingewiesene Person | A1, F2 |
| Schutzbrille bei Wartungsarbeiten an Schlauchanschlüssen empfohlen | B2, D1 |

---

## 6. Restrisikobewertung

Nach Umsetzung aller Schutzmaßnahmen verbleiben folgende Restrisiken:

| Nr. | Beschreibung | RPZ (Rest) | Bewertung |
|-----|-------------|-----------|-----------|
| A1 | Berühren 230V nach unbefugtem Öffnen | 5 | 🟡 Tolerierbar, da Einweisung vorausgesetzt |
| E1 | Alkohol-Entzündung bei grobem Bedienfehler (offene Behälter neben Wärmequellen) | 5 | 🟡 Tolerierbar, da Betrieb laut Anleitung voraussetzt: Behälter verschlossen |
| F1 | Hygiene bei unterlassener Reinigung | 6 | 🟡 Tolerierbar, da Reinigungsmodus in SW implementiert und vorgeschrieben |

**Fazit Restrisiko:** Alle verbleibenden Restrisiken sind **tolerierbar**, wenn die in der Betriebsanleitung vorgeschriebenen Maßnahmen eingehalten werden.

---

## 7. Fazit und Freigabe

Der Cocktailautomat wurde systematisch auf Gefährdungen untersucht. Insgesamt wurden **25 Einzelgefährdungen** in 8 Kategorien identifiziert. Vor Schutzmaßnahmen wiesen **3 Risiken** eine erhöhte RPZ (≥ 10) auf. Durch die implementierten technischen, organisatorischen und softwareseitigen Schutzmaßnahmen konnten **alle Risiken auf ein akzeptables Maß** reduziert werden.

Die Inbetriebnahme des Geräts ist unter folgenden Bedingungen freigegeben:

- [x] Netzteil korrekt geerdet und gesichert eingebaut
- [x] Alle Module verschraubt und verriegelt
- [x] Silikonschläuche mit Klemmen gesichert, Sichtprüfung positiv
- [x] Reinigungsmodus vor erster Inbetriebnahme durchgeführt
- [x] Bediener mit Betriebsanleitung eingewiesen
- [x] Auffangwanne eingebaut
- [x] Firmware mit Watchdog und Timeout-Absicherung kompiliert

---

*Gefahrenanalyse erstellt nach dem BAuA-Handbuch zur Gefährdungsbeurteilung (Bundesanstalt für Arbeitsschutz und Arbeitsmedizin) sowie dem STOP-Prinzip gemäß DIN EN ISO 12100.*

*Abschlussprojekt Gerstner Philip – Meisterschule für Handwerker Kaiserslautern – FSTEL24 – 2026*
