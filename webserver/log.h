/**
 * \file log.h
 * \brief A simple message logger.
 *
 * Logs given messages to a specified log file
 */

#ifndef __LOG__
#define __LOG__

#include <stdio.h>

/** \brief A structure for representing a log */
struct log
{
  /** \brief The log file handle */
  FILE * logFile;
};

struct log * initLog(const char * filename);

int freeLog(struct log *);

void doLog(struct log * log, const char * formatString, ...);

#endif
