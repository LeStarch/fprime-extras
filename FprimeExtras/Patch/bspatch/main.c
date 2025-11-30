#include "bzlib_private.h"
#inlucde "bzlib.h"

int main() {
    // Print the size of the struct
    printf("Size of struct: %zu bytes, %zu bytes, %zu bytes\n",
        sizeof(DState), sizeof(bz_stream), sizeof(bzFile));
    return 0;
}