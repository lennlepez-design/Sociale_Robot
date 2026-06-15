#include <Wire.h>
#include <LiquidCrystal.h>
#include <Dwenguino.h>
#include <EEPROM.h>
#include <Servo.h>
#include <SPI.h> 

// Hier bewaren we de instellingen in het geheugen zodat ze niet verloren gaan als de robot uitvalt
#define EEPROM_LANG_ADDR      0
#define EEPROM_BEEP_ADDR      1
#define EEPROM_LAST_ROOM_ADDR 2  

// De pin die we gebruiken om het led-matrixscherm aan te sturen
#define MATRIX_CS 10  

// De aansluitingen voor de servo's van de wielen en de armen
#define PIN_SERVO_WIEL_L  40  
#define PIN_SERVO_WIEL_R  41  
#define PIN_SERVO_ARM_L   42  
#define PIN_SERVO_ARM_R   43  

Servo wielServoLinks;
Servo wielServoRechts;
Servo armServoLinks;
Servo armServoRechts;

// De talen die de robot spreekt, standaard zetten we hem op Nederlands
enum Language { NL = 0, FR, EN, DE };
int huidigeTaal = NL;
bool geluidAan = true;

// Alle mogelijke situaties waarin de robot zich kan bevinden
enum State { STATE_KIES_KAMER, STATE_RIJDEN, STATE_OBSTAKEL, STATE_MENU_TAAL, STATE_MENU_GELUID };
State huidigeStatus = STATE_KIES_KAMER;

int startKamer = -1;         
int geselecteerdeKamer = -1;  
unsigned long rijTijdVoorRoute = 4000;
// Hiermee bepalen we welke afbeelding er op het led-matrixscherm komt (0=blij gezichtje, 1=pijl omhoog, etc.)
int actieveRichting = 0; 

unsigned long statusTimer = 0;
// Deze timer houdt bij hoe lang het waarschuwingsscherm voor een obstakel al in beeld staat
unsigned long obstakelTimer = 0; 
unsigned long zwaailichtTimer = 0;
bool zwaailichtWissel = false;

// Alle vertalingen voor de teksten op het LCD-scherm
const char* txtKiesKamer[]  = {"Kies een kamer:", "Choisir chambre:", "Choose a room:", "Zimmer waehlen:"};
const char* txtLocatie[]    = {"Locatie:", "Emplacement:", "Location:", "Standort:"};
const char* txtVoltooid[]   = {"Voltooid!", "Termine!", "Completed!", "Fertig!"};
const char* txtInstellingen[]= {"Instellingen", "Parametres", "Settings", "Einstellungen"};
const char* txtTaal[]       = {"Taal:", "Langue:", "Language:", "Sprache:"};
const char* txtGeluid[]     = {"Geluid:", "Son:", "Sound:", "Ton:"};
const char* txtTalenNamen[] = {"Nederlands", "Francais", "English", "Deutsch"};
const char* txtAanUit[][2]  = {{"aan", "uit"}, {"on", "off"}, {"on", "off"}, {"an", "aus"}};
const char* txtNu[]         = {"Nu: ", "Actuel: ", "Now: ", "Jetzt: "};
const char* txtOnbekend[]   = {"Onbekend", "Inconnu", "Unknown", "Unbekannt"};
// Tekst die we tonen wanneer de robot een denkbeeldig obstakel tegenkomt
const char* txtObstakelL1[] = {"Obstakel", "Obstacle", "Obstacle", "Hindernis"};
const char* txtObstakelL2[] = {"gedetecteerd", "detecte", "detected", "erkannt"};

const char* kamers[][5] = {
  {"Keuken", "Badkamer", "Toilet", "Slaapkamer", "Living"},
  {"Cuisine", "SdB", "Toilettes", "Chambre", "Salon"},
  {"Kitchen", "Bathroom", "Toilet", "Bedroom", "Living room"},
  {"Kueche", "Badezimmer", "Toilette", "Schlafzimmer", "Wohnzimmer"}
};
// Een lijstje van alle functies die we later in de code gaan gebruiken
void checkMenuTrigger();
void verwerkKamerKeuze();
void verwerkMenuNavigatie();
void updateMenuScherm();
void toonKiesKamerScherm();
void toonLocatieScherm();
void piep();
void actieVoltooid();
void stuurMotorenAchteruit();
void stopMotoren();
void setRGB(int r, int g, int b);
byte spiegelByte(byte b);
void max7219_cascade_write(byte reg, byte dataMatrix1, byte dataMatrix2);
void initMatrixHardware();
void updateMatrixScherm();
void bepaalRoute(int van, int naar);

// Dit gedeelte wordt één keer uitgevoerd zodra de robot opstart
void setup() {
  initDwenguino(); 
  
  pinMode(MATRIX_CS, OUTPUT);
  digitalWrite(MATRIX_CS, HIGH);
  SPI.begin();
  initMatrixHardware();

  wielServoLinks.attach(PIN_SERVO_WIEL_L);
  wielServoRechts.attach(PIN_SERVO_WIEL_R);
  armServoLinks.attach(PIN_SERVO_ARM_L);
  armServoRechts.attach(PIN_SERVO_ARM_R);
  stopMotoren();
  
  // De pinnen voor de ingebouwde gekleurde led instellen als uitgang
  pinMode(11, OUTPUT); pinMode(12, OUTPUT); pinMode(13, OUTPUT);
  pinMode(14, OUTPUT); pinMode(15, OUTPUT);
  setRGB(0, 0, 0);

  // Opgeslagen taal ophalen uit het geheugen en controleren of het een geldige optie is
  huidigeTaal = EEPROM.read(EEPROM_LANG_ADDR);
  if(huidigeTaal > 3 || huidigeTaal < 0) huidigeTaal = NL;
  geluidAan = EEPROM.read(EEPROM_BEEP_ADDR);
  if(geluidAan > 1) geluidAan = true;

  startKamer = EEPROM.read(EEPROM_LAST_ROOM_ADDR);
  if(startKamer > 4) startKamer = -1; 

  toonKiesKamerScherm();
  actieveRichting = 0;
}

// Dit blijft zich continu herhalen zolang de robot aanstaat
void loop() {
  checkMenuTrigger();
  updateMatrixScherm();

  if (huidigeStatus == STATE_KIES_KAMER) {
    verwerkKamerKeuze();
  } 
  else if (huidigeStatus == STATE_RIJDEN) {
    if (rijTijdVoorRoute > 0) {
      stuurMotorenAchteruit();
      // Dit zorgt ervoor dat het rode en blauwe
      //licht om de beurt knipperen als een zwaailicht
      if (millis() - zwaailichtTimer > 250) {
        zwaailichtTimer = millis();
        zwaailichtWissel = !zwaailichtWissel;
        if (zwaailichtWissel) setRGB(255, 0, 0); else setRGB(0, 0, 255);
      }
    }

    // Dit is een geprogrammeerd obstakel om te testen: we triggeren dit
    //alleen bij specifieke routes en als de rit lang genoeg duurt
    if ((geselecteerdeKamer == 0 || geselecteerdeKamer == 2) && rijTijdVoorRoute > 2000) {
      if (millis() - statusTimer > 2000) { 
        stopMotoren();
        huidigeStatus = STATE_OBSTAKEL;
        obstakelTimer = millis(); // De timer start nu het obstakelscherm tevoorschijn komt
        dwenguinoLCD.clear();
      }
    } else {
      // Als er geen obstakels op de weg zijn, rijdt de robot gewoon netjes door tot de tijd om is
      if (millis() - statusTimer > rijTijdVoorRoute) { 
        stopMotoren();
        actieVoltooid();
      }
    }
  } 
  else if (huidigeStatus == STATE_OBSTAKEL) {
    stopMotoren();
    dwenguinoLCD.setCursor(0, 0);
    dwenguinoLCD.print(txtObstakelL1[huidigeTaal]); 
    dwenguinoLCD.setCursor(0, 1);
    dwenguinoLCD.print(txtObstakelL2[huidigeTaal]); 
    
    setRGB(255, 0, 0); // De led blijft felrood branden als waarschuwing
    
    // Na anderhalve seconde doet de robot alsof het obstakel weg is en rondt hij de rit af
    if (millis() - obstakelTimer > 1500) {
      dwenguinoLCD.clear();
      actieVoltooid();
    }
  } 
  else {
    verwerkMenuNavigatie();
  }
}

// Hier berekenen we hoe lang de robot moet rijden op basis van waar hij vertrekt en waar hij naartoe moet
void bepaalRoute(int van, int naar) {
  if (van == -1) {
    rijTijdVoorRoute = 4000;
    return;
  }

  if (van == naar) {
    rijTijdVoorRoute = 0;
    return;
  }

  // Overzicht van de kamers: 0=Keuken, 1=Badkamer, 2=Toilet, 3=Slaapkamer, 4=Living
  if (van == 0 && naar == 1)      rijTijdVoorRoute = 3000;
  else if (van == 1 && naar == 0) rijTijdVoorRoute = 3000;
  else if (van == 0 && naar == 2) rijTijdVoorRoute = 5000;
  else if (van == 4 && naar == 3) rijTijdVoorRoute = 2000;
  else                            rijTijdVoorRoute = 4000;
  // Dit is de standaardtijd als de specifieke route niet in het lijstje hierboven staat
}

// Hier kijken we of de Noord- en Centrumknop tegelijk worden ingedrukt om het menu te openen of te sluiten
void checkMenuTrigger() {
  if (digitalRead(SW_N) == LOW && digitalRead(SW_C) == LOW) {
    delay(50);
    if (digitalRead(SW_N) == LOW && digitalRead(SW_C) == LOW) {
      piep();
      if (huidigeStatus != STATE_MENU_TAAL && huidigeStatus != STATE_MENU_GELUID) {
        stopMotoren();
        dwenguinoLCD.clear();
        dwenguinoLCD.setCursor(2, 0);
        dwenguinoLCD.print(txtInstellingen[huidigeTaal]);
        delay(2000); 
        huidigeStatus = STATE_MENU_TAAL;
        updateMenuScherm();
      } else {
        huidigeStatus = STATE_KIES_KAMER;
        actieveRichting = 0; 
        toonKiesKamerScherm();
      }
      while(digitalRead(SW_N) == LOW || digitalRead(SW_C) == LOW); 
      delay(200);
    }
  }
}

// Zorgt voor het bladeren door de instellingen en het aanpassen van de taal of het geluid
void verwerkMenuNavigatie() {
  if (digitalRead(SW_N) == LOW || digitalRead(SW_S) == LOW) {
    piep();
    huidigeStatus = (huidigeStatus == STATE_MENU_TAAL) ? STATE_MENU_GELUID : STATE_MENU_TAAL;
    updateMenuScherm();
    delay(300);
  }
  
  if (huidigeStatus == STATE_MENU_TAAL) {
    if (digitalRead(SW_E) == LOW) { 
      piep();
      huidigeTaal = (huidigeTaal + 1) % 4;
      EEPROM.write(EEPROM_LANG_ADDR, huidigeTaal);
      updateMenuScherm();
      delay(300);
    }
    if (digitalRead(SW_W) == LOW) { 
      piep();
      huidigeTaal = (huidigeTaal - 1 + 4) % 4;
      EEPROM.write(EEPROM_LANG_ADDR, huidigeTaal);
      updateMenuScherm();
      delay(300);
    }
  } 
  else if (huidigeStatus == STATE_MENU_GELUID) {
    if (digitalRead(SW_E) == LOW || digitalRead(SW_W) == LOW) {
      piep();
      geluidAan = !geluidAan;
      EEPROM.write(EEPROM_BEEP_ADDR, geluidAan);
      updateMenuScherm();
      delay(300);
    }
  }
}

// Tekent de huidige menu-opties op het LCD-scherm
void updateMenuScherm() {
  dwenguinoLCD.clear();
  if (huidigeStatus == STATE_MENU_TAAL) {
    dwenguinoLCD.setCursor(0, 0);
    dwenguinoLCD.print(txtTaal[huidigeTaal]);
    dwenguinoLCD.setCursor(0, 1);
    dwenguinoLCD.print(txtTalenNamen[huidigeTaal]);
  } else if (huidigeStatus == STATE_MENU_GELUID) {
    dwenguinoLCD.setCursor(0, 0);
    dwenguinoLCD.print(txtGeluid[huidigeTaal]);
    dwenguinoLCD.setCursor(0, 1);
    dwenguinoLCD.print(txtAanUit[huidigeTaal][geluidAan ? 0 : 1]);
  }
}

// Controleert welke richtingknop is ingedrukt om een kamer te kiezen en start daarna de rit
void verwerkKamerKeuze() {
  int gekozen = -1;
  if (digitalRead(SW_N) == LOW) { gekozen = 0; actieveRichting = 1;
  } 
  if (digitalRead(SW_E) == LOW) { gekozen = 1; actieveRichting = 2;
  } 
  if (digitalRead(SW_S) == LOW) { gekozen = 2; actieveRichting = 3;
  } 
  if (digitalRead(SW_W) == LOW) { gekozen = 3; actieveRichting = 4;
  } 
  if (digitalRead(SW_C) == LOW) { gekozen = 4; actieveRichting = 5;
  } 

  if (gekozen != -1) {
    piep();
    geselecteerdeKamer = gekozen;
    
    bepaalRoute(startKamer, geselecteerdeKamer);
    
    huidigeStatus = STATE_RIJDEN;
    statusTimer = millis(); 
    toonLocatieScherm();
    delay(500); 
  }
}

// Toont het startscherm waarop je een bestemming kunt kiezen en laat zien waar de robot nu staat
void toonKiesKamerScherm() {
  dwenguinoLCD.clear();
  dwenguinoLCD.setCursor(0, 0);
  dwenguinoLCD.print(txtKiesKamer[huidigeTaal]);
  
  dwenguinoLCD.setCursor(0, 1);
  dwenguinoLCD.print(txtNu[huidigeTaal]);
  if (startKamer == -1) {
    dwenguinoLCD.print(txtOnbekend[huidigeTaal]);
  } else {
    dwenguinoLCD.print(kamers[huidigeTaal][startKamer]);
  }
}

// Laat tijdens het rijden op het scherm zien naar welke kamer de robot onderweg is
void toonLocatieScherm() {
  dwenguinoLCD.clear();
  dwenguinoLCD.setCursor(0, 0);
  dwenguinoLCD.print(txtLocatie[huidigeTaal]);
  dwenguinoLCD.setCursor(0, 1);
  dwenguinoLCD.print(kamers[huidigeTaal][geselecteerdeKamer]);
}

// Laat de ingebouwde zoemer een kort piepje geven, mits het geluid niet is uitgezet
void piep() {
  if (geluidAan) {
    tone(BUZZER, 1000, 70); 
  }
}

// Dit gebeurt er als de robot veilig op zijn bestemming is aangekomen
void actieVoltooid() {
  dwenguinoLCD.clear();
  dwenguinoLCD.setCursor(0, 0);
  dwenguinoLCD.print(txtVoltooid[huidigeTaal]); 
  
  startKamer = geselecteerdeKamer;
  EEPROM.write(EEPROM_LAST_ROOM_ADDR, startKamer);

  if (geluidAan) {
    tone(BUZZER, 880, 150); delay(150);
    tone(BUZZER, 1100, 150); delay(150);
    tone(BUZZER, 1320, 300);
  }
  
  // Laat de ingebouwde led rustig groen uitfaden als succes-signaal
  for (int i = 255; i >= 0; i--) {
    setRGB(0, i, 0);
    delay(6); 
  }
  setRGB(0, 0, 0); 
  
  dwenguinoLCD.clear(); 
  actieveRichting = 0; 
  huidigeStatus = STATE_KIES_KAMER;
  toonKiesKamerScherm(); 
}

// Laat beide motoren achteruit draaien
void stuurMotorenAchteruit() {
  wielServoLinks.write(0);     
  wielServoRechts.write(180);
}

// Zet beide motoren stil
void stopMotoren() {
  wielServoLinks.write(90);    
  wielServoRechts.write(90);
}

// Stuurt de juiste kleurwaarden naar de RGB-led (omdat deze omgekeerd werkt trekken we de waarde af van 255)
void setRGB(int r, int g, int b) {
  analogWrite(11, 255 - r);
  analogWrite(14, 255 - g);
  analogWrite(12, 255 - g); 
  analogWrite(15, 255 - b);
  analogWrite(13, 255 - b);
}

// Dit bit-trucje spiegelt een byte horizontaal om afbeeldingen correct weer te geven op het tweede scherm
byte spiegelByte(byte b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
  return b;
}

// Stuurt de data via SPI door naar de twee gekoppelde led-matrixschermen
void max7219_cascade_write(byte reg, byte dataMatrix1, byte dataMatrix2) {
  digitalWrite(MATRIX_CS, LOW);
  SPI.transfer(reg);
  SPI.transfer(dataMatrix2); 
  SPI.transfer(reg);
  SPI.transfer(dataMatrix1); 
  digitalWrite(MATRIX_CS, HIGH);
}

// De basisinstellingen voor de MAX7219 chips om de led-matrices correct op te starten
void initMatrixHardware() {
  max7219_cascade_write(0x09, 0x00, 0x00); 
  max7219_cascade_write(0x0A, 0x02, 0x02); 
  max7219_cascade_write(0x0B, 0x07, 0x07);
  max7219_cascade_write(0x0C, 0x01, 0x01); 
  max7219_cascade_write(0x0F, 0x00, 0x00); 
}

// Tekent pixel voor pixel de geselecteerde vorm (pijl, smiley of druppel) op de led-schermen
void updateMatrixScherm() {
  byte patSmiley[8] = {0x00, 0x00, 0x24, 0x00, 0x00, 0x42, 0x3C, 0x00};
  byte patBoven[8]  = {0x18, 0x3C, 0x7E, 0xFF, 0x3C, 0x3C, 0x3C, 0x3C};
  byte patRechts[8] = {0x08, 0x0C, 0x0E, 0xFF, 0xFF, 0x0E, 0x0C, 0x08};
  byte patOnder[8]  = {0x3C, 0x3C, 0x3C, 0x3C, 0xFF, 0x7E, 0x3C, 0x18};
  byte patLinks[8]  = {0x10, 0x30, 0x70, 0xFF, 0xFF, 0x70, 0x30, 0x10};
  byte patDruppel[8]= {0x18, 0x18, 0x24, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E};
  for (int i = 0; i < 8; i++) {
    byte m1 = 0x00;
    byte m2 = 0x00;

    switch(actieveRichting) {
      case 0: m1 = patSmiley[i];  m2 = spiegelByte(patSmiley[7 - i]); break;
      case 1: m1 = patBoven[i];  m2 = spiegelByte(patBoven[7 - i]);  break;
      case 2: m1 = patRechts[i]; m2 = spiegelByte(patRechts[7 - i]); break; 
      case 3: m1 = patOnder[i];  m2 = spiegelByte(patOnder[7 - i]);  break; 
      case 4: m1 = patLinks[i];  m2 = spiegelByte(patLinks[7 - i]);  break;
      case 5: m1 = patDruppel[i];m2 = spiegelByte(patDruppel[7 - i]);break; 
    }
    
    max7219_cascade_write(i + 1, m1, m2);
  }
}
