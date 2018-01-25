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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>


unsigned int gDebug = 0;
bool         gIsRead = false;
FILE        *gFp = NULL;

static struct channel_s {
  double   average;
  bool     isIdle;
  bool     isChanging;
  uint32_t changeCount;
  uint64_t detectTime;
  bool     hasEvent;
} channelData[2] = {
  {0x200, true, 0, 0, false},
  {0x200, true, 0, 0, false}
};

uint64_t frontWheelStamp = 0;


void
analyzeChannel(unsigned int chan,
	       uint16_t     pressure,
	       uint64_t     stamp)
{
  // Reject obviously bad samples
  if (pressure < 0x0180 || 0x1000 < pressure) return;

  char HxL = 'x';
  
  if (pressure >= channelData[chan].average + 0x0c0) {

    // High pressure detected
    HxL = 'H';
    
    if (channelData[chan].isIdle) {
      // Are we in the middle of a transition?
      if (channelData[chan].isChanging) {
	// Have we reached the end of the transition?
	// Must see 20 consecutive high-pressure samples
	if (channelData[chan].changeCount++ >= 20) {

	  channelData[chan].isIdle      = false;
	  channelData[chan].isChanging  = false;
	  channelData[chan].changeCount = 0;
	  channelData[chan].detectTime = stamp;
	  channelData[chan].hasEvent    = true;

	  if (gDebug > 0) {
	    unsigned int otherChan = ((chan + 1) & 0x1);
	    if (channelData[otherChan].hasEvent) {
	      printf("DTCT %d %04x > %04x at %08llx with pending event on %d %lld ms ago\n",
		     chan, pressure, (unsigned int) channelData[chan].average, stamp,
		     otherChan, stamp - channelData[otherChan].detectTime);
	    } else {
	      printf("DTCT %d %04x > %04x at %08llx with no event on %d\n",
		     chan, pressure, (unsigned int) channelData[chan].average, stamp, otherChan);
	    }
	  }
	}
	
      } else {
	// We started changing
	channelData[chan].isChanging = true;
	channelData[chan].changeCount = 1;
      }
    } else {
      // A single high-pressure sample cancels a low-pressure transition
      channelData[chan].isChanging  = false;
      channelData[chan].changeCount = 0;
    }

  } else if (pressure <= channelData[chan].average + 0x020) {

    // Low ressure detected
    HxL = 'L';
    
    if (not channelData[chan].isIdle) {
      // Are we in the middle of a transition?
      if (channelData[chan].isChanging) {
	// Must see > 20 consecutive low-pressure samples
	if (channelData[chan].changeCount++ >= 60) {

	  channelData[chan].isIdle      = true;
	  channelData[chan].isChanging  = false;
	  channelData[chan].changeCount = 0;
	  
	  if (gDebug > 0) {
	    printf("IDLE %d %04x < %04x at %08llx\n",
		   chan, pressure, (unsigned int) channelData[chan].average, stamp);
	  }
	}
	
      } else {
	// We started changing
	channelData[chan].isChanging = true;
	channelData[chan].changeCount = 1;
      }
    } else {
      // A single low-pressure sample cancels a high-pressure transition
      channelData[chan].isChanging  = false;
      channelData[chan].changeCount = 0;
    }
  }
  
  if (gDebug > 1) {
    printf("%04x %04x %c %08llx %c%s%3d    ", pressure, (unsigned int) channelData[chan].average, HxL, stamp,
	   channelData[chan].isIdle ? 'L' : 'H',
	   channelData[chan].isChanging ? "->" : "  ",
	   channelData[chan].changeCount);
  }
  

  if (channelData[chan].isIdle && !channelData[chan].isChanging) {
    // Update the running average
    #define AVERAGE_WIN 250
    channelData[chan].average = ((channelData[chan].average * (double) (AVERAGE_WIN-1)) + pressure) / (double) AVERAGE_WIN;
  }
}

  
void
analyzeSample(uint16_t chan0,
	      uint16_t chan1,
	      uint64_t stamp)
{
  if (gFp != NULL && !gIsRead) {
    fprintf(gFp, "%04x %04x %08llx\n", chan0, chan1, stamp);
  }
  
  analyzeChannel(0, chan0, stamp);
  analyzeChannel(1, chan1, stamp);
  if (gDebug > 1) {
    printf("\n");
  }

  //  printf("%d %f\n", chan0, channelData[0].average);
  //  if (channelData[0].average < 425) exit(9);

  // Do we have an event recorded on both channels?
  if (!channelData[0].hasEvent || !channelData[1].hasEvent) return;

  // Which one occured first?
  long long int ms = channelData[0].detectTime - channelData[1].detectTime;

  bool isUp = true;
  if (ms < 0) {
    isUp = false;
    ms = -ms;
  }

  // Reject detections that are way to slow
  if (ms > 2000) {
    // But save the latest event to recover
    if (isUp) channelData[1].hasEvent = false;
    else channelData[0].hasEvent = false;
    return;
  }

  // If it takes 'ms' to cover 12 inches, what is the speed?
  double mph = ((double) 681.8) / ms;

  // Marked these event has handled
  channelData[0].hasEvent = false;
  channelData[1].hasEvent = false;

  time_t now = stamp/1000;
  struct tm *lt = localtime(&now);
  printf("%ld  %4d/%02d/%02d %02d:%02d:%02d ", now,
	 lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec);
  
  // Reject if the speed is too high
  if (0 && mph > 60) {
    printf("                     ");
  } else {
    printf("%6.1f MPH %4shill.", mph, (isUp) ? "Up" : "Down");
  };

  // Measure wheel base (It does not matter which hose we use)
  stamp = channelData[1].detectTime;
  
  ms = stamp - frontWheelStamp;
  if (ms < 0) ms = -ms;
  double feet = 0.00147 * ms * mph;

  frontWheelStamp = stamp;

  // Reject if the wheelbase is obviously too long
  if (feet < 25) {
    printf(" Wheel base =%5.1f ft.", feet);
  }

  printf("\n");
  fflush(stdout);
}


//
// GPIO operations
//
int
gpioOpen(int num, char rw)
{
  int fd;
  char buf[256];

  fd = open("/sys/class/gpio/unexport", O_WRONLY);
  if (fd < 0) {
    fprintf(stderr, "Unable to open GPIO unexport: ");
    perror(NULL);
    return -1;
  }

  sprintf(buf, "%d", num);
  if (write(fd, buf, 3) < 3) {
    fprintf(stderr, "Unable to unexport GPIO %d: ", num);
    perror(NULL);
    // That's OK, as long as they can be exported next...
  }
  close(fd);

  fd = open("/sys/class/gpio/export", O_WRONLY);
  if (fd < 0) {
    fprintf(stderr, "Unable to export GPIO %d: ", num);
    perror(NULL);
    return -1;
  }

  if (write(fd, buf, 3) < 3) {
    fprintf(stderr, "Unable to export GPIO %d: ", num);
    perror(NULL);
    close(fd);
    return -1;
  }
  close(fd);

  sprintf(buf, "/sys/class/gpio/gpio%d/direction", num);

  fd = open(buf, O_WRONLY);
  if (fd < 0) {
    fprintf(stderr, "Unable to open GPIO %d: ", num);
    perror(NULL);
    return -1;
  }
  if (rw == 'w') {
    if (write(fd, "out", 3) < 3) {
      fprintf(stderr, "Unable to set GPIO %d to OUT: ", num);
      perror(NULL);
      close(fd);
      return -1;
    }
  } else {
    if (write(fd, "in", 2) < 2) {
      fprintf(stderr, "Unable to set GPIO %d to IN: ", num);
      perror(NULL);
      close(fd);
      return -1;
    }
  }
  
  sprintf(buf, "/sys/class/gpio/gpio%d/value", num);

  fd = open(buf, (rw == 'w') ? O_WRONLY : O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "Unable to open GPIO %d: ", num);
    perror(NULL);
    return -1;
  }

  return fd;
}


char rbuf[32];
bool
gpioRead(int fd)
{
  lseek(fd, 0, SEEK_SET);
  while (1) {
    int n = read(fd, rbuf, sizeof(rbuf));
    for (int i = 0; i < n; i++) {
      if (rbuf[i] == '1') return 1;
      if (rbuf[i] == '0') return 0;
    }
  }

  return 0;
}


//
// fd for the GPIO 'value' files
//
int CSn = 0;
int CLK = 0;
int DO  = 0;
int DI  = 0;

//
// Initialize the MCP3202 serial interface
//
void
initADC()
{
  write(CSn, "1", 1);
  write(CLK, "0", 1);
}


//
// Read a digital channel on a MCP3202 ADC
//
uint16_t readADC(int channel)
{
  // Prepare next conversion
  write(CLK, "1", 1);
  write(CLK, "0", 1);

  // Start bit
  write(CSn, "0", 1);
  write(DI,  "1", 1);
  write(CLK, "1", 1);
  write(CLK, "0", 1);

  // Single-Ended
  write(CLK, "1", 1);
  write(CLK, "0", 1);

  // Channel selection
  write(DI,  (channel) ? "1" : "0", 1);
  write(CLK, "1", 1);
  write(CLK, "0", 1);

  // MSBF
  write(DI,  "1", 1);
  write(CLK, "1", 1);
  write(CLK, "0", 1);

  // Null bit
  write(CLK, "1", 1);
  write(CLK, "0", 1);

  uint16_t dval = 0;
  for (int i = 0; i < 12; i++) {
    // Bn
    write(CLK, "1", 1);
    dval = (dval << 1) | gpioRead(DO);
    write(CLK, "0", 1);
  }

  // End of conversion
  write(CSn, "1", 1);

  return dval;
}


int
main(int argc, char* argv[])
{
  const char* fname = NULL;

  int optc;
  while ((optc = getopt(argc, argv, "D:hr:w:")) != -1) {
    switch (optc) {
    case 'D':
      gDebug = atoi(optarg);
      break;
      
    case 'h':
    case '?':
      fprintf(stderr, "Usage: %s [-D n] [-r fname | -w fname]\n", argv[0]);
      exit(1);
      
    case 'r':
      gIsRead = true;
      fname = optarg;
      break;
      
    case 'w':
      gIsRead = false;
      fname = optarg;
      break;
    }
  }

  if (fname != NULL) {
    gFp = fopen(fname, (gIsRead) ? "r" : "w");
    if (gFp == NULL) {
      fprintf(stderr, "ERROR: Cannot open \"%s\": ", fname);
      perror(NULL);
      return -1;
    }
  }
  
  //
  // Write our process ID in a file so we can be easily killed later
  //
  int pid = getpid();
  FILE *fp = fopen("CarCount.pid", "w");
  if (fp == NULL) {
    fprintf(stderr, "Cannot open \"CarCount.pid\" for writing: ");
    perror(0);
    exit(-1);
  }
  fprintf(fp, "%d\n", pid);
  fclose(fp);

  uint64_t       ms;
  uint32_t       chan0;
  uint32_t       chan1;
  
  if (gFp != NULL && gIsRead) {
    fscanf(gFp, "%x%x%llx", &chan0, &chan1, &ms);
    channelData[0].average = chan0;
    channelData[1].average = chan1;
    while (fscanf(gFp, "%x%x%llx", &chan0, &chan1, &ms) == 3) {
      if (ms < 0x10000000000) ms += 0x16100000000;
      analyzeSample(chan0, chan1, ms);
    }

    fclose(gFp);
    return 0;
  }
  
  CSn = gpioOpen(132, 'w');
  CLK = gpioOpen(134, 'w');
  DO  = gpioOpen(136, 'r');
  DI  = gpioOpen(138, 'w');
  if (CSn <= 0 || CLK <= 0 || DO <= 0 || DI <= 0) return -1;

  initADC();
  channelData[0].average = readADC(0);
  channelData[1].average = readADC(1);

  struct timeval tv;
  while (1) {
    chan0 = readADC(0);
    chan1 = readADC(1);

    gettimeofday(&tv, NULL);
    ms = (((uint64_t) tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
    
    analyzeSample(chan0, chan1, ms);
  }
  
  return 0;
}
