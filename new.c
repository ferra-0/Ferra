#include <stdio.h>
#include <ctype.h> // Required for toupper() and tolower()

int main() {
  char lower = 'a';
  char upper = 'Z';

  printf("Uppercase of %c is %c\n", lower, toupper(lower));
  printf("Lowercase of %c is %c\n", upper, tolower(upper));

  return 0;
}
