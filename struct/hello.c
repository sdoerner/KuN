#include <stdio.h>
#include <string.h>

struct person {
  char name[20];
  int alter;
};

void printPerson(struct person * pPerson)
{
  printf("%s ist %d Jahre alt.\n",pPerson->name,pPerson->alter);
}

int main(int argc, char** argv)
{
  struct person theo = {"theo", 27};
  struct person * pTheo = &theo;
  pTheo->alter++;
// oder auch (*pTheo).alter++;
  printPerson(&theo);
  return 0;
}
