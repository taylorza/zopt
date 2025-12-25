#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#ifdef __ZXNEXT
#include <arch/zxn.h>
#endif

#include "platform.h"
#include "dataarea.h"
#include "fileio.h"

int rule_count;
uint8_t paren_depth;

typedef struct TokenizedExpr TokenizedExpr;

/* Note: pattern/replacement line counts are always <= MAX_WINDOW_SIZE (<=255)
   so use uint8_t to save space and help the optimizer. */

/* Forward declarations for compiled-expression API */
TokenizedExpr* compile_expression(const char* expr, int lineno);
void free_tokenized_expr(TokenizedExpr* e);
int eval_tokenized(TokenizedExpr* e, char* bindings[10], int lineno);

typedef struct Rule {
    int lineno;
    char** pattern_lines;
    uint8_t pattern_linecount;
    char** replacement_lines;
    uint8_t replacement_linecount;
    TokenizedExpr* constraint_expr;
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
        error(ERROR_OUT_OF_MEMORY, 0);
        return NULL;
    }

    enum { STATE_START, STATE_IN_PATTERN, STATE_IN_REPLACEMENT, STATE_IN_CONSTRAINT } state = STATE_START;

    int current_lineno = 0;
    int rule_lineno = 0;
    char** pattern_lines = NULL;
    uint8_t pattern_linecount = 0;
    char** replacement_lines = NULL;
    uint8_t replacement_linecount = 0;
    TokenizedExpr* constraint_expr = NULL;
    rule_count = 0;
    while (read_line(fp, line, MAX_LINE_LENGTH) >= 0) {
        ++current_lineno;
        char* trimmed = trim(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;
        do {
            switch (state) {
                case STATE_START:
                    if (strncmp(trimmed, "pattern:", 8) != 0) error(ERROR_EXPECTED_REPLACEMENT_OR_CONSTRAINT, current_lineno);
                    state = STATE_IN_PATTERN;
                    rule_lineno = current_lineno;

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
                        if (pattern_linecount == MAX_WINDOW_SIZE) error(ERROR_TOO_MANY_LINES, current_lineno);
                        strcpy(window[pattern_linecount++], line);
                    }
                    else {
                        pattern_lines = malloc(pattern_linecount * sizeof(char*));
                        if (pattern_lines == NULL) error(ERROR_OUT_OF_MEMORY, current_lineno);
                        for (uint8_t i = 0; i < pattern_linecount; ++i)
                            pattern_lines[i] = hash(window[i]);
                    }
                    break;

                case STATE_IN_CONSTRAINT:
                    if (strncmp(trimmed, "replacement:", 12) == 0)
                        state = STATE_IN_REPLACEMENT;
                    else {
                        if (constraint_expr != NULL && strlen(trim(line)) != 0) error(ERROR_MULTILINE_CONSTRAINT, current_lineno);
                        constraint_expr = compile_expression(trimmed, current_lineno);
                    }
                    break;

                case STATE_IN_REPLACEMENT:
                    if (strncmp(trimmed, "pattern:", 8) == 0)
                        state = STATE_START;

                    if (state == STATE_IN_REPLACEMENT) {
                        if (pattern_linecount == MAX_WINDOW_SIZE) error(ERROR_TOO_MANY_LINES, current_lineno);
                        if (trimmed[0] == '-') {
                            strcpy(window[replacement_linecount++], hash(""));                            
                        } else {
                            strcpy(window[replacement_linecount++], line);
                        }
                    }
                    else {
                        replacement_lines = malloc(replacement_linecount * sizeof(char*));
                        if (replacement_lines == NULL) error(ERROR_OUT_OF_MEMORY, current_lineno);
                        for (uint8_t i = 0; i < replacement_linecount; ++i)
                            replacement_lines[i] = hash(window[i]);

                        Rule* rule = &rules[rule_count++];
                        rule->lineno = rule_lineno;
                        rule->pattern_lines = pattern_lines;
                        rule->pattern_linecount = pattern_linecount;
                        rule->replacement_lines = replacement_lines;
                        rule->replacement_linecount = replacement_linecount;
                        rule->constraint_expr = constraint_expr;

                        pattern_lines = NULL; pattern_linecount = 0;
                        replacement_lines = NULL; replacement_linecount = 0;
                        constraint_expr = NULL;
                    }
                    break;
            }
        } while (state == STATE_START);
    }

    if (pattern_linecount || replacement_linecount) {
        if (replacement_linecount == 0) error(ERROR_EXPECTED_REPLACEMENT_OR_CONSTRAINT, current_lineno);
        if (pattern_linecount == 0) error(ERROR_EXPECTED_PATTERN, current_lineno);

        replacement_lines = malloc(replacement_linecount * sizeof(char*));
        if (replacement_lines == NULL) error(ERROR_OUT_OF_MEMORY, current_lineno);
        for (uint8_t i = 0; i < replacement_linecount; ++i)
            replacement_lines[i] = hash(window[i]); 

        Rule* rule = &rules[rule_count++];
        rule->lineno = rule_lineno;
        rule->pattern_lines = pattern_lines;
        rule->pattern_linecount = pattern_linecount;
        rule->replacement_lines = replacement_lines;
        rule->replacement_linecount = replacement_linecount;
        rule->constraint_expr = constraint_expr;

        pattern_lines = NULL; pattern_linecount = 0;
        replacement_lines = NULL; replacement_linecount = 0;
        constraint_expr = NULL;
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
int token_lineno;

// Tokenized expression representation for compiled constraints
typedef struct {
    TokenType type;
    char* strval; /* interned string for literals */
    int intval;   /* numeric value or variable index */
} TokenEntry;

typedef struct TokenizedExpr {
    TokenEntry* entries;
    int count;
    int capacity;
} TokenizedExpr;

/* Compile an expression into a token array for fast repeated evaluation */
TokenizedExpr* compile_expression(const char* expr, int lineno);
void free_tokenized_expr(TokenizedExpr* e);
int eval_tokenized(TokenizedExpr* e, char* bindings[10], int lineno);

// Global pointer that tracks our current position in the input string.
static const char* tokptr = NULL;

void init_tokenizer(const char* str, int lineno) {
    tokptr = str;
    paren_depth = 0;
    token_lineno = lineno;
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
            if (*tokptr != terminator) error(ERROR_INVALID_EXPRESSION, token_lineno);
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
                /* Reduce repeated strcmp calls by routing based on first char */
                switch (token[0]) {
                    case 'i':
                        if (strcmp(token, "isnumeric") == 0) tok = tokIsNumeric;
                        else tok = tokLiteral;
                        break;
                    case 's':
                        if (strcmp(token, "startswith") == 0) tok = tokStartsWith;
                        else tok = tokLiteral;
                        break;
                    case 'a':
                        if (strcmp(token, "and") == 0) tok = tokAnd;
                        else tok = tokLiteral;
                        break;
                    case 'o':
                        if (strcmp(token, "or") == 0) tok = tokOr;
                        else tok = tokLiteral;
                        break;
                    case 'x':
                        if (strcmp(token, "xor") == 0) tok = tokXor;
                        else tok = tokLiteral;
                        break;
                    default:
                        tok = tokLiteral;
                        break;
                }
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
    if (top < 2) error(ERROR_INVALID_EXPRESSION, token_lineno);
    Value y = stack[--top];
    Value x = stack[--top];
    if ((x.vt == vtInt && y.vt == vtString) || (x.vt == vtString && y.vt == vtInt)) {
        switch (op) {
            case tokLt:
            case tokGt:
            case tokLe:
            case tokGe:
            case tokEq:
            case tokNe:
            {
                char leftbuf[32];
                char rightbuf[32];
                const char *ls, *rs;
                if (x.vt == vtInt) {
                    snprintf(leftbuf, sizeof(leftbuf), "%d", x.intval);
                    ls = leftbuf;
                    rs = y.strval;
                } else {
                    ls = x.strval;
                    snprintf(rightbuf, sizeof(rightbuf), "%d", y.intval);
                    rs = rightbuf;
                }
                int r = strcmp(ls, rs);
                switch (op) {
                    case tokLt: x.intval = r < 0; break;
                    case tokGt: x.intval = r > 0; break;
                    case tokLe: x.intval = r <= 0; break;
                    case tokGe: x.intval = r >= 0; break;
                    case tokEq: x.intval = r == 0; break;
                    case tokNe: x.intval = r != 0; break;
                }
                x.vt = vtInt;
                stack[top++] = x;
                return;
            }
            default:
                error(ERROR_INVALID_EXPRESSION, token_lineno);
        }
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
        error(ERROR_INVALID_EXPRESSION, token_lineno);
    }
    stack[top++] = x;
}

int eval_expression(const char* expr, char* bindings[10], int lineno) {
    Value v1, v2;
    top = 0;
    init_tokenizer(expr, lineno);
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
                if (id < 0 || id > 9) error(ERROR_INVALID_BINDING, lineno);
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

    if (top != 1 || stack[0].vt != vtInt) error(ERROR_INVALID_EXPRESSION, lineno);
    return stack[0].intval;
}

/* Compile expression into token entries */
TokenizedExpr* compile_expression(const char* expr, int lineno) {
    TokenizedExpr* e = malloc(sizeof(TokenizedExpr));
    if (!e) error(ERROR_OUT_OF_MEMORY, lineno);
    e->count = 0; e->capacity = 16;
    e->entries = malloc(e->capacity * sizeof(TokenEntry));
    if (!e->entries) error(ERROR_OUT_OF_MEMORY, lineno);

    init_tokenizer(expr, lineno);
    while (get_token() != tokEos) {
        TokenEntry te = {0};
        te.type = tok;
        te.strval = NULL;
        te.intval = 0;
        switch (tok) {
            case tokNumber:
                te.intval = atoi(token);
                break;
            case tokVariable:
                te.intval = token[0] - '0';
                break;
            case tokLiteral:
                te.strval = hash(token);
                break;
            case tokLParen:
            case tokRParen:
                continue;
            default:
                break;
        }
        if (e->count >= e->capacity) {
            e->capacity *= 2;
            e->entries = realloc(e->entries, e->capacity * sizeof(TokenEntry));
            if (!e->entries) error(ERROR_OUT_OF_MEMORY, lineno);
        }
        e->entries[e->count++] = te;
    }
    return e;
}

void free_tokenized_expr(TokenizedExpr* e) {
    if (!e) return;
    free(e->entries);
    free(e);
}

int eval_tokenized(TokenizedExpr* e, char* bindings[10], int lineno) {
    top = 0;
    token_lineno = lineno;
    for (uint16_t i = 0; i < e->count; ++i) {
        TokenEntry* te = &e->entries[i];
        switch (te->type) {
            case tokNumber: {
                Value v; v.vt = vtInt; v.intval = te->intval; stack[top++] = v;
            } break;
            case tokVariable: {
                int id = te->intval;
                if (id < 0 || id > 9) error(ERROR_INVALID_BINDING, lineno);
                Value v;
                if (is_numeric(bindings[id])) {
                    v.vt = vtInt; v.intval = atoi(bindings[id]);
                } else {
                    v.vt = vtString; v.strval = bindings[id];
                }
                stack[top++] = v;
            } break;
            case tokLiteral: {
                Value v; v.vt = vtString; v.strval = te->strval; stack[top++] = v;
            } break;
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
                eval_binop(te->type);
                break;
            case tokIsNumeric: {
                Value v1 = stack[--top];
                if (v1.vt == vtInt) v1.intval = 1;
                else if (is_numeric(v1.strval)) v1.intval = 1;
                else v1.intval = 0;
                v1.vt = vtInt;
                stack[top++] = v1;
            } break;
            case tokStartsWith: {
                Value v1 = stack[--top];
                Value v2 = stack[--top];
                Value vr;
                if (v1.vt == vtString && v2.vt == vtString) {
                    vr.vt = vtInt;
                    vr.intval = (strncmp(v2.strval, v1.strval, strlen(v1.strval)) == 0);
                } else {
                    vr.vt = vtInt; vr.intval = 0;
                }
                stack[top++] = vr;
            } break;
            case tokLParen:
            case tokRParen:
                /* parentheses ignored in RPN evaluation */
                break;
            default:
                error(ERROR_INVALID_EXPRESSION, lineno);
        }
    }
    if (top != 1 || stack[0].vt != vtInt) error(ERROR_INVALID_EXPRESSION, lineno);
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


uint8_t match_rule(Rule* rule, uint8_t window_size, char* bindings[10]) {
    uint8_t last_line = (rule->pattern_linecount < window_size ? rule->pattern_linecount : window_size);
    
    if (!match_pattern_line(rule->pattern_lines[0], window[0], bindings))
        return 0;
    if (last_line == 1) return 1;

    if (!match_pattern_line(rule->pattern_lines[last_line-1], window[last_line-1], bindings))
        return 0;
    
    for (uint8_t i = 1; i < last_line-1; ++i) {
        if (!match_pattern_line(rule->pattern_lines[i], window[i], bindings)) 
            return 0;    
    }
    return rule->pattern_linecount;
}

static void substitute_line(const char* templ, char* bindings[10], char* result, int lineno) {
    char* out = result;
    const char* p = templ;
    while (*p) {
        if (p[0] == '$') {
            if (isdigit((unsigned char)p[1])) {
                int index = p[1] - '0';
                if (bindings[index]) {
                    const char* s = bindings[index];
                    while (*s) *out++ = *s++;
                }
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

                if (paren_depth != 0) error(ERROR_INVALID_EXPRESSION, lineno);

                int expr_len = end - start;
                char expr[MAX_LINE_LENGTH + 1];
                if (expr_len > MAX_LINE_LENGTH)
                    expr_len = MAX_LINE_LENGTH;
                strncpy(expr, start, expr_len);
                expr[expr_len] = '\0';
                int evaluated = eval_expression(expr, bindings, lineno);
                char buf[64];
                sprintf(buf, "%d", evaluated);
                const char* s = buf;
                while (*s) *out++ = *s++;
                p = end;
            }
            else {
                *out++ = *p++;
            }
        }
        else {
            *out++ = *p++;
        }
    }
    *out = '\0';
}

void apply_replacement(Rule* rule, char** bindings) {
    for (uint8_t i = 0; i < rule->replacement_linecount; i++) {
        const char* line = rule->replacement_lines[i];
        const char* line_body = line;
        substitute_line(line_body, bindings, &tmp_line1[0], rule->lineno);
        strcpy(window[i], tmp_line1);
    }
}

void optimize(int8_t in_fd, int8_t out_fd, Rule* rules, uint8_t max_window_size) {
    uint8_t window_size = 0;

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
            uint8_t matched_line_count = match_rule(rule, window_size, bindings);
            if (matched_line_count) {
                uint8_t constraints_ok = 1;
                if (rule->constraint_expr) {
                    constraints_ok = eval_tokenized(rule->constraint_expr, bindings, rule->lineno);
                }
                if (constraints_ok) {
                    apply_replacement(rule, bindings);

                    // Count lines of the pattern that were not replaced
                    int count = (int)rule->pattern_linecount - (int)rule->replacement_linecount;

                    // scroll the window
                    {
                        int old_window_size = window_size;
                        int P = (int)rule->pattern_linecount;
                        int R = (int)rule->replacement_linecount;
                        window_size -= count;
                        int rows_to_move = old_window_size - P; /* rows after the pattern */
                        if (rows_to_move > 0) {
                            memmove(&window[R], &window[P], rows_to_move * sizeof(window[0]));
                        }
                    }

                    // fill window
                    while (window_size < max_window_size) {
                        int16_t n = read_line(in_fd, line, MAX_LINE_LENGTH);
                        if (n < 0) break;
                        strcpy(window[window_size++], line);
                    }
                    for (uint8_t i = window_size; i < max_window_size; ++i) {
                        window[i][0] = '\0';
                    }
                    r = -1; // restart rule matching
                }
            }
        }

        write_line(out_fd, window[0], strlen(window[0]));
        if (window_size > 1) {
            memmove(&window[0], &window[1], (window_size - 1) * sizeof(window[0]));
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

    uint8_t code_window = 0;
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
        free_tokenized_expr(rules[i].constraint_expr);
    }
    free(rules);
    return 0;
}