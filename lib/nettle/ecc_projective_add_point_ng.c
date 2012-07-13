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

/* experimantal point addition. broken right now. */

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
  /* The algorithm used is "add-2007-bl" method.
   * It costs 11M + 5S.
   * See http://hyperelliptic.org/EFD/g1p/auto-shortw-jacobian.html for
   * description and example.
   * The original version uses several vars:
   * Z1Z1, Z2Z2, U1, U2, S1, S2, H, I, J, r, V.
   * This version uses only:
   * Z1Z1, Z2Z2, S1, H, J, r, V.
   * The rest of the vars are not needed for final
   * computation, so we calculate them, but don't store.
   */
  mpz_t t0, t1, Z1Z1, Z2Z2, S1, H, J, r, V;
  int err;

  if (P == NULL || Q == NULL || R == NULL || modulus == NULL)
    return -1;

  if ((err = mp_init_multi (&Z1Z1, &Z2Z2, &S1, &H, &J, &r, &V, &t0, &t1, NULL)) != 0)
    {
      return err;
    }

  /* Check if P == Q and do doubling in that case 
   * If Q == -P then P+Q=point at infinity
   */
  if ((mpz_cmp (P->x, Q->x) == 0) &&
      (mpz_cmp (P->z, Q->z) == 0))
    {
      /* x and z coordinates match. Check if P->y = Q->y, or P->y = -Q->y
       */
      if (mpz_cmp (P->y, Q->y) == 0)
        {
          mp_clear_multi (&Z1Z1, &Z2Z2, &S1, &H, &J, &r, &V, &t0, &t1, NULL);
          return ecc_projective_dbl_point (P, R, a, modulus);
        }
      
      mpz_sub (t1, modulus, Q->y);
      if (mpz_cmp (P->y, t1) == 0)
        {
          mp_clear_multi (&Z1Z1, &Z2Z2, &S1, &H, &J, &r, &V, &t0, &t1, NULL);
          mpz_set_ui(R->x, 1);
          mpz_set_ui(R->y, 1);
          mpz_set_ui(R->z, 0);
          return 0;
        }
    }

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

  /* H = U2 - U1  = t1 - t0*/
  mpz_sub (H, t1, t0);
  if (mpz_cmp_ui (H, 0) < 0)
    {
      mpz_add (H, H, modulus);
    }
  
  /* t1 = 2H */
  mpz_add (t1, H, H);
  if (mpz_cmp (H, modulus) >= 0)
    {
      mpz_sub (H, H, modulus);
    }
  /* t1 = (2H)^2 */
  /* it is the original I */
  mpz_mul (t1, t0, t0);
  mpz_mod (t1, t1, modulus);
  
  /* J = H * I = H * t1*/ 
  mpz_mul (J, t1, H);
  mpz_mod (J, J, modulus);

  /* V = U1 * I = t0 * t1 */ 
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

  /* t0 = S2 - S1 = t0 - S1 */
  mpz_sub (t0, t0, S1);
  if (mpz_cmp_ui (t0, 0) < 0)
    {
      mpz_add (t0, t0, modulus);
    }
  /* r = 2(S2 - S1) */
  mpz_add (r, t0, t0);
  if (mpz_cmp (r, modulus) >= 0)
    {
      mpz_sub (r, r, modulus);
    }
  
  /* we've calculated all needed vars: 
   * Z1Z1, Z2Z2, S1, H, J, r, V
   * now, we will calculate the coordinates */

  /* t0 = (r)^2 */
  mpz_mul (t0, r, r);
  mpz_mod (t0, t0, modulus);
  /* t1 = 2V */
  mpz_add (t1, V, V);
  if (mpz_cmp (V, modulus) >= 0)
    {
      mpz_sub (V, V, modulus);
    }
  /* t0 = t0 - J */
  mpz_sub (t0, t0, J);
  if (mpz_cmp_ui (t0, 0) < 0)
    {
      mpz_add (t0, t0, modulus);
    }
  /* X = r^2 - J - 2V = t0 - t1 */
  mpz_sub (R->x, t0, t1);
  if (mpz_cmp_ui (R->x, 0) < 0)
    {
      mpz_add (R->x, R->x, modulus);
    }
  

  /* t0 = V - X */
  mpz_sub (t0, V, R->x);
  if (mpz_cmp_ui (t0, 0) < 0)
    {
      mpz_add (t0, t0, modulus);
    }
  /* t0 = r * t0 */ 
  mpz_mul (t0, r, t0);
  mpz_mod (t0, t0, modulus);
  /* t1 = S1 * J */ 
  mpz_mul (t1, S1, J);
  mpz_mod (t1, t1, modulus);
  /* t1 = 2t1 */
  mpz_add (t1, t1, t1);
  if (mpz_cmp (t1, modulus) >= 0)
    {
      mpz_sub (t1, t1, modulus);
    }
  /* Y = r*(V - X) - 2*S1*J = t0 - t1 */
  mpz_sub (R->y, t0, t1);
  if (mpz_cmp_ui (R->y, 0) < 0)
    {
      mpz_add (R->y, R->y, modulus);
    }


  /* t0 = Z1 + Z2 */
  mpz_add (t0, P->z, Q->z);
  if (mpz_cmp (t0, modulus) >= 0)
    {
      mpz_sub (t0, t0, modulus);
    }
  /* t0 = (t0)^2 */
  mpz_mul (t0, t0, t0);
  mpz_mod (t0, t0, modulus);
  /* t0 = t0 - Z1Z1 */
  mpz_sub (t0, t0, Z1Z1);
  if (mpz_cmp_ui (t0, 0) < 0)
    {
      mpz_add (t0, t0, modulus);
    }
  /* t0 = t0 - Z2Z2 */
  mpz_sub (t0, t0, Z2Z2);
  if (mpz_cmp_ui (t0, 0) < 0)
    {
      mpz_add (t0, t0, modulus);
    }
  /* Z = ((Z1 + Z2)^2 - Z1Z1 - Z2Z2)*H = t0*H */ 
  mpz_mul (R->z, t0, H);
  mpz_mod (R->z, R->z, modulus);

  err = 0;

  mp_clear_multi (&Z1Z1, &Z2Z2, &S1, &H, &J, &r, &V, &t0, &t1, NULL);
  return err;
}