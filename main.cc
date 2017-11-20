//
// Copyright 2017 Janick Bergeron <janick@bergeron.com>
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

#include <error.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>

#define DEBUG
//#define DEBUGRX


static struct channel_s {
  double   average;
  bool     isIdle;
  uint32_t idleCount;
  uint32_t detectTime;
  bool     hasEvent;
} channelData[2] = {
  {0x200, true, 0, 0, false},
  {0x200, true, 0, 0, false}
};

uint32_t frontWheelStamp = 0;


void
analyzeChannel(unsigned int chan,
	       uint16_t     pressure,
	       uint32_t     stamp)
{
  // Reject obviously bad samples
  if (pressure < 0x0100 || 0x400 < pressure) return;
  
  if (pressure >= channelData[chan].average + 0x15) {
    
    if (channelData[chan].isIdle
	// Reject detections caused by bouncing in the hose
	&& channelData[chan].idleCount > 100) {

#ifdef DEBUG
      printf("DTCT %d %04x %04x after %d Idle\n",
	     chan, pressure, stamp, channelData[chan].idleCount);
#endif
      
      channelData[chan].detectTime = stamp;
      channelData[chan].hasEvent   = true;
    }
    
    channelData[chan].isIdle     = false;
    channelData[chan].idleCount  = 0;

  } else {
#ifdef DEBUG
    if (!channelData[chan].isIdle) {
      printf("IDLE %d %04x %04x\n", chan, pressure, stamp);
    }
#endif
    channelData[chan].isIdle = true;
    if (channelData[chan].idleCount < 0x10000000) channelData[chan].idleCount++;

    // Make sure a spurious event gets cleared
    if (channelData[chan].idleCount > 100000) channelData[0].hasEvent = false;
  }

  // Update the running average
  #define AVERAGE_WIN 100
  channelData[chan].average = ((channelData[chan].average * (double) (AVERAGE_WIN-1)) + pressure) / (double) AVERAGE_WIN;
}

  
void
analyzeSample(uint16_t chan0,
	      uint16_t chan1,
	      uint32_t stamp)
{
#ifdef DEBUGRX
  printf("%04x %04x %08x\n", chan0, chan1, stamp);
#endif
  analyzeChannel(0, chan0, stamp);
  analyzeChannel(1, chan1, stamp);

  // Do we have an event recorded on both channels?
  if (!channelData[0].hasEvent || !channelData[1].hasEvent) return;

  // Which one occured first?
  long int ms = channelData[0].detectTime - channelData[1].detectTime;

  bool isUp = false;
  if (ms < 0) {
    isUp = true;
    ms = -ms;
  }

  double mph = ((double) 681.8) / ms;

  // Marked these event has handled
  channelData[0].hasEvent = false;
  channelData[1].hasEvent = false;

  // Measure wheel base
  // It does not matter which hose we use...
  stamp = channelData[0].detectTime;
  
  ms = stamp - frontWheelStamp;
  if (ms < 0) ms = -ms;
  double feet = 0.00147 * ms * mph;
  
  printf("%.1f MPH %shill. Wheel base = %.1f ft\n", mph, (isUp) ? "Up" : "Down", feet);

  // Reject if the speed is too high or the wheelbase is obviously too long
  if (mph > 60 || feet > 50) {
  }

  frontWheelStamp = stamp;
}


// Keep a rolling window of sampled characters
union rxData_u {
  char     c[12];

  // Must use 16-bit segments because the compiler insists on aligning 32-bit values
  // Further, the Teensy and CHIP use a different endian
  struct channelSample_s {
    uint16_t SOFR;
    uint16_t pressureData_0;
    uint16_t pressureData_1;
    uint16_t stampA;
    uint16_t stampB;
    uint16_t EOFR;
  } d;
  
} rxData;


void
analyzeChar(char c)
{
  for (unsigned int i = 0; i < sizeof(rxData.c)-1; i++) {
    rxData.c[i] = rxData.c[i+1];
  }
  rxData.c[sizeof(rxData.c)-1] = c;

#ifdef DEBUGRX
  for (unsigned int i = 0; i < sizeof(rxData.c); i++) printf("%02x", rxData.c[i]);
  printf("   ");
  printf("%04x %04x %04x %04x%04x %04x\n", rxData.d.SOFR, rxData.d.pressureData_0, rxData.d.pressureData_1, rxData.d.stampA, rxData.d.stampB, rxData.d.EOFR);   
#endif

  if (rxData.d.SOFR == 0x0AAF && rxData.d.EOFR == 0xF550) {
    analyzeSample(rxData.d.pressureData_0, rxData.d.pressureData_1,
		  (((uint32_t) rxData.d.stampB) << 16) + rxData.d.stampA);
  }
}


int
main(int argc, char* argv[])
{
  int TTY = open("/dev/ttyS0", O_RDWR);
  if (TTY <= 0) {
    fprintf(stderr, "ERROR: Cannot open /dev/ttyS0: ");
    perror(NULL);
    exit(1);
  }

  struct termios ttySettings;
  if (tcgetattr(TTY, &ttySettings) != 0) {
    fprintf(stderr, "ERROR: Cannot get TTY settings: ");
    perror(NULL);
    goto abort;
  }

  cfsetispeed(&ttySettings, (speed_t)B115200);
  ttySettings.c_cflag &= ~PARENB;
  ttySettings.c_cflag &= ~CSTOPB;
  ttySettings.c_cflag &= ~CSIZE;
  ttySettings.c_cflag |=  CS8;

  ttySettings.c_iflag =  IGNPAR;
  
  if (tcsetattr(TTY, TCSANOW, &ttySettings) != 0) {
    fprintf(stderr, "ERROR: Cannot set TTY: ");
    perror(NULL);
    goto abort;
  }

  while (1) {
    char inChar;
    while (read(TTY, &inChar, 1) != 1);
    analyzeChar(inChar);
  }
  
  close(TTY);
  return 0;

 abort:
  close(TTY);
  return 1;
}
