#include <stdlib.h> 
#include <stdio.h> 
#include <time.h>
#include <gmp.h>

#include "ecc.c"
#include "my_ecc.h"

#define NUM_OF_TRIES 10

int main(void) {
    mpz_t k, a, modulus;
    ecc_point *G, *Rclas, *Rprecomp;
    int map = 1;

    int rand, i;

    clock_t start;
    double precomp_time, classic_time;

    char check;

    mpz_inits(k, a, modulus, NULL);

    G = ecc_new_point();
    Rclas = ecc_new_point();
    Rprecomp = ecc_new_point();
    
    GNUTLS_ECC_CURVE_LOOP (
        printf("Running test sequence for curve %s\n", p->name);

        mpz_set_str(G->x,    p->Gx,     16);
        mpz_set_str(G->y,    p->Gy,     16);
        mpz_set_ui (G->z,    1);

        mpz_set_str(modulus, p->prime,  16);
        mpz_set_si(a, -3);

        ecc_precomp_init(G, a, modulus);

        for (i = 0; i < NUM_OF_TRIES; ++i) {
            rand = random();
            printf("Testing for k = %i\n", rand);

            mpz_set_ui(k, rand);

            start = clock();
            ecc_mulmod(k, G, Rclas, a, modulus, map);
            classic_time = ((double) (clock() - start)) / CLOCKS_PER_SEC;

            start = clock();
            ecc_mulmod_precomp_wmnaf(k, Rprecomp, a, modulus, map);
            precomp_time = ((double) (clock() - start)) / CLOCKS_PER_SEC;

            check = (!mpz_cmp(Rprecomp->x, Rclas->x)) && (!mpz_cmp(Rprecomp->y, Rclas->y));

            printf("Classic time: %.15f; Precomp time: %.15f; Check: %i\n", classic_time, precomp_time, check);
        }

        ecc_precomp_free();

        printf("\n");

    );

    ecc_del_point(Rprecomp);
    ecc_del_point(Rclas);
    ecc_del_point(G);

    mpz_clears(k, a, modulus, NULL);

    return 0;
}