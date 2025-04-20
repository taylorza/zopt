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

char* trim(char* s) {
    char* end = s + strlen(s) - 1;

    while (s < end && *s == ' ')
        ++s;

    while (end >= s && *end == ' ')
        --end;
    end[1] = '\0';
    return s;
}

Rule* parse_rules(const char* filename) {
    int8_t fp = open_file(filename);
    if (fp < 0) {
        printf("Error opening rule file: %s\n", filename);
        return NULL;
    }
    int capacity = 5;
    Rule* rules = malloc(capacity * sizeof(Rule));
    if (rules == NULL) {
        printf("Out of memory\n");
        return NULL;
    }

    enum { STATE_START, STATE_IN_PATTERN, STATE_IN_REPLACEMENT, STATE_IN_CONSTRAINT } state = STATE_START;

    int pattern_capacity = 0;
    int replacement_capacity = 0;

    char** pattern_lines = NULL;
    int pattern_linecount = 0;
    char** replacement_lines = NULL;
    int replacement_linecount = 0;
    char* constraint = NULL;
    rule_count = 0;
    while (read_line(fp, line, MAX_LINE_LENGTH) >= 0) {
        char* trimmed = trim(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;

        if (strncmp(trimmed, "pattern:", 8) == 0) {
            if (rule_count >= capacity) {
                capacity *= 2;
                rules = realloc(rules, capacity * sizeof(Rule));
                if (rules == NULL) {
                    printf("Out of memory\n");
                    return NULL;
                }
            }

            if (rule_count) {
                Rule* rule = &rules[rule_count - 1];
                rule->pattern_lines = pattern_lines;
                rule->pattern_linecount = pattern_linecount;
                rule->replacement_lines = replacement_lines;
                rule->replacement_linecount = replacement_linecount;
                rule->constraint = constraint;

                pattern_lines = NULL; pattern_linecount = 0; pattern_capacity = 0;
                replacement_lines = NULL; replacement_linecount = 0; replacement_capacity = 0;
                constraint = NULL;
            }

            ++rule_count;
            state = STATE_IN_PATTERN;
            continue;
        }
        else if (strncmp(trimmed, "replacement:", 12) == 0) {
            state = STATE_IN_REPLACEMENT;
            continue;
        }
        else if (strncmp(trimmed, "constraints:", 12) == 0) {
            state = STATE_IN_CONSTRAINT;
            continue;
        }

        switch (state) {
            case STATE_IN_PATTERN:
                if (pattern_linecount >= pattern_capacity) {
                    pattern_capacity = (pattern_capacity == 0) ? 4 : pattern_capacity * 2;
                    pattern_lines = realloc(pattern_lines, pattern_capacity * sizeof(char*));
                    if (pattern_lines == NULL) {
                        printf("Out of memory\n");
                        return NULL;
                    }
                }
                pattern_lines[pattern_linecount++] = hash(trimmed);
                break;
            case STATE_IN_REPLACEMENT:
                if (replacement_linecount >= replacement_capacity) {
                    replacement_capacity = (replacement_capacity == 0) ? 4 : replacement_capacity * 2;
                    replacement_lines = realloc(replacement_lines, replacement_capacity * sizeof(char*));
                    if (replacement_lines == NULL) {
                        printf("Out of memory\n");
                        return NULL;
                    }
                }
                replacement_lines[replacement_linecount++] = hash(line);
                break;
            case STATE_IN_CONSTRAINT:
                if (constraint != NULL && strlen(trim(line)) != 0) {
                    printf("multi-line constrriant not supported\n");
                    return NULL;
                }
                constraint = hash(trimmed);
                break;
        }
    }
    if (rule_count) {
        Rule* rule = &rules[rule_count - 1];
        rule->pattern_lines = pattern_lines;
        rule->pattern_linecount = pattern_linecount;
        rule->replacement_lines = replacement_lines;
        rule->replacement_linecount = replacement_linecount;
        rule->constraint = constraint;
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
    tokPlus,
    tokMinus,
    tokTimes,
    tokDivide,
    tokMod,
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

void error(const char* msg) {
    printf(msg);
    exit(1);
}

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
            get_token(); // skip quote
            while (*tokptr && *tokptr != terminator) {
                *temp++ = *tokptr++;
            }
            if (*tokptr != terminator) error("invalid expression");
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
    if (top < 2) error("invalid expression");
    Value x = stack[--top];
    Value y = stack[--top];
    switch (op) {
        case tokPlus: x.intval = x.intval + y.intval; break;
        case tokMinus: x.intval = x.intval - y.intval; break;
        case tokTimes: x.intval = x.intval * y.intval; break;
        case tokDivide: x.intval = x.intval / y.intval; break;
        case tokMod: x.intval = x.intval % y.intval; break;
        case tokAnd: x.intval = (x.intval != 0) && (y.intval != 0); break;
        case tokOr: x.intval = (x.intval != 0) || (y.intval != 0); break;
        case tokXor: x.intval = (x.intval && !y.intval) || (!x.intval && y.intval); break;
    }
    stack[top++] = x;
}

int eval_expression(const char* expr, char* bindings[10]) {
    Value v;
    top = 0;
    init_tokenizer(expr);
    get_token();
    while (tok != tokEos) {
        switch (tok) {
            case tokNumber:
            {
                v.vt = vtInt;
                v.intval = atoi(token);
                stack[top++] = v;
                get_token();
            }
            break;
            case tokVariable:
            {
                int id = token[0] - '0';
                if (id < 0 || id > 9) error("invalid binding");
                if (is_numeric(bindings[id])) {
                    v.vt = vtInt;
                    v.intval = atoi(bindings[id]);
                    stack[top++] = v;
                }
                else {
                    v.vt = vtString;
                    v.intval = bindings[id];
                    stack[top++] = v;
                }
                get_token();
            }
            break;
            case tokLiteral:
                v.vt = vtString;
                v.strval = hash(token);
                stack[top++] = v;
                get_token();
                break;
            case tokPlus:
            case tokMinus:
            case tokTimes:
            case tokDivide:
            case tokMod:
            case tokAnd:
            case tokOr:
            case tokXor:
                eval_binop(tok);
                get_token();
                break;
            case tokIsNumeric:
                v = stack[--top];
                if (v.vt == vtInt) v.intval = 1;
                else if (is_numeric(v.strval)) v.intval = 1;
                else v.intval = 0;
                v.vt = vtInt;
                stack[top++] = v;
                get_token();
                break;
            case tokLParen:
            case tokRParen:
                get_token();
                break;
        }
    }

    if (top != 1 || stack[0].vt != vtInt) error("invalid expresison");
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
            char literal[MAX_LINE_LENGTH + 1];
            if (lit_len > 0) {
                if (lit_len > MAX_LINE_LENGTH) lit_len = MAX_LINE_LENGTH;
                strncpy(literal, lit_start, lit_len);
            }
            literal[lit_len] = '\0';
            if (lit_len == 0) {
                /* No literal after the placeholder: grab the rest of the line */
                if (bindings[var_index]) {
                    if (strcmp(bindings[var_index], l) != 0)
                        return 0;
                }
                else {
                    bindings[var_index] = _strdup(l);
                }
                l += strlen(l);
            }
            else {
                char* pos = strstr(l, literal);
                if (!pos)
                    return 0;
                int var_len = pos - l;
                char var_val[MAX_LINE_LENGTH + 1];
                if (var_len > MAX_LINE_LENGTH) var_len = MAX_LINE_LENGTH;
                strncpy(var_val, l, var_len);
                var_val[var_len] = '\0';
                if (bindings[var_index]) {
                    if (strcmp(bindings[var_index], var_val) != 0)
                        return 0;
                }
                else {
                    bindings[var_index] = _strdup(var_val);
                }
                l = pos;
                if (strncmp(l, literal, lit_len) != 0)
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


int match_rule(Rule* rule, char** window, int window_size, char* bindings[10]) {
    for (int i = 0; i < rule->pattern_linecount; ++i) {
        if (i >= window_size)
            return 0;

        if (!match_pattern_line(rule->pattern_lines[i], window[i], bindings))
            return 0;
    }
    return rule->pattern_linecount;
}

static void substitute_line(const char* templ, char* bindings[10], char *result) {
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
                while(*end && paren_depth) {
                    if(*end == '(') ++paren_depth;
                    else if(*end==')') --paren_depth;
                    ++end;
                }

                if (paren_depth != 0) error("invalid expression");                
                
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
                p = end + 1;                
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

void apply_replacement(Rule* rule, char** bindings, int8_t out_fd) {
    zx_border(1);
    for (int i = 0; i < rule->replacement_linecount; i++) {
        const char* line = rule->replacement_lines[i];
        const char* line_body = line;
        substitute_line(line_body, bindings, &tmp_line[0]);
        write_line(out_fd, tmp_line, strlen(tmp_line));        
    }
}

void optimize(int8_t in_fd, int8_t out_fd, Rule* rules, int max_window_size) {
    char** window = malloc(max_window_size * sizeof(char*));
    int window_size = 0;

    while (window_size < max_window_size) {
        int16_t n = read_line(in_fd, line, MAX_LINE_LENGTH);
        if (n < 0) break;
        window[window_size++] = _strdup(line);
    }

    while (window_size > 0) {
        zx_border(0);
        uint8_t rule_applied = 0;
        for (int r = 0; !rule_applied && r < rule_count; ++r) {
            // check we have enough lines in the code window
            if (rules[r].pattern_linecount > window_size) continue;
            char* bindings[10];
            memset(bindings, 0, sizeof(bindings));
            int matched_line_count = match_rule(&rules[r], window, window_size, bindings);
            if (matched_line_count) {
                uint8_t constraints_ok = 1;
                if (rules[r].constraint) {
                    constraints_ok = eval_expression(rules[r].constraint, bindings);
                }
                if (constraints_ok) {
                    apply_replacement(&rules[r], bindings, out_fd);

                    // free the consumed lines
                    for (int i = 0; i < matched_line_count; ++i)
                        free(window[i]);

                    // scroll the window
                    for (int i = matched_line_count; i < max_window_size; ++i) {
                        window[i - matched_line_count] = window[i];
                    }
                    window_size -= matched_line_count;

                    // fill window
                    while (window_size < max_window_size) {
                        int16_t n = read_line(in_fd, line, MAX_LINE_LENGTH);
                        if (n < 0) break;
                        window[window_size++] = _strdup(line);
                    }
                    for (int i = window_size; i < max_window_size; ++i) {
                        window[i] = NULL;
                    }
                    rule_applied = 1;
                }
                for (int b = 0; b < 10; ++b) {
                    if (bindings[b]) {
                        free(bindings[b]);
                        bindings[b] = NULL;
                    }
                }
            }
        }
        if (!rule_applied) {
            write_line(out_fd, window[0], strlen(window[0]));
            free(window[0]);
            for (int i = 1; i < window_size; ++i) {
                window[i - 1] = window[i];
            }
            --window_size;
            int16_t n = read_line(in_fd, line, MAX_LINE_LENGTH);
            if (n >= 0)
                window[window_size++] = _strdup(line);
        }
    }
    free(window);
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
    } else {
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