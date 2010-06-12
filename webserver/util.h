/**
 * \file util.h
 * \brief Several utility functions
 */
#ifndef __UTIL__
#define __UTIL__

/**
 * Minimum function
 * \param a First value that might be the minimum
 * \param b Second value that might be the minimum
 * \returns The Minimum of \a a and \a b
 */
int min (int a, int b)
{
  return a<b?a:b;
}

#endif
