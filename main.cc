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
#include <termios.h>
#include <unistd.h>

#define DEBUG


static struct channel_s {
  uint32_t lastStamp;
} channelData[2];


void
analyzeChannel(unsigned int chan,
	       uint32_t     stamp)
{
#ifdef DEBUG
  printf("%d %d", chan, stamp);
#endif
  channelData[chan].lastStamp = stamp;
  
}


// Keep a rolling window of sampled characters
union rxData_u {
  char     c[8];
  uint64_t bin;
  
  struct channelSample_s {
    uint32_t tag;
    uint32_t stamp;
  } d;
  
} rxData;


void
analyzeChar(char c)
{
  if (c == '\n') {
    switch (rxData.d.tag) {

    case 0xAAAAAAAA:
      analyzeChannel(0, rxData.d.stamp);
      break;

    case 0x55555555:
      analyzeChannel(1, rxData.d.stamp);
      break;

    case 0xA5A5A5A5:
      // Heartbeat.
      break;
    }
  }
  
#ifdef DEBUGRX
  if (c != '\n') printf("%02x ", c);
#endif
  
#ifdef DEBUGRX
  if (c == '\n') putchar(c);
  fflush(stdout);
#endif
  
  rxData.bin = (rxData.bin >> 8);
  rxData.c[7] = c;
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
