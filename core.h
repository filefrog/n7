#ifndef CORE_H
#define CORE_H

/**


       n7: a Lisp dialect for system programming


 **/

/*******************************************************

  TODO:

  LISP ESSENTIALS
  - support lambda
  - full reader
  - funcall stack
  - map out core operators

  DATA TYPES
  - vectors
  - strings
  - complex numbers
  - floating point numbers
  - ratios
  - bignums

  FFI
  - syscall interface

  COMPILING
  - tail-call optimization / CPS?
  - intermediate language (3-addr?)
  - code optimizer
  - machine code translation

  FUN STUFF TO ADD
  - something like Perl's topic var $_
    or Arc's 'it' pronoun.

********************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

#define TAGGED_BITS 8
#define TAGGED_MASK 0xf

#define TAG_POINTER   0x0   /* 0000 */
#define TAG_FIXNUM    0x1   /* 0001 */
#define TAG_CONSTANT  0x3   /* 0011 */

#define hitag(obj) ((unsigned long)(obj)&~TAGGED_MASK)
#define lotag(obj) ((unsigned long)(obj)& TAGGED_MASK)

#define OBJ_SYMBOL       0x01
#define OBJ_CONS         0x02
#define OBJ_FIXNUM       0x03
#define OBJ_BUILTIN      0x04

typedef struct big_object  bigobj;
typedef struct big_object* obj;

typedef obj (*op_fn)(obj);

struct big_object {
	unsigned short type;
	union {
		long fixnum;
		op_fn builtin;

		struct {
			size_t len;
			char name[];
		} sym;

		struct {
			obj car;
			obj cdr;
		} cons;

	} value;
};

#define IS_A(obj,tag) (lotag(obj) == 0x00 && (obj)->type == OBJ_ ## tag)
#define IS_CONS(obj) IS_A(obj,CONS)
#define IS_SYM(obj)  IS_A(obj,SYMBOL)
#define IS_FIXNUM(obj) IS_A(obj,FIXNUM)
#define IS_BUILTIN(obj) IS_A(obj,BUILTIN)

#define MAKE_CONSTANT(n) (struct big_object*)((n<<TAGGED_BITS)|TAG_CONSTANT)
#define NIL MAKE_CONSTANT(0)
#define T   MAKE_CONSTANT(1)

/* these are used internally by the parser */
#define CLOSE_PAREN MAKE_CONSTANT(33)
#define CONS_DOT    MAKE_CONSTANT(34)

#define IS_T(obj)   ((obj) == T)
#define IS_NIL(obj) ((obj) == NIL)

/**************************************************/

void on_abort(jmp_buf *jmp);
void _abort(const char *file, unsigned int line, const char *msg);
#define abort(m) _abort(__FILE__, __LINE__, (m))

/**************************************************/

void INIT(void);

unsigned int hash(const char *str, unsigned int lim);

/* read... */
obj readx(FILE *io);

/* consing to a better tomorrow */
obj cons(obj car, obj cdr);
obj car(obj cons);
obj cdr(obj cons);
obj revl(obj lst);
obj nlist(size_t n, ...);
#define push(ls,v) (ls) = cons((v),(ls))
#define for_list(obj, list) \
	for (obj = (list); !IS_NIL(obj); obj = cdr(obj))

obj fixnum(long n);

/* symbol manipulation */
obj intern(const char *name);

/* primitive ops */
obj op_add(obj args);
obj op_sub(obj args);
obj op_mult(obj args);
obj op_div(obj args);
obj op_apply(obj fn, obj args);

/* debugging; it happens to the best of us */
void dump_obj(const char *tag, obj o);
void dump_sym(obj sym);
void dump_syms(void);

#endif
