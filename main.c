#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#ifdef __ZXNEXT
#include <arch/zxn.h>
#endif

#include "dataarea.h"
#include "fileio.h"

int rule_count;
uint8_t paren_depth;

typedef struct Rule {
    char** pattern_lines;
    int pattern_linecount;
    char** replacement_lines;
    int replacement_linecount;
    char* constraint;
} Rule;

Rule* parse_rules(const char* filename) {
    int8_t fp = open_file(filename);
    if (fp < 0) {
        printf("Error opening rule file: %s\n", filename);
        return NULL;
    }
    int capacity = 5;
    Rule* rules = malloc(capacity * sizeof(Rule));
    if (rules == NULL) {
        error(ERROR_OUT_OF_MEMORY);
        return NULL;
    }

    enum { STATE_START, STATE_IN_PATTERN, STATE_IN_REPLACEMENT, STATE_IN_CONSTRAINT } state = STATE_START;

    char** pattern_lines = NULL;
    int pattern_linecount = 0;
    char** replacement_lines = NULL;
    int replacement_linecount = 0;
    char* constraint = NULL;
    rule_count = 0;
    while (read_line(fp, line, MAX_LINE_LENGTH) >= 0) {
        char* trimmed = trim(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;
        do {
            switch (state) {
                case STATE_START:
                    if (strncmp(trimmed, "pattern:", 8) != 0) error(ERROR_EXPECTED_REPLACEMENT_OR_CONSTRAINT);
                    state = STATE_IN_PATTERN;

                    if (rule_count >= capacity) {
                        capacity *= 2;
                        rules = realloc(rules, capacity * sizeof(Rule));
                        if (rules == NULL) {
                            printf("Out of memory\n");
                            return NULL;
                        }
                    }
                    break;

                case STATE_IN_PATTERN:
                    if (strncmp(trimmed, "replacement:", 12) == 0)
                        state = STATE_IN_REPLACEMENT;
                    else if (strncmp(trimmed, "constraints:", 12) == 0)
                        state = STATE_IN_CONSTRAINT;

                    if (state == STATE_IN_PATTERN) {
                        if (pattern_linecount == MAX_WINDOW_SIZE) error(ERROR_TOO_MANY_LINES);
                        strcpy(window[pattern_linecount++], line);
                    }
                    else {
                        pattern_lines = malloc(pattern_linecount * sizeof(char*));
                        if (pattern_lines == NULL) error(ERROR_OUT_OF_MEMORY);
                        for (int i = 0; i < pattern_linecount; ++i)
                            pattern_lines[i] = hash(window[i]);
                    }
                    break;

                case STATE_IN_CONSTRAINT:
                    if (strncmp(trimmed, "replacement:", 12) == 0)
                        state = STATE_IN_REPLACEMENT;
                    else {
                        if (constraint != NULL && strlen(trim(line)) != 0) error(ERROR_MULTILINE_CONSTRAINT);
                        constraint = hash(trimmed);
                    }
                    break;

                case STATE_IN_REPLACEMENT:
                    if (strncmp(trimmed, "pattern:", 8) == 0)
                        state = STATE_START;

                    if (state == STATE_IN_REPLACEMENT) {
                        if (pattern_linecount == MAX_WINDOW_SIZE) error(ERROR_TOO_MANY_LINES);
                        strcpy(window[replacement_linecount++], line);
                    }
                    else {
                        replacement_lines = malloc(replacement_linecount * sizeof(char*));
                        if (replacement_lines == NULL) error(ERROR_OUT_OF_MEMORY);
                        for (int i = 0; i < replacement_linecount; ++i)
                            replacement_lines[i] = hash(window[i]);

                        Rule* rule = &rules[rule_count++];
                        rule->pattern_lines = pattern_lines;
                        rule->pattern_linecount = pattern_linecount;
                        rule->replacement_lines = replacement_lines;
                        rule->replacement_linecount = replacement_linecount;
                        rule->constraint = constraint;

                        pattern_lines = NULL; pattern_linecount = 0;
                        replacement_lines = NULL; replacement_linecount = 0;
                        constraint = NULL;
                    }
                    break;
            }
        } while (state == STATE_START);
    }

    if (rule_count) {
        replacement_lines = malloc(replacement_linecount * sizeof(char*));
        if (replacement_lines == NULL) error(ERROR_OUT_OF_MEMORY);
        for (int i = 0; i < replacement_linecount; ++i)
            replacement_lines[i] = hash(window[i]); 

        Rule* rule = &rules[rule_count++];
        rule->pattern_lines = pattern_lines;
        rule->pattern_linecount = pattern_linecount;
        rule->replacement_lines = replacement_lines;
        rule->replacement_linecount = replacement_linecount;
        rule->constraint = constraint;

        pattern_lines = NULL; pattern_linecount = 0;
        replacement_lines = NULL; replacement_linecount = 0;
        constraint = NULL;
    }

    return rules;
}

typedef enum { vtInt, vtString } ValueType;
typedef struct Value {
    ValueType vt;
    union {
        char* strval;
        int intval;
    };
} Value;

// Define the various token types.
typedef enum {
    tokNone,
    tokNumber,
    tokVariable,
    tokLiteral,
    tokLParen,
    tokRParen,
    tokIsNumeric,
    tokStartsWith,
    tokPlus,
    tokMinus,
    tokTimes,
    tokDivide,
    tokMod,
    tokLt,
    tokGt,
    tokLe,
    tokGe,
    tokEq,
    tokNe,
    tokAnd,
    tokOr,
    tokXor,
    tokEos,
} TokenType;

// Global array to hold the current token string.
char token[64];
TokenType tok;

// Global pointer that tracks our current position in the input string.
static const char* tokptr = NULL;

void init_tokenizer(const char* str) {
    tokptr = str;
    paren_depth = 0;
}

TokenType get_token(void) {
    tok = tokNone;
    char* temp = &token[0];

    // Skip whitespace.
    while (*tokptr && *tokptr == ' ') {
        tokptr++;
    }

    if (*tokptr == '\0') {
        return (tok = tokEos);
    }

    switch (*tokptr) {
        case '(': *temp++ = *tokptr++; tok = tokLParen; ++paren_depth; break;
        case ')': *temp++ = *tokptr++; tok = tokRParen; --paren_depth; break;
        case '+': *temp++ = *tokptr++; tok = tokPlus; break;
        case '-': *temp++ = *tokptr++; tok = tokMinus; break;
        case '*': *temp++ = *tokptr++; tok = tokTimes; break;
        case '/': *temp++ = *tokptr++; tok = tokDivide; break;
        case '%': *temp++ = *tokptr++; tok = tokMod; break;
        case '<': *temp++ = *tokptr++; tok = tokLt;
            if (*tokptr == '=') {
                *temp++ = *tokptr++;
                tok = tokLe;
            }
            else if (*tokptr == '>') {
                *temp++ = *tokptr++;
                tok = tokNe;
            }
            break;
        case '>': *temp++ = *tokptr++; tok = tokGt;
            if (*tokptr == '=') {
                *temp++ = *tokptr++;
                tok = tokGe;
            }
            break;
        case '=': *temp++ = *tokptr++; tok = tokEq; break;
        case '$':
            tokptr++; // skip '$'            
            if (*tokptr == '$') {
                *temp++ = '$';
                tokptr++; // skip '$'
                tok = tokLiteral;
            }
            else if (isdigit(*tokptr)) {
                while (*tokptr && isdigit(*tokptr)) {
                    *temp++ = *tokptr++;
                }
                tok = tokVariable;
            }
            break;
        case '"':
        case '\'':
        {
            char terminator = *tokptr;
            tokptr++; // skip quote
            while (*tokptr && *tokptr != terminator) {
                *temp++ = *tokptr++;
            }
            if (*tokptr != terminator) error(ERROR_INVALID_EXPRESSION);
            tokptr++; // skip closing quote
            tok = tokLiteral;
        }
        break;
        default:
            if (isdigit(*tokptr)) {
                while (*tokptr && isdigit(*tokptr)) {
                    *temp++ = *tokptr++;
                }
                tok = tokNumber;
            }
            else {
                while (*tokptr && *tokptr != ' ' && *tokptr != ')') {
                    *temp++ = *tokptr++;
                }
                *temp = '\0';
                if (strcmp(token, "isnumeric") == 0) tok = tokIsNumeric;
                else if (strcmp(token, "startswith") == 0) tok = tokStartsWith;
                else if (strcmp(token, "and") == 0) tok = tokAnd;
                else if (strcmp(token, "or") == 0) tok = tokOr;
                else if (strcmp(token, "xor") == 0) tok = tokXor;
                else tok = tokLiteral;
            }
            break;
    }
    *temp = '\0';

    return tok;
}



Value stack[10];
int top = 0;

int is_numeric(const char* s) {
    if (s == NULL || *s == '\0')
        return 0;
    const char* p = s;
    if (*p == '-' || *p == '+')
        p++;
    while (*p) {
        if (!isdigit((unsigned char)*p))
            return 0;
        p++;
    }
    return 1;
}

void eval_binop(TokenType op) {
    if (top < 2) error(ERROR_INVALID_EXPRESSION);
    Value y = stack[--top];
    Value x = stack[--top];

    char str[10];
    if (x.vt == vtInt && y.vt == vtString) {
        x.vt = vtString;
        x.strval = hash(itoa(x.intval, str, 10));
    }
    else if (x.vt == vtString && y.vt == vtInt) {
        y.vt = vtString;
        y.strval = hash(itoa(y.intval, str, 10));
    }

    if (x.vt == vtInt && y.vt == vtInt) {
        switch (op) {
            case tokPlus: x.intval = x.intval + y.intval; break;
            case tokMinus: x.intval = x.intval - y.intval; break;
            case tokTimes: x.intval = x.intval * y.intval; break;
            case tokDivide: x.intval = x.intval / y.intval; break;
            case tokMod: x.intval = x.intval % y.intval; break;
            case tokLt: x.intval = (x.intval < y.intval); break;
            case tokGt: x.intval = (x.intval > y.intval); break;
            case tokLe: x.intval = (x.intval <= y.intval); break;
            case tokGe: x.intval = (x.intval >= y.intval); break;
            case tokEq: x.intval = (x.intval == y.intval); break;
            case tokNe: x.intval = (x.intval != y.intval); break;
            case tokAnd: x.intval = (x.intval != 0) && (y.intval != 0); break;
            case tokOr: x.intval = (x.intval != 0) || (y.intval != 0); break;
            case tokXor: x.intval = (x.intval && !y.intval) || (!x.intval && y.intval); break;
        }
    }
    else if (x.vt == vtString && y.vt == vtString) {
        int r = strcmp(x.strval, y.strval);
        switch (op) {
            case tokLt: x.intval = r < 0; break;
            case tokGt: x.intval = r > 0; break;
            case tokLe: x.intval = (r <= 0); break;
            case tokGe: x.intval = (r >= 0); break;
            case tokEq: x.intval = (r == 0); break;
            case tokNe: x.intval = (r != 0); break;
        }
        x.vt = vtInt;
    }
    else {
        error(ERROR_INVALID_EXPRESSION);
    }
    stack[top++] = x;
}

int eval_expression(const char* expr, char* bindings[10]) {
    Value v1, v2;
    top = 0;
    init_tokenizer(expr);
    get_token();
    while (tok != tokEos) {
        switch (tok) {
            case tokNumber:
            {
                v1.vt = vtInt;
                v1.intval = atoi(token);
                stack[top++] = v1;
                get_token();
            }
            break;
            case tokVariable:
            {
                int id = token[0] - '0';
                if (id < 0 || id > 9) error(ERROR_INVALID_BINDING);
                if (is_numeric(bindings[id])) {
                    v1.vt = vtInt;
                    v1.intval = atoi(bindings[id]);
                    stack[top++] = v1;
                }
                else {
                    v1.vt = vtString;
                    v1.strval = bindings[id];
                    stack[top++] = v1;
                }
                get_token();
            }
            break;
            case tokLiteral:
                v1.vt = vtString;
                v1.strval = hash(token);
                stack[top++] = v1;
                get_token();
                break;
            case tokPlus:
            case tokMinus:
            case tokTimes:
            case tokDivide:
            case tokMod:
            case tokLt:
            case tokGt:
            case tokLe:
            case tokGe:
            case tokEq:
            case tokNe:
            case tokAnd:
            case tokOr:
            case tokXor:
                eval_binop(tok);
                get_token();
                break;
            case tokIsNumeric:
                v1 = stack[--top];
                if (v1.vt == vtInt) v1.intval = 1;
                else if (is_numeric(v1.strval)) v1.intval = 1;
                else v1.intval = 0;
                v1.vt = vtInt;
                stack[top++] = v1;
                get_token();
                break;
            case tokStartsWith:
                v1 = stack[--top];
                v2 = stack[--top];
                if (v1.vt == vtString && v2.vt == vtString) {
                    char* prefix = v1.strval;
                    char* str = v2.strval;
                    v1.intval = (strncmp(str, prefix, strlen(prefix)) == 0);
                    v1.vt = vtInt;
                    stack[top++] = v1;
                }
                else {
                    v1.vt = vtInt;
                    v1.intval = 0;
                    stack[top++] = v1;
                }
                get_token();
                break;
            case tokLParen:
            case tokRParen:
                get_token();
                break;
        }
    }

    if (top != 1 || stack[0].vt != vtInt) error(ERROR_INVALID_EXPRESSION);
    return stack[0].intval;
}

int match_pattern_line(const char* pattern, const char* line, char* bindings[10]) {
    const char* p = pattern;
    const char* l = line;

    while (*p) {
        while (*p == ' ') ++p;
        while (*l == ' ') ++l;
        if (p[0] == '$' && isdigit(p[1])) {
            int var_index = p[1] - '0';
            p += 2; // skip over "$n"
            /* Find next literal segment in pattern */
            const char* lit_start = p;
            while (*p && !(p[0] == '$' && isdigit(p[1])))
                p++;
            int lit_len = p - lit_start;
            if (lit_len > 0) {
                if (lit_len > MAX_LINE_LENGTH) lit_len = MAX_LINE_LENGTH;
                strncpy(tmp_line1, lit_start, lit_len);
            }
            tmp_line1[lit_len] = '\0';
            if (lit_len == 0) {
                /* No literal after the placeholder: grab the rest of the line */
                if (bindings[var_index]) {
                    if (strcmp(bindings[var_index], l) != 0)
                        return 0;
                }
                else {
                    bindings[var_index] = hash(l);
                }
                l += strlen(l);
            }
            else {
                char* pos = strstr(l, tmp_line1);
                if (!pos)
                    return 0;
                int var_len = pos - l;
                if (var_len > MAX_LINE_LENGTH) var_len = MAX_LINE_LENGTH;
                strncpy(tmp_line2, l, var_len);
                tmp_line2[var_len] = '\0';
                if (bindings[var_index]) {
                    if (strcmp(bindings[var_index], tmp_line2) != 0)
                        return 0;
                }
                else {
                    bindings[var_index] = hash(tmp_line2);
                }
                l = pos;
                if (strncmp(l, tmp_line1, lit_len) != 0)
                    return 0;
                l += lit_len;
            }
        }
        else {
            if (*p != *l)
                return 0;
            p++;
            l++;
        }
    }
    if (*l != '\0' && *l != '\n')
        return 0;
    return 1;
}


int match_rule(Rule* rule, int window_size, char* bindings[10]) {
    for (int i = 0; i < rule->pattern_linecount; ++i) {
        if (i >= window_size)
            return 0;

        if (!match_pattern_line(rule->pattern_lines[i], window[i], bindings))
            return 0;
    }
    return rule->pattern_linecount;
}

static void substitute_line(const char* templ, char* bindings[10], char* result) {
    result[0] = '\0';
    const char* p = templ;
    while (*p) {
        if (p[0] == '$') {
            if (isdigit(p[1])) {
                int index = p[1] - '0';
                if (bindings[index])
                    strcat(result, bindings[index]);
                p += 2;
            }
            else if (strncmp(p, "$eval(", 6) == 0) {
                const char* start = p + 6;
                const char* end = start;
                paren_depth++;
                while (*end && paren_depth) {
                    if (*end == '(') ++paren_depth;
                    else if (*end == ')') --paren_depth;
                    ++end;
                }

                if (paren_depth != 0) error(ERROR_INVALID_EXPRESSION);

                int expr_len = end - start;
                char expr[MAX_LINE_LENGTH + 1];
                if (expr_len > MAX_LINE_LENGTH)
                    expr_len = MAX_LINE_LENGTH;
                strncpy(expr, start, expr_len);
                expr[expr_len] = '\0';
                int evaluated = eval_expression(expr, bindings);
                char buf[64];
                sprintf(buf, "%d", evaluated);
                strcat(result, buf);
                p = end;
            }
            else {
                strncat(result, p, 1);
                p++;
            }
        }
        else {
            int rlen = strlen(result);
            result[rlen] = *p;
            result[rlen + 1] = '\0';
            p++;
        }
    }
}

void apply_replacement(Rule* rule, char** bindings) {
#ifdef __ZXNEXT
    zx_border(1);
#endif
    for (int i = 0; i < rule->replacement_linecount; i++) {
        const char* line = rule->replacement_lines[i];
        const char* line_body = line;
        substitute_line(line_body, bindings, &tmp_line1[0]);
        strcpy(window[i], tmp_line1);
    }
}

void optimize(int8_t in_fd, int8_t out_fd, Rule* rules, int max_window_size) {
    int window_size = 0;

    while (window_size < max_window_size) {
        int16_t n = read_line(in_fd, line, MAX_LINE_LENGTH);
        if (n < 0) break;
        strcpy(window[window_size++], line);
    }

    char* bindings[10];

    while (window_size > 0) {
#ifdef __ZXNEXT
        zx_border(0);
#endif      
        for (int r = 0; r < rule_count; ++r) {
            Rule* rule = &rules[r];

            memset(bindings, 0, sizeof(bindings));

            if (rule->pattern_linecount > window_size) continue;
            int matched_line_count = match_rule(rule, window_size, bindings);
            if (matched_line_count) {
                uint8_t constraints_ok = 1;
                if (rule->constraint) {
                    constraints_ok = eval_expression(rule->constraint, bindings);
                }
                if (constraints_ok) {
                    apply_replacement(rule, bindings);

                    // Count lines of the pattern that were not replaced
                    int count = rule->pattern_linecount - rule->replacement_linecount;

                    // scroll the window
                    window_size -= count;
                    for (int i = rule->replacement_linecount; i < window_size; ++i) {
                        strcpy(window[i], window[count + i]);
                    }

                    // fill window
                    while (window_size < max_window_size) {
                        int16_t n = read_line(in_fd, line, MAX_LINE_LENGTH);
                        if (n < 0) break;
                        strcpy(window[window_size++], line);
                    }
                    for (int i = window_size; i < max_window_size; ++i) {
                        window[i][0] = '\0';
                    }
                    r = -1;
                }
            }
        }

        write_line(out_fd, window[0], strlen(window[0]));
        for (int i = 1; i < window_size; ++i) {
            strcpy(window[i - 1], window[i]);
        }
        --window_size;
        int16_t n = read_line(in_fd, line, MAX_LINE_LENGTH);
        if (n >= 0)
            strcpy(window[window_size++], line);
    }
}

uint8_t old_speed;
uint8_t old_border;

void cleanup(void) {
#ifdef __ZXNEXT
    ZXN_NEXTREGA(0x07, old_speed);
    zx_border(old_border);
#endif
}

void init(void) {
    atexit(cleanup);
    init_file_io();
#ifdef __ZXNEXT
    old_speed = ZXN_READ_REG(0x07) & 0x03;
    old_border = ((*(uint8_t*)(0x5c48)) & 0b00111000) >> 3;
    ZXN_NEXTREG(0x07, 3);
#endif
}

int main(int argc, char** argv) {
    printf("ZOPT v0.1 (c)2025\nPeephole optimizer\n\n");
    if (argc < 2 || argc > 3) {
        printf("Usage:\n .zopt [rulefile] <asmfile>\n");
        printf("Default rule file:rules.opt\n\n");
        return 1;
    }

    init();
    const char* rule_filename;
    const char* input_filename;

    if (argc == 2) {
        rule_filename = "rules.opt";
        input_filename = argv[1];
    }
    else {
        rule_filename = argv[1];
        input_filename = argv[2];
    }

    strcpy(output_filename, input_filename);
    strcat(output_filename, ".tmp");

    Rule* rules = parse_rules(rule_filename);
    if (!rules) return 1;

    int code_window = 0;
    for (int i = 0; i < rule_count; ++i) {
        if (rules[i].pattern_linecount > code_window) code_window = rules[i].pattern_linecount;
    }

    int8_t in_fd = open_file(input_filename);
    if (in_fd < 0) {
        printf("Error opening input file\n");
        return 1;
    }

    int8_t out_fd = create_file(output_filename);
    if (out_fd < 0) {
        printf("Error creating output file\n");
        close_file(in_fd);
        return 1;
    }

    printf("Optimizing %s\n", input_filename);
    optimize(in_fd, out_fd, rules, code_window);

    close_file(in_fd);
    close_file(out_fd);

    delete_file(input_filename);
    rename_file(output_filename, input_filename);

    free_strtbl();
    for (int i = 0; i < rule_count; ++i) {
        free(rules[i].pattern_lines);
        free(rules[i].replacement_lines);
    }
    free(rules);
    return 0;
}