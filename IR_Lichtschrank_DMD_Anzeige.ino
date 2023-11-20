#include "SPI.h"      
#include "DMD.h" 
#include <TimerOne.h>

//FONTS - you can remove them if unused
#include "Arial_black_16.h" <Arial_black_16.h>
#include "SystemFont5x7.h"

/**************************************
************** SETTINGS ***************
**************************************/

/* change these values if you have more than one DMD connected */
#define DISPLAYS_ACROSS 1   
#define DISPLAYS_DOWN 1

#define SPEED_MESSUNG false

const int irSensorInput = A5;                     //analog IN for fototransistor input
const int signalLED = 4;                          //digital OUT for signal LED (LED that indicates activation of the light barrier, helpful for debugging/development)
const long sperrfrist = 3000;                     //Sperrfrist in Millisekunden, die die LS gesperrt wird, nachdem eine Zählung begonnen hat. Das soll versehentliches Beenden noch am Start verhindern, z.B. wenn ein Arm nochmal durch die Lichtschranke pendelt

const int setupDelay = 50;                        //Duration of the ir-sensor-init-loop (during setup()) in milliseconds; number of cycles * setupDelay = duration of initialisation (default = 2 sec)
const int loopDelay = 100;                        //Duration of the actual loop in milliseconds (avoid high values, or the light-barrier might not work (or, better: might not recognize crossing of the barrier))

const int numSetupCycles = 40;                    //Number of cycles for initial IR-sensor setup

const int sensitivity_delta = 60;                 //Crossing of the light barrier is measured by a difference between the current light value and the average value.

const int distance = 10;                          //Distance in meters to run/ride before the barrier is crossed again; is used to calculate and display the speed

const unsigned int counterLimit = 60;             //Limit in seconds after that the counter resets automatically to avoid never ending counting (e.g. in case the seconds passing of the barrier hasn't been recognized...)

const unsigned int ergebnisAnzeigeDauer = 7000;   //Dauer der Anzeige der Ergebniszeit und -geschwindigkeit (falls eingestellt) in Millisekunden

/**************************************
*********** END OF SETTINGS ***********
**************************************/

//Helper variables
int i = 0;
int iMin = 999;
int iMax = 0;
int delta = 0;
bool initialisierung = true;
String serialOut = "";
unsigned int currentValue = 0;
unsigned long averageValue = 0;
double speed = 0.0;
bool statusLsDurchbrochen = false;
unsigned long counterStartValue = 0;
unsigned long counterCounting = 0;
unsigned long counterDifference = 0;
enum zaehlerStatus {COUNTING, NOT_COUNTING};
int irStatus = NOT_COUNTING;
char zeitanzeige[5];
char zeitanzeige_float[7];
unsigned long endeLetzteZaehlung = 0;
int zeit = 0;
double zeit_float = 0.0;
byte textLaenge;

/**************************************/

/*** FUNCTIONS **/

DMD dmd(DISPLAYS_ACROSS,DISPLAYS_DOWN);

void ScanDMD()
{ 
  dmd.scanDisplayBySPI();
}

/*Helper function to draw running text across all displays*/

void drawText(String dispString){
  dmd.clearScreen( true );
  dmd.selectFont( SystemFont5x7 );
  char newString[256];
  int sLength = dispString.length();
  dispString.toCharArray( newString, sLength+1 );
  dmd.drawMarquee(newString,sLength,( 32*DISPLAYS_ACROSS )-1 , 3 );
  long start=millis();
  long timer=start;
  long timer2=start;
  boolean ret=false;
  while(!ret){
    if ( ( timer+40 ) < millis() ) {
      ret=dmd.stepMarquee( -1 , 0 );
      timer=millis();
    }
  }
}

void calibrateSensorsAndClearScreen(){

    while (initialisierung){    

    currentValue = analogRead(irSensorInput);

    //Calculate min and max values continuously (mainly for debugging)
    if (analogRead(irSensorInput) > iMax) iMax = analogRead(irSensorInput);
    if (analogRead(irSensorInput) < iMin) iMin = analogRead(irSensorInput);

    //For development/debugging -> Print IR values to the serial monitor
    serialOut = String("Initialisierung. Avg: " + String(averageValue) + ", Durchlauf: " + String(i+1) + "/" + String(numSetupCycles) + " Current: " + String(currentValue));
    Serial.println(serialOut);

    averageValue = averageValue + currentValue;
    i++;

    delay(setupDelay);

    //After the defined count of probes, exit init stage
    if (i == numSetupCycles-1) {
      averageValue = averageValue/numSetupCycles;
      initialisierung = false;

      //Warte die oben eingestellte Zeit für 'ergebnisAnzeigeDauer', aber minus die schon für das Initialisieren verbrauchte Zeit
      delay(ergebnisAnzeigeDauer-(setupDelay*numSetupCycles));
      dmd.clearScreen(true);
    }

  }

}

void setup() {

  Serial.begin(9600);

  /***********************************
  DMD setup
  ***********************************/

   Timer1.initialize( 5000 );           
  /*period in microseconds to call ScanDMD. Anything longer than 5000 (5ms) and you can see flicker.*/

   Timer1.attachInterrupt( ScanDMD );  
  /*attach the Timer1 interrupt to ScanDMD which goes to dmd.scanDisplayBySPI()*/

  dmd.selectFont( SystemFont5x7 );

//  //Auskommentiert, um Zeit zu sparen und eigentlich muss man vorher auch nicht die LEDs kontrollieren  
//
//  //Teste alle LEDs, indem eine senkrechte Linie durchläuft
//  for (int i = 0;i<32*DISPLAYS_ACROSS;i++){
//    dmd.drawLine(i, 0, i, 15, GRAPHICS_NORMAL);
//   delay(100);
//    dmd.clearScreen(true);
//
//  }

  dmd.clearScreen( true );            
  delay(500);  

  /***************************************
      Lichtschranken-Setup
  ***************************************/
  pinMode(irSensorInput, INPUT);
  pinMode(signalLED, OUTPUT);

  //Initialize the signal LED turned off
  digitalWrite(signalLED, LOW);

  //Show a text while the IR light-barrier is setting up   
  //dmd.drawString( 2,0, "IR", 2, GRAPHICS_NORMAL );
  //dmd.drawString( 2,9, "Setup", 5, GRAPHICS_NORMAL );

  //IF desired, the font can be set to the big, bold letters before the main loop, but on only one DMD that is too large for more than 3 digits
  //dmd.selectFont( Arial_Black_16 );

  //Erste Kalibrierung, später nach jeder Benutzung; dazu hier ausnahmsweise einen String anzeigen (später Kalibirierung während der Ergebnisanzeige)
  dmd.drawString( 1,4, "SETUP", 5, GRAPHICS_NORMAL );
  calibrateSensorsAndClearScreen();
}

void loop() {

  /***************************************
  Überwachung der Lichtschranke
  "statusLsDurchbrochen" wird true, falls LS durchbrochen wurde...
  ***************************************/ 

  currentValue = analogRead(irSensorInput);

  //Calculate min and max values continuously for debugging
  if (analogRead(irSensorInput) > iMax) iMax = analogRead(irSensorInput);
  if (analogRead(irSensorInput) < iMin) iMin = analogRead(irSensorInput);

  delta = averageValue-currentValue;
  if (delta < 0) delta = delta * -1;

  //For development/debugging -> Print IR values to the serial monitor
  serialOut = String("Current value: " + String(currentValue) + " - Avg.: " + String(averageValue) + ", delta: " + String(delta) + ", Thresh: " + String(sensitivity_delta) + ", Max: " + String(iMax) + ", Min: " + String(iMin));
  Serial.println(serialOut);

  //Falls durchbrochen wurde
  if (delta > sensitivity_delta){    

    //Light a signal LED to show that we have a "crossing"
    digitalWrite(signalLED, HIGH);

    statusLsDurchbrochen = true;  
    
  } else {
    
    digitalWrite(signalLED, LOW);
    statusLsDurchbrochen = false;
  }

  //Zentrale Unterscheidung des Systemzustands: entweder sind wir im Zählmodus, oder nicht...

  /****************************************
  ****** SYSTEM STATE: NOT COUNTING ******/

  if (irStatus == NOT_COUNTING){

    //Zeige Schriftzug "Los?" im idle-Status des Systems
    dmd.drawString(1, 4, "Los?", 4, GRAPHICS_NORMAL);
  
    //Falls LS durchbrochen und die letzte Zählung 
    if (statusLsDurchbrochen && (millis() - endeLetzteZaehlung) > sperrfrist){
      
      //Starte Zählung
      irStatus = COUNTING;

      counterStartValue = millis();   //Wir merken uns den Anfangsstand zum Zählbeginn (Erklärung: millis() ist eine Systemfunktion, die fortlaufend die Millisekunden seit dem letzten Reset des Arduino zählt;
      counterCounting = millis();     //um eine Dauer zu messen, müssen wir uns den Stand von millis() zum Start merken und später vom Endwert abziehen)

      //Zeige die Zeit für das Debugging im Serialmonitor an 
      Serial.println(String("Messung gestartet!"));
    
    }

    //Falls LS nicht durchbrochen ist
    else{
      //Falls Barriere nicht durchbrochen ist passiert eigentlich weiter nichts. Ich lasse das hier aber drin, falls ich im Idle-Zustand etwas anzeigen will, oder so...   
    }
  }

  /****************************************
  ****** SYSTEM STATE: COUNTING ******/

  else if (irStatus == COUNTING){

    //Aktualisiere auf jeden Fall zuerst den aktuellen Zählerwert und die aktuelle Zählzeit
    counterCounting = millis();
    counterDifference = counterCounting-counterStartValue; //Tatsächliche Zeit ist Jetzt-Zeit minus Startzeit (so funktioniert die millis()-Funktion)

    //ANZEIGE MIT NACHKOMMASTELLEN (die alte Anzeige ohne habe ich entfernt)
    //Bereite die Sekunden für die Anzeige auf der DMD vor: Die Funktion ist etwas picky was die Datentypen angeht; sie will den Text als char-Array und dessen Länge als byte 
    zeit_float = counterDifference/1000.0;
    String(zeit_float).toCharArray(zeitanzeige_float,7);  //Wandele die Zeit in einen String um und dann in ein char-Array, weil das Display das braucht
    zeitanzeige_float[5] = NULL;
    textLaenge = byte(String(zeit_float).length()); //Die Anzeige auf dem Display braucht die Textlänge als Byte

    //Zeige dann die Zeit auf dem Display an
    dmd.clearScreen(true);
    dmd.drawString( 1,4, zeitanzeige_float, textLaenge, GRAPHICS_NORMAL );

    //Zeige die Zeit für das Debugging im Serialmonitor an
    //serialOut = String("Aktuelle Zeit: " + zeit + " Sekunden");
    //Serial.println(serialOut);

    //Falls LS durchbrochen und Sperrfrist abgelaufen beenden wir die Zählung
    if (statusLsDurchbrochen && counterDifference > sperrfrist){    

      //Zeige die Zeit für das Debugging im Serialmonitor an (ACHTUNG: das wirft Fehler im Compiler, den ich nicht beheben konnte, daher auskommentiert)
      //Serial.println(String("Messung zu Ende!\tGemessene Endzeit: " + counterDifference + " Sekunden"));

      endeLetzteZaehlung = millis();  //Hilfsvariable, damit nicht sofort wieder eine neue Zählung losgeht
      irStatus = NOT_COUNTING;        //Aktualisiere Systemstatus

      //Wir zeigen die Ergebniszeit für die oben eingestellte Dauer an und nutzen die Zeit für die Kalibrierung der Sensoren
      dmd.clearScreen(true);
      dmd.drawString( 1,1, zeitanzeige_float, textLaenge, GRAPHICS_NORMAL );
      dmd.drawString( 1,9, "Sek.", 4, GRAPHICS_NORMAL );

      calibrateSensorsAndClearScreen();
      
      //Falls die Geschwindigkeitsmessung aktiv ist (einstellbar oben in den Settings), berechne und zeige an für die oben eingestellte Dauer
      if (SPEED_MESSUNG){
        // Berechne die Geschwindigkeit; geht aber nur bei einer klar definierbaren Strecke (einstellbar in den Settings, default 10m)
        speed = distance/(counterDifference/1000.0) * 3.6; //Speed in Meter/Sekunde, Umrechnung in km/h mit "Mal 3,6"

        String(speed).toCharArray(zeitanzeige_float,7);   //Wandele die Zeit in einen String um und dann in ein char-Array, weil das Display das braucht
        textLaenge = byte(String(speed).length());   //Die Anzeige auf dem Display braucht die Textlänge als Byte

        dmd.clearScreen(true);
        dmd.drawString( 1,1, zeitanzeige_float, textLaenge, GRAPHICS_NORMAL );
        dmd.drawString( 1,9, "km/h", 4, GRAPHICS_NORMAL );

        delay(ergebnisAnzeigeDauer);
      }
      dmd.clearScreen(true);
    }
    
    //Falls LS nicht durchbrochen
    else{
      //Falls Barriere nicht durchbrochen ist passiert eigentlich weiter nichts, die Anzeige auf dem Display machen wir ja schon oben. Ich lasse das hier aber drin, falls doch mal was kommen soll...
    }
  }  
  delay(loopDelay);
}
