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

#include "ecc.h"

/* We use two algorithms for different cases.
 * See http://hyperelliptic.org/EFD/g1p/auto-shortw-jacobian.html
 *
 * The algorithm used for general case is "add-1998-cmo-2"
 * It costs 12M + 4S.
 *
 * If Z2 = 1 we use "madd". It costs 8M + 3S.
 *
 * The original versions use a lot of vars:
 * Z1Z1, Z2Z2, U1, U2, S1, S2, H, I, HHH, r, V, etc.
 * We use only the needed minimum:
 * Z1Z1, Z2Z2, S1, H, HHH, r, V.
 * The rest of the vars are not needed for final
 * computation, so we calculate them, but don't store.
 * Follow the comments.
 */


#define __WITH_EXTENDED_CHECKS
/* Check if H == 0
 * In this case P + Q = neutral element.
 *
 * In most of the cases below when H == 0 then Z == 0
 * and the resulting point lies at infinity.
 *
 * And the only point on the curve with Z == 0 MUST be
 * the neutral point.
 *
 * Of course, if there wasn't a mistake somewhere before.
 * We will be gullible and won't do any checks and simply
 * return neutral point in that case.
 */


/*
   Add two ECC points
   @param P        The point to add
   @param Q        The point to add
   @param R        [out] The destination of the double
   @param a        Curve's a value
   @param modulus  The modulus of the field the ECC curve is in
   @return 0 on success
*/
int
ecc_projective_add_point_ng (ecc_point * P, ecc_point * Q, ecc_point * R,
                              mpz_t a, mpz_t modulus)
{
    mpz_t t0, t1, Z1Z1, Z2Z2, S1, H, HHH, r, V;
    int err;

    if (P == NULL || Q == NULL || R == NULL || modulus == NULL)
        return -1;

    /* check all special cases first */

    /* check for neutral points */
    if (!ecc_projective_isneutral(Q, modulus)) {
        /* P + Q = P + neutral = P */

        mpz_set (R->x, P->x);
        mpz_set (R->y, P->y);
        mpz_set (R->z, P->z);

        return 0;
    }

    if (!ecc_projective_isneutral(P, modulus)) {
        /* P + Q = neutral + Q = Q */

        mpz_set (R->x, Q->x);
        mpz_set (R->y, Q->y);
        mpz_set (R->z, Q->z);

        return 0;
    }

    if ((err = mp_init_multi (&Z1Z1, &Z2Z2, &S1, &H, &HHH, &r, &V, &t0, &t1, NULL)) != 0 )
        return err;

    /* Check if P == Q and do doubling in that case 
     * If Q == -P then P + Q = neutral element */
    if ((mpz_cmp (P->x, Q->x) == 0) &&
        (mpz_cmp (P->z, Q->z) == 0)) {

        /* x and z coordinates match.
         * Check if P->y = Q->y, or P->y = -Q->y */

        if (mpz_cmp (P->y, Q->y) == 0) {
            mp_clear_multi (&Z1Z1, &Z2Z2, &S1, &H, &HHH, &r, &V, &t0, &t1, NULL);
            return ecc_projective_dbl_point (P, R, a, modulus);
        }

        mpz_sub (t1, modulus, Q->y);
        if (mpz_cmp (P->y, t1) == 0) {
            mp_clear_multi (&Z1Z1, &Z2Z2, &S1, &H, &HHH, &r, &V, &t0, &t1, NULL);
            mpz_set_ui(R->x, 1);
            mpz_set_ui(R->y, 1);
            mpz_set_ui(R->z, 0);
            return 0;
        }
    }


    /* check if Z2 == 1 and do "madd" in that case */
    if ((mpz_cmp_ui(Q->z, 1) == 0)) {

        /* Z1Z1 = Z1 * Z1 */
        mpz_mul (Z1Z1, P->z, P->z);
        mpz_mod (Z1Z1, Z1Z1, modulus);

        /* t1 = X2 * Z1Z1 */
        /* it is the original U2 */
        mpz_mul (t1, Z1Z1, Q->x);
        mpz_mod (t1, t1, modulus);
        /* H = U2 - X1  = t1 - X1 */
        mpz_sub (H, t1, P->x);
#ifdef __WITH_EXTENDED_CHECKS
        err = mpz_cmp_ui (H, 0);
        if (err < 0) {
            mpz_add (H, H, modulus);
        } else if (!err) {
            mpz_set_ui (R->x, 1);
            mpz_set_ui (R->y, 1);
            mpz_set_ui (R->z, 0);
            mp_clear_multi (&Z1Z1, &Z2Z2, &S1, &H, &HHH, &r, &V, &t0, &t1, NULL);

            return 0;
        }
#else
        if (mpz_cmp_ui (H, 0) < 0)
            mpz_add (H, H, modulus);
#endif
        /* S1 = 2H */
        mpz_add (S1, H, H);
        if (mpz_cmp (S1, modulus) >= 0)
            mpz_sub (S1, S1, modulus);
        /* t1 = (2H)^2 */
        /* it is the original I */
        mpz_mul (t1, S1, S1);
        mpz_mod (t1, t1, modulus);

        /* HHH = H * I = H * t1 */
        /* it is the original J */
        mpz_mul (HHH, H, t1);
        mpz_mod (HHH, HHH, modulus);

        /* V = X1 * I = X1 * t1 */
        mpz_mul (V, P->x, t1);
        mpz_mod (V, V, modulus);

        /* t1 = Z1 * Z1Z1 */
        mpz_mul (t1, Z1Z1, P->z);
        mpz_mod (t1, t1, modulus);
        /* t0 = Y2 * Z1 * Z1Z1 */
        /* it is the original S2 */
        mpz_mul (t0, t1, Q->y);
        mpz_mod (t0, t0, modulus);

        /* t0 = S2 - Y1 = t0 - Y1 */
        mpz_sub (t0, t0, P->y);
        if (mpz_cmp_ui (t0, 0) < 0)
            mpz_add (t0, t0, modulus);
        /* r = 2*(S2 - Y1) */
        mpz_add (r, t0, t0);
        if (mpz_cmp (r, modulus) >= 0)
            mpz_sub (r, r, modulus);

        /* we've calculated all needed vars:
         * H, J, r, V
         * now, we will calculate the coordinates */

        /* t0 = (r)^2 */
        mpz_mul (t0, r, r);
        mpz_mod (t0, t0, modulus);
        /* t0 = t0 - HHH */
        mpz_sub (t0, t0, HHH);
        if (mpz_cmp_ui (t0, 0) < 0)
            mpz_add (t0, t0, modulus);
        /* t1 = 2V */
        mpz_add (t1, V, V);
        if (mpz_cmp (t1, modulus) >= 0)
            mpz_sub (t1, t1, modulus);
        /* X = r^2 - J - 2V = t0 - t1 */
        mpz_sub (R->x, t0, t1);
        if (mpz_cmp_ui (R->x, 0) < 0)
            mpz_add (R->x, R->x, modulus);


        /* t1 = V - X */
        mpz_sub (t1, V, R->x);
        if (mpz_cmp_ui (t1, 0) < 0)
            mpz_add (t1, t1, modulus);
        /* t0 = r * t1 */
        mpz_mul (t0, r, t1);
        mpz_mod (t0, t0, modulus);
        /* t1 = Y1 * HHH */
        mpz_mul (t1, P->y, HHH);
        mpz_mod (t1, t1, modulus);
        /* t1 = 2t1 */
        mpz_add (t1, t1, t1);
        if (mpz_cmp (t1, modulus) >= 0)
            mpz_sub (t1, t1, modulus);
        /* Y = r*(V - X) - 2*Y1*J = t0 - t1 */
        mpz_sub (R->y, t0, t1);
        if (mpz_cmp_ui (R->y, 0) < 0)
            mpz_add (R->y, R->y, modulus);


        /* Z = 2*Z1*H = Z1*S1 */
        mpz_mul (R->z, P->z, S1);
        mpz_mod (R->z, R->z, modulus);

        mp_clear_multi (&Z1Z1, &Z2Z2, &S1, &H, &HHH, &r, &V, &t0, &t1, NULL);

        return 0;
    }

    /* no special cases occured 
     * do general routine */

    /* Z1Z1 = Z1 * Z1 */
    mpz_mul (Z1Z1, P->z, P->z);
    mpz_mod (Z1Z1, Z1Z1, modulus);

    /* Z2Z2 = Z2 * Z2 */
    mpz_mul (Z2Z2, Q->z, Q->z);
    mpz_mod (Z2Z2, Z2Z2, modulus);

    /* t0 = X1 * Z2Z2 */
    /* it is the original U1 */
    mpz_mul (t0, Z2Z2, P->x);
    mpz_mod (t0, t0, modulus);

    /* t1 = X2 * Z1Z1 */
    /* it is the original U2 */
    mpz_mul (t1, Z1Z1, Q->x);
    mpz_mod (t1, t1, modulus);

    /* H = U2 - U1 = t1 - t0 */
    mpz_sub (H, t1, t0);
#ifdef __WITH_EXTENDED_CHECKS
    err = mpz_cmp_ui(H, 0);
    if (err < 0) {
        mpz_add (H, H, modulus);
    } else if (!err) {
        mpz_set_ui (R->x, 1);
        mpz_set_ui (R->y, 1);
        mpz_set_ui (R->z, 0);
        mp_clear_multi (&Z1Z1, &Z2Z2, &S1, &H, &HHH, &r, &V, &t0, &t1, NULL);

        return 0;
    }
#else
    if (mpz_cmp_ui (H, 0) < 0) {
        mpz_add (H, H, modulus);
    }
#endif
    /* t1 = H^2 */
    /* it is the original HH */
    mpz_mul (t1, H, H);
    mpz_mod (t1, t1, modulus);
    /* HHH = H * HH */
    mpz_mul (HHH, t1, H);
    mpz_mod (HHH, HHH, modulus);

    /* V = U1 * HH = t0 * t1 */
    mpz_mul (V, t1, t0);
    mpz_mod (V, V, modulus);

    /* t0 = Z2 * Z2Z2 */
    mpz_mul (t0, Z2Z2, Q->z);
    mpz_mod (t0, t0, modulus);
    /* S1 = Y1 * Z2 * Z2Z2 */
    mpz_mul (S1, t0, P->y);
    mpz_mod (S1, S1, modulus);

    /* t1 = Z1 * Z1Z1 */
    mpz_mul (t1, Z1Z1, P->z);
    mpz_mod (t1, t1, modulus);
    /* t0 = Y2 * Z1 * Z1Z1 */
    /* it is the original S2 */
    mpz_mul (t0, t1, Q->y);
    mpz_mod (t0, t0, modulus);

    /* r = S2 - S1 = t0 - S1 */
    mpz_sub (r, t0, S1);
    if (mpz_cmp_ui (r, 0) < 0)
        mpz_add (r, r, modulus);

    /* we've calculated all needed vars:
     * S1, H, HHH, r, V
     * now, we will calculate the coordinates */

    /* t0 = (r)^2 */
    mpz_mul (t0, r, r);
    mpz_mod (t0, t0, modulus);
    /* t0 = t0 - HHH */
    mpz_sub (t0, t0, HHH);
    if (mpz_cmp_ui (t0, 0) < 0)
        mpz_add (t0, t0, modulus);
    /* t1 = 2V */
    mpz_add (t1, V, V);
    if (mpz_cmp (t1, modulus) >= 0)
        mpz_sub (t1, t1, modulus);
    /* X = r^2 - HHH - 2V = t0 - t1 */
    mpz_sub (R->x, t0, t1);
    if (mpz_cmp_ui (R->x, 0) < 0)
        mpz_add (R->x, R->x, modulus);


    /* t1 = V - X */
    mpz_sub (t1, V, R->x);
    if (mpz_cmp_ui (t1, 0) < 0)
        mpz_add (t1, t1, modulus);
    /* t0 = r * t1 */
    mpz_mul (t0, r, t1);
    mpz_mod (t0, t0, modulus);
    /* t1 = S1 * HHH */
    mpz_mul (t1, S1, HHH);
    mpz_mod (t1, t1, modulus);
    /* Y = r*(V - X) - S1*HHH = t0 - t1 */
    mpz_sub (R->y, t0, t1);
    if (mpz_cmp_ui (R->y, 0) < 0)
        mpz_add (R->y, R->y, modulus);


    /* t1 = Z1 * Z2 */
    mpz_mul (t1, P->z, Q->z);
    mpz_mod (t1, t1, modulus);
    /* Z = Z1 * Z2 * H = t1 * H */
    mpz_mul (R->z, t1, H);
    mpz_mod (R->z, R->z, modulus);

    mp_clear_multi (&Z1Z1, &Z2Z2, &S1, &H, &HHH, &r, &V, &t0, &t1, NULL);

    return 0;
}

/*
   Add two ECC points, when it is known that Z2 == 1
   @param P        The point to add
   @param Q        The point with Z == 1 to add
   @param R        [out] The destination of the double
   @param a        Curve's a value
   @param modulus  The modulus of the field the ECC curve is in
   @return 0 on success
*/
int
ecc_projective_madd (ecc_point * P, ecc_point * Q, ecc_point * R,
                              mpz_t a, mpz_t modulus)
{
    mpz_t t0, t1, Z1Z1, S1, H, HHH, r, V;
    int err;

    if (P == NULL || Q == NULL || R == NULL || modulus == NULL)
        return -1;

    /* check all special cases first */

    /* check for neutral points */
    if (!ecc_projective_isneutral(Q, modulus)) {
        /* P + Q = P + neutral = P */

        mpz_set (R->x, P->x);
        mpz_set (R->y, P->y);
        mpz_set (R->z, P->z);

        return 0;
    }

    if (!ecc_projective_isneutral(P, modulus)) {
        /* P + Q = neutral + Q = Q */

        mpz_set (R->x, Q->x);
        mpz_set (R->y, Q->y);
        mpz_set (R->z, Q->z);

        return 0;
    }

    if ((err = mp_init_multi (&Z1Z1, &S1, &H, &HHH, &r, &V, &t0, &t1, NULL)) != 0 )
        return err;

    /* Check if P == Q and do doubling in that case 
     * If Q == -P then P + Q = neutral element */
    if ((mpz_cmp (P->x, Q->x) == 0) &&
        (mpz_cmp (P->z, Q->z) == 0)) {

        /* x and z coordinates match.
         * Check if P->y = Q->y, or P->y = -Q->y */

        if (mpz_cmp (P->y, Q->y) == 0) {
            mp_clear_multi (&Z1Z1, &S1, &H, &HHH, &r, &V, &t0, &t1, NULL);
            return ecc_projective_dbl_point (P, R, a, modulus);
        }

        mpz_sub (t1, modulus, Q->y);
        if (mpz_cmp (P->y, t1) == 0) {
            mp_clear_multi (&Z1Z1, &S1, &H, &HHH, &r, &V, &t0, &t1, NULL);
            mpz_set_ui(R->x, 1);
            mpz_set_ui(R->y, 1);
            mpz_set_ui(R->z, 0);
            return 0;
        }
    }


    /* Z1Z1 = Z1 * Z1 */
    mpz_mul (Z1Z1, P->z, P->z);
    mpz_mod (Z1Z1, Z1Z1, modulus);

    /* t1 = X2 * Z1Z1 */
    /* it is the original U2 */
    mpz_mul (t1, Z1Z1, Q->x);
    mpz_mod (t1, t1, modulus);
    /* H = U2 - X1  = t1 - X1 */
    mpz_sub (H, t1, P->x);
#ifdef __WITH_EXTENDED_CHECKS
    err = mpz_cmp_ui (H, 0);
    if (err < 0) {
        mpz_add (H, H, modulus);
    } else if (!err) {
        mpz_set_ui (R->x, 1);
        mpz_set_ui (R->y, 1);
        mpz_set_ui (R->z, 0);
        mp_clear_multi (&Z1Z1, &S1, &H, &HHH, &r, &V, &t0, &t1, NULL);

        return 0;
    }
#else
    if (mpz_cmp_ui (H, 0) < 0)
        mpz_add (H, H, modulus);
#endif
    /* S1 = 2H */
    mpz_add (S1, H, H);
    if (mpz_cmp (S1, modulus) >= 0)
        mpz_sub (S1, S1, modulus);
    /* t1 = (2H)^2 */
    /* it is the original I */
    mpz_mul (t1, S1, S1);
    mpz_mod (t1, t1, modulus);

    /* HHH = H * I = H * t1 */
    /* it is the original J */
    mpz_mul (HHH, H, t1);
    mpz_mod (HHH, HHH, modulus);

    /* V = X1 * I = X1 * t1 */
    mpz_mul (V, P->x, t1);
    mpz_mod (V, V, modulus);

    /* t1 = Z1 * Z1Z1 */
    mpz_mul (t1, Z1Z1, P->z);
    mpz_mod (t1, t1, modulus);
    /* t0 = Y2 * Z1 * Z1Z1 */
    /* it is the original S2 */
    mpz_mul (t0, t1, Q->y);
    mpz_mod (t0, t0, modulus);

    /* t0 = S2 - Y1 = t0 - Y1 */
    mpz_sub (t0, t0, P->y);
    if (mpz_cmp_ui (t0, 0) < 0)
        mpz_add (t0, t0, modulus);
    /* r = 2*(S2 - Y1) */
    mpz_add (r, t0, t0);
    if (mpz_cmp (r, modulus) >= 0)
        mpz_sub (r, r, modulus);

    /* we've calculated all needed vars:
     * H, J, r, V
     * now, we will calculate the coordinates */

    /* t0 = (r)^2 */
    mpz_mul (t0, r, r);
    mpz_mod (t0, t0, modulus);
    /* t0 = t0 - HHH */
    mpz_sub (t0, t0, HHH);
    if (mpz_cmp_ui (t0, 0) < 0)
        mpz_add (t0, t0, modulus);
    /* t1 = 2V */
    mpz_add (t1, V, V);
    if (mpz_cmp (t1, modulus) >= 0)
        mpz_sub (t1, t1, modulus);
    /* X = r^2 - J - 2V = t0 - t1 */
    mpz_sub (R->x, t0, t1);
    if (mpz_cmp_ui (R->x, 0) < 0)
        mpz_add (R->x, R->x, modulus);


    /* t1 = V - X */
    mpz_sub (t1, V, R->x);
    if (mpz_cmp_ui (t1, 0) < 0)
        mpz_add (t1, t1, modulus);
    /* t0 = r * t1 */
    mpz_mul (t0, r, t1);
    mpz_mod (t0, t0, modulus);
    /* t1 = Y1 * HHH */
    mpz_mul (t1, P->y, HHH);
    mpz_mod (t1, t1, modulus);
    /* t1 = 2t1 */
    mpz_add (t1, t1, t1);
    if (mpz_cmp (t1, modulus) >= 0)
        mpz_sub (t1, t1, modulus);
    /* Y = r*(V - X) - 2*Y1*J = t0 - t1 */
    mpz_sub (R->y, t0, t1);
    if (mpz_cmp_ui (R->y, 0) < 0)
        mpz_add (R->y, R->y, modulus);


    /* Z = 2*Z1*H = Z1*S1 */
    mpz_mul (R->z, P->z, S1);
    mpz_mod (R->z, R->z, modulus);

    mp_clear_multi (&Z1Z1, &S1, &H, &HHH, &r, &V, &t0, &t1, NULL);

    return 0;
}
