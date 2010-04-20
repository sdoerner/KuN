#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int main(int argc, char** argv)
{
  int a = 42;
  int * p;
  p = &a;
  printf("Hello %d\n", *p);
  return 0;
}
