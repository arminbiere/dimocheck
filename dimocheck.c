// clang-format off
static const char * usage =
"usage: dimocheck [ <option> ... ] <dimacs> <solution\n"
"\n"
"-h | -help       print this command line option summary\n"
"-s | --strict    strict parsing (default is relaxed parsing)\n"
"-c | --complete  require full models (otherwise partial is fined)\n"
"-p | --pedantic  strict and complete mode\n"
"-v | --verbose   increase verbosity\n"
"-q | --quiet     no messages except errors\n"
;
// clang-format on

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct stack {
  int *begin, *end, *allocated;
};

static int verbosity;
static bool complete;
static bool strict;

static const char *dimacs_path;
static const char *model_path;

static void msg(const char *, ...) __attribute__((format(printf, 1, 2)));
static void vrb(const char *, ...) __attribute__((format(printf, 1, 2)));

static void die(const char *, ...) __attribute__((format(printf, 1, 2)));
static void fatal(const char *, ...) __attribute__((format(printf, 1, 2)));

static void msg(const char *fmt, ...) {
  if (verbosity < 0)
    return;
  fputs("[dimocheck] ", stdout);
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  fputc('\n', stdout);
  fflush(stdout);
}

static void vrb(const char *fmt, ...) {
  if (verbosity < 1)
    return;
  fputs("[dimocheck] ", stdout);
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  fputc('\n', stdout);
  fflush(stdout);
}

static void die(const char *fmt, ...) {
  fputs("dimocheck: error: ", stderr);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

static void fatal(const char *fmt, ...) {
  fputs("dimocheck: fatal error: ", stderr);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  abort();
  exit(1);
}

static void can_not_combine(const char *a, const char *b) {
  if (a && b)
    die("can not combine '%s' and '%s' (try '-h')", a, b);
}

int main(int argc, char **argv) {
  const char *strict_option = 0;
  const char *complete_option = 0;
  const char *pedantic_option = 0;
  const char *verbose_option = 0;
  const char *quiet_option = 0;
  for (int i = 1; i != argc; i++) {
    const char *arg = argv[i];
    if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
      fputs(usage, stdout);
      return 0;
    } else if (!strcmp(arg, "-s") || !strcmp(arg, "--strict")) {
      strict_option = arg;
      can_not_combine(pedantic_option, strict_option);
      strict = true;
    } else if (!strcmp(arg, "-c") || !strcmp(arg, "--complete")) {
      complete_option = arg;
      can_not_combine(pedantic_option, complete_option);
      complete = true;
    } else if (!strcmp(arg, "-p") || !strcmp(arg, "--pedantic")) {
      pedantic_option = arg;
      can_not_combine(strict_option, pedantic_option);
      can_not_combine(complete_option, pedantic_option);
      complete = true;
    } else if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose")) {
      if (!verbose_option)
        verbose_option = arg;
      can_not_combine(quiet_option, verbose_option);
      verbosity = -1;
    } else if (!strcmp(arg, "-q") || !strcmp(arg, "--quiet")) {
      quiet_option = arg;
      can_not_combine(verbose_option, quiet_option);
      verbosity = -1;
    } else if (arg[0] == '-')
      die("invalid option '%s' (try '-h')", arg);
    else if (!dimacs_path)
      dimacs_path = arg;
    else if (!model_path)
      model_path = arg;
    else
      die("too many files '%s', '%s' and '%s'", dimacs_path, model_path, arg);
  }
  if (!dimacs_path)
    die("DIMACS file missing (try '-h')");
  if (!model_path)
    die("model file missing (try '-h')");
  msg("DiMoCheck DIMACS Model Checker");
  return 0;
}
