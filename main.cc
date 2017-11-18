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
#define DEBUGRX


static struct channel_s {
  uint32_t lastStamp;
} channelData[2];


void
analyzeChannel(unsigned int chan,
	       uint32_t     stamp,
	       uint16_t     pressure)
{
#ifdef DEBUG
  fprintf(stderr,  "%lu %d %u %d\n", time(NULL), chan, stamp, pressure);
#endif
  channelData[chan].lastStamp = stamp;
}


// Keep a rolling window of sampled characters
union rxData_u {
  char     c[12];
  
  struct channelSample_s {
    uint32_t tag;
    uint32_t stamp;
    uint16_t pressure;
    uint16_t EOS;
  } d;
  
} rxData;


void
analyzeChar(char c)
{
  for (unsigned int i = 0; i < sizeof(rxData.c)-1; i++) {
    rxData.c[i] = rxData.c[i+1];
  }
  rxData.c[sizeof(rxData.c)-1] = c;

  switch (rxData.d.tag) {

  case 0xFFAAAA00:
    if (rxData.d.EOS == 0xFF00) analyzeChannel(0, rxData.d.stamp, rxData.d.pressure);
    break;

  case 0xFF555500:
    if (rxData.d.EOS == 0xFF00) analyzeChannel(1, rxData.d.stamp, rxData.d.pressure);
    break;
    
  case 0xFFA5A500:
    // Heartbeat.
    break;
  }
  
#ifdef DEBUGRX
  if (c != '\n') fprintf(stderr, "%02x ", c);
#endif
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
