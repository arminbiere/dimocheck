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

#include "config.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFIX "[dimocheck] "

static int verbosity;
static bool complete;
static bool strict;

struct clause {
  size_t lineno;
  size_t size;
  int literals[];
};

static const char *dimacs_path;
static const char *model_path;

static FILE *file;
static size_t lineno;
static const char *path;
static int last_char;

static int maximum_variable;
static signed char *values;
static size_t added_clauses;

struct {
  int *begin, *end, *allocated;
} literals;

struct {
  struct clause **begin, **end, **allocated;
} clauses;

static void msg(const char *, ...) __attribute__((format(printf, 1, 2)));
static void vrb(const char *, ...) __attribute__((format(printf, 1, 2)));

static void die(const char *, ...) __attribute__((format(printf, 1, 2)));
static void fatal(const char *, ...) __attribute__((format(printf, 1, 2)));

static void err(const char *, ...) __attribute__((format(printf, 1, 2)));

static void msg(const char *fmt, ...) {
  if (verbosity < 0)
    return;
  fputs(PREFIX, stdout);
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  fputc('\n', stdout);
  fflush(stdout);
}

// TODO static
void vrb(const char *fmt, ...) {
  if (verbosity < 1)
    return;
  fputs(PREFIX, stdout);
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

static void err(const char *fmt, ...) {
  fprintf(stderr, "%s:%zu: parse error: ", path, lineno);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

static bool full_literals() { return literals.end == literals.allocated; }

static size_t size_literals() { return literals.end - literals.begin; }

static size_t capacity_literals() {
  return literals.allocated - literals.begin;
}

static void enlarge_literals() {
  const size_t old_capacity = capacity_literals();
  const size_t new_capacity = old_capacity ? 2 * old_capacity : 1;
  literals.begin =
      realloc(literals.begin, new_capacity * sizeof *literals.begin);
  if (!literals.begin)
    fatal("out-of-memory reallocating stack of literals");
  literals.end = literals.begin + old_capacity;
  literals.allocated = literals.begin + new_capacity;
  vrb("enlarged literal stack to %zu", new_capacity);
}

static void push_literal(int lit) {
  assert(lit);
  if (full_literals())
    enlarge_literals();
  *literals.end++ = lit;
}

static void clear_literals() { literals.end = literals.begin; }

static bool full_clauses() { return clauses.end == clauses.allocated; }

static size_t capacity_clauses() { return clauses.allocated - clauses.begin; }

static void enlarge_clauses() {
  const size_t old_capacity = capacity_clauses();
  const size_t new_capacity = old_capacity ? 2 * old_capacity : 1;
  clauses.begin = realloc(clauses.begin, new_capacity * sizeof *clauses.begin);
  if (!clauses.begin)
    fatal("out-of-memory reallocating stack of clauses");
  clauses.end = clauses.begin + old_capacity;
  clauses.allocated = clauses.begin + new_capacity;
  vrb("enlarged clauses stack to %zu", new_capacity);
}

static size_t bytes_clause(size_t size) {
  return sizeof(struct clause) + size * sizeof(int);
}

static void push_clause(size_t lineno) {
  size_t size = size_literals();
  size_t bytes = bytes_clause(size);
  struct clause *clause = malloc(bytes);
  if (!clause)
    fatal("out-of-memory allocating clause");
  clause->size = size;
  clause->lineno = size;
  size_t bytes_literals = size * sizeof(int);
  memcpy(clause->literals, literals.begin, bytes_literals);
  if (full_clauses())
    enlarge_clauses();
  *clauses.end++ = clause;
  added_clauses++;
  if (verbosity > 1) {
    printf(PREFIX "new clause[%zu]", added_clauses);
    const int *p = clause->literals, *end = p + size;
    while (p != end)
      printf(" %d", *p++);
    fflush(stdout);
  }
}

static void init_parsing(const char *p) {
  if (!(file = fopen(path = p, "r")))
    die("can not open and read '%s'", path);
  last_char = 0;
  lineno = 1;
}

static void parse_dimacs() { init_parsing(dimacs_path); }

static void parse_model() { init_parsing(model_path); }

static void check_model() {
  // TODO
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
  msg("Version %s", VERSION);
  msg("Compiled '%s", COMPILE);
  parse_dimacs();
  parse_model();
  check_model();
  free(literals.begin);
  for (struct clause **p = clauses.begin; p != clauses.end; p++)
    free(*p);
  free(clauses.begin);
  free(values);
  return 0;
}
