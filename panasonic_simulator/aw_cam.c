#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>

#define DEBUG 0

#define TALLY_RED 0x1
#define TALLY_GREEN 0x2

// @@@ CHANGE TO FLOATING POINT DATES!

int main(int argc, char *argv[]) {
  printf("\r\n");

  // Read current_speed here.
  int current_speed = 0, current_tally = 0, speed_change_position = 0;
  long double speed_change_date = 0;
  FILE *fp = fopen("/tmp/aw_cam.dat", "r");
  if (fp) {
    char line[100];
    if (fgets(line, 80, fp) != NULL) {
      current_tally = atoi(line);
    }
  }
  int last_tally = current_tally;

  char *qs = getenv("QUERY_STRING");
  if (!qs) {
    printf("Query string missing (not running from CGI)\n\r");

  // Set tally
  } else if (!strncmp(qs, "cmd=TLG:", 8)) {
    char *raw_tally = &(qs[8]);
    int enabled = atoi(raw_tally);
    current_tally = enabled ? (current_tally | TALLY_GREEN) : (current_tally & ~TALLY_GREEN);

    printf("TLG:%d\r\n", (current_tally & TALLY_GREEN) ? 1 : 0);
  } else if (!strncmp(qs, "cmd=TLR:", 8)) {
    char *raw_tally = &(qs[8]);
    int enabled = atoi(raw_tally);
    current_tally = enabled ? (current_tally | TALLY_RED) : (current_tally & ~TALLY_RED);

    printf("TLR:%d\r\n", (current_tally & TALLY_RED) ? 1 : 0);

  // Query tally
  } else if (!strcmp(qs, "cmd=QLG&res=1")) {
    printf("TLG:%d\r\n", (current_tally & TALLY_GREEN) ? 1 : 0);
  } else if (!strcmp(qs, "cmd=QLR&res=1")) {
    printf("TLR:%d\r\n", (current_tally & TALLY_RED) ? 1 : 0);
  } else {
    printf("Unknown command %s\n\r", qs);
  }

  // Write current_tally here.
  if (last_tally != current_tally) {
    FILE *fp = fopen("/tmp/aw_cam.dat", "w");
    fprintf(fp, "%d\n", current_tally);
    fclose(fp);
  }
}
