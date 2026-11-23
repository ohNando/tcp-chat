#include "utility.h"

#include <stdarg.h>
#include <time.h>

void printToConsole(const char* szMsg, int showPrompt){
    printf("\r\033[K"); //For clearing the current line

    if(szMsg) printf("%s", szMsg);
    if(showPrompt) printf("%s",PROMPT);

    fflush(stdout);
}

void log_info(const char* fmt, ...){
    char stamp[16];
    time_t now = time(NULL);
    struct tm tm;
    va_list ap;

    localtime_r(&now, &tm);
    strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm);

    printf("[%s] ", stamp);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}
