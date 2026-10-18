/* check_hard.c -- verify BaCSeL hard-case lists for fixed's Q48.16 functions.
 *
 * This is a development tool, not part of the library and not built by CMake.
 * For every `<raw> <e> <distance>` line it recomputes f(raw / 2^16) at 256
 * bits and the distance to the nearest rounding midpoint in units of 2^-16,
 * which is what a Q48.16 round-to-nearest must resolve.
 *
 * Usage:
 *   check_hard --check   <fn> <list>
 *   check_hard --convert <fn> <e> <k>       (BaCSeL output on stdin)
 *   check_hard --getexp  <fn> <raw>
 *
 * The lists carry `# k = e + N` or `# k = N` in their header; --check uses
 * that to enforce distance <= 2^-k.  Exit status 1 on any bad case.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mpfr.h>

#define PREC 256

static int
apply_fn (mpfr_t y, const char *fn, mpfr_t x)
{
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

/* distance to the nearest midpoint of the 2^-16 grid, in units of 2^-16 */
static void
distance_units (mpfr_t d, const char *fn, unsigned long long raw)
{
  mpfr_t x, y, u, f;
  mpfr_inits2 (PREC, x, y, u, f, (mpfr_ptr) 0);
  {
    mpz_t zr;
    mpz_init_set_ui (zr, raw);
    mpfr_set_z (x, zr, MPFR_RNDN);
    mpz_clear (zr);
  }
  mpfr_div_2ui (x, x, 16, MPFR_RNDN);       /* x = raw / 2^16 */
  apply_fn (y, fn, x);
  mpfr_mul_2ui (u, y, 16, MPFR_RNDN);       /* u = f * 2^16 */
  mpfr_floor (f, u);
  mpfr_sub (u, u, f, MPFR_RNDN);            /* fractional part in [0,1) */
  mpfr_set_d (f, 0.5, MPFR_RNDN);
  mpfr_sub (d, u, f, MPFR_RNDN);
  mpfr_abs (d, d, MPFR_RNDN);
  mpfr_clears (x, y, u, f, (mpfr_ptr) 0);
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
              /* advance past the number */
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
  if (fp == NULL) { perror (path); return 2; }
  set_k_from_header (fp, &kbase);
  if (kbase == -1000000)
    {
      fprintf (stderr, "check_hard: %s: no k header\n", path);
      fclose (fp);
      return 2;
    }
  while (fgets (line, sizeof line, fp))
    {
      unsigned long long raw;
      long e;
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
      k = k_for (kbase, e);
      {
        unsigned long long lo = 1ULL << (e + 16);
        unsigned long long hi = 1ULL << (e + 17);
        if (e + 16 < 0 || e + 17 > 63)
          {
            fprintf (stderr, "check_hard: %s: raw %llu e %ld out of range\n",
                     path, raw, e);
            bad++;
            continue;
          }
        if (raw < lo || raw >= hi)
          {
            fprintf (stderr,
                     "check_hard: %s: raw %llu e %ld: raw not in [2^(e+16), 2^(e+17)): [%llu, %llu)\n",
                     path, raw, e, lo, hi);
            bad++;
            continue;
          }
      }
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
  if (bad)
    {
      printf ("%s: FAIL (%llu bad of %llu)\n", path, bad, n);
      return 1;
    }
  printf ("%s: OK (%llu cases)\n", path, n);
  return n > 0 ? 0 : 1;
}

/* read BaCSeL's `*** x=<hex float>, distance is: ...` lines and emit
 * `<raw> <e> <recomputed distance>` for cases within 2^-k. */
static int
do_convert (const char *fn, long e, long k)
{
  char line[8192];
  mpfr_t x, rawf, d, bound;
  mpz_t z;
  mpfr_inits2 (PREC, x, rawf, d, bound, (mpfr_ptr) 0);
  mpz_init (z);
  mpfr_set_ui (bound, 1, MPFR_RNDN);
  mpfr_div_2ui (bound, bound, (unsigned long) k, MPFR_RNDN);
  while (fgets (line, sizeof line, stdin))
    {
      char *p = strstr (line, "x=");
      char *q;
      unsigned long long raw;
      if (p == NULL) continue;
      p += 2;
      q = p;
      while (*q && *q != ',' && *q != ' ' && *q != '\n') q++;
      char save = *q;
      *q = 0;
      if (mpfr_set_str (x, p, 0, MPFR_RNDN) != 0) { *q = save; continue; }
      *q = save;
      mpfr_mul_2ui (rawf, x, 16, MPFR_RNDN);
      if (!mpfr_integer_p (rawf)) continue;
      mpfr_get_z (z, rawf, MPFR_RNDN);
      if (mpz_sgn (z) <= 0 || mpz_sizeinbase (z, 2) > 63) continue;
      raw = mpz_get_ui (z);
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
  mpfr_t x, y;
  mpfr_inits2 (PREC, x, y, (mpfr_ptr) 0);
  {
    mpz_t zr;
    mpz_init_set_ui (zr, raw);
    mpfr_set_z (x, zr, MPFR_RNDN);
    mpz_clear (zr);
  }
  mpfr_div_2ui (x, x, 16, MPFR_RNDN);
  apply_fn (y, fn, x);
  if (mpfr_zero_p (y))
    printf ("0\n");
  else
    printf ("%ld %d\n", (long) mpfr_get_exp (y), mpfr_sgn (y));
  mpfr_clears (x, y, (mpfr_ptr) 0);
  return 0;
}

int
main (int argc, char **argv)
{
  if (argc == 4 && strcmp (argv[1], "--check") == 0)
    return do_check (argv[2], argv[3]);
  if (argc == 5 && strcmp (argv[1], "--convert") == 0)
    return do_convert (argv[2], atol (argv[3]), atol (argv[4]));
  if (argc == 4 && strcmp (argv[1], "--getexp") == 0)
    return do_getexp (argv[2], strtoull (argv[3], NULL, 10));
  fprintf (stderr,
           "usage: %s --check <fn> <list>\n"
           "       %s --convert <fn> <e> <k> < bacsel.out\n"
           "       %s --getexp <fn> <raw>\n", argv[0], argv[0], argv[0]);
  return 2;
}
