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

const char* weekDay[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};


unsigned int  gDebug = 0;
FILE         *gPlot  = NULL;
typedef struct event_s {
  time_t stamp;
  double speed;
  bool   isUp;
} event_t;

time_t gStartOfDay;


typedef struct bins_s {
  unsigned int up;
  unsigned int dn;

  bins_s()
    : up(0)
    , dn(0)
  {}
} bins_t;

bins_t dailyCount[24 * 4];

struct speedBins_s {
  struct avg_s {
    double sum;
    double max;
  } up;
  struct avg_s dn;
} speeds;
  
bool
analyzeEvent(event_t ev)
{
  // Gather metrics in 15mins intervals
  unsigned int interval = (ev.stamp - gStartOfDay) / (15 * 60);
  if (ev.isUp) {
    dailyCount[interval].up++;
    // A speed below 5 MPH or above 30 MPH is probably bogus
    if (5.0 < ev.speed && ev.speed < 30.0) {
      speeds.up.sum += ev.speed;
      if (speeds.up.max < ev.speed) speeds.up.max = ev.speed;
    }
  } else {
    dailyCount[interval].dn++;
    // A speed below 5 MPH or above 30 MPH is probably bogus
    if (5.0 < ev.speed && ev.speed < 30.0) {
      speeds.dn.sum += ev.speed;
      if (speeds.dn.max < ev.speed) speeds.dn.max = ev.speed;
    }
  }

  if (gDebug > 1) {
    struct tm *lt = localtime(&ev.stamp);
    printf("CAR: %4d/%02d/%02d %02d:%02d:%02d %.1f MPH +%d/-%d\n", 
	   lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec,
	   ev.speed, dailyCount[interval].up, dailyCount[interval].dn);
  }

  return true;
}


bool
reportDay()
{
  bins_t total;

  // Collapse 00:00-05:59 into a single bin
  bins_t early;
  for (int i = 0; i < 6*4; i++) {
    early.up += dailyCount[i].up;
    early.dn += dailyCount[i].dn;
  }
  total = early;

  // Collapse 22:00-23:59 into a single bin
  bins_t late;
  for (int i = 22*4; i < 24*4; i++) {
    late.up += dailyCount[i].up;
    late.dn += dailyCount[i].dn;
  }
  total = early;

  printf("Up: %2d ", early.up);
  for (int i = 6*4; i < 22*4; i++) {
    if (i == 14*4) printf("\n       ");
    if (i > 0 && i % 4 == 0) printf("[%02d:00] ", i / 4);
    printf("%2d ", dailyCount[i].up);
    total.up += dailyCount[i].up;
  }
  printf("[22:00] %2d : %3d", late.up, total.up);
  printf(" %2.0f/%2.0f MPH\n", speeds.up.sum/total.up, speeds.up.max);
  
  printf("Dn: %2d ", early.dn);
  for (int i = 6*4; i < 22*4; i++) {
    if (i == 14*4) printf("\n       ");
    if (i > 0 && i % 4 == 0) printf("[%02d:00] ", i / 4);
    printf("%2d ", dailyCount[i].dn);
    total.dn += dailyCount[i].dn;
  }
  printf("[22:00] %2d : %3d", late.dn, total.dn);
  printf(" %2.0f/%2.0f MPH\n", speeds.dn.sum/total.dn, speeds.dn.max);
  
  printf("    %2d ", early.up + early.dn);
  for (int i = 6*4; i < 22*4; i++) {
    if (i == 14*4) printf("\n       ");
    if (i > 0 && i % 4 == 0) printf("[%02d:00] ", i / 4);
    printf("%2d ", dailyCount[i].up+dailyCount[i].dn);
  }
  printf("[22:00] %2d : %3d", late.up + late.dn, total.up + total.dn);
  printf(" %2.0f/%2.0f MPH\n", (speeds.up.sum + speeds.dn.sum)/(total.up + total.dn),
	 (speeds.up.max > speeds.dn.max) ? speeds.up.max : speeds.dn.max);

  return true;
}


bool
gnuplotDay(FILE *fp, const char *date, const char *wday)
{
  char fname[64];
  sprintf(fname, "data.%s.dat", date);
  FILE *data = fopen(fname, "w");
  int j = 0;
  bins_t total;
  for (int i = 0; i < 24*4; i++) {
    // Only plot from 6:00 to 22:00
    if (6*4 <= i && i < 22*4) {
      fprintf(data, "%02d:%02d %d -%d", 6 + (j/4),  15 * (j % 4), dailyCount[i].up, dailyCount[i].dn);
      if (j%4 == 0) fprintf(data, " 0");
      fprintf(data, "\n");
      j++;
    }

    total.up += dailyCount[i].up;
    total.dn += dailyCount[i].dn;
  }

  fclose(data);

  // Skip empty files
  if (total.up + total.dn < 20) return false;

  fprintf(fp, "set title '%s %s' offset 0,-7\n", wday, date);
  fprintf(fp, "set key center right\n");
  fprintf(fp, "set style data histograms\n");
  fprintf(fp, "set style histogram rowstacked\n");
  fprintf(fp, "set boxwidth 1 relative\n");
  fprintf(fp, "set style fill solid 1.0 border -1\n");
  fprintf(fp, "set yrange [-20:80]\n");
  fprintf(fp, "set datafile separator \" \"\n");
  fprintf(fp, "set xtics auto\n");
  fprintf(fp, "set ytics (-20,0,20,40,60)\n");
  fprintf(fp, "plot '%s' using 4:xtic(1) notitle, '' using 2 title '%d   Uphill', '' using 3 title '%d Downhill'\n", fname, total.up, total.dn);

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

  bzero(dailyCount, sizeof(dailyCount));
  bzero(&speeds, sizeof(speeds));

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

  printf("%s %s\n", fname+13, weekDay[lt->tm_wday]);

  if (gDebug > 1) fputs(line, stdout);
    
  while ((len = getline(&line, &lineLen, fp)) > 0) {

    event_t ev = {atol(line), atof(line+34), line[45] == 'U'};

    // Two events, seperated by 3 sec or less are the same car
    if ((prevEv.stamp <= ev.stamp && ev.stamp <= prevEv.stamp+3)
	&& prevEv.isUp == ev.isUp) {
      // If the two speeds are too far apart, there's been a glitch
      double diff = ev.speed - prevEv.speed;
      if (-5.0 < diff && diff < 5.0) {
	// Average the speed
	ev.speed = (ev.speed + prevEv.speed) / 2;
	ev.stamp = prevEv.stamp;
      } else {
	// Ignore the speed
	ev.speed = 0.0;
      }

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
  if (gPlot != NULL) gnuplotDay(gPlot, fname+13, weekDay[lt->tm_wday]);
  
  fclose(fp);

  return true;
}


void
usage(const char* cmd)
{
  fprintf(stderr, "Usage: %s [-D n] [-P] {fname}\n", cmd);
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr, "    -P           Plot analysis\n");
  exit(-1);
}


int
main(int argc, char* argv[])
{
  int optc;
  while ((optc = getopt(argc, argv, "D:hP")) != -1) {
    switch (optc) {
    case 'D':
      gDebug = atoi(optarg);
      break;
      
    case 'h':
    case '?':
      usage(argv[0]);

    case 'P':
      gPlot = popen("gnuplot > gnuplot.jpg", "w");
      if (gPlot == NULL) {
	fprintf(stderr, "ERROR: Cannot open gnuplot: %s\n", strerror(errno));
	exit(-1);
      }
      break;
    }
  }

  if (optind == argc) usage(argv[0]);

  if (gPlot != NULL) {
    unsigned int nPlots = argc - optind;

    fprintf(gPlot, "set terminal jpeg small size 1000,%d\n", nPlots * 200);
    fprintf(gPlot, "set tmargin 0\n");
    fprintf(gPlot, "set bmargin 0\n");
    fprintf(gPlot, "set multiplot layout %d,1\n", nPlots);
  }
  
  while (optind < argc) {
    if (!analyzeFile(argv[optind++])) return -1;
  }

  if (gPlot != NULL) {
    //    fprintf(gPlot, "unset multiplot\n");
    fprintf(gPlot, "pause mouse\n");
    pclose(gPlot);
  }
  
  return 0;
}
