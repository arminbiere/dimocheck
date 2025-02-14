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
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFIX "[dimocheck] "

static int verbosity;
static bool complete;
static bool strict;

static const char *strict_option;
static const char *complete_option;

struct clause {
  size_t lineno;
  size_t size;
  int literals[];
};

static const char *dimacs_path;
static const char *model_path;

static FILE *file;
static size_t lineno;
static size_t charno;
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
  assert(last_char != '\n' || lineno > 1);
  fprintf(stderr, "%s:%zu: parse error: ", path, lineno - (last_char == '\n'));
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
  last_char = EOF;
  lineno = 1;
  charno = 0;
}

static void reset_parsing() {
  vrb("closing '%s'", path);
  fclose(file);
}

static int next_char() {
  int res = getc(file);
  if (res == '\n')
    lineno++;
  if (res != EOF)
    charno++;
  return last_char = res;
}

static bool is_space(int ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static bool is_digit(int ch) { return '0' <= ch && ch <= '9'; }

static void parse_dimacs() {
  init_parsing(dimacs_path);
  msg("parsing DIMACS '%s'", path);
  if (strict) {
    assert(strict_option);
    msg("parsing in strict mode (due to '%s')", strict_option);
  } else
    msg("parsing in relaxed mode (use '--strict' or '--pedantic')");
  for (;;) {
    int ch = next_char();
    if (ch == EOF) {
      if (charno)
        err("end-of-file before header (truncated file)");
      else
        err("end-of-file before header (empty file)");
    } else if (is_space(ch)) {
      if (strict)
        err("expected 'c' or 'p' at start of line before header");
    } else if (ch == 'c') {
      while ((ch = next_char()) != '\n')
        if (ch == EOF)
          err("end-of-file in header comment");
      continue;
    } else if (ch == 'p')
      break;
    else
      err("unexpected character (expected 'p')");
  }
  if (next_char() != ' ')
    err("expected space after 'p'");
  if (next_char() != 'c')
    err("expected 'c' after 'p '");
  if (next_char() != 'n')
    err("expected 'n' after 'p c'");
  if (next_char() != 'f')
    err("expected 'f' after 'p cn'");
  if (next_char() != ' ')
    err("expected space after 'p cnf'");
  size_t specified_variables;
  {
    int ch = next_char();
    if (!is_digit(ch))
      err("expected digit after 'p cnf '");
    const size_t maximum_variables_limit = INT_MAX;
    specified_variables = ch - '0';
    while (is_digit(ch = next_char())) {
      if (maximum_variables_limit / 10 < specified_variables)
        err("maximum variable limit exceeded");
      specified_variables *= 10;
      unsigned digit = ch - '0';
      if (maximum_variables_limit - digit < specified_variables)
        err("maximum variable limit exceeded");
      specified_variables += digit;
    }
    if (ch != ' ')
      err("expected space after 'p cnf %zu'", specified_variables);
  }
  size_t specified_clauses;
  {
    int ch = next_char();
    if (!is_digit(ch))
      err("expected digit after 'p cnf %zu '", specified_variables);
    const size_t maximum_clauses_limit = ~(size_t)0;
    specified_clauses = ch - '0';
    while (is_digit(ch = next_char())) {
      if (maximum_clauses_limit / 10 < specified_clauses)
        err("maximum clauses limit exceeded");
      specified_clauses *= 10;
      unsigned digit = ch - '0';
      if (maximum_clauses_limit - digit < specified_clauses)
        err("maximum clauses limit exceeded");
      specified_clauses += digit;
    }
    if (ch == EOF)
      err("unexpected end-of-file after 'p cnf %zu %zu'", specified_variables,
          specified_clauses);
    if (strict) {
      if (ch == '\r') {
        ch = next_char();
        if (ch != '\n')
          err("expected new-line after carriage return after 'p cnf %zu %zu'",
              specified_variables, specified_clauses);
      } else if (ch != '\n')
        err("expected new-line after 'p cnf %zu %zu'", specified_variables,
            specified_clauses);
    } else {
      if (!is_space(ch))
        err("expected space or new-line after 'p cnf %zu %zu'",
            specified_variables, specified_clauses);
      while (is_space(ch) && ch != '\n')
        ch = next_char();
      if (ch == EOF)
        err("unexpected end-of-file after 'p cnf %zu %zu'", specified_variables,
            specified_clauses);
      if (ch != '\n')
        err("expected new-line 'p cnf %zu %zu'", specified_variables,
            specified_clauses);
    }
  }
  msg("parsed header 'p cnf %zu %zu'", specified_variables, specified_clauses);
  reset_parsing();
}

static void parse_model() {
  init_parsing(model_path);
  msg("parsing model '%s'", path);
  reset_parsing();
}

static void check_model() {
  // TODO
}

static void can_not_combine(const char *a, const char *b) {
  if (a && b)
    die("can not combine '%s' and '%s' (try '-h')", a, b);
}

int main(int argc, char **argv) {
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
      strict_option = complete_option = pedantic_option;
      strict = complete = true;
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
  msg("Compiled with '%s'", COMPILE);
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
