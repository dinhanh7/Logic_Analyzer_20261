#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "capture_buffer.h"
int main(void)
{
    uint8_t data[258], original[256];
    for (size_t n = 1; n <= 256; n++) {
        for (size_t i = 0; i < n; i++) original[i] = (uint8_t)(i ^ (i >> 3));
        for (size_t offset = 0; offset <= n; offset++) {
            data[0] = 0x91; data[n + 1] = 0x82;
            memcpy(data + 1, original, n);
            la_rotate_left(data + 1, n, offset);
            for (size_t i = 0; i < n; i++) assert(data[i + 1] == original[(i + offset) % n]);
            assert(data[0] == 0x91 && data[n + 1] == 0x82);
        }
    }
    uint32_t base = 123;
    assert(la_trigger_window(32768,32768,65536,262144,64,&base) && base==0);
    assert(la_trigger_window(229312,32768,65536,262144,64,&base) && base==196544);
    assert(!la_trigger_window(229313,32768,65536,262144,64,&base));
    assert(!la_trigger_window(32767,32768,65536,262144,64,&base));
    assert(la_trigger_window(0,0,65536,262144,64,&base) && base==0);
    assert(!la_trigger_window(100,100,100,262144,64,&base));
    assert(!la_trigger_window(100,0,262144,262144,64,&base));
    assert(!la_trigger_window(0,0,0,0,1,&base));
    la_rotate_left(NULL, 0, 123);
    puts("capture buffer: all lengths/rotations/bounds passed");
}
