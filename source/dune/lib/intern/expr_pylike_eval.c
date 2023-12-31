/* Simple evaluator for a subset of Python expressions that can be
 * computed using purely double precision floating point vals.
 *
 * Supported subset:
 *
 *  - Ids use only ASCII chars.
 *  - Literals:
 *      floating point and decimal integer.
 *  - Constants:
 *      pi, True, False
 *  - Ops:
 *      +, -, *, /, ==, !=, <, <=, >, >=, and, or, not, ternary if
 *  - Fns:
 *      min, max, radians, degrees,
 *      abs, fabs, floor, ceil, trunc, int,
 *      sin, cos, tan, asin, acos, atan, atan2,
 *      exp, log, sqrt, pow, fmod
 *
 * The implementation has no global state and can be used multi-threaded. */

#include <ctype.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_alloc.h"
#include "lib_expr_pylike_eval.h"
#include "lib_math_base.h"
#include "lib_utildefines.h"

#ifdef _MSC_VER
#  pragma fenv_access(on)
#endif

/* Internal Types */
typedef enum eOpCode {
  /* Double constant: (-> dval). */
  OPCODE_CONST,
  /* 1 argument fn call: (a -> func1(a)). */
  OPCODE_FN1,
  /* 2 argument fn call: (a b -> func2(a,b)). */
  OPCODE_FN2,
  /* 3 argument fn call: (a b c -> func3(a,b,c)). */
  OPCODE_FN3,
  /* Parameter access: (-> params[ival]) */
  OPCODE_PARAM,
  /* Min of multiple inputs: (a b c... -> min); ival = arg count. */
  OPCODE_MIN,
  /* Max of multiple inputs: (a b c... -> max); ival = arg count. */
  OPCODE_MAX,
  /* Jump (pc += jmp_offset) */
  OPCODE_JMP,
  /* Pop and jump if zero: (a -> ); JUMP IF NOT a. */
  OPCODE_JMP_ELSE,
  /* Jump if nonzero, or pop: (a -> a JUMP) IF a ELSE (a -> ). */
  OPCODE_JMP_OR,
  /* Jump if zero, or pop: (a -> a JUMP) IF NOT a ELSE (a -> ). */
  OPCODE_JMP_AND,
  /* For comparison chaining: (a b -> 0 JUMP) IF NOT func2(a,b) ELSE (a b -> b). */
  OPCODE_CMP_CHAIN,
} eOpCode;

typedef double (*UnaryOpFunc)(double);
typedef double (*BinaryOpFunc)(double, double);
typedef double (*TernaryOpFunc)(double, double, double);

typedef struct ExprOp {
  eOpCode opcode;

  int jmp_offset;

  union {
    int ival;
    double dval;
    void *ptr;
    UnaryOpFn fn1;
    BinaryOpFn fn2;
    TernaryOpFn fn3;
  } arg;
} ExprOp;

struct ExprPyLike_Parsed {
  int ops_count;
  int max_stack;

  ExprOp ops[];
};

/* Public API */
void lib_expr_pylike_free(ExprPyLike_Parsed *expr)
{
  if (expr != NULL) {
    mem_free(expr);
  }
}

bool lib_expr_pylike_is_valid(ExprPyLike_Parsed *expr)
{
  return expr != NULL && expr->ops_count > 0;
}

bool lib_expr_pylike_is_constant(ExprPyLike_Parsed *expr)
{
  return expr != NULL && expr->ops_count == 1 && expr->ops[0].opcode == OPCODE_CONST;
}

bool lib_expr_pylike_is_using_param(ExprPyLike_Parsed *expr, int index)
{
  int i;

  if (expr == NULL) {
    return false;
  }

  for (i = 0; i < expr->ops_count; i++) {
    if (expr->ops[i].opcode == OPCODE_PARAMETER && expr->ops[i].arg.ival == index) {
      return true;
    }
  }

  return false;
}

/* Stack Machine Eval */
eExprPyLike_EvalStatus lib_expr_pylike_eval(ExprPyLike_Parsed *expr,
                                            const double *param_values,
                                            int param_values_len,
                                            double *r_result)
{
  *r_result = 0.0;

  if (!lib_expr_pylike_is_valid(expr)) {
    return EXPR_PYLIKE_INVALID;
  }

#define FAIL_IF(condition) \
  if (condition) { \
    return EXPR_PYLIKE_FATAL_ERROR; \
  } \
  ((void)0)

  /* Check the stack requirement is at least remotely sane and allocate on the actual stack. */
  FAIL_IF(expr->max_stack <= 0 || expr->max_stack > 1000);

  double *stack = lib_array_alloc(stack, expr->max_stack);

  /* Eval expression. */
  ExprOp *ops = expr->ops;
  int sp = 0, pc;

  feclearexcept(FE_ALL_EXCEPT);

  for (pc = 0; pc >= 0 && pc < expr->ops_count; pc++) {
    switch (ops[pc].opcode) {
      /* Arithmetic */
      case OPCODE_CONST:
        FAIL_IF(sp >= expr->max_stack);
        stack[sp++] = ops[pc].arg.dval;
        break;
      case OPCODE_PAR:
        FAIL_IF(sp >= expr->max_stack || ops[pc].arg.ival >= param_values_len);
        stack[sp++] = param_vals[ops[pc].arg.ival];
        break;
      case OPCODE_FN1:
        FAIL_IF(sp < 1);
        stack[sp - 1] = ops[pc].arg.func1(stack[sp - 1]);
        break;
      case OPCODE_FN2:
        FAIL_IF(sp < 2);
        stack[sp - 2] = ops[pc].arg.func2(stack[sp - 2], stack[sp - 1]);
        sp--;
        break;
      case OPCODE_FN3:
        FAIL_IF(sp < 3);
        stack[sp - 3] = ops[pc].arg.func3(stack[sp - 3], stack[sp - 2], stack[sp - 1]);
        sp -= 2;
        break;
      case OPCODE_MIN:
        FAIL_IF(sp < ops[pc].arg.ival);
        for (int j = 1; j < ops[pc].arg.ival; j++, sp--) {
          CLAMP_MAX(stack[sp - 2], stack[sp - 1]);
        }
        break;
      case OPCODE_MAX:
        FAIL_IF(sp < ops[pc].arg.ival);
        for (int j = 1; j < ops[pc].arg.ival; j++, sp--) {
          CLAMP_MIN(stack[sp - 2], stack[sp - 1]);
        }
        break;

      /* Jumps */
      case OPCODE_JMP:
        pc += ops[pc].jmp_offset;
        break;
      case OPCODE_JMP_ELSE:
        FAIL_IF(sp < 1);
        if (!stack[--sp]) {
          pc += ops[pc].jmp_offset;
        }
        break;
      case OPCODE_JMP_OR:
      case OPCODE_JMP_AND:
        FAIL_IF(sp < 1);
        if (!stack[sp - 1] == !(ops[pc].opcode == OPCODE_JMP_OR)) {
          pc += ops[pc].jmp_offset;
        }
        else {
          sp--;
        }
        break;

      /* For chaining comparisons, i.e. "a < b < c" as "a < b and b < c" */
      case OPCODE_CMP_CHAIN:
        FAIL_IF(sp < 2);
        /* If comparison fails, return 0 and jump to end. */
        if (!ops[pc].arg.fn2(stack[sp - 2], stack[sp - 1])) {
          stack[sp - 2] = 0.0;
          pc += ops[pc].jmp_offset;
        }
        /* Otherwise keep b on the stack and proceed. */
        else {
          stack[sp - 2] = stack[sp - 1];
        }
        sp--;
        break;

      default:
        return EXPR_PYLIKE_FATAL_ERROR;
    }
  }

  FAIL_IF(sp != 1 || pc != expr->ops_count);

#undef FAIL_IF

  *r_result = stack[0];

  /* Detect floating point eval errs. */
  int flags = fetestexcept(FE_DIVBYZERO | FE_INVALID);
  if (flags) {
    return (flags & FE_INVALID) ? EXPR_PYLIKE_MATH_ERROR : EXPR_PYLIKE_DIV_BY_ZERO;
  }

  return EXPR_PYLIKE_SUCCESS;
}

/* Built-In Ops */
static double op_negate(double arg)
{
  return -arg;
}

static double op_mul(double a, double b)
{
  return a * b;
}

static double op_div(double a, double b)
{
  return a / b;
}

static double op_add(double a, double b)
{
  return a + b;
}

static double op_sub(double a, double b)
{
  return a - b;
}

static double op_radians(double arg)
{
  return arg * M_PI / 180.0;
}

static double op_degrees(double arg)
{
  return arg * 180.0 / M_PI;
}

static double op_log2(double a, double b)
{
  return log(a) / log(b);
}

static double op_lerp(double a, double b, double x)
{
  return a * (1.0 - x) + b * x;
}

static double op_clamp(double arg)
{
  CLAMP(arg, 0.0, 1.0);
  return arg;
}

static double op_clamp3(double arg, double minv, double maxv)
{
  CLAMP(arg, minv, maxv);
  return arg;
}

static double op_smoothstep(double a, double b, double x)
{
  double t = (x - a) / (b - a);
  CLAMP(t, 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

static double op_not(double a)
{
  return a ? 0.0 : 1.0;
}

static double op_eq(double a, double b)
{
  return a == b ? 1.0 : 0.0;
}

static double op_ne(double a, double b)
{
  return a != b ? 1.0 : 0.0;
}

static double op_lt(double a, double b)
{
  return a < b ? 1.0 : 0.0;
}

static double op_le(double a, double b)
{
  return a <= b ? 1.0 : 0.0;
}

static double op_gt(double a, double b)
{
  return a > b ? 1.0 : 0.0;
}

static double op_ge(double a, double b)
{
  return a >= b ? 1.0 : 0.0;
}

typedef struct BuiltinConstDef {
  const char *name;
  double val;
} BuiltinConstDef;

static BuiltinConstDef builtin_consts[] = {
    {"pi", M_PI}, {"True", 1.0}, {"False", 0.0}, {NULL, 0.0}};

typedef struct BuiltinOpDef {
  const char *name;
  eOpCode op;
  void *fnptr;
} BuiltinOpDef;

#ifdef _MSC_VER
/* Prevent MSVC from inlining calls to ceil/floor so the table below can get a function pointer to
 * them. */
#  pragma fn(ceil)
#  pragma fn(floor)
#endif

static BuiltinOpDef builtin_ops[] = {
    {"radians", OPCODE_FN1, op_radians},
    {"degrees", OPCODE_FN1, op_degrees},
    {"abs", OPCODE_FN1, fabs},
    {"fabs", OPCODE_FN1, fabs},
    {"floor", OPCODE_FN1, floor},
    {"ceil", OPCODE_FN1, ceil},
    {"trunc", OPCODE_FN1, trunc},
    {"round", OPCODE_FN1, round},
    {"int", OPCODE_FN1, trunc},
    {"sin", OPCODE_FN1, sin},
    {"cos", OPCODE_FN1, cos},
    {"tan", OPCODE_FN1, tan},
    {"asin", OPCODE_FN1, asin},
    {"acos", OPCODE_FN1, acos},
    {"atan", OPCODE_FN1, atan},
    {"atan2", OPCODE_FN2, atan2},
    {"exp", OPCODE_FN1, exp},
    {"log", OPCODE_FN1, log},
    {"log", OPCODE_FN2, op_log2},
    {"sqrt", OPCODE_FN1, sqrt},
    {"pow", OPCODE_FN2, pow},
    {"fmod", OPCODE_FN2, fmod},
    {"lerp", OPCODE_FN3, op_lerp},
    {"clamp", OPCODE_FN1, op_clamp},
    {"clamp", OPCODE_FN3, op_clamp3},
    {"smoothstep", OPCODE_FN3, op_smoothstep},
    {NULL, OPCODE_CONST, NULL},
};

/* Expression Parser Stat */
#define MAKE_CHAR2(a, b) (((a) << 8) | (b))

#define CHECK_ERROR(condition) \
  if (!(condition)) { \
    return false; \
  } \
  ((void)0)

/* For simplicity simple token types are represented by their own char;
 * these are special ids for multi-char tokens. */
#define TOKEN_ID MAKE_CHAR2('I', 'D')
#define TOKEN_NUMBER MAKE_CHAR2('0', '0')
#define TOKEN_GE MAKE_CHAR2('>', '=')
#define TOKEN_LE MAKE_CHAR2('<', '=')
#define TOKEN_NE MAKE_CHAR2('!', '=')
#define TOKEN_EQ MAKE_CHAR2('=', '=')
#define TOKEN_AND MAKE_CHAR2('A', 'N')
#define TOKEN_OR MAKE_CHAR2('O', 'R')
#define TOKEN_NOT MAKE_CHAR2('N', 'O')
#define TOKEN_IF MAKE_CHAR2('I', 'F')
#define TOKEN_ELSE MAKE_CHAR2('E', 'L')

static const char *token_eq_chars = "!=><";
static const char *token_chars = "~`!@#$%^&*+-=/\\?:;<>(){}[]|.,\"'";

typedef struct KeywordTokenDef {
  const char *name;
  short token;
} KeywordTokenDef;

static KeywordTokenDef keyword_list[] = {
    {"and", TOKEN_AND},
    {"or", TOKEN_OR},
    {"not", TOKEN_NOT},
    {"if", TOKEN_IF},
    {"else", TOKEN_ELSE},
    {NULL, TOKEN_ID},
};

typedef struct ExprParseState {
  int param_names_len;
  const char **param_names;

  /* Original expression */
  const char *expr;
  const char *cur;

  /* Current token */
  short token;
  char *tokenbuf;
  double tokenval;

  /* Opcode buf */
  int ops_count, max_ops, last_jmp;
  ExprOp *ops;

  /* Stack space requirement tracking */
  int stack_ptr, max_stack;
} ExprParseState;

/* Reserve space for the specified num of ops in the buf. */
static ExprOp *parse_alloc_ops(ExprParseState *state, int count)
{
  if (state->ops_count + count > state->max_ops) {
    state->max_ops = power_of_2_max_i(state->ops_count + count);
    state->ops = mem_realloc(state->ops, state->max_ops * sizeof(ExprOp));
  }

  ExprOp *op = &state->ops[state->ops_count];
  state->ops_count += count;
  return op;
}

/* Add one op and track stack usage. */
static ExprOp *parse_add_op(ExprParseState *state, eOpCode code, int stack_delta)
{
  /* track eval stack depth */
  state->stack_ptr += stack_delta;
  CLAMP_MIN(state->stack_ptr, 0);
  CLAMP_MIN(state->max_stack, state->stack_ptr);

  /* alloc the new instruction */
  ExprOp *op = parse_alloc_ops(state, 1);
  memset(op, 0, sizeof(ExprOp));
  op->opcode = code;
  return op;
}

/* Add one jump op and return an index for parse_set_jump. */
static int parse_add_jump(ExprParseState *state, eOpCode code)
{
  parse_add_op(state, code, -1);
  return state->last_jmp = state->ops_count;
}

/* Set the jump offset in a prev added jump op. */
static void parse_set_jump(ExprParseState *state, int jump)
{
  state->last_jmp = state->ops_count;
  state->ops[jump - 1].jmp_offset = state->ops_count - jump;
}

/* Returns the required arg count of the given fn call code. */
static int opcode_arg_count(eOpCode code)
{
  switch (code) {
    case OPCODE_FN1:
      return 1;
    case OPCODE_FN2:
      return 2;
    case OPCODE_FN3:
      return 3;
    default:
      lib_assert_msg(0, "unexpected opcode");
      return -1;
  }
}

/* Add a fn call op, applying constant folding when possible. */
static bool parse_add_fn(ExprParseState *state, eOpCode code, int args, void *fnptr)
{
  ExprOp *prev_ops = &state->ops[state->ops_count];
  int jmp_gap = state->ops_count - state->last_jmp;

  feclearexcept(FE_ALL_EXCEPT);

  switch (code) {
    case OPCODE_FN1:
      CHECK_ERROR(args == 1);

      if (jmp_gap >= 1 && prev_ops[-1].opcode == OPCODE_CONST) {
        UnaryOpFn fn = fnptr;

        /* volatile bc some compilers overly aggressive optimize this call out.
         * see D6012 for details. */
        volatile double result = fn(prev_ops[-1].arg.dval);

        if (fetestexcept(FE_DIVBYZERO | FE_INVALID) == 0) {
          prev_ops[-1].arg.dval = result;
          return true;
        }
      }
      break;

    case OPCODE_FN2:
      CHECK_ERROR(args == 2);

      if (jmp_gap >= 2 && prev_ops[-2].opcode == OPCODE_CONST &&
          prev_ops[-1].opcode == OPCODE_CONST) {
        BinaryOpFn fn = fnptr;

        /* volatile bc some compilers overly aggressive optimize this call out.
         * see D6012 for details. */
        volatile double result = fn(prev_ops[-2].arg.dval, prev_ops[-1].arg.dval);

        if (fetestexcept(FE_DIVBYZERO | FE_INVALID) == 0) {
          prev_ops[-2].arg.dval = result;
          state->ops_count--;
          state->stack_ptr--;
          return true;
        }
      }
      break;

    case OPCODE_FN3:
      CHECK_ERROR(args == 3);

      if (jmp_gap >= 3 && prev_ops[-3].opcode == OPCODE_CONST &&
          prev_ops[-2].opcode == OPCODE_CONST && prev_ops[-1].opcode == OPCODE_CONST)
      {
        TernaryOpFn fn = funcptr;

        /* volatile bc some compilers overly aggressive optimize this call out.
         * see D6012 for details. */
        volatile double result = func(
            prev_ops[-3].arg.dval, prev_ops[-2].arg.dval, prev_ops[-1].arg.dval);

        if (fetestexcept(FE_DIVBYZERO | FE_INVALID) == 0) {
          prev_ops[-3].arg.dval = result;
          state->ops_count -= 2;
          state->stack_ptr -= 2;
          return true;
        }
      }
      break;

    default:
      lib_assert(false);
      return false;
  }

  parse_add_op(state, code, 1 - args)->arg.ptr = fnptr;
  return true;
}

/* Extract the next token from raw chars. */
static bool parse_next_token(ExprParseState *state)
{
  /* Skip white-space. */
  while (isspace(*state->cur)) {
    state->cur++;
  }

  /* End of string. */
  if (*state->cur == 0) {
    state->token = 0;
    return true;
  }

  /* Floating point nums. */
  if (isdigit(*state->cur) || (state->cur[0] == '.' && isdigit(state->cur[1]))) {
    char *end, *out = state->tokenbuf;
    bool is_float = false;

    while (isdigit(*state->cur)) {
      *out++ = *state->cur++;
    }

    if (*state->cur == '.') {
      is_float = true;
      *out++ = *state->cur++;

      while (isdigit(*state->cur)) {
        *out++ = *state->cur++;
      }
    }

    if (ELEM(*state->cur, 'e', 'E')) {
      is_float = true;
      *out++ = *state->cur++;

      if (ELEM(*state->cur, '+', '-')) {
        *out++ = *state->cur++;
      }

      CHECK_ERROR(isdigit(*state->cur));

      while (isdigit(*state->cur)) {
        *out++ = *state->cur++;
      }
    }

    *out = 0;

    /* Forbid C-style octal constants. */
    if (!is_float && state->tokenbuf[0] == '0') {
      for (char *p = state->tokenbuf + 1; *p; p++) {
        if (*p != '0') {
          return false;
        }
      }
    }

    state->token = TOKEN_NUMBER;
    state->tokenval = strtod(state->tokenbuf, &end);
    return (end == out);
  }

  /* ?= tokens */
  if (state->cur[1] == '=' && strchr(token_eq_chars, state->cur[0])) {
    state->token = MAKE_CHAR2(state->cur[0], state->cur[1]);
    state->cur += 2;
    return true;
  }

  /* Special chars (single char tokens) */
  if (strchr(token_chars, *state->cur)) {
    state->token = *state->cur++;
    return true;
  }

  /* Ids */
  if (isalpha(*state->cur) || ELEM(*state->cur, '_')) {
    char *out = state->tokenbuf;

    while (isalnum(*state->cur) || ELEM(*state->cur, '_')) {
      *out++ = *state->cur++;
    }

    *out = 0;

    for (int i = 0; keyword_list[i].name; i++) {
      if (STREQ(state->tokenbuf, keyword_list[i].name)) {
        state->token = keyword_list[i].token;
        return true;
      }
    }

    state->token = TOKEN_ID;
    return true;
  }

  return false;
}

/* Recursive Descent Parser */
static bool parse_expr(ExprParseState *state);

static int parse_fn_args(ExprParseState *state)
{
  if (!parse_next_token(state) || state->token != '(' || !parse_next_token(state)) {
    return -1;
  }

  int arg_count = 0;

  for (;;) {
    if (!parse_expr(state)) {
      return -1;
    }

    arg_count++;

    switch (state->token) {
      case ',':
        if (!parse_next_token(state)) {
          return -1;
        }
        break;

      case ')':
        if (!parse_next_token(state)) {
          return -1;
        }
        return arg_count;

      default:
        return -1;
    }
  }
}

static bool parse_unary(ExprParseState *state)
{
  int i;

  switch (state->token) {
    case '+':
      return parse_next_token(state) && parse_unary(state);

    case '-':
      CHECK_ERROR(parse_next_token(state) && parse_unary(state));
      parse_add_fn(state, OPCODE_FN1, 1, op_negate);
      return true;

    case '(':
      return parse_next_token(state) && parse_expr(state) && state->token == ')' &&
             parse_next_token(state);

    case TOKEN_NUMBER:
      parse_add_op(state, OPCODE_CONST, 1)->arg.dval = state->tokenval;
      return parse_next_token(state);

    case TOKEN_ID:
      /* Params: search in reverse order in case of dup names -
       * the last one should win. */
      for (i = state->param_names_len - 1; i >= 0; i--) {
        if (STREQ(state->tokenbuf, state->param_names[i])) {
          parse_add_op(state, OPCODE_PARAMETER, 1)->arg.ival = i;
          return parse_next_token(state);
        }
      }

      /* Ordinary builtin constants. */
      for (i = 0; builtin_consts[i].name; i++) {
        if (STREQ(state->tokenbuf, builtin_consts[i].name)) {
          parse_add_op(state, OPCODE_CONST, 1)->arg.dval = builtin_consts[i].value;
          return parse_next_token(state);
        }
      }

      /* Ordinary builtin fns. */
      for (i = 0; builtin_ops[i].name; i++) {
        if (STREQ(state->tokenbuf, builtin_ops[i].name)) {
          int args = parse_fn_args(state);

          /* Search for other arg count versions if necessary. */
          if (args != opcode_arg_count(builtin_ops[i].op)) {
            for (int j = i + 1; builtin_ops[j].name; j++) {
              if (opcode_arg_count(builtin_ops[j].op) == args &&
                  STREQ(builtin_ops[j].name, builtin_ops[i].name)) {
                i = j;
                break;
              }
            }
          }

          return parse_add_fn(state, builtin_ops[i].op, args, builtin_ops[i].funcptr);
        }
      }

      /* Specially supported fns. */
      if (STREQ(state->tokenbuf, "min")) {
        int count = parse_fn_args(state);
        CHECK_ERROR(count > 0);

        parse_add_op(state, OPCODE_MIN, 1 - count)->arg.ival = count;
        return true;
      }

      if (STREQ(state->tokenbuf, "max")) {
        int count = parse_fn_args(state);
        CHECK_ERROR(count > 0);

        parse_add_op(state, OPCODE_MAX, 1 - count)->arg.ival = count;
        return true;
      }

      return false;

    default:
      return false;
  }
}

static bool parse_mul(ExprParseState *state)
{
  CHECK_ERROR(parse_unary(state));

  for (;;) {
    switch (state->token) {
      case '*':
        CHECK_ERROR(parse_next_token(state) && parse_unary(state));
        parse_add_fn(state, OPCODE_FN2, 2, op_mul);
        break;

      case '/':
        CHECK_ERROR(parse_next_token(state) && parse_unary(state));
        parse_add_fn(state, OPCODE_FN2, 2, op_div);
        break;

      default:
        return true;
    }
  }
}

static bool parse_add(ExprParseState *state)
{
  CHECK_ERROR(parse_mul(state));

  for (;;) {
    switch (state->token) {
      case '+':
        CHECK_ERROR(parse_next_token(state) && parse_mul(state));
        parse_add_fn(state, OPCODE_FN2, 2, op_add);
        break;

      case '-':
        CHECK_ERROR(parse_next_token(state) && parse_mul(state));
        parse_add_fn(state, OPCODE_FUNC2, 2, op_sub);
        break;

      default:
        return true;
    }
  }
}

static BinaryOpFn parse_get_cmp_fn(short token)
{
  switch (token) {
    case TOKEN_EQ:
      return op_eq;
    case TOKEN_NE:
      return op_ne;
    case '>':
      return op_gt;
    case TOKEN_GE:
      return op_ge;
    case '<':
      return op_lt;
    case TOKEN_LE:
      return op_le;
    default:
      return NULL;
  }
}

static bool parse_cmp_chain(ExprParseState *state, BinaryOpFn cur_fn)
{
  BinaryOpFn next_fn = parse_get_cmp_fn(state->token);

  if (next_fn) {
    parse_add_op(state, OPCODE_CMP_CHAIN, -1)->arg.fn2 = cur_fn;
    int jump = state->last_jmp = state->ops_count;

    CHECK_ERROR(parse_next_token(state) && parse_add(state));
    CHECK_ERROR(parse_cmp_chain(state, next_fn));

    parse_set_jump(state, jump);
  }
  else {
    parse_add_fn(state, OPCODE_FN2, 2, cur_fn);
  }

  return true;
}

static bool parse_cmp(ExprParseState *state)
{
  CHECK_ERROR(parse_add(state));

  BinaryOpFn fn = parse_get_cmp_fn(state->token);

  if (fn) {
    CHECK_ERROR(parse_next_token(state) && parse_add(state));

    return parse_cmp_chain(state, fn);
  }

  return true;
}

static bool parse_not(ExprParseState *state)
{
  if (state->token == TOKEN_NOT) {
    CHECK_ERROR(parse_next_token(state) && parse_not(state));
    parse_add_fn(state, OPCODE_FN1, 1, op_not);
    return true;
  }

  return parse_cmp(state);
}

static bool parse_and(ExprParseState *state)
{
  CHECK_ERROR(parse_not(state));

  if (state->token == TOKEN_AND) {
    int jump = parse_add_jump(state, OPCODE_JMP_AND);

    CHECK_ERROR(parse_next_token(state) && parse_and(state));

    parse_set_jump(state, jump);
  }

  return true;
}

static bool parse_or(ExprParseState *state)
{
  CHECK_ERROR(parse_and(state));

  if (state->token == TOKEN_OR) {
    int jump = parse_add_jump(state, OPCODE_JMP_OR);

    CHECK_ERROR(parse_next_token(state) && parse_or(state));

    parse_set_jump(state, jump);
  }

  return true;
}

static bool parse_expr(ExprParseState *state)
{
  /* Tmp set the constant expression eval barrier */
  int prev_last_jmp = state->last_jmp;
  int start = state->last_jmp = state->ops_count;

  CHECK_ERROR(parse_or(state));

  if (state->token == TOKEN_IF) {
    /* Ternary IF expression in python requires swapping the
     * main body with condition, so stash the body opcodes. */
    int size = state->ops_count - start;
    int bytes = size * sizeof(ExprOp);

    ExprOp *body = mem_malloc(bytes, "driver if body");
    memcpy(body, state->ops + start, bytes);

    state->last_jmp = state->ops_count = start;
    state->stack_ptr--;

    /* Parse condition. */
    if (!parse_next_token(state) || !parse_or(state) || state->token != TOKEN_ELSE ||
        !parse_next_token(state))
    {
      mem_free(body);
      return false;
    }

    int jmp_else = parse_add_jump(state, OPCODE_JMP_ELSE);

    /* Add body back. */
    memcpy(parse_alloc_ops(state, size), body, bytes);
    mem_free(body);

    state->stack_ptr++;

    int jmp_end = parse_add_jump(state, OPCODE_JMP);

    /* Parse the else block. */
    parse_set_jump(state, jmp_else);

    CHECK_ERROR(parse_expr(state));

    parse_set_jump(state, jmp_end);
  }
  /* If no actual jumps happened, restore previous barrier */
  else if (state->last_jmp == start) {
    state->last_jmp = prev_last_jmp;
  }

  return true;
}

/* Main Parsing Fn */
ExprPyLike_Parsed *lib_expr_pylike_parse(const char *expression,
                                         const char **param_names,
                                         int param_names_len)
{
  /* Prep the parser state. */
  ExprParseState state;
  memset(&state, 0, sizeof(state));

  state.cur = state.expr = expression;

  state.param_names_len = param_names_len;
  state.param_names = param_names;

  state.tokenbuf = mem_malloc(strlen(expression) + 1, __func__);

  state.max_ops = 16;
  state.ops = mem_malloc(state.max_ops * sizeof(ExprOp), __func__);

  /* Parse the expression. */
  ExprPyLike_Parsed *expr;

  if (parse_next_token(&state) && parse_expr(&state) && state.token == 0) {
    lib_assert(state.stack_ptr == 1);

    int bytesize = sizeof(ExprPyLike_Parsed) + state.ops_count * sizeof(ExprOp);

    expr = mem_malloc(bytesize, "ExprPyLike_Parsed");
    expr->ops_count = state.ops_count;
    expr->max_stack = state.max_stack;

    memcpy(expr->ops, state.ops, state.ops_count * sizeof(ExprOp));
  }
  else {
    /* Always return a non-NULL object so that parse failure can be cached. */
    expr = mem_calloc(sizeof(ExprPyLike_Parsed), "ExprPyLike_Parsed(empty)");
  }

  mem_free(state.tokenbuf);
  mem_free(state.ops);
  return expr;
}

/** \} */
