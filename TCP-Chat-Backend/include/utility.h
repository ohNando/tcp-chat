#ifndef UTILITY_H
#define UTILITY_H

#include "common.h"

//For both client and server

void error(const char*);
void printToConsole(const char*,int);

/* printf-style log line with a HH:MM:SS timestamp, to stdout. */
void log_info(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#endif