#include "pico/stdlib.h"
#include "dm441bscpi.h"

int main(void)
{
    stdio_init_all();

    while (true) {
        tight_loop_contents();
    }
}
