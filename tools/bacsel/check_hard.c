/* check_hard.c -- verify BaCSeL hard-case lists for fixed's Q48.16 functions.
 *
 * This is a development tool, not part of the library and not built by CMake.
 * For every `<raw> <e> <distance>` line it recomputes f(raw / 2^16) at 256
 * bits and the distance to the nearest rounding midpoint in units of 2^-16,
 * which is what a Q48.16 round-to-nearest must resolve.
 *
 * Usage:
 *   check_hard [--check] <fn> <list>
 *   check_hard --convert <fn> <e> <k>       (BaCSeL output on stdin)
 *   check_hard --getexp  <fn> <raw>
 *   check_hard --window  <fn> <e> <log2w>
 *
 * The lists carry `# k = e + N` or `# k = N` in their header; --check uses
 * that to enforce distance <= 2^-k and verifies that every required binade
 * (-16..46 for ln, log2, log10, rsqrt; 16..46 for sin, cos, tan) holds at
 * least 8 cases (or all 2^(e+16) cases when e < -13).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpfr.h>

#define PREC 256

static const char *
canon_fn (const char *fn)
{
  if (strcmp (fn, "sin_big") == 0) return "sin";
  if (strcmp (fn, "cos_big") == 0) return "cos";
  if (strcmp (fn, "tan_big") == 0) return "tan";
  return fn;
}

static int
is_trig_fn (const char *fn)
{
  fn = canon_fn (fn);
  return strcmp (fn, "sin") == 0 || strcmp (fn, "cos") == 0 || strcmp (fn, "tan") == 0;
}

static int
apply_fn (mpfr_t y, const char *fn, mpfr_t x)
{
  fn = canon_fn (fn);
  if (strcmp (fn, "ln") == 0) return mpfr_log (y, x, MPFR_RNDN);
  if (strcmp (fn, "log2") == 0) return mpfr_log2 (y, x, MPFR_RNDN);
  if (strcmp (fn, "log10") == 0) return mpfr_log10 (y, x, MPFR_RNDN);
  if (strcmp (fn, "sin") == 0) return mpfr_sin (y, x, MPFR_RNDN);
  if (strcmp (fn, "cos") == 0) return mpfr_cos (y, x, MPFR_RNDN);
  if (strcmp (fn, "tan") == 0) return mpfr_tan (y, x, MPFR_RNDN);
  if (strcmp (fn, "rsqrt") == 0) return mpfr_rec_sqrt (y, x, MPFR_RNDN);
  fprintf (stderr, "check_hard: unknown function '%s'\n", fn);
  exit (2);
}

static void
eval_scaled (mpfr_t u, const char *fn, unsigned long long raw)
{
  mpfr_t x, y;
  mpz_t zr;
  mpfr_inits2 (PREC, x, y, (mpfr_ptr) 0);
  mpz_init_set_ui (zr, raw);
  mpfr_set_z (x, zr, MPFR_RNDN);
  mpz_clear (zr);
  mpfr_div_2ui (x, x, 16, MPFR_RNDN);
  apply_fn (y, fn, x);
  mpfr_mul_2ui (u, y, 16, MPFR_RNDN);
  mpfr_clears (x, y, (mpfr_ptr) 0);
}

/* distance to the nearest midpoint of the 2^-16 grid, in units of 2^-16 */
static void
distance_units (mpfr_t d, const char *fn, unsigned long long raw)
{
  mpfr_t u, f;
  mpfr_inits2 (PREC, u, f, (mpfr_ptr) 0);
  eval_scaled (u, fn, raw);
  mpfr_floor (f, u);
  mpfr_sub (u, u, f, MPFR_RNDN);
  mpfr_set_d (f, 0.5, MPFR_RNDN);
  mpfr_sub (d, u, f, MPFR_RNDN);
  mpfr_abs (d, d, MPFR_RNDN);
  mpfr_clears (u, f, (mpfr_ptr) 0);
}

static long
raw_exp (const char *fn, unsigned long long raw, int *sgn_out)
{
  mpfr_t x, y;
  mpz_t zr;
  long ex;
  mpfr_inits2 (PREC, x, y, (mpfr_ptr) 0);
  mpz_init_set_ui (zr, raw);
  mpfr_set_z (x, zr, MPFR_RNDN);
  mpz_clear (zr);
  mpfr_div_2ui (x, x, 16, MPFR_RNDN);
  apply_fn (y, fn, x);
  if (mpfr_zero_p (y))
    {
      if (sgn_out) *sgn_out = 0;
      ex = 0;
    }
  else
    {
      if (sgn_out) *sgn_out = mpfr_sgn (y);
      ex = (long) mpfr_get_exp (y);
    }
  mpfr_clears (x, y, (mpfr_ptr) 0);
  return ex;
}

static long k_map_e[256], k_map_k[256];
static int k_map_n;

static int
set_k_from_header (FILE *fp, long *kbase)
{
  char line[4096];
  k_map_n = 0;
  *kbase = -1000000;
  while (fgets (line, sizeof line, fp))
    {
      if (line[0] != '#')
        {
          fseek (fp, -(long) strlen (line), SEEK_CUR);
          return 0;
        }
      if (strstr (line, "k per binade:") != NULL)
        {
          char *p = strstr (line, "k per binade:") + 13;
          long e, k;
          while (sscanf (p, " %ld:%ld", &e, &k) == 2 && k_map_n < 256)
            {
              k_map_e[k_map_n] = e;
              k_map_k[k_map_n] = k;
              k_map_n++;
              p = strchr (p, ':') + 1;
              if (*p == 0) break;
              while (*p == ' ') p++;
              while (*p && *p != ' ' && *p != '\n') p++;
            }
          continue;
        }
      if (strstr (line, "k = e + ") != NULL)
        *kbase = atol (strstr (line, "k = e + ") + 8);
      else if (strstr (line, "k = e+") != NULL)
        *kbase = atol (strstr (line, "k = e+") + 6);
      else if (strstr (line, "k = ") != NULL && strstr (line, "k = e"))
        *kbase = -2000000 - atol (strstr (line, "k = ") + 4);
      else if (strstr (line, "k = ") != NULL)
        *kbase = -2000000 - atol (strstr (line, "k = ") + 4);
    }
  return 0;
}

static long
k_for (long kbase, long e)
{
  int i;
  for (i = 0; i < k_map_n; i++)
    if (k_map_e[i] == e)
      return k_map_k[i];
  if (kbase <= -2000000)
    return -(kbase + 2000000);
  return e + kbase;
}

static int
do_check (const char *fn, const char *path)
{
  FILE *fp = fopen (path, "r");
  char line[4096];
  long kbase, k;
  unsigned long long n = 0, bad = 0;
  unsigned long long bin_count[64];
  long e_min = is_trig_fn (fn) ? 16 : -16;
  long e_max = 46;
  long e;

  memset (bin_count, 0, sizeof bin_count);
  if (fp == NULL) { perror (path); return 2; }
  set_k_from_header (fp, &kbase);
  if (kbase == -1000000 && k_map_n == 0)
    {
      fprintf (stderr, "check_hard: %s: no k header\n", path);
      fclose (fp);
      return 2;
    }
  while (fgets (line, sizeof line, fp))
    {
      unsigned long long raw;
      double claimed;
      mpfr_t d, bound, c;
      if (line[0] == '#' || line[0] == '\n') continue;
      if (sscanf (line, "%llu %ld %lf", &raw, &e, &claimed) != 3)
        {
          fprintf (stderr, "check_hard: %s: bad line: %s", path, line);
          bad++;
          continue;
        }
      if (raw == 0 || raw >= (1ULL << 63))
        {
          fprintf (stderr, "check_hard: %s: raw %llu is not a Q48.16 raw\n",
                   path, raw);
          bad++;
          continue;
        }
      if (e < e_min || e > e_max)
        {
          fprintf (stderr, "check_hard: %s: raw %llu e %ld out of range [%ld..%ld]\n",
                   path, raw, e, e_min, e_max);
          bad++;
          continue;
        }
      k = k_for (kbase, e);
      {
        unsigned long long lo = 1ULL << (e + 16);
        unsigned long long hi = 1ULL << (e + 17);
        if (raw < lo || raw >= hi)
          {
            fprintf (stderr,
                     "check_hard: %s: raw %llu e %ld: raw not in [2^(e+16), 2^(e+17)): [%llu, %llu)\n",
                     path, raw, e, lo, hi);
            bad++;
            continue;
          }
      }
      bin_count[e + 16]++;
      mpfr_inits2 (PREC, d, bound, c, (mpfr_ptr) 0);
      distance_units (d, fn, raw);
      mpfr_set_ui (bound, 1, MPFR_RNDN);
      mpfr_div_2ui (bound, bound, (unsigned long) k, MPFR_RNDN);
      if (mpfr_cmp (d, bound) > 0)
        {
          fprintf (stderr, "check_hard: %s: raw %llu e %ld distance ", path,
                   raw, e);
          mpfr_printf ("%Re > 2^-%ld\n", d, k);
          bad++;
        }
      else
        {
          mpfr_t tol;
          mpfr_init2 (tol, PREC);
          mpfr_set_d (c, claimed, MPFR_RNDN);
          mpfr_sub (c, c, d, MPFR_RNDN);
          mpfr_abs (c, c, MPFR_RNDN);
          mpfr_mul_d (tol, d, 1e-9, MPFR_RNDN);
          mpfr_add_d (tol, tol, 1e-15, MPFR_RNDN);
          if (mpfr_cmp (c, tol) > 0)
            {
              mpfr_printf ("check_hard: %s: raw %llu claimed distance differs: "
                           "claimed %.17g recomputed %Re\n", path, raw,
                           claimed, d);
              bad++;
            }
          mpfr_clear (tol);
        }
      mpfr_clears (d, bound, c, (mpfr_ptr) 0);
      n++;
    }
  fclose (fp);
  for (e = e_min; e <= e_max; e++)
    {
      unsigned long long min_req = (e + 16 < 3) ? (1ULL << (e + 16)) : 8ULL;
      unsigned long long cnt = bin_count[e + 16];
      if (cnt < min_req)
        {
          fprintf (stderr, "check_hard: %s: binade e=%ld has %llu cases (need >= %llu)\n",
                   path, e, cnt, min_req);
          bad++;
        }
    }
  if (bad)
    {
      printf ("%s: FAIL (%llu bad, %llu total cases)\n", path, bad, n);
      return 1;
    }
  printf ("%s: OK (%llu cases across binades %ld..%ld)\n", path, n, e_min, e_max);
  return n > 0 ? 0 : 1;
}

/* read BaCSeL's `*** x=..., distance is: ...` lines and emit
 * `<raw> <e> <recomputed distance>` for cases within 2^-k. */
static int
do_convert (const char *fn, long e, long k)
{
  char line[8192];
  mpfr_t x, rawf, d, bound;
  mpz_t z;
  unsigned long long lo = 1ULL << (e + 16);
  unsigned long long hi = 1ULL << (e + 17);
  mpfr_inits2 (PREC, x, rawf, d, bound, (mpfr_ptr) 0);
  mpz_init (z);
  mpfr_set_ui (bound, 1, MPFR_RNDN);
  mpfr_div_2ui (bound, bound, (unsigned long) k, MPFR_RNDN);
  while (fgets (line, sizeof line, stdin))
    {
      char *p = strstr (line, "x=");
      char *q, *star;
      unsigned long long raw = 0;
      if (p == NULL) continue;
      p += 2;
      q = p;
      while (*q && *q != ',' && *q != ' ' && *q != '\n') q++;
      char save = *q;
      *q = 0;
      star = strstr (p, "*2^");
      if (star != NULL)
        {
          long exp2 = atol (star + 3);
          *star = 0;
          raw = strtoull (p, NULL, 10);
          if (exp2 > -16)
            raw <<= (exp2 + 16);
          else if (exp2 < -16)
            raw >>= (-16 - exp2);
        }
      else
        {
          if (mpfr_set_str (x, p, 0, MPFR_RNDN) != 0) { *q = save; continue; }
          mpfr_mul_2ui (rawf, x, 16, MPFR_RNDN);
          if (!mpfr_integer_p (rawf)) { *q = save; continue; }
          mpfr_get_z (z, rawf, MPFR_RNDN);
          if (mpz_sgn (z) <= 0 || mpz_sizeinbase (z, 2) > 63) { *q = save; continue; }
          raw = mpz_get_ui (z);
        }
      *q = save;
      if (raw < lo || raw >= hi) continue;
      distance_units (d, fn, raw);
      if (mpfr_cmp (d, bound) <= 0)
        mpfr_printf ("%llu %ld %.17Rg\n", raw, e, d);
    }
  mpfr_clears (x, rawf, d, bound, (mpfr_ptr) 0);
  mpz_clear (z);
  return 0;
}

static int
do_getexp (const char *fn, unsigned long long raw)
{
  int sgn = 0;
  long ex = raw_exp (fn, raw, &sgn);
  if (sgn == 0)
    printf ("0\n");
  else
    printf ("%ld %d\n", ex, sgn);
  return 0;
}

/* Find a window [t0, t1) of width <= 2^log2w inside [2^(e+16), 2^(e+17))
 * that has constant MPFR exponent/sign and centers on a half-integer crossing
 * of 2^16 * f(raw * 2^-16) when one exists. */
static int
do_window (const char *fn, long e, long log2w)
{
  unsigned long long lo = 1ULL << (e + 16);
  unsigned long long hi = 1ULL << (e + 17);
  unsigned long long W = 1ULL << log2w;
  unsigned long long lo_safe = lo;
  int s0 = 0, s1 = 0;
  long ex0 = raw_exp (fn, lo, &s0);
  if (lo + 1 < hi)
    {
      long ex1 = raw_exp (fn, lo + 1, &s1);
      if (s0 == 0 || ex0 != ex1 || s0 != s1)
        lo_safe = lo + 1;
    }
  if (hi - lo_safe <= W)
    {
      /* Small binade: find the largest power-of-2 window starting at lo_safe
       * (or lo if e <= -14) with constant exponent. */
      unsigned long long t0 = (e <= -14) ? lo : lo_safe;
      unsigned long long t1 = hi;
      long ex = raw_exp (fn, t0, &s0);
      printf ("%llu %llu %ld\n", t0, t1, ex);
      return 0;
    }
  mpfr_t u_lo, u_hi, target, u_mid, d_lo, d_hi;
  mpfr_inits2 (PREC, u_lo, u_hi, target, u_mid, d_lo, d_hi, (mpfr_ptr) 0);
  eval_scaled (u_lo, fn, lo_safe);
  eval_scaled (u_hi, fn, hi - 1);
  int increasing = (mpfr_cmp (u_lo, u_hi) < 0);
  mpfr_t umin, umax, n_min, n_max, n_mid;
  mpfr_inits2 (PREC, umin, umax, n_min, n_max, n_mid, (mpfr_ptr) 0);
  mpfr_set (umin, increasing ? u_lo : u_hi, MPFR_RNDN);
  mpfr_set (umax, increasing ? u_hi : u_lo, MPFR_RNDN);
  mpfr_sub_d (n_min, umin, 0.5, MPFR_RNDN);
  mpfr_ceil (n_min, n_min);
  mpfr_sub_d (n_max, umax, 0.5, MPFR_RNDN);
  mpfr_floor (n_max, n_max);
  unsigned long long t0 = lo_safe;
  if (mpfr_cmp (n_min, n_max) <= 0)
    {
      static const unsigned long num[6] = { 7, 3, 6, 4, 8, 2 };
      for (int ci = 0; ci < 6; ci++)
        {
          mpfr_sub (n_mid, n_max, n_min, MPFR_RNDN);
          mpfr_mul_ui (n_mid, n_mid, num[ci], MPFR_RNDN);
          mpfr_div_ui (n_mid, n_mid, 10, MPFR_RNDN);
          mpfr_add (n_mid, n_min, n_mid, MPFR_RNDN);
          mpfr_floor (n_mid, n_mid);
          mpfr_add_d (target, n_mid, 0.5, MPFR_RNDN);
          unsigned long long a = lo_safe, b = hi - 1;
          for (int it = 0; it < 64 && a + 1 < b; it++)
            {
              unsigned long long m = a + (b - a) / 2;
              eval_scaled (u_mid, fn, m);
              int cmp = mpfr_cmp (u_mid, target);
              if ((increasing && cmp < 0) || (!increasing && cmp > 0))
                a = m;
              else
                b = m;
            }
          unsigned long long center = a;
          if (center > lo_safe + W / 2)
            t0 = center - W / 2;
          else
            t0 = lo_safe;
          if (t0 + W > hi)
            t0 = hi - W;
          int sa = 0, sb = 0;
          long ea = raw_exp (fn, t0, &sa);
          long eb = raw_exp (fn, t0 + W - 1, &sb);
          if (ea == eb && sa == sb && sa != 0)
            break;
        }
    }
  else
    {
      /* No half-integer crossing inside the binade; pick whichever end is closer
       * to a half-integer. */
      distance_units (d_lo, fn, lo_safe);
      distance_units (d_hi, fn, hi - 1);
      if (mpfr_cmp (d_hi, d_lo) < 0)
        t0 = hi - W;
      else
        t0 = lo_safe;
    }
  unsigned long long t1 = t0 + W;
  long ex_a = raw_exp (fn, t0, &s0);
  long ex_b = raw_exp (fn, t1 - 1, &s1);
  if (ex_a != ex_b || s0 != s1)
    {
      t0 = lo_safe;
      t1 = lo_safe + W;
      ex_a = raw_exp (fn, t0, &s0);
    }
  mpfr_clears (u_lo, u_hi, target, u_mid, d_lo, d_hi, umin, umax, n_min, n_max, n_mid, (mpfr_ptr) 0);
  printf ("%llu %llu %ld\n", t0, t1, ex_a);
  return 0;
}

int
main (int argc, char **argv)
{
  if (argc == 3 && argv[1][0] != '-')
    return do_check (argv[1], argv[2]);
  if (argc == 4 && strcmp (argv[1], "--check") == 0)
    return do_check (argv[2], argv[3]);
  if (argc == 5 && strcmp (argv[1], "--convert") == 0)
    return do_convert (argv[2], atol (argv[3]), atol (argv[4]));
  if (argc == 4 && strcmp (argv[1], "--getexp") == 0)
    return do_getexp (argv[2], strtoull (argv[3], NULL, 10));
  if (argc == 5 && strcmp (argv[1], "--window") == 0)
    return do_window (argv[2], atol (argv[3]), atol (argv[4]));
  fprintf (stderr,
           "usage: %s [--check] <fn> <list>\n"
           "       %s --convert <fn> <e> <k> < bacsel.out\n"
           "       %s --getexp  <fn> <raw>\n"
           "       %s --window  <fn> <e> <log2w>\n",
           argv[0], argv[0], argv[0], argv[0]);
  return 2;
}
