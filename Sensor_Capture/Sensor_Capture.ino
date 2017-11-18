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
 * Capture pressure sensor data, tracking background level,
 * and send the timing of a detection of a window of significantly above-background values
 */

#define DEBUG

int led = 11;

uint16_t EOS = 0xFF00;

#define AVERAGE_WIN   256  // Number of samples to include in running average
#define THRESHOLD      15  // Sensor value above average value indicating a detection
#define THRESHOLD_WIN   5  // How many consecutive samples above the THRESHOLD constitutes a valid detection

struct channel_s {
  uint32_t ID;                        // Channel identifier sent to C.H.I.P with the peak sample
  int      pin;                       // ADC pin for the channel
  double   average;                   // Running average (i.e. "background" or "idle" level)
  uint8_t  aboveThreshold;            // (Saturated) count of consecutive samples above threshold
  uint8_t  belowThreshold;            //      "        "            "        "    below      "
  bool     isAbove;
  uint16_t peakPressure;
  uint16_t sampleCount;
} g_channel[2] = {
  {0xFFAAAA00, 9, 512, 0, 0, 0, 0, 0},
  {0xFF555500, 1, 512, 0, 0, 0, 0, 0}
};


void setup() {
  DIDR0 = 0xFF;                                   // Disable digital inputs
  ADCSRA = (1<<ADEN) /*| ADC_PRESCALER*/;             // enable ADC
 
  pinMode(led, OUTPUT);
  //Serial1.begin(9600);
  Serial1.begin(115200);
}


// Sample a channel
void sample_channel(int chan)
{
  uint16_t adcVal;
  int i;
  bool isRising;
  
  digitalWrite(led, chan);
  for (i = 0; i < 5; i++) {    // Must do multiple read to let the ADC input cap fully charge
     adcVal= analogRead(g_channel[chan].pin);
  }
#ifdef DEBUGALL
  Serial.print(g_channel[chan].average);
  Serial.print(' ');
  Serial.print(adcVal);
#endif

  // Did we cross  
  if (adcVal >= g_channel[chan].average + THRESHOLD) {
    if (g_channel[chan].aboveThreshold < 255) g_channel[chan].aboveThreshold++;
    g_channel[chan].belowThreshold = 0;
    if (g_channel[chan].peakPressure < adcVal) g_channel[chan].peakPressure = adcVal;
  } else {
    if (g_channel[chan].belowThreshold < 255) g_channel[chan].belowThreshold++;
    g_channel[chan].aboveThreshold = 0;
    g_channel[chan].peakPressure = 0;
  }
#ifdef DEBUGALL
  Serial.print(' ');
  Serial.print(g_channel[chan].belowThreshold);
  Serial.print(' ');
  Serial.print(g_channel[chan].aboveThreshold);
  Serial.print(' ');
#endif

  // Do we have a solid transition?
  isRising = 0;
  if ( g_channel[chan].isAbove && g_channel[chan].belowThreshold > THRESHOLD_WIN*3) {
    g_channel[chan].isAbove = 0;
  }
  if (!g_channel[chan].isAbove && g_channel[chan].aboveThreshold > THRESHOLD_WIN) {
    g_channel[chan].isAbove = 1;
    isRising = 1;
  }

#ifdef DEBUGALL
  Serial.print(g_channel[chan].isAbove);
  Serial.print(' ');
#endif

  // Save the differential value (aka pressure)
  uint16_t pressure = g_channel[chan].peakPressure - g_channel[chan].average;
  
  // Update the background value (so we can track change in sensor due to temperature variations)
  g_channel[chan].average = ((g_channel[chan].average * (double) (AVERAGE_WIN-1)) + adcVal) / (double) AVERAGE_WIN;
  // If the average goes too low (i.e. grounded ADC input), any noise will easily generate triggers
  if (g_channel[chan].average < 250) g_channel[chan].average = 250;

  // Do we have a valid detection?
  // i.e. a solid rising edge
  if (isRising) {
#ifdef DEBUGALL
    Serial.print("*** | ");
#endif
    // Send the timestamp and pressure value to C.H.I.P
    uint32_t stamp = millis();
    
    Serial1.write((uint8_t*) &g_channel[chan].ID, sizeof(g_channel[chan].ID));
    Serial1.write((uint8_t*) &stamp, sizeof(stamp));
    Serial1.write((uint8_t*) &pressure, sizeof(pressure));
    Serial1.write((uint8_t*) &EOS, sizeof(EOS));
    Serial1.write('\n');
#ifdef DEBUG
    Serial.print(chan);
    Serial.print(' ');
    Serial.print(stamp);
    Serial.print(' ');
    Serial.println(pressure);
#endif

    g_channel[chan].sampleCount = 0;
  } else {
    // Send a heartbeat to C.H.I.P.
    if (g_channel[chan].sampleCount++ == 0xFFFF) {
      uint32_t heartbeat = 0xFFA5A500;
      Serial1.write((uint8_t*) &heartbeat, sizeof(heartbeat));
      Serial1.write('\n');
#ifdef DEBUGALL
      Serial.print("--- | ");
    } else {
      Serial.print("    | ");
#endif
    }
  }
}

void loop() {
  sample_channel(0);
  sample_channel(1);
#ifdef DEBUGALL
  Serial.println();
#endif
}
