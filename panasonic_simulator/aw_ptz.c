#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>

#define DEBUG 0

#define SPEED_BIAS 50

double timeInMicroseconds(void);
int speed_multiplier(int speed);

// @@@ CHANGE TO FLOATING POINT DATES!

int main(int argc, char *argv[]) {
  printf("\r\n");

  // Read current_speed here.
  int current_speed = 0, speed_change_position = 0;
  long double speed_change_date = 0;
  FILE *fp = fopen("/tmp/aw_ptz.dat", "r");
  if (fp) {
    char line[100];
    if (fgets(line, 80, fp) != NULL) {
      current_speed = atoi(line);
    }
    if (fgets(line, 80, fp) != NULL) {
      speed_change_date = strtold(line, NULL);
    }
    if (fgets(line, 80, fp) != NULL) {
      speed_change_position = atoi(line);
    }
  }
  int last_speed = current_speed, last_speed_change_position = speed_change_position;
  long double last_speed_change_date = speed_change_date;

  long double zoom_duration = timeInMicroseconds() - speed_change_date;
  int zoom_position = (zoom_duration * speed_multiplier(current_speed)) + speed_change_position;
  zoom_position = (zoom_position < 0) ? 0 : (zoom_position > 4720) ? 4720 : zoom_position;

#if DEBUG
  printf("POINT: %Lf\n", (zoom_duration * speed_multiplier(current_speed)) + speed_change_position);
  printf("MIN: %Lf\n", MIN((zoom_duration * speed_multiplier(current_speed)) + speed_change_position, 4720));
  printf("current_speed: %d\nspeed_change_date: %Lf\nspeed_change_position: %d\nzoom_duration: %Lf\n zoom_position : %d\n"
         "speed_multiplier: %d\n",
         current_speed, speed_change_date, speed_change_position, zoom_duration, zoom_position, speed_multiplier(current_speed));
#endif

  char *qs = getenv("QUERY_STRING");
  if (!qs) {
    printf("Query string missing (not running from CGI)\n\r");
  // Zoom
  } else if (!strcmp(qs, "cmd=%23GZ&res=1")) { // #GZ
    printf("gz%d\r\n", zoom_position);
  } else if (!strncmp(qs, "cmd=%23Z", 8)) { // #Z
    char *raw_speed = &(qs[8]);
    int positive_biased_speed = atoi(raw_speed);
    current_speed = positive_biased_speed - SPEED_BIAS;
    printf("zS%d\r\n", positive_biased_speed);
    speed_change_date = timeInMicroseconds();
    speed_change_position = zoom_position;
  } else {
    printf("Unknown command %s\n\r", qs);
  }

  // Write current_speed here.
  if (last_speed != current_speed || last_speed_change_date != speed_change_date ||
      last_speed_change_position != speed_change_position) {
    FILE *fp = fopen("/tmp/aw_ptz.dat", "w");
    fprintf(fp, "%d\n%Lf\n%d\n", current_speed, speed_change_date, speed_change_position);
    fclose(fp);
  }
}

// Rough approximation.
int speed_multiplier(int speed) {
  return (1000 * speed) / 49;
}

double timeInMicroseconds(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);
}
