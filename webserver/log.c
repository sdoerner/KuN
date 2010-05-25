#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


void printTimeStamp(struct log * log)
{
  time_t rawtime;
  struct tm * timeinfo;
  char buffer [80];

  time ( &rawtime );
  timeinfo = localtime ( &rawtime );

  strftime (buffer,80,"[%d/%b/%Y %H:%M:%S] ",timeinfo);
  fputs (buffer, log->logFile);
}

/**
 * Initializes a log file for writing.
 * \param filename The name of the file to log to.
 * \returns A log pointer that serves as a reference to the log file.
 */
struct log * initLog(const char * filename)
{
  FILE * file = fopen(filename, "a");
  if (file == NULL)
    return NULL; /* errno is already set by fopen */
  struct log * log = malloc(sizeof(struct log));
  if (log == NULL)
  {
    errno = ENOMEM;
    return NULL;
  }
  memset(log, 0, sizeof(struct log));
  log->logFile = file;
  return log;
}

/**
 * Closes the corresponding log file.
 * \param log The log to close.
 * \returns 0 if the log was closed successfully, 1 otherwise and errno is set.
 */
int freeLog(struct log * log)
{
  if (log == NULL || log->logFile==NULL)
    return 0;
  int i = fclose(log->logFile);
  free(log);
  return (i==0?0:1);
}

/**
 * Logs the specified message in the given log file.
 * \param log The log file to use
 * \param formatString Format string for the logging message (as in printf)
 */
void doLog(struct log * log, const char * formatString, ...)
{
  printTimeStamp(log);
  /* print message */
  va_list argptr;
  va_start(argptr,formatString);
  vfprintf(log->logFile, formatString, argptr);
  va_end(argptr);
  /* append newline */
  fputc('\n', log->logFile);
  fflush(log->logFile);
}
