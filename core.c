#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>

#include "core.h"

/* abort-specific implementation details */
static jmp_buf *ABORT_JMP = NULL;

#define ABORT_OOM(subsys) abort(subsys ": out of memory")

static obj
OBJECT(unsigned short type, size_t varlen)
{
	obj o = calloc(1,sizeof(bigobj)+varlen);
	if (!o) ABORT_OOM("OBJECT"); /* LCOV_EXCL_LINE */
	o->type = type;
	return o;
}

static char*
lc(const char *s)
{
	char *p, *new = strdup(s);
	if (!new) ABORT_OOM("lc"); /* LCOV_EXCL_LINE */
	for (p = new; *p; p++) {
		if (isupper(*p)) *p = tolower(*p);
	}
	return new;
}

/**************************************************/

#define SYMBOL_TABLE_SIZE 211
obj SYMBOL_TABLE[SYMBOL_TABLE_SIZE];

/**************************************************/

void
_abort(const char *file, unsigned int line, const char *msg)
{
	/* LCOV_EXCL_START */
	if (ABORT_JMP) {
		longjmp(*ABORT_JMP, 42);
	} else {
		fprintf(stderr, "ABORT @%s:%u: %s\n", file, line, msg);
		fprintf(stderr, "So long, and thanks for all the fish!\n");
		exit(42);
	}
	/* LCOV_EXCL_STOP */
}

void on_abort(jmp_buf *jmp)
{
	ABORT_JMP = jmp;
}

/**  Initialization  ********************************************/

void
INIT(void)
{
	unsigned int i;
	for (i = 0; i < SYMBOL_TABLE_SIZE; i++) {
		SYMBOL_TABLE[i] = NIL;
	}
}

unsigned int
hash(const char *str, unsigned int lim)
{
	/* the positively stupidest hashing function you ever did meet */
	if (!str) return 0;
	return *str % lim;
}

#define OP_FN(x)    (x)->value.builtin

#define FNUM(x)     (x)->value.fixnum

#define CAR(x)      (x)->value.cons.car
#define CDR(x)      (x)->value.cons.cdr

#define SYM(x) (x)->value.sym.name

#define STR_N(s)    (s)->value.string.n
#define STR_LEN(s)  (s)->value.string.len
#define STR_VAL(s)  (s)->value.string.data
#define STR_LEFT(s) (STR_N(s) - STR_LEN(s))

#define IO_FD(io)   (io)->value.io.fd
#define IO_IDX(io)  (io)->value.io.i
#define IO_LEN(io)  (io)->value.io.len
#define IO_STR(io)  (io)->value.io.data


obj
globals(void)
{
	obj e = NIL;
	e = set(e, intern("+"), builtin(op_add));
	e = set(e, intern("-"), builtin(op_sub));
	e = set(e, intern("*"), builtin(op_mult));
	e = set(e, intern("/"), builtin(op_div));

	e = set(e, intern("eq"),    builtin(op_eq));
	e = set(e, intern("eql"),   builtin(op_eql));
	e = set(e, intern("equal"), builtin(op_equal));

	e = set(e, intern("prs"),   builtin(op_prs));
	return e;
}

obj
builtin(op_fn fn)
{
	obj op = OBJECT(OBJ_BUILTIN, 0);
	OP_FN(op) = fn;
	return op;
}

obj
eq(obj a, obj b)
{
	if (DEF(a) && DEF(b) && a == b) return T;
	return NIL;
}

obj
eql(obj a, obj b)
{
	if (IS_T(eq(a,b))) return T;
	if (TYPE(a) != TYPE(b)) return NIL;

	if (TYPE(a) == OBJ_FIXNUM) {
		return FNUM(a) == FNUM(b) ? T : NIL;
	}

	return NIL;
}

obj
equal(obj a, obj b)
{
	if (IS_T(eql(a,b))) return T;
	char *rep_a = cdump(a);
	char *rep_b = cdump(b);

	obj rc = (strcmp(rep_a, rep_b) == 0 ? T : NIL);
	free(rep_a);
	free(rep_b);
	return rc;
}

/**************************************************/

obj
get(obj env, obj sym)
{
	obj x;
	for_list(x, env) {
		if (IS_T(eq(car(car(x)), sym))) {
			return cdr(car(x));
		}
	}
	return UNDEF;
}

obj
set(obj env, obj sym, obj val)
{
	obj x;
	for_list(x, env) {
		if (IS_T(eq(car(car(x)), sym))) {
			/* FIXME: need setcdr! (and setcar) */
			CDR(car(x)) = val;
			return env;
		}
	}

	return cons(
		cons(sym, val),
		env
	);
}

/**************************************************/

char*
next_token(obj io)
{

	obj token = str_dupc("");
	char c;
next_char:
	switch (c = io_getc(io)) {
		case EOF:
			break;

		case ' ':
		case '\t':
		case '\r':
		case '\n':
			if (STR_LEN(token) > 0) break;
			goto next_char;

		case ';': /* Lisp-style comments */
			for (c = io_getc(io); c != EOF && c != '\n'; c = io_getc(io))
				;
			if (STR_LEN(token) > 0) break;
			goto next_char;

		case '(':
		case ')':
		case '`':
		case '\'':
			if (STR_LEN(token) > 0) {
				io_ungetc(io, c);
				break;
			}
			strc(token, c);
			break;

		default:
			strc(token, c);
			goto next_char;
	}

	if (STR_LEN(token) == 0) return NULL;
	return strdup(STR_VAL(token));
}

static obj
read_number(const char *token)
{
	/* FIXME: read_number should support negatives, imaginaries, ratios, decimals, arbitrary base, etc. */
	long fixn = 0;
	size_t i, l;
	for (i = 0, l = strlen(token); i < l; i++) {
		fixn = fixn*10 + (token[i]-'0');
	}
	return fixnum(fixn);
}

#define READ_STRING_MAX 100

static obj
read_string(obj io)
{
	obj s = str_dupc("");
	char c;
	int esc = 0;

again:
	c = io_getc(io);
	if (c == EOF || (c == '"' && !esc)) return s;

	if (esc) {
		switch (c) {
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			// default: fall-through (\", \', \\)
		}

		esc = 0;
	} else if (c == '\\') {
		esc = 1;
		goto again;
	}

	strc(s,c);
	goto again;
}

static obj
read_list(obj io)
{
	size_t nread = 1;
	obj first, next, rest;
	first = next = readx(io);

	if (next == CLOSE_PAREN) return NIL;
	if (next == CONS_DOT) abort("invalid form starting with (.");

	obj lst = NIL;
	for (; next != CLOSE_PAREN; next = readx(io)) {
		if (next == CONS_DOT) {
			if (nread == 1) {
				rest = readx(io);
				if (readx(io) == CLOSE_PAREN)
					return cons(first, rest);
			}
			abort("abuse of dotted notation!");
		}

		push(lst, next);
	}

	return revl(lst);
}

obj
readx(obj io)
{
	char *token, c;
	obj val;

	token = NULL;

again:
	switch (c = io_getc(io)) {
		case EOF:
			return NIL;

		case ' ':
		case '\t':
		case '\r':
		case '\n':
			goto again;

		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			io_ungetc(io, c);
			token = next_token(io);
			val = read_number(token);
			break;

		case '(':
			val = read_list(io);
			break;

		case ')':
			val = CLOSE_PAREN;
			break;

		case '.':
			val = CONS_DOT;
			break;

		case '"':
			val = read_string(io);
			break;

		case '\'':
			val = cons(intern("quote"), cons(readx(io), NIL));
			break;

		default:
			io_ungetc(io, c);
			token = next_token(io);
			val = intern(token);
			break;
	}

	free(token);
	return val;
}

static void vdump_x(obj str, obj what);
static void vdump_l(obj str, obj rest);

static void
vdump_l(obj str, obj rest)
{
	vdump_x(str, car(rest));
	if (!IS_NIL(cdr(rest))) {
		strc(str, ' ');
		if (IS_CONS(cdr(rest))) {
			vdump_l(str, cdr(rest));
		} else {
			strc(str, '.');
			strc(str, ' ');
			vdump_x(str, cdr(rest));
			strc(str, ')');
		}
	} else {
		strc(str, ')');
	}
}

static void
vdump_x(obj str, obj what)
{
	if (IS_T(what)) {
		strc(str, 't');
	} else if (IS_NIL(what)) {
		strx(str, "nil");
	} else {
		switch (TYPE(what)) {
			case OBJ_FIXNUM:
				strf(str, "%li", FNUM(what));
				break;

			case OBJ_CONS:
				strc(str, '(');
				vdump_l(str, what);
				break;

			case OBJ_SYMBOL:
				strf(str, "%s", SYM(what));
				break;

			case OBJ_STRING:
				/* FIXME: need to handle escaped characters... */
				strf(str, "\"%s\"", STR_VAL(what));
				break;

			case OBJ_BUILTIN:
				strf(str, "<#:builtin:%p:#>", what);
				break;

			default:
				strf(str, "<#:UNKNOWN:%x:%p:#>", TYPE(what), what);
				break;
		}
	}
}

obj
vdump(obj what)
{
	obj str = str_dupc("");
	vdump_x(str, what);
	return str;
}

char*
cdump(obj what)
{
	return STR_VAL(vdump(what));
}

/**************************************************/

obj
cons(obj car, obj cdr)
{
	obj c = OBJECT(OBJ_CONS, 0);
	CAR(c) = car;
	CDR(c) = cdr;
	return c;
}

obj
car(obj cons)
{
	if (!cons) abort("car() called with NULL cons");
	if (IS_NIL(cons)) return NIL;
	if (!IS_CONS(cons)) abort("car() called with non-cons arg");

	return CAR(cons);
}

obj
cdr(obj cons)
{
	if (!cons) abort("cdr() called with NULL cons");
	if (IS_NIL(cons)) return NIL;
	if (!IS_CONS(cons)) abort("cdr() called with non-cons arg");

	return CDR(cons);
}

obj
revl(obj lst)
{
	/* FIXME: type checking in revl! */
	obj t, new = NIL;
	for_list(t, lst) {
		push(new, car(t));
	}
	return new;
}

obj
nlist(size_t n, ...)
{
	va_list ap;
	obj lst = NIL;
	va_start(ap, n);

	for (; n > 0; n--) {
		/* this line is lcov-excluded b/c va_arg
		   has weird branching... */
		push(lst, va_arg(ap, obj)); /* LCOV_EXCL_LINE */
	}

	va_end(ap);
	return revl(lst);
}

obj
fixnum(long n)
{
	obj fixn = OBJECT(OBJ_FIXNUM, 0);
	FNUM(fixn) = n;
	return fixn;
}

/**  Symbols  ***************************************************/

#define symname(cons) (const char*)untag((car(cons)))

static obj
findsym(unsigned int key, const char *name)
{
	obj rest;
	for_list(rest, SYMBOL_TABLE[key]) {
		if (strcasecmp(name, SYM(car(rest))) == 0) {
			return car(rest);
		}
	}
	return NIL;
}

static obj
mksym(unsigned int key, const char *name)
{
	size_t len = strlen(name)+1;
	obj sym = OBJECT(OBJ_SYMBOL, len);
	strncpy(SYM(sym), name, len);

	push(SYMBOL_TABLE[key], sym);
	return sym;
}

obj
intern(const char *name)
{
	if (!name) return NIL;

	char *symname = lc(name);
	if (strcmp(symname, "nil") == 0) return NIL;
	if (strcmp(symname, "t") == 0)   return T;

	unsigned int key = hash(symname, SYMBOL_TABLE_SIZE);
	obj sym = findsym(key, symname);

	if (IS_NIL(sym)) {
		sym = mksym(key, symname);
	}

	free(symname);
	return sym;
}

/**  Evaluation  ************************************************/

obj
eval(obj args, obj env)
{
	/* self-evaluating stuff */
	if (IS_NIL(args)) return NIL;
	if (IS_T(args)) return T;

	if (IS_FIXNUM(args)) return args;
	if (IS_STRING(args)) return args;
	//if (IS_IO(args))     return args;

	/* symbol lookup */
	if (IS_SYMBOL(args)) {
		obj val = get(env, args);
		return DEF(val) ? val : NIL;
	}

	if (IS_CONS(args)) {
		obj fn = car(args);

		/* handle special forms like QUOTE and DO */
		if (fn == intern("quote")) {
			/* FIXME check args to quote */
			return car(cdr(args));
		} else if (fn == intern("do")) {
			obj x, result;
			for_list(x, args) {
				result = eval(car(x), env);
			}
			return result;
		}

		/* normal function application */
		fn = get(env, car(args));
		args = cdr(args);

		obj x;
		for_list(x, args) {
			CAR(x) = eval(car(x), env);
		}

		return op_apply(fn, args);
	}

	fprintf(stderr, "  can't eval %s\n", cdump(args));
	abort("eval not finished");
}

/**  String Handling  *******************************************/

#define STR_BLK(n) ((n) + STR_SEGMENT_SIZE - ((n) % STR_SEGMENT_SIZE))

static void
str_segm(obj s, size_t plus)
{
	if (STR_LEFT(s) > plus) return;

#if 0
	if (STR_VAL(s)) {
		fprintf(stderr, "STR_SEGM:grow %p from %lu/%lu to %lu/%lu\n",
				STR_VAL(s),
				STR_LEN(s), STR_N(s),
				STR_LEN(s)+plus,STR_N(s)+STR_BLK(plus));
	} else {
		fprintf(stderr, "STR_SEGM:init %p to %lu/%lu\n",
				STR_VAL(s),
				STR_LEN(s)+plus,STR_N(s)+STR_BLK(plus));
	}
#endif

	size_t l = STR_N(s) + STR_BLK(plus);
	char *re = realloc(STR_VAL(s), l+1);
	if (!re) ABORT_OOM("str_segm"); /* LCOV_EXCL_LINE */

	STR_N(s) = l;
	STR_VAL(s) = re;

	/* put a null-terminator at len+plus */
	STR_VAL(s)[STR_LEN(s)+plus] = '\0';
}

obj
str_dup(obj s)
{
	return str_dupc(cstr(s));
}

obj
str_dupc(const char *c)
{
	if (!c) return NIL;
	return str_dupb(c, strlen(c));
}

obj
str_dupb(const char *buf, size_t len)
{
	if (!buf) return NIL;
	obj s = OBJECT(OBJ_STRING, 0);

	str_segm(s, len);
	memcpy(STR_VAL(s), buf, len);
	STR_LEN(s) = len;
	return s;
}

obj
str_dupf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	size_t len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	obj s = OBJECT(OBJ_STRING, 0);
	str_segm(s, len);

	va_start(ap, fmt);
	vsnprintf(STR_VAL(s), len+1, fmt, ap);
	STR_LEN(s) = len;
	va_end(ap);

	return s;
}

obj
str_cat(obj dst, obj src)
{
	if (!IS_STRING(dst) || !IS_STRING(src)) return NIL;
	return str_dupf("%s%s", STR_VAL(dst), STR_VAL(src));
}

obj
str_catc(obj dst, const char *src)
{
	if (!IS_STRING(dst)) return NIL;
	if (!src) return str_dup(dst);
	return str_dupf("%s%s", STR_VAL(dst), src);
}

obj
str_catb(obj dst, const char *buf, size_t len)
{
	return str_cat(dst, str_dupb(buf, len));
}

obj
str_catf(obj dst, const char *fmt, ...)
{
	if (!IS_STRING(dst)) return NIL;

	va_list ap;
	va_start(ap, fmt);
	size_t len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	char *buf = calloc(len+1, sizeof(char));
	if (!buf) ABORT_OOM("str_catf"); /* LCOV_EXCL_LINE */

	va_start(ap, fmt);
	vsnprintf(buf, len+1, fmt, ap);
	va_end(ap);

	obj s = str_catc(dst, buf);
	free(buf);
	return s;
}

char*
cstr(obj s)
{
	if (!IS_STRING(s)) return NULL;
	return STR_VAL(s);
}

void
strx(obj dst, const char *src)
{
	if (!IS_STRING(dst)) return;

	size_t d_len = STR_LEN(dst);
	size_t s_len = strlen(src);

	str_segm(dst, s_len);
	strncpy(STR_VAL(dst)+d_len, src, s_len+1);
	STR_LEN(dst) += s_len;
}

void
strc(obj dst, char c)
{
	if (!IS_STRING(dst)) return;

	str_segm(dst, 1);
	STR_VAL(dst)[STR_LEN(dst)++] = c;
	STR_VAL(dst)[STR_LEN(dst)] = '\0';
}

void
strf(obj dst, const char *fmt, ...)
{
	if (!IS_STRING(dst)) return;

	va_list ap;
	va_start(ap, fmt);
	size_t len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	char *buf = calloc(len+1, sizeof(char));
	if (!buf) ABORT_OOM("strf"); /* LCOV_EXCL_LINE */

	va_start(ap, fmt);
	vsnprintf(buf, len+1, fmt, ap);
	va_end(ap);

	strx(dst, buf);
	free(buf);
}

/**  IO  ********************************************************/

obj
io_fdopen(FILE *file)
{
	obj io = OBJECT(OBJ_IO, 0);
	IO_STR(io) = NULL;
	IO_LEN(io) = IO_IDX(io) = 0;

	IO_FD(io) = file;
	return io;
}

obj
io_fopen(const char *path, const char *mode)
{
	FILE *fd = fopen(path, mode);
	return fd ? io_fdopen(fd) : NIL;
}

obj
io_string(const char *str)
{
	obj io = OBJECT(OBJ_IO, 0);
	IO_FD(io) = NULL;

	IO_STR(io) = strdup(str);
	IO_LEN(io) = strlen(str);
	IO_IDX(io) = 0;
	return io;
}

void
io_rewind(obj io)
{
	/* FIXME: check type of io and str! */
	if (IO_FD(io)) {
		fseek(IO_FD(io), 0, SEEK_SET);
	} else {
		IO_IDX(io) = 0;
	}
}

obj
io_close(obj io)
{
	/* FIXME: check type of io and str! */
	if (IO_FD(io)) {
		fclose(IO_FD(io));
		IO_FD(io) = NULL;
	}

	return T;
}

char
io_getc(obj io)
{
	/* FIXME: check type of io and str! */
	if (IO_FD(io)) {
		return fgetc(IO_FD(io));
	} else {
		if (IO_IDX(io) >= IO_LEN(io)) return EOF;
		return IO_STR(io)[IO_IDX(io)++];
	}

	return EOF;
}

char
io_ungetc(obj io, char c)
{
	/* FIXME: check type of io and str! */
	if (IO_FD(io)) {
		return ungetc(c, IO_FD(io));
	} else {
		if (IO_IDX(io) > 0) {
			IO_STR(io)[--IO_IDX(io)] = c;
			return c;
		}
	}

	return EOF;
}

obj
io_write_str(obj io, obj str)
{
	/* FIXME: check type of io and str! */
	if (IO_FD(io)) {
		int nc = fprintf(IO_FD(io), "%s", STR_VAL(str));
		if (nc < 0) return NIL;
		return T;
	} else {
		/* FIXME: io_write_str needs some work */
		free(IO_STR(io));
		IO_STR(io) = strdup(STR_VAL(str));
		IO_LEN(io) = strlen(IO_STR(io));
		IO_IDX(io) = IO_LEN(io);
		return T;
	}
	return NIL;
}

obj
io_read_buf(obj io, size_t n)
{
	/* FIXME: check type of io and str! */
	obj res = NIL;

	char *buf = calloc(n+1, sizeof(char));
	if (!buf) ABORT_OOM("io_read_buf"); /* LCOV_EXCL_LINE */

	if (IO_FD(io)) {
		if (fread(buf, 1, n, IO_FD(io)) > 0) {
			res = str_dupc(buf);
		}
		free(buf);

	} else {
		strncpy(buf, IO_STR(io)+IO_IDX(io), n);
		IO_LEN(io) += strlen(buf);

		res = str_dupc(buf);
		free(buf);
	}

	return res;
}

/**  Primitive Operators  ***************************************/

/* FIXME: math operations DON'T handle overflow well */

obj
op_add(obj args)
{
	long acc = 0;
	obj term;
	for_list(term, args) {
		/* FIXME: op_add: check types! */
		acc += FNUM(car(term));
	}
	return fixnum(acc);
}

obj
op_sub(obj args)
{
	long acc = 0;
	obj term;

	/* FIXME: op_sub: check types! */
	if (IS_NIL(args)) return fixnum(0);
	if (IS_NIL(cdr(args))) {
		acc = 0; /* (- 45) === (- 0 45) */
	} else {
		acc = FNUM(car(args));
		args = cdr(args);
	}

	for_list(term, args) {
		acc -= FNUM(car(term));
	}
	return fixnum(acc);
}

obj
op_mult(obj args)
{
	long acc = 1;
	obj term;
	for_list(term, args) {
		acc *= FNUM(car(term));
	}
	return fixnum(acc);
}

obj
op_div(obj args)
{
	long acc;
	obj term;

	/* FIXME: op_div: check types and arity (i.e. (/) -> ABORT) */
	if (IS_NIL(args)) abort("/: wrong number of arguments");
	if (IS_NIL(cdr(args))) {
		acc = 1; /* (/ 4) == 1/4 */
		abort("/: no ratio support; ergo, no (/ x) support -- go bug jrh");
	} else {
		acc = FNUM(car(args));
		args = cdr(args);
	}

	for_list(term, args) {
		acc /= FNUM(car(term));
	}
	return fixnum(acc);
}

obj
op_apply(obj fn, obj args)
{
	if (!fn) abort("fn cannot be NULL");
	if (!IS_BUILTIN(fn)) abort("fn is not a builtin");
	return (*(OP_FN(fn)))(args);
}

obj
op_eq(obj args)
{
	/* FIXME: check arity */
	return eq(
		car(args),
		car(cdr(args)));
}

obj
op_eql(obj args)
{
	/* FIXME: check arity */
	return eql(
		car(args),
		car(cdr(args)));
}

obj
op_equal(obj args)
{
	/* FIXME: check arity */
	return equal(
		car(args),
		car(cdr(args)));
}

obj
op_prs(obj args)
{
	obj STDOUT = io_fdopen(stdout);
	io_write_str(STDOUT, car(args));
	return T;
}
