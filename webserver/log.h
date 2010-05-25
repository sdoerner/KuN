/**
 * \brief A simple message logger.
 *
 * Logs given messages to a specified log file
 */

#ifndef __LOG__
#define __LOG__

#include <stdio.h>

struct log
{
  FILE * logFile;
};

struct log * initLog(const char * filename);

int freeLog(struct log *);

void doLog(struct log * log, const char * formatString, ...);

#endif
