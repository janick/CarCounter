//
// Copyright 2018 Janick Bergeron <janick@bergeron.com>
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

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


unsigned int gDebug = 0;

typedef struct event_s {
  time_t stamp;
  double speed;
  bool   isUp;
} event_t;

time_t gStartOfDay;

unsigned int dailyCount[24 * 4];

bool
analyzeEvent(event_t ev)
{
  // Gather metrics in 15mins intervals
  unsigned int interval = (ev.stamp - gStartOfDay) / (15 * 60);
  dailyCount[interval]++;

  if (gDebug > 1) {
    struct tm *lt = localtime(&ev.stamp);
    printf("CAR: %4d/%02d/%02d %02d:%02d:%02d %d\n", 
	   lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec,
	   dailyCount[interval]);
  }
  
  return true;
}


bool
reportDay()
{
  // Collapse 00:00-05:59 into a single bing
  unsigned early = 0;
  for (int i = 0; i < 6*4; i++) {
    early += dailyCount[i];
  }
  printf("%d ", early);
  for (int i = 6*4; i < 24*4; i++) {
    if (i > 0 && i % 4 == 0) printf("[%02d:00] ", i / 4);
    printf("%d ", dailyCount[i]);
  }
  printf("\n");

  return true;
}


bool
analyzeFile(const char* fname)
{
  FILE *fp = fopen(fname, "r");
  if (fp == NULL) {
    fprintf(stderr, "ERROR: Cannot open \"%s\" for reading: %s\n", fname, strerror(errno));
    return false;
  }

  char *line = NULL;
  size_t lineLen = 0;

  event_t prevEv = {0, 0, false};

  ssize_t len;
  len = getline(&line, &lineLen, fp);
  if (len < 0) return false;
  prevEv = {atol(line), atof(line+34), line[45] == 'U'};

  struct tm *lt = localtime(&prevEv.stamp);
  lt->tm_hour = 0;
  lt->tm_min  = 0;
  lt->tm_sec  = 0;
  gStartOfDay = mktime(lt);

  bzero(dailyCount, sizeof(dailyCount));

  if (gDebug > 1) fputs(line, stdout);
    
  while ((len = getline(&line, &lineLen, fp)) > 0) {

    event_t ev = {atol(line), atof(line+34), line[45] == 'U'};

    // Two events, seperated by 3 sec or less are the same car
    if ((prevEv.stamp <= ev.stamp && ev.stamp <= prevEv.stamp+3)
	&& prevEv.isUp == ev.isUp) {
      // Average the speed
      ev.speed = (ev.speed + prevEv.speed) / 2;
      ev.stamp = prevEv.stamp;

      if (gDebug > 1) fputs(line, stdout);

      analyzeEvent(ev);
      prevEv.stamp = 0;
    } else {
      if (prevEv.stamp > 0) analyzeEvent(prevEv);
      prevEv = ev;

      if (gDebug > 1) fputs(line, stdout);
    }

  }

  // Don't forget the last event of the day!
  if (prevEv.stamp > 0) analyzeEvent(prevEv);

  reportDay();
  
  fclose(fp);

  return true;
}


void
usage(const char* cmd)
{
  fprintf(stderr, "Usage: %s [-D n] {fname}\n", cmd);
  exit(-1);
}


int
main(int argc, char* argv[])
{
  int optc;
  while ((optc = getopt(argc, argv, "D:h")) != -1) {
    switch (optc) {
    case 'D':
      gDebug = atoi(optarg);
      break;
      
    case 'h':
    case '?':
      usage(argv[0]);

    }
  }

  if (optind == argc) usage(argv[0]);

  while (optind < argc) {
    if (!analyzeFile(argv[optind++])) return -1;
  }
  
  return 0;
}
