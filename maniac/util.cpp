#include "util.h"
#include <stdio.h>
#include <stdint.h>

void indent(int n) {
    for (int i=0; i<n; i++) {
        printf("  ");
    }
}
