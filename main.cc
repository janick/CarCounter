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
  uint32_t lastStamp;
} channelData[2];


void
analyzeSample(uint16_t chan0,
	      uint16_t chan1,
	      uint32_t stamp)
{
#ifdef DEBUG
  printf("%04x %04x %08x\n", chan0, chan1, stamp);
#endif

  //  channelData[chan].lastStamp = stamp;
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
