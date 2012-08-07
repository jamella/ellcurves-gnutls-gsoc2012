/*
 * Copyright (C) 2011-2012 Free Software Foundation, Inc.
 *
 * This file is part of GNUTLS.
 *
 * The GNUTLS library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 */

/* needed for gnutls_* types */
#include <gnutls_int.h>
#include <algorithms.h>

#include "ecc.h"

/* size of sliding window, don't change this! */
#ifndef WINSIZE
    #define WINSIZE 4
#endif

/* length of one array of precomputed values
 * we have two such arrays for positive and negative multipliers */
#ifndef PRECOMPUTE_LENGTH
    #define PRECOMPUTE_LENGTH (1 << (WINSIZE - 1))
#endif

/* per-curve cache structure */
typedef struct {
    /* curve's id */
    gnutls_ecc_curve_t id;

    /** The prime that defines the field the curve is in */
    mpz_t modulus;

    /** The array of positive multipliers of G */
    ecc_point *pos[PRECOMPUTE_LENGTH];

    /** The array of negative multipliers of G */
    ecc_point *neg[PRECOMPUTE_LENGTH];
} gnutls_ecc_curve_cache_entry_t;

/* global cache */
static gnutls_ecc_curve_cache_entry_t* ecc_wmnaf_cache = NULL;

/* free single cache entry */
static void _ecc_wmnaf_cache_entry_free(gnutls_ecc_curve_cache_entry_t* p) {
    int i;

    mpz_clear(p->modulus);

    for(i = 0; i < PRECOMPUTE_LENGTH; ++i) {
        ecc_del_point(p->pos[i]);
        ecc_del_point(p->neg[i]);
    }
}

/* free array of cache entries */
static void _ecc_wmnaf_cache_array_free(gnutls_ecc_curve_cache_entry_t* p) {
    if (p) {
        for (; p->id; ++p) {
            _ecc_wmnaf_cache_entry_free(p);
        }

        free(p);
    }
}

/* free curves caches */
void ecc_wmnaf_cache_free(void) {
    _ecc_wmnaf_cache_array_free(ecc_wmnaf_cache);
}

/* initialize single cache entry */
static int _ecc_wmnaf_cache_entry_init(gnutls_ecc_curve_cache_entry_t* p, \
        gnutls_ecc_curve_t id) {
    int i, j, err;
    ecc_point* G;
    mpz_t a;

    const gnutls_ecc_curve_entry_st *st = NULL;

    if (p == NULL || id == 0)
        return -1;

    mpz_init(a);
    G = ecc_new_point();
    mpz_set_si(a, -3);

    st =  _gnutls_ecc_curve_get_params(id);
    if (st == NULL) {
        err = -1;
        goto done;
    }

    /* set id */
    p->id = id;

    /* set modulus*/
    mpz_init(p->modulus);
    mpz_set_str(p->modulus, st->prime, 16);

    /* get generator point*/
    mpz_set_str(G->x, st->Gx, 16);
    mpz_set_str(G->y, st->Gy, 16);
    mpz_set_ui (G->z, 1);        

    /* alloc ram for precomputed values */
    for (i = 0; i < PRECOMPUTE_LENGTH; ++i) {
        p->pos[i] = ecc_new_point();
        p->neg[i] = ecc_new_point();
        if (p->pos[i] == NULL || p->neg[i] == NULL) {
            for (j = 0; j < i; ++j) {
              ecc_del_point(p->pos[j]);
              ecc_del_point(p->neg[j]);
            }

            err = -1;
            goto done;
        }
    }

    /* fill in pos and neg arrays with precomputed values
     * pos holds kG for k ==  1, 3, 5, ..., (2^w - 1)
     * neg holds kG for k == -1,-3,-5, ...,-(2^w - 1)
     */

    /* pos[0] == 2G for a while, later it will be set to the expected 1G */
    if ((err = ecc_projective_dbl_point(G, p->pos[0], a, p->modulus)) != 0)
        goto done;

    /* pos[1] == 3G */
    if ((err = ecc_projective_add_point_ng(p->pos[0], G, p->pos[1], a, p->modulus)) != 0)
        goto done;

    /* fill in kG for k = 5, 7, ..., (2^w - 1) */
    for (j = 2; j < PRECOMPUTE_LENGTH; ++j) {
        if ((err = ecc_projective_add_point_ng(p->pos[j-1], p->pos[0], p->pos[j], a, p->modulus)) != 0)
           goto done;
    }

    /* set pos[0] == 1G as expected
     * after this step we don't need G at all */
    mpz_set (p->pos[0]->x, G->x);
    mpz_set (p->pos[0]->y, G->y);
    mpz_set (p->pos[0]->z, G->z);

    /* map to affine all elements in pos
     * this will allow to use ecc_projective_madd later
     * set neg[i] == -pos[i] */
    for (j = 0; j < PRECOMPUTE_LENGTH; ++j) {
        if ((err = ecc_map(p->pos[j], p->modulus)) != 0)
            goto done;

        if ((err = ecc_projective_negate_point(p->pos[j], p->neg[j], p->modulus)) != 0)
            goto done;
    }

    err = 0;
done:
    mpz_clear(a);
    ecc_del_point(G);

    if (err) {
        mpz_clear(p->modulus);
    }

    return err;
}

/* initialize array of cache entries */
static int _ecc_wmnaf_cache_array_init 
(gnutls_ecc_curve_cache_entry_t **cache) {
    int j, err;

    gnutls_ecc_curve_cache_entry_t* ret;

    const gnutls_ecc_curve_t *p;

    ret = (gnutls_ecc_curve_cache_entry_t*) \
          malloc(MAX_ALGOS*sizeof(gnutls_ecc_curve_cache_entry_t));
    if (ret == NULL)
        return -1;

    /* get supported curves' ids */
    p = gnutls_ecc_curve_list();

    for (j = 0; *p; ++p, ++j) {
        if ((err = _ecc_wmnaf_cache_entry_init(ret + *p - 1, *p)) != 0)
            goto done;
    }

    ret[++j].id = 0;

    err = 0;

    *cache = ret;
done:
    if (err) {
        int i;
        for(i = 0; i < j; ++i) {
            _ecc_wmnaf_cache_entry_free(ret + i);
        }

        free(ret);
        *cache = NULL;
    }
    return err;
}

/* initialize curves caches */
int ecc_wmnaf_cache_init(void) {
    return _ecc_wmnaf_cache_array_init(&ecc_wmnaf_cache);
}

/*
   Perform a point multiplication utilizing cache
   @param k    The scalar to multiply by
   @param R    [out] Destination for kG
   @param id   Curve's id
   @return CRYPT_OK on success
*/
int
ecc_mulmod_wmnaf_cached (mpz_t k, ecc_point * R, gnutls_ecc_curve_t id, mpz_t a, int map)
{
    int j, err;
    
    gnutls_ecc_curve_cache_entry_t * cache = NULL;
    signed char* wmnaf = NULL;
    size_t wmnaf_len;
    signed char digit;

    if (k == NULL || R == NULL || id == 0)
        return -1;

    /* calculate wMNAF */
    wmnaf = ecc_wMNAF(k, WINSIZE, &wmnaf_len);
    if (!wmnaf) {
        err = -2;
        goto done;
    }

    /* set R to neutral */
    mpz_set_ui(R->x, 1);
    mpz_set_ui(R->y, 1);
    mpz_set_ui(R->z, 0);

    /* do cache lookup */
    cache = ecc_wmnaf_cache + id - 1;

    /* perform ops */
    for (j = wmnaf_len - 1; j >= 0; --j) {
        if ((err = ecc_projective_dbl_point(R, R, a, cache->modulus)) != 0)
            goto done;

        digit = wmnaf[j];

        if (digit) {
            if (digit > 0) {
                if ((err = ecc_projective_madd(R, cache->pos[( digit / 2)], R, a, cache->modulus)) != 0)
                    goto done;
            } else {
                if ((err = ecc_projective_madd(R, cache->neg[(-digit / 2)], R, a, cache->modulus)) != 0)
                    goto done;
            }
        }
    }


    /* map R back from projective space */
    if (map) {
        err = ecc_map(R, cache->modulus);
    } else {
        err = 0;
    }
done:
    if (wmnaf) free(wmnaf);
    return err;
}
