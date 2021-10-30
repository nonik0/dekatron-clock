int Guide1 = D8;       // Guide 1 - G1 pin of 2-guide Dekatron           // D8
int Guide2 = D7;       // Guide 2 - G2 pin of 2-guide Dekatron           // D7
int Index  = D6;       // Index   - NDX input pin. High when glow at K0  // D6
int HVEnable = D5;
int PIRPin = D4;

volatile byte digitStep = 0;
volatile byte phaseStep = 0;

byte hours = 0;
byte mins = 5;
byte secs = 0;

#define INT_MUX_COUNTS         550

// ************************************************************
// Interrupt routine for scheduled interrupts
// Spin through each of the pins on the decatron, using a short
// interrupt time for "off" pins (not really off, but quite dim)
// and longer dwell times for the lit pins.
// ************************************************************
ICACHE_RAM_ATTR void displayUpdate() {
  int delayCount = INT_MUX_COUNTS;
  phaseStep++;
  
  if (phaseStep == 3) {
    phaseStep = 0;
    digitStep++;
    if (digitStep==30){
      digitStep = 0;
      // Check the index, bump if necessary
      if (digitalRead(Index)) {
        digitStep++;
      }
    }
  }

  G_step(phaseStep);

  byte pinStep = digitStep*3+phaseStep;
    if (pinStep == hours) {
      delayCount = 60*INT_MUX_COUNTS;
    }
    if (pinStep == mins) {
      delayCount = 30*INT_MUX_COUNTS;
    }
    if (pinStep == secs) {
      delayCount = 15*INT_MUX_COUNTS;
    }

  timer1_write(delayCount);
}



ICACHE_RAM_ATTR void G_step(int CINT)
{
  if (CINT == 0)
  {
    digitalWrite(Guide1, LOW);
    digitalWrite(Guide2, LOW);
  }
  if (CINT == 1)
  {
    digitalWrite(Guide1, HIGH);
    digitalWrite(Guide2, LOW);
  }
  if (CINT == 2)
  {
    digitalWrite(Guide1, LOW);
    digitalWrite(Guide2, HIGH);
  }
}

// setup() runs once, at reset, to initialize system
void setup() {
  Serial.begin(115200);
  Serial.println("------- START -------");
  
  pinMode(Guide1, OUTPUT);
  pinMode(Guide2, OUTPUT);
  pinMode(Index, INPUT);
  pinMode(HVEnable, OUTPUT);
  digitalWrite(HVEnable, HIGH);

  Serial.println("Starting display interrupt handler");
  
  timer1_attachInterrupt(displayUpdate);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
  timer1_write(INT_MUX_COUNTS);
  
  Serial.println("Started");
}

// the loop function runs over and over again forever
void loop() {
//  Ndx = digitalRead(Index); // Sample for glow at K0
//  digitalWrite(LED, Ndx);
  delay(2000);
  secs++;
  if (secs == 30) {
    secs = 0;
  }
  if (secs == 0) {
    digitalWrite(HVEnable, LOW);
  } else {
    digitalWrite(HVEnable, HIGH);
  }
}
