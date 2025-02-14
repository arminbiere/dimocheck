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

#include <sys/resource.h>

#define PREFIX "[dimocheck] "

static int verbosity;
static bool complete;
static bool strict;

static const char *strict_option;
static const char *complete_option;

struct clause {
  size_t lineno;
  size_t column;
  size_t size;
  int literals[];
};

static const char *dimacs_path;
static const char *model_path;

static FILE *file;
static int close_file;
static size_t lineno;
static size_t column;
static size_t charno;
static const char *path;
static int last_char[2];

static int maximum_dimacs_variable;
static int maximum_model_variable;
static size_t parsed_clauses;

static struct {
  int *begin, *end, *allocated;
} literals;

static struct {
  struct clause **begin, **end, **allocated;
} clauses;

static struct {
  int *begin;
  size_t size, capacity;
} values;

static void msg(const char *, ...) __attribute__((format(printf, 1, 2)));
static void vrb(const char *, ...) __attribute__((format(printf, 1, 2)));
static void wrn(const char *, ...) __attribute__((format(printf, 1, 2)));

static void die(const char *, ...) __attribute__((format(printf, 1, 2)));
static void fatal(const char *, ...) __attribute__((format(printf, 1, 2)));

static void err(size_t, const char *, ...)
    __attribute__((format(printf, 2, 3)));
static void srr(size_t, const char *, ...)
    __attribute__((format(printf, 2, 3)));

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

static void vrb(const char *fmt, ...) {
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

static void wrn(const char *fmt, ...) {
  if (verbosity < 0)
    return;
  fputs(PREFIX "warning: ", stdout);
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

static void err(size_t token, const char *fmt, ...) {
  assert(last_char[0] != '\n' || lineno > 1);
  fprintf(stderr, "%s:%zu:%zu: parse error: ", path,
          lineno - (last_char[0] == '\n'), token);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

static void srr(size_t token, const char *fmt, ...) {
  assert(last_char[0] != '\n' || lineno > 1);
  fprintf(stderr, "%s:%zu:%zu: strict parsing error: ", path,
          lineno - (last_char[0] == '\n'), token);
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

static void push_clause(size_t lineno, size_t column) {
  size_t size = size_literals();
  size_t bytes = bytes_clause(size);
  struct clause *clause = malloc(bytes);
  if (!clause)
    fatal("out-of-memory allocating clause");
  clause->size = size;
  clause->lineno = lineno;
  clause->column = column;
  size_t bytes_literals = size * sizeof(int);
  memcpy(clause->literals, literals.begin, bytes_literals);
  if (full_clauses())
    enlarge_clauses();
  *clauses.end++ = clause;
  if (verbosity > 1) {
    printf(PREFIX "new size %zu clause[%zu]", size, parsed_clauses);
    const int *p = clause->literals, *end = p + size;
    while (p != end)
      printf(" %d", *p++);
    fputc('\n', stdout);
    fflush(stdout);
  }
}

static void fit_values(size_t idx) {
  assert(idx <= (size_t)INT_MAX);
  const size_t old_capacity = values.capacity;
  if (idx >= old_capacity) {
    size_t new_capacity = old_capacity ? 2 * old_capacity : 1;
    while (idx >= new_capacity)
      new_capacity *= 2;
    values.begin = realloc(values.begin, new_capacity * sizeof *values.begin);
    if (!values.begin)
      fatal("out-of-memory reallocating value array");
    values.capacity = new_capacity;
  }
  while (idx >= values.size) {
    assert(values.size < values.capacity);
    values.begin[values.size++] = 0;
  }
}

static bool has_suffix(const char *p, const char *q) {
  size_t k = strlen(p), l = strlen(q);
  return k >= l && !strcmp(p + k - l, q);
}

static FILE *read_zipped(const char *zipper, const char *p) {
  size_t len = strlen(p) + 32;
  char *cmd = malloc(len);
  if (!cmd)
    fatal("out-of-memory allocating unzipping command");
  snprintf(cmd, len, "%s -c -d %s", zipper, p);
  FILE *file = popen(cmd, "r");
  free(cmd);
  return file;
}

static void init_parsing(const char *p) {
  close_file = 2;
  if (has_suffix(p, ".bz2"))
    file = read_zipped("bunzip2", p);
  else if (has_suffix(p, ".gz"))
    file = read_zipped("gunzip", p);
  else if (has_suffix(p, ".xz"))
    file = read_zipped("xz", p);
  else {
    file = fopen(path = p, "r");
    close_file = 1;
  }
  if (!file)
    die("can not open and read '%s'", path);
  last_char[0] = last_char[1] = EOF;
  lineno = 1;
  column = 0;
  charno = 0;
}

static void reset_parsing() {
  vrb("closing '%s'", path);
  if (close_file == 1)
    fclose(file);
  if (close_file == 2)
    pclose(file);
}

static int next_char() {
  int res = getc(file);
  if (res == '\n')
    lineno++;
  if (res != EOF) {
    if (last_char[0] == '\n')
      column = 1;
    else
      column++;
    charno++;
  }
  last_char[1] = last_char[0];
  last_char[0] = res;
  return res;
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
    msg("parsing in relaxed mode (without '--strict' nor '--pedantic')");
  for (;;) {
    int ch = next_char();
    if (ch == EOF) {
      if (charno)
        err(column, "end-of-file before header (truncated file)");
      else
        err(column, "end-of-file before header (empty file)");
    } else if (is_space(ch)) {
      if (strict)
        srr(column, "expected 'c' or 'p' at start of line before header");
    } else if (ch == 'c') {
      while ((ch = next_char()) != '\n')
        if (ch == EOF)
          err(column, "end-of-file in header comment");
      continue;
    } else if (ch == 'p')
      break;
    else
      err(column, "unexpected character (expected 'p' or 'c')");
  }
  if (next_char() != ' ')
    err(column, "expected space after 'p'");
  if (next_char() != 'c')
    err(column, "expected 'c' after 'p '");
  if (next_char() != 'n')
    err(column, "expected 'n' after 'p c'");
  if (next_char() != 'f')
    err(column, "expected 'f' after 'p cn'");
  if (next_char() != ' ')
    err(column, "expected space after 'p cnf'");
  size_t specified_variables;
  {
    int ch = next_char();
    if (!is_digit(ch))
      err(column, "expected digit after 'p cnf '");
    const size_t maximum_variables_limit = INT_MAX;
    specified_variables = ch - '0';
    while (is_digit(ch = next_char())) {
      if (strict && !specified_variables)
        srr(column, "leading '0' digit in number of variables");
      if (maximum_variables_limit / 10 < specified_variables)
        err(column, "maximum variable limit exceeded");
      specified_variables *= 10;
      unsigned digit = ch - '0';
      if (maximum_variables_limit - digit < specified_variables)
        err(column, "maximum variable limit exceeded");
      specified_variables += digit;
    }
    if (ch != ' ')
      err(column, "expected space after 'p cnf %zu'", specified_variables);
  }
  size_t specified_clauses;
  {
    int ch = next_char();
    if (!is_digit(ch))
      err(column, "expected digit after 'p cnf %zu '", specified_variables);
    const size_t maximum_clauses_limit = ~(size_t)0;
    specified_clauses = ch - '0';
    while (is_digit(ch = next_char())) {
      if (strict && !specified_clauses)
        srr(column, "leading '0' digit in number of clauses");
      if (maximum_clauses_limit / 10 < specified_clauses)
        err(column, "maximum clauses limit exceeded");
      specified_clauses *= 10;
      unsigned digit = ch - '0';
      if (maximum_clauses_limit - digit < specified_clauses)
        err(column, "maximum clauses limit exceeded");
      specified_clauses += digit;
    }
    if (ch == EOF)
      err(column, "unexpected end-of-file after 'p cnf %zu %zu'",
          specified_variables, specified_clauses);
    if (strict) {
      if (ch == '\r') {
        ch = next_char();
        if (ch != '\n')
          srr(column,
              "expected new-line after carriage return after 'p cnf %zu %zu'",
              specified_variables, specified_clauses);
      } else if (ch != '\n')
        srr(column, "expected new-line after 'p cnf %zu %zu'",
            specified_variables, specified_clauses);
    } else {
      if (!is_space(ch))
        err(column, "expected space or new-line after 'p cnf %zu %zu'",
            specified_variables, specified_clauses);
      while (is_space(ch) && ch != '\n')
        ch = next_char();
      if (ch == EOF)
        err(column, "unexpected end-of-file after 'p cnf %zu %zu'",
            specified_variables, specified_clauses);
      if (ch != '\n')
        err(column, "expected new-line 'p cnf %zu %zu'", specified_variables,
            specified_clauses);
    }
  }
  msg("parsed header 'p cnf %zu %zu'", specified_variables, specified_clauses);
  {
    int last_lit = 0;
    int ch = next_char();
    size_t clause_lineno = lineno;
    size_t clause_column = column;
    for (;;) {

      size_t token = column;

      if (ch == EOF) {
        if (last_lit)
          err(column, "terminating zero missing in last clause");
        if (last_char[1] != '\n') {
          if (strict)
            srr(column, "new-line missing after last clause");
          else
            wrn("new-line missing after last clause");
        }
        if (parsed_clauses < specified_clauses) {
          size_t missing_clauses = specified_clauses - parsed_clauses;
          if (strict) {
            if (missing_clauses == 1)
              srr(column, "one clause missing (parsed %zu but %zu specified)",
                  parsed_clauses, specified_clauses);
            else
              srr(column, "%zu clauses missing (parsed %zu but %zu specified)",
                  missing_clauses, parsed_clauses, specified_clauses);
          } else {
            if (missing_clauses == 1)
              wrn("one clause missing (parsed %zu but %zu specified)",
                  parsed_clauses, specified_clauses);
            else
              wrn("%zu clauses missing (parsed %zu but %zu specified)",
                  missing_clauses, parsed_clauses, specified_clauses);
          }
        }
        break;
      }

      if (is_space(ch)) {
        ch = next_char();
        continue;
      }

      if (ch == 'c') {
        if (strict)
          srr(column, "unexpected comment after header");
        while ((ch = next_char()) != '\n')
          if (ch == EOF)
            err(column, "end-of-file in comment");
        ch = next_char();
        continue;
      }

      if (!last_lit) {
        clause_lineno = lineno;
        clause_column = column;
      }

      int sign = 1;
      if (ch == '-') {
        ch = next_char();
        if (strict && ch == '0')
          srr(column, "invalid '0' after '-'");
        if (!is_digit(ch))
          err(column, "expected digit after '-'");
        sign = -1;
      } else if (!is_digit(ch))
        err(column, "expected integer literal (digit or sign)");

      const size_t maximum_variable_index = INT_MAX;
      size_t idx = ch - '0';
      while (is_digit(ch = next_char())) {
        if (strict && !idx)
          srr(column, "leading '0' digit in literal");
        if (maximum_variable_index / 10 < idx)
          err(column, "literal exceeds maximum variable limit");
        idx *= 10;
        const unsigned digit = ch - '0';
        if (maximum_variable_index - digit < idx)
          err(column, "literal exceeds maximum variable limit");
        idx += digit;
      }

      const int lit = sign * (int)idx;
      assert(abs(lit) <= maximum_variable_index);

      if (strict && ch == EOF)
        srr(column, "end-of-file after literal '%d'", lit);

      if (!is_space(ch) && ch != 'c')
        err(column, "unexpected character after literal '%d'", lit);

      if (strict && specified_clauses == parsed_clauses)
        srr(token,
            "too many clauses "
            "(start of clause %zu but only %zu specified)",
            parsed_clauses + 1, specified_clauses);

      if (strict && idx > specified_variables)
        srr(token, "literal '%d' exceeds specified maximum variable '%zu'", lit,
            specified_variables);

      if (sign < 0 && !lit)
        err(token, "negative zero literal '-0'");

      if (lit) {
        push_literal(lit);
        if (idx > maximum_dimacs_variable)
          maximum_dimacs_variable = idx;
      } else {
        parsed_clauses++;
        push_clause(clause_lineno, clause_column);
        clear_literals();
      }
      last_lit = lit;
    }
  }
  reset_parsing();
  msg("parsed %zu clauses with maximum variable index '%d'", parsed_clauses,
      maximum_dimacs_variable);
}

static void parse_model() {
  init_parsing(model_path);
  msg("parsing model '%s'", path);
  if (strict) {
    assert(strict_option);
    msg("parsing in strict mode (due to '%s')", strict_option);
  } else
    msg("parsing in relaxed mode (without '--strict' nor '--pedantic')");
  size_t parsed_values = 0, positive_values = 0, negative_values = 0;
  bool found_status_line = false, reported_on_status_line_found = false;
  for (;;) {
    int ch = next_char();
    size_t token = column;
    if (ch == EOF)
      break;
    if (ch == 'c') {
      while ((ch = next_char()) != '\n')
        if (ch == EOF)
          err(column, "end-of-file in comment");
    } else if (ch == 's') {
      if (next_char() != ' ')
        err(column, "expected space after 's'");
      for (const char *p = "SATISFIABLE"; *p; p++)
        if (next_char() != *p)
          err(token, "invalid status line (expected 's SATISFIABLE')");
      ch = next_char();
      if (strict) {
        if (ch == '\r') {
          ch = next_char();
          if (ch != '\n')
            err(column, "expected new-line after carriage return after "
                        "'s SATISFIABLE'");
        } else if (ch != '\n')
          err(column, "expected new-line after 's SATISFIABLE'");
      } else {
        while (is_space(ch) && ch != '\n')
          ch = next_char();
        if (ch != '\n')
          err(column, "expected new-line after 's SATISFIABLE'");
      }
      msg("found 's SATISFIABLE' status line");
      found_status_line = true;
    } else if (ch == 'v') {
      if (!reported_on_status_line_found) {
        if (!found_status_line) {
          if (strict)
            srr(column, "'v' line without 's SATISFIABLE' status line");
          else
            wrn("'v' line without 's SATISFIABLE' status line");
        }
        reported_on_status_line_found = true;
      }
      int last_lit = INT_MIN;
    CONTINUE_WITH_V_LINES:
      if (next_char() != ' ')
        err(column, "expected space after 'v'");
      for (;;) {
        ch = next_char();
      CONTINUE_WITH_V_LINE_BUT_WITHOUT_READING_CHAR:
        token = column;
        if (ch == EOF)
          err(column, "end-of-file in 'v' line");
        else if (ch == ' ' || ch == '\t')
          continue;
        else if (ch == '\n') {
        END_OF_V_LINE:
          if (last_lit) {
            ch = next_char();
            if (ch != 'v')
              err(column, "expected continuation of 'v' lines (zero missing)");
            goto CONTINUE_WITH_V_LINES;
          } else
            goto CONTINUE_OUTER_LOOP;
        } else if (ch == '\r') {
          ch = next_char();
          if (ch != '\n')
            err(column, "expected new-line after carriage-return in 'v' line");
          goto END_OF_V_LINE;
        } else {

          int sign = 1;
          if (ch == '-') {
            ch = next_char();
            if (strict && ch == '0')
              err(column, "invalid '0' after '-'");
            if (!is_digit(ch))
              err(column, "expected digit after '-'");
            sign = -1;
          } else if (!is_digit(ch))
            err(column, "expected integer literal (digit or sign)");

          const size_t maximum_variable_index = INT_MAX;
          size_t idx = ch - '0';
          while (is_digit(ch = next_char())) {
            if (strict && !idx)
              srr(column, "leading '0' digit in literal");
            if (maximum_variable_index / 10 < idx)
              err(column, "literal exceeds maximum variable limit");
            idx *= 10;
            const unsigned digit = ch - '0';
            if (maximum_variable_index - digit < idx)
              err(column, "literal exceeds maximum variable limit");
            idx += digit;
          }

          const int lit = sign * (int)idx;
          assert(abs(lit) <= maximum_variable_index);

          if (sign < 0 && !lit)
            err(token, "negative zero literal '-0'");

          if (strict && idx > maximum_dimacs_variable)
            srr(token, "literal '%d' exceeds maximum DIMACS variable '%d'", lit,
                maximum_dimacs_variable);

          if (!last_lit) {
            if (lit)
              err(token, "literal '%d' after '0' in 'v' line", lit);
            else
              err(token, "two consecutive '0' in 'v' line");
          }

          if (verbosity > 1) {
            if (lit)
              printf(PREFIX "parsed value literal '%d'\n", lit);
            else
              printf(PREFIX "parsed terminating zero '0'\n");
            fflush(stdout);
          }

          if (idx) {
            parsed_values++;
            if (idx > maximum_model_variable)
              maximum_model_variable = idx;
          }

          if (idx >= values.size)
            fit_values(idx);

          assert(idx <= (size_t)INT_MAX);
          const int old_value = values.begin[idx];
          const int new_value = lit;

          if (old_value && old_value != new_value)
            err(token, "old value '%d' overwritten by new value '%d'",
                old_value, new_value);

          if (strict && old_value) {
            assert(old_value == new_value);
            srr(token, "value '%d' set twice", new_value);
          }

          if (old_value != new_value) {
            if (new_value < 0)
              negative_values++;
            else
              positive_values++;
          }

          values.begin[idx] = new_value;

          last_lit = lit;
          goto CONTINUE_WITH_V_LINE_BUT_WITHOUT_READING_CHAR;
        }
      }
    } else
      err(column, "expected 'c', 's' or 'v' as first character");
  CONTINUE_OUTER_LOOP:;
  }
  reset_parsing();
  msg("parsed values of %zu variables with maximum index '%d'", parsed_values,
      maximum_model_variable);
  msg("set %zu positive and %zu negative values", positive_values,
      negative_values);
}

static void check_model() {
  msg("checking model to satisfy DIMACS formula");
  if (complete) {
    msg("checking completeness of model (due to '%s')", complete_option);
    for (size_t idx = 1; idx <= (size_t)maximum_dimacs_variable; idx++)
      if (idx >= values.size || !values.begin[idx])
        die("no value for for DIMACS variable '%zu' found", idx);
    msg("model complete (all DIMACS variables are assigned)");
  } else
    msg("partial model checking (without '--complete' nor '--pedantic')");
  for (struct clause **p = clauses.begin; p != clauses.end; p++) {
    const struct clause *c = *p;
    const int *q = c->literals, *end_literals = q + c->size;
    bool satisfied = false;
    while (!satisfied && q != end_literals) {
      const int lit = *q++;
      assert(lit != INT_MIN);
      const size_t idx = abs(lit);
      if (idx >= values.size)
        continue;
      int value = values.begin[idx];
      if (value == lit)
        satisfied = true;
    }
    if (satisfied)
      continue;
    fprintf(stderr, "%s:%zu:%zu: fatal error: clause[%zu] unsatisfied:\n",
            dimacs_path, c->lineno, c->column, p - clauses.begin + 1);
    for (q = c->literals; q != end_literals; q++)
      fprintf(stderr, "%d ", *q);
    fputs("0\n", stderr);
    fflush(stderr);
    abort();
    exit(1);
  }
  msg("checked all %zu clauses to be satisfied by model", parsed_clauses);
}

static void can_not_combine(const char *a, const char *b) {
  if (a && b)
    die("can not combine '%s' and '%s' (try '-h')", a, b);
}

size_t maximum_resident_set_size() {
  size_t res = 0;
  struct rusage u;
  if (!getrusage(RUSAGE_SELF, &u)) {
    res = (size_t)u.ru_maxrss;
#ifndef __APPLE__
    res <<= 10;
#endif
  }
  return res;
}

static double process_time() {
  double res = 0;
  struct rusage u;
  if (!getrusage(RUSAGE_SELF, &u)) {
    res = u.ru_utime.tv_sec + 1e-6 * u.ru_utime.tv_usec;
    res += u.ru_stime.tv_sec + 1e-6 * u.ru_stime.tv_usec;
  }
  return res;
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
      assert(verbosity >= 0);
      verbosity += (verbosity != INT_MAX);
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
  msg("Copyright (c) 2025, Armin Biere, University of Freiburg");
  msg("Version %s", VERSION);
  msg("Compiled with '%s'", COMPILE);
  parse_dimacs();
  parse_model();
  check_model();
  free(literals.begin);
  for (struct clause **p = clauses.begin; p != clauses.end; p++)
    free(*p);
  free(clauses.begin);
  free(values.begin);
  fputs ("s MODEL_SATISFIES_FORMULA\n", stdout);
  fflush (stdout);
  size_t bytes = maximum_resident_set_size();
  if (bytes >= 1u<<30)
    msg("maximum resident-set size %.2f GB (%zu bytes)",
	bytes / (double)(1u << 30), bytes);
  else
    msg("maximum resident-set size %.2f MB (%zu bytes)",
	bytes / (double)(1 << 20), bytes);
  msg("total process-time %.2f seconds", process_time());
  return 0;
}
