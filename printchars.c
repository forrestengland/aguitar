#include <stdio.h>

int main(int argc, char* argv[]) {

  for (char c = ' '; c <= '~'; c++) {
    printf("%c", c);
  }
  printf("\n");
  printf("count: %d\n", '~' - ' ');

  return 0;
}
