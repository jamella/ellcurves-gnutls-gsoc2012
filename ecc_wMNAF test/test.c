#include <stdlib.h>
#include <stdio.h>
#include <gmp.h>

#define MAX_NUM 128

int main(void) {

    mpz_t x;
    signed char* wmnaf = NULL;
    size_t wmnaf_len;

    int i, j;

    mpz_init(x);

    printf("\nRunning tests for ecc_wMNAF()\n\n");

    for (i = -MAX_NUM; i <= MAX_NUM; ++i) {

        mpz_set_si(x, i);

        wmnaf = ecc_wMNAF(x, &wmnaf_len);

        printf("\"%5i\" has wMNAF repr. = [", i);
        for (j = wmnaf_len - 1; j > 0; --j) {
            printf(" %i,", wmnaf[j]);
        }
        printf(" %i ]\n", wmnaf[0]);

        free(wmnaf);
    }

    mpz_clear(x);

    return 0;
}
