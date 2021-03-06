/*******************************************************************************
 * BSD 3-Clause License
 *
 * Copyright (c) 2017, Matt Davis (enferex) https://github.com/enferex
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sqlite3.h>
#include <readline/readline.h>
#include <readline/history.h>

#define DEFAULT_COMMENT     "#"
#define DEFAULT_COMMENT_C   '#'
#define DEFAULT_DELIMITER   ","
#define DEFAULT_DELIMITER_C ','
#define DEFAULT_PROMPT      "sql> "
#define DEFAULT_DB          ":memory:"
#define DEFAULT_TABLE       "csv"
#define MAX_LINE_LENGTH     1024

#define IS_COMMENT(_c)   ((_c) == DEFAULT_COMMENT[0])
#define IS_DELIMITER(_c) ((_c) == DEFAULT_DELIMITER[0])

#define ERR(_msg, ...) \
    fprintf(stderr, _msg "\n", ## __VA_ARGS__)

/* Callbacks for our own defined commands */
typedef void (*cmd_callback_t)(void);
static void cmd_exit();

struct {
    const char     *name;
    cmd_callback_t  callback;
} static const commands[] = {
    {"quit", cmd_exit},
    {".q",   cmd_exit},
    {"q",    cmd_exit},
    {"exit", cmd_exit}
};

static void usage(const char *execname)
{
    printf("Usage: %s <file.csv>\n", execname);
    exit(EXIT_SUCCESS);
}

/* Remove leading and trailing whitespace */
static char *trim(char *line)
{
    int i;
    char *c;

    if (!line)
      return NULL;

    c = line;
    while (isspace(*c))
      ++c;
    line = c;

    for (i=strlen(c)-1; i>=0; --i)
      if (isspace(c[i]))
        continue;
      else
        break;
    c[i+1] = '\0';

    /* Rip off any comments */
    if ((c = strchr(line, DEFAULT_COMMENT_C)))
      *c = '\0';

    return line;
}

static char **define_cols(const char *line, int n_cols)
{
    size_t len;
    char **cols;
    char *mutable_line = strndup(line, MAX_LINE_LENGTH);

    if (!mutable_line || !(cols = malloc(sizeof(char *))))
      ERR("Error allocating column headers.");

    len = strlen(mutable_line);
    for (int i=0; i<len; ++i)
      if (IS_COMMENT(mutable_line[i]))
        ++mutable_line;
      else
        break;

    mutable_line = trim(mutable_line);

    for (int i=0; i<n_cols; ++i) {
        char *tok = strtok(i==0 ? mutable_line : NULL, DEFAULT_DELIMITER);
        cols[i] = strndup(tok, MAX_LINE_LENGTH);
    }

    return cols;
}

static char **define_ncols(int n_cols)
{
    char **cols = malloc(sizeof(char *));

    if (!cols)
      ERR("Error allocating column headers.");

    for (int i=0; i<n_cols; ++i) {
        char buf[32];
        sprintf(buf, "C%d", i);
        cols[i] = strdup(buf);
        if (!cols[i])
          ERR("Error allocating column header.");
    }

    return cols;
}

static int count_cols(const char *line)
{
    int count = 0;
    const size_t n_chars = strlen(line);

    for (size_t i=0; i<n_chars; ++i)
      if (IS_DELIMITER(line[i]))
        ++count;

    /* Number of delimiters is always 1 less than number of columns */
    return count + 1;
}

static char **determine_columns(FILE *csv, int *n_cols)
{
    int  buffer_idx;
    char buffer[2][MAX_LINE_LENGTH] = {0};
    char **cols;
    _Bool found_first_line;

    assert(csv && n_cols);
    rewind(csv);

    buffer_idx = 0;
    found_first_line = false;
    while (!ferror(csv) && !feof(csv)) {
        char *line = fgets(buffer[buffer_idx], MAX_LINE_LENGTH, csv);
        line = trim(line);

        /* If this is a empty line... grab next line, but do not flip the
         * buffer, overrwite the empty line.
         */
        if (strlen(line) == 0)
          continue;

        /* Not a comment, so first real line! */
        if (!IS_COMMENT(line[0])) {
            found_first_line = true;
            break;
        }

        /* Flip double buffer */
        buffer_idx = (buffer_idx + 1)% 2;
    }

    cols    = NULL;
    *n_cols = 0;
    if (found_first_line) {
        const char *curr = buffer[(buffer_idx) % 2];
        const char *prev = buffer[(buffer_idx+1) % 2];
        *n_cols = count_cols(curr);

        /* If previous line had comment */
        if (IS_COMMENT(prev[0]) && (count_cols(prev) == *n_cols))
          cols = define_cols(prev, *n_cols);
        else
          cols = define_ncols(*n_cols);
    }

    return cols;
}

static void create_table(sqlite3 *sql, char **fields, int n_fields)
{
    char *query, *errmsg;
    size_t len = 0;

    /* Length of all column names and some extra */
    for (int i=0; i<n_fields; ++i)
      len += strlen(fields[i]);
    len += 32 + (n_fields * 8); /* extra */

    if (!(query = malloc(len)))
      ERR("Could not allocate database schema.");

    sprintf(query, "CREATE TABLE %s (", DEFAULT_TABLE);

    for (int i=0; i<n_fields; ++i) {
        strcat(query, fields[i]);
        strcat(query, " TEXT");
        if (i+1 < n_fields)
          strcat(query, ",");
    }
    strcat(query, ");");

    if (sqlite3_exec(sql, query, NULL, NULL, &errmsg))
      ERR("Error creating table: %s", errmsg);

    free(query);
}

static void insert(sqlite3 *sql, const char *line, int n_fields)
{
    char *errmsg;
    char query[MAX_LINE_LENGTH + (n_fields*2) + 16];
    char *row = strdup(line);

    if (!row)
      ERR("Error allocating temporary row storage.");

    sprintf(query, "INSERT INTO %s VALUES(", DEFAULT_TABLE);
    for (int i=0; i<n_fields; ++i) {
        char *value = strtok((i==0) ? row : NULL, DEFAULT_DELIMITER);
        strcat(query, value);
        if (i+1 < n_fields)
          strcat(query, ", ");
    }
   
    strcat(query, ");");

    if (sqlite3_exec(sql, query, NULL, NULL, &errmsg))
      ERR("Error inserting row: %s", errmsg);
}

static void load_data(FILE *csv, sqlite3 *sql, int n_fields)
{
    size_t len;
    char buffer[MAX_LINE_LENGTH];

    rewind(csv);

    while (!ferror(csv) && !feof(csv)) {
        char *line = fgets(buffer, MAX_LINE_LENGTH - 1, csv);
        if (!line)
          continue;    

        len = strlen(line);
        if (len == 0)
          continue;

        line[len] = '\0';
        line = trim(line);

        if (strlen(line) == 0 || IS_COMMENT(line[0]))
          continue;

        /* Not a comment... real line! */
        insert(sql, (const char *)line, n_fields);
    }
}

static void load_csv(FILE *csv, sqlite3 *sql)
{
    int n_fields;
    char **fields;
    
    /* Define the fields/columns of the database */
    fields = determine_columns(csv, &n_fields);

    create_table(sql, fields, n_fields);
    load_data(csv, sql, n_fields);
}

/* Callback for each result row */
static int row_callback(
    void  *print_header,
    int    n_cols,
    char **result,
    char **colname)
{
    /* If we haven't printed the header for this result set... do so now */
    if (*(_Bool *)print_header) {
        for (int i=0; i<n_cols; ++i)
          fprintf(stdout, "\t%s", colname[i]);

        /* Do not print for subsequent result rows. */
        fputc('\n', stdout);
        *(_Bool *)print_header = false;
    }

    for (int i=0; i<n_cols; ++i)
      fprintf(stdout, "\t%s", result[i]);
    fputc('\n', stdout);

    return 0;
}

/* Returns 'true' if a command is found */
static _Bool is_command(const char *line)
{
    for (int i=0; i<sizeof(commands)/sizeof(commands[0]); ++i) {
        if (strcmp(commands[i].name, line) == 0) {
            commands[i].callback();
            return true;
        }
    }

    return false;
}

/* Query loop utilizing readline for user input */
static void query_loop(sqlite3 *db)
{
    char *query, *errmsg;

    while ((query = readline(DEFAULT_PROMPT))) {
        _Bool print_header = true;
        if (is_command(query))
          continue;
        if (sqlite3_exec(db, query, row_callback,
                         (void *)&print_header, &errmsg)) {
            fprintf(stdout, "Error %s\n", errmsg);
            free(errmsg);
        }
        add_history(query);
    }
}

static void cmd_exit(void)
{
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    int         err;
    FILE       *csv_fp;
    sqlite3    *db;
    const char *fname;

    if (argc != 2)
      usage(argv[0]);

    fname = argv[1];

    /* Open the CSV */
    if (!(csv_fp = fopen(fname, "r"))) {
        fprintf(stderr, "Error opening .csv file: %s: %s\n",
                fname, strerror(errno));
        exit(errno);
    }

    /* Open a fresh DB */
    if ((err = sqlite3_open(DEFAULT_DB, &db))) {
        fprintf(stderr, "Error opening in-memory db: %s\n",
                sqlite3_errmsg(db));
        exit(EXIT_FAILURE);
    }

    /* Input the csv */
    load_csv(csv_fp, db);

    /* All errors up to this point are fatal... so if we are this far, then
     * assume everything is cool.
     */
    query_loop(db);

    sqlite3_close(db);
    fclose(csv_fp);

    return 0;
}
