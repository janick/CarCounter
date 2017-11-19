//
//  Copyright 2017 Janick Bergeron <janick@bergeron.com>
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//   
   
/*
 * Teensy 2.0 sketch
 * 
 * Capture pressure sensor data and send them out as fast as possible.
 */

#undef DEBUG
#undef DEBUGALL

int led = 11;

struct txFrame_s {
  uint16_t SOFR;
  uint16_t chan[2];
  uint32_t stamp;
  uint16_t EOFR;
} txFrame;

void setup() {
  DIDR0 = 0xFF;                                   // Disable digital inputs
  ADCSRA = (1<<ADEN) /*| ADC_PRESCALER*/;             // enable ADC

  txFrame.SOFR = 0x0AAF;
  txFrame.EOFR = 0xF550;
 
  pinMode(led, OUTPUT);

  // Have to wait before we use the UART because it'll prevent the CHIP from booting
  int i;
  for (i = 0; i < 30; i++) {
    digitalWrite(led, i & 0x00000001);
    delay(1000);
  }
  
  //Serial1.begin(9600);
  Serial1.begin(115200);
}


// Sample a channel
uint16_t sample_channel(int pin)
{
  int i;
  uint16_t adcVal;
  
  for (i = 0; i < 5; i++) {    // Must do multiple read to let the ADC input cap fully charge
     adcVal = analogRead(pin);
  }
#ifdef DEBUGALL
  Serial.print(adcVal);
  Serial.print(' ');
#endif

  return adcVal;
}

unsigned int count = 0;
void loop()
{
  digitalWrite(led, (count++ >> 9) & 0x01);
  
  txFrame.chan[0] = sample_channel(1);
  txFrame.chan[1] = sample_channel(9);
  txFrame.stamp   = millis();

#ifdef DEBUGALL
  Serial.println(txFrame.stamp);
#endif
    
  Serial1.write((uint8_t*) &txFrame, sizeof(txFrame));
}
