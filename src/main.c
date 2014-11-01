
#include <stdio.h>

extern void execute_command (int argc, char *argv[]);

void main (int argc, char *argv[]) {
    execute_command(argc - 1, argv + 1);
}
