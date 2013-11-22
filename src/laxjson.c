/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "laxjson.h"

#include <stdlib.h>
#include <assert.h>

#define WHITESPACE \
    ' ': \
    case '\t': \
    case '\n': \
    case '\f': \
    case '\r': \
    case 0xb

#define DIGIT \
    '0': \
    case '1': \
    case '2': \
    case '3': \
    case '4': \
    case '5': \
    case '6': \
    case '7': \
    case '8': \
    case '9'

#define ALPHANUMERIC \
    'a': \
    case 'b': \
    case 'c': \
    case 'd': \
    case 'e': \
    case 'f': \
    case 'g': \
    case 'h': \
    case 'i': \
    case 'j': \
    case 'k': \
    case 'l': \
    case 'm': \
    case 'n': \
    case 'o': \
    case 'p': \
    case 'q': \
    case 'r': \
    case 's': \
    case 't': \
    case 'u': \
    case 'v': \
    case 'w': \
    case 'x': \
    case 'y': \
    case 'z': \
    case 'A': \
    case 'B': \
    case 'C': \
    case 'D': \
    case 'E': \
    case 'F': \
    case 'G': \
    case 'H': \
    case 'I': \
    case 'J': \
    case 'K': \
    case 'L': \
    case 'M': \
    case 'N': \
    case 'O': \
    case 'P': \
    case 'Q': \
    case 'R': \
    case 'S': \
    case 'T': \
    case 'U': \
    case 'V': \
    case 'W': \
    case 'X': \
    case 'Y': \
    case 'Z': \
    case DIGIT

#define VALID_UNQUOTED \
    '-': \
    case '_': \
    case '#': \
    case '$': \
    case '%': \
    case '&': \
    case '<': \
    case '>': \
    case '=': \
    case '~': \
    case '|': \
    case '@': \
    case '?': \
    case ';': \
    case '.': \
    case '+': \
    case '*': \
    case '(': \
    case ')'
    

static const int HEX_MULT[] = {4096, 256, 16, 1};

static enum LaxJsonError push_state(struct LaxJsonContext *context, enum LaxJsonState state) {
    enum LaxJsonState *new_ptr;

    if (context->state_stack_index >= context->state_stack_size) {
        context->state_stack_size += 1024;
        if (context->state_stack_size > context->max_state_stack_size)
            return LaxJsonErrorExceededMaxStack;
        new_ptr = realloc(context->state_stack, context->state_stack_size);
        if (!new_ptr)
            return LaxJsonErrorNoMem;
        context->state_stack = new_ptr;
    }
    context->state_stack[context->state_stack_index] = state;
    context->state_stack_index += 1;
    return LaxJsonErrorNone;
}

struct LaxJsonContext *lax_json_create(void) {
    struct LaxJsonContext *context = calloc(1, sizeof(struct LaxJsonContext));

    context->value_buffer_size = 1024;
    context->value_buffer = malloc(context->value_buffer_size);
    /* TODO handle malloc returning NULL */
    if (!context->value_buffer) abort();

    context->state_stack_size = 1024;
    context->state_stack = malloc(context->state_stack_size);
    if (!context->state_stack) abort();

    context->line = 1;
    context->max_state_stack_size = 16384;
    context->max_value_buffer_size = 1048576; /* 1 MB */

    push_state(context, LaxJsonStateEnd);

    return context;
}

void lax_json_destroy(struct LaxJsonContext *context) {
    free(context->state_stack);
    free(context->value_buffer);
    free(context);
}

static void pop_state(struct LaxJsonContext *context) {
    context->state = context->state_stack[context->state_stack_index];
    context->state_stack_index -= 1;
    assert(context->state_stack_index >= 0);
}

static enum LaxJsonError buffer_char(struct LaxJsonContext *context, char c) {
    char *new_ptr;
    if (context->value_buffer_index >= context->value_buffer_size) {
        context->value_buffer_size += 16384;
        if (context->value_buffer_size > context->max_value_buffer_size)
            return LaxJsonErrorExceededMaxValueSize;
        new_ptr = realloc(context->value_buffer, context->value_buffer_size);
        if (!new_ptr)
            return LaxJsonErrorNoMem;
        context->value_buffer = new_ptr;
    }
    context->value_buffer[context->value_buffer_index] = c;
    context->value_buffer_index += 1;
    return LaxJsonErrorNone;
}

enum LaxJsonError lax_json_feed(struct LaxJsonContext *context, int size, char *data) {
#define PUSH_STATE(state) \
    err = push_state(context, state); \
    if (err) return err;
#define BUFFER_CHAR(c) \
    err = buffer_char(context, c); \
    if (err) return err;

    enum LaxJsonError err = LaxJsonErrorNone;
    int x;
    char *end;
    char c;
    for (end = data + size; data < end; data += 1) {
        c = *data;
        if (c == '\n') {
            context->line += 1;
            context->column = 1;
        } else {
            context->column += 1;
        }
        switch (context->state) {
            case LaxJsonStateEnd:
                switch (c) {
                    case WHITESPACE:
                        /* ignore */
                        break;
                    case '/':
                        context->state = LaxJsonStateCommentBegin;
                        PUSH_STATE(LaxJsonStateEnd);
                        break;
                    default:
                        return LaxJsonErrorExpectedEof;
                }
                break;
            case LaxJsonStateObject:
                switch (c) {
                    case WHITESPACE:
                    case ',':
                        /* do nothing except eat these characters */
                        break;
                    case '/':
                        context->state = LaxJsonStateCommentBegin;
                        PUSH_STATE(LaxJsonStateObject);
                        break;
                    case '"':
                    case '\'':
                        context->state = LaxJsonStateString;
                        context->value_buffer_index = 0;
                        context->delim = c;
                        context->string_type = LaxJsonTypeProperty;
                        PUSH_STATE(LaxJsonStateColon);
                        break;
                    case VALID_UNQUOTED:
                        context->state = LaxJsonStateBareProp;
                        context->value_buffer[0] = c;
                        context->value_buffer_index = 1;
                        context->delim = 0;
                        break;
                    case '}':
                        context->end(context, LaxJsonTypeObject);
                        pop_state(context);
                        break;
                    default:
                        return LaxJsonErrorUnexpectedChar;
                }
                break;
            case LaxJsonStateBareProp:
                switch (c) {
                    case VALID_UNQUOTED:
                        BUFFER_CHAR(c);
                        break;
                    case WHITESPACE:
                        BUFFER_CHAR('\0');
                        context->string(context, LaxJsonTypeProperty, context->value_buffer,
                                context->value_buffer_index);
                        context->state = LaxJsonStateColon;
                        break;
                    case ':':
                        BUFFER_CHAR('\0');
                        context->string(context, LaxJsonTypeProperty, context->value_buffer,
                                context->value_buffer_index);
                        context->state = LaxJsonStateValue;
                        context->string_type = LaxJsonTypeString;
                        PUSH_STATE(LaxJsonStateObject);
                        break;
                    default:
                        return LaxJsonErrorUnexpectedChar;
                }
                break;
            case LaxJsonStateString:
                if (c == context->delim) {
                    BUFFER_CHAR('\0');
                    context->string(context, context->string_type, context->value_buffer,
                            context->value_buffer_index);
                    pop_state(context);
                } else if (c == '\\') {
                    context->state = LaxJsonStateStringEscape;
                } else {
                    BUFFER_CHAR(c);
                }
                break;
            case LaxJsonStateStringEscape:
                switch (c) {
                    case '\'':
                    case '"':
                    case '/':
                    case '\\':
                        BUFFER_CHAR(c);
                        break;
                    case 'b':
                        BUFFER_CHAR('\b');
                        break;
                    case 'f':
                        BUFFER_CHAR('\f');
                        break;
                    case 'n':
                        BUFFER_CHAR('\n');
                        break;
                    case 'r':
                        BUFFER_CHAR('\r');
                        break;
                    case 't':
                        BUFFER_CHAR('\t');
                        break;
                    case 'u':
                        context->state = LaxJsonStateUnicodeEscape;
                        context->unicode_digit_index = 0;
                        context->unicode_point = 0;
                        break;
                }
                break;
            case LaxJsonStateUnicodeEscape:
                switch (c) {
                    case '0':
                        x = 0;
                        break;
                    case '1':
                        x = 1;
                        break;
                    case '2':
                        x = 2;
                        break;
                    case '3':
                        x = 3;
                        break;
                    case '4':
                        x = 4;
                        break;
                    case '5':
                        x = 5;
                        break;
                    case '6':
                        x = 6;
                        break;
                    case '7':
                        x = 7;
                        break;
                    case '8':
                        x = 8;
                        break;
                    case '9':
                        x = 9;
                        break;
                    case 'a':
                    case 'A':
                        x = 10;
                        break;
                    case 'b':
                    case 'B':
                        x = 11;
                        break;
                    case 'c':
                    case 'C':
                        x = 12;
                        break;
                    case 'd':
                    case 'D':
                        x = 13;
                        break;
                    case 'e':
                    case 'E':
                        x = 14;
                        break;
                    case 'f':
                    case 'F':
                        x = 15;
                        break;
                    default:
                        return LaxJsonErrorInvalidHexDigit;
                }
                context->unicode_point += x * HEX_MULT[context->unicode_digit_index];
                context->unicode_digit_index += 1;
                if (context->unicode_digit_index == 4) {
                    if (context->unicode_point <= 0x007f) {
                        /* 1 byte ascii */
                        BUFFER_CHAR((char)context->unicode_point);
                        context->state = LaxJsonStateString;
                    } else if (context->unicode_point <= 0x07ff) {
                        /* 2 bytes */
                        /* TODO */
                        abort();
                    } else if (context->unicode_point <= 0xffff) {
                        /* 3 bytes */
                        /* TODO */
                        abort();
                    } else if (context->unicode_point <= 0x1fffff) {
                        /* 4 bytes */
                        /* TODO */
                        abort();
                    } else if (context->unicode_point <= 0x3ffffff) {
                        /* 5 bytes */
                        /* TODO */
                        abort();
                    } else if (context->unicode_point <= 0x7fffffff) {
                        /* 6 bytes */
                        /* TODO */
                        abort();
                    } else {
                        return LaxJsonErrorInvalidUnicodePoint;
                    }
                }
                break;
            case LaxJsonStateColon:
                switch (c) {
                    case WHITESPACE:
                        /* ignore it */
                        break;
                    case '/':
                        context->state = LaxJsonStateCommentBegin;
                        PUSH_STATE(LaxJsonStateColon);
                        break;
                    case ':':
                        context->state = LaxJsonStateValue;
                        context->string_type = LaxJsonTypeString;
                        PUSH_STATE(LaxJsonStateObject);
                        break;
                    default:
                        return LaxJsonErrorExpectedColon;
                }
                break;
            case LaxJsonStateValue:
                switch (c) {
                    case WHITESPACE:
                        /* ignore */
                        break;
                    case '/':
                        context->state = LaxJsonStateCommentBegin;
                        PUSH_STATE(LaxJsonStateValue);
                        break;
                    case '{':
                        context->begin(context, LaxJsonTypeObject);
                        context->state = LaxJsonStateObject;
                        break;
                    case '[':
                        context->begin(context, LaxJsonTypeArray);
                        context->state = LaxJsonStateArray;
                        break;
                    case '\'':
                    case '"':
                        context->state = LaxJsonStateString;
                        context->delim = c;
                        context->value_buffer_index = 0;
                        break;
                    case '-':
                        context->state = LaxJsonStateNumber;
                        context->value_buffer[0] = c;
                        context->value_buffer_index = 1;
                        break;
                    case '+':
                        context->state = LaxJsonStateNumber;
                        context->value_buffer_index = 0;
                        break;
                    case DIGIT:
                        context->state = LaxJsonStateNumber;
                        context->value_buffer_index = 1;
                        context->value_buffer[0] = c;
                        break;
                    case 't':
                        context->primitive(context, LaxJsonTypeTrue);
                        context->state = LaxJsonStateExpect;
                        context->expected = "rue";
                        break;
                    case 'f':
                        context->primitive(context, LaxJsonTypeFalse);
                        context->state = LaxJsonStateExpect;
                        context->expected = "alse";
                        break;
                    case 'n':
                        context->primitive(context, LaxJsonTypeNull);
                        context->state = LaxJsonStateExpect;
                        context->expected = "ull";
                        break;
                    default:
                        return LaxJsonErrorUnexpectedChar;
                }
                break;
            case LaxJsonStateArray:
                switch (c) {
                    case WHITESPACE:
                    case ',':
                        /* ignore */
                        break;
                    case '/':
                        context->state = LaxJsonStateCommentBegin;
                        PUSH_STATE(LaxJsonStateArray);
                        break;
                    case ']':
                        pop_state(context);
                        break;
                    default:
                        context->state = LaxJsonStateValue;
                        PUSH_STATE(LaxJsonStateArray);

                        /* rewind 1 character */
                        data -= 1;
                        context->column -= 1;
                        continue;
                }
                break;
            case LaxJsonStateNumber:
                switch (c) {
                    case DIGIT:
                        BUFFER_CHAR(c);
                        break;
                    case '.':
                        BUFFER_CHAR(c);
                        context->state = LaxJsonStateNumberDecimal;
                        break;
                    default:
                        return LaxJsonErrorUnexpectedChar;
                }
                break;
            case LaxJsonStateNumberDecimal:
                switch (c) {
                    case DIGIT:
                        BUFFER_CHAR(c);
                        break;
                    case 'e':
                    case 'E':
                        BUFFER_CHAR('e');
                        context->state = LaxJsonStateNumberExponentSign;
                        break;
                    default:
                        return LaxJsonErrorUnexpectedChar;
                }
                break;
            case LaxJsonStateNumberExponentSign:
                switch (c) {
                    case '+':
                    case '-':
                        BUFFER_CHAR(c);
                        context->state = LaxJsonStateNumberExponent;
                        break;
                    default:
                        return LaxJsonErrorUnexpectedChar;
                }
            case LaxJsonStateNumberExponent:
                switch (c) {
                    case DIGIT:
                        BUFFER_CHAR(c);
                        break;
                    default:
                        BUFFER_CHAR('\0');
                        context->number(context, atof(context->value_buffer));
                        pop_state(context);

                        /* rewind 1 */
                        data -= 1;
                        context->column -= 1;
                        continue;
                }
            case LaxJsonStateExpect:
                if (c == *context->expected) {
                    context->expected += 1;
                    if (*context->expected == 0) {
                        pop_state(context);
                    }
                } else {
                    return LaxJsonErrorUnexpectedChar;
                }
                break;
            case LaxJsonStateCommentBegin:
                switch (c) {
                    case '/':
                        context->state = LaxJsonStateCommentLine;
                        break;
                    case '*':
                        context->state = LaxJsonStateCommentMultiLine;
                        break;
                    default:
                        return LaxJsonErrorUnexpectedChar;
                }
                break;
            case LaxJsonStateCommentLine:
                if (c == '\n')
                    pop_state(context);
                break;
            case LaxJsonStateCommentMultiLine:
                if (c == '*')
                    context->state = LaxJsonStateCommentMultiLineStar;
                break;
            case LaxJsonStateCommentMultiLineStar:
                if (c == '/')
                    pop_state(context);
                else
                    context->state = LaxJsonStateCommentMultiLine;
                break;
        }
    }
    return err;
}