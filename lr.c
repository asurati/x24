// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2023 Amol Surati

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* In order of appearance from A.2.1 Expressions */
const char *terminals[] = {
	"(",
	")",
	"_Generic",
	",",
	":",
	"default",
	"[",
	"]",
	".",
	"->",
	"++",
	"--",
	"sizeof",
	"alignof",
	"&",
	"*",
	"+",
	"-",
	"~",
	"!",
	"/",
	"%",
	"<<",
	">>",
	"<",
	">",
	"<=",
	">=",
	"==",
	"!=",
	"^",
	"|",
	"&&",
	"||",
	"?",
	":",
	"=",
	"*=",
	"/=",
	"%=",
	"+=",
	"-=",
	"<<=",
	">>=",
	"&=",
	"^=",
	"|=",
	";",
	"auto",
	"constexpr",
	"extern",
	"register",
	"static",
	"thread_local",
	"typedef",
	"void",
	"char",
	"short",
	"int",
	"long",
	"float",
	"double",
	"signed",
	"unsigned",
	"_BitInt",
	"bool",
	"_Complex",
	"_Decimal32",
	"_Decimal64",
	"_Decimal128",
	"{",
	"}",
	"struct",
	"union",
	"enum",
	"_Atomic",
	"typeof",
	"typeof_unqual",
	"const",
	"restrict",
	"volatile",
	"inline",
	"_Noreturn",
	"alignas",
	"static",
	"...",
	"static_assert",
	"::",
	"case",
	"if",
	"switch",
	"else",
	"while",
	"do",
	"for",
	"goto",
	"continue",
	"break",
	"return",
	"Identifier",
	"StringLiteral",
	"Constant",
	"epsilon",
};

struct rule {
	int	lhs;	/* index into elements to a non-terminal */
	int	rhs[64];	/* indices into elements */
	int num_rhs;
#if 0
	int	first[1024];
	int	num_firsts;
	bool	find_first_done;
#endif
	bool can_generate_epsilon;
	bool generate_epsilon_done;
};

/* directed but not necessary acyclic graph */
struct node {
	int	index;	/* our own element index */
	int outgoing[1024];
	int incoming[1024];
	int num_outgoing;
	int	num_incoming;
	bool	is_on_queue;
};

struct element {
	struct node node;

	int	index;
	bool	is_terminal;
	const char	*name;

	struct rule	rules[32];
	int num_rules;

	int	first[1024];
	int	num_firsts;

	bool can_generate_epsilon;
	bool generate_epsilon_done;
};

struct element elements[1024];
int num_elements;


static
void print_rule(const struct rule *r)
{
	int i;
	const struct element *e;

	e = &elements[r->lhs];
	printf("%s(%d,%d):", e->name,
		   r->generate_epsilon_done, r->can_generate_epsilon);

	for (i = 0; i < r->num_rhs; ++i) {
		e = &elements[r->rhs[i]];
		printf(" %s", e->name);
	}
	printf("\n");
}

static
void print_element(const int index)
{
	int i;
	const struct element *e;

	e = &elements[index];
	if (e->is_terminal)
		return;
	for (i = 0; i < e->num_rules; ++i)
		print_rule(&e->rules[i]);

	/* firsts */
	printf("%s firsts:", e->name);
	for (i = 0; i < e->num_firsts; ++i) {
		printf(" %s", elements[e->first[i]].name);
		if (i != e->num_firsts - 1)
			printf(",");
	}
	printf("\n");
	printf("\n");
}

static
int find_element(const char *name)
{
	int i;

	for (i = 0; i < num_elements; ++i)
		if (!strcmp(name, elements[i].name))
			return i;
	return -1;
}

/* Terminals: Identifier, StringLiteral, Constant, epsilon */
static
bool is_terminal(const char *name)
{
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(terminals); ++i)
		if (!strcmp(name, terminals[i]))
			return true;
	return false;
}

int add_element(const char *name)
{
	int i;
	struct element *e;

	i = find_element(name);
	if (i >= 0)
		return i;

	/* not found */
	e = &elements[num_elements++];
	e->is_terminal = is_terminal(name);
	e->index = num_elements - 1;
	e->name = name;
	e->generate_epsilon_done = false;
	e->num_rules = 0;
	e->num_firsts = 0;
	e->node.index = e->index;
	e->node.num_incoming = e->node.num_outgoing = 0;
	e->node.is_on_queue = false;
	return e->index;
}

static
void calc_generate_epsilon()
{
	int i, j, k, num_rules_done;
	bool progress;
	struct rule *r;
	struct element *e, *te;

	while (true) {
		progress = false;
		for (i = 0; i < num_elements; ++i) {
			e = &elements[i];
			if (e->generate_epsilon_done)
				continue;
			assert(e->num_rules);
			assert(!e->is_terminal);
			num_rules_done = 0;
			for (j = 0; j < e->num_rules; ++j) {
				r = &e->rules[j];
				assert(r->lhs == e->index);
				if (r->generate_epsilon_done) {
					++num_rules_done;
					continue;
				}
				/*
				 * If any rhs cannot generate epsilon, this rule can't gen
				 * epsilon.
				 * If all rhs can generate epsilon, this rule can gen
				 * epsilon.
				 */
				for (k = 0; k < r->num_rhs; ++k) {
					te = &elements[r->rhs[k]];
					if (!te->generate_epsilon_done)
						continue;
					if (te->can_generate_epsilon)
						continue;
					/* Found a rhs that cannot generate eps. */
					r->generate_epsilon_done = true;
					r->can_generate_epsilon = false;
					break;
				}

				if (r->generate_epsilon_done) {
					progress = true;
					++num_rules_done;
					continue;
				}

				for (k = 0; k < r->num_rhs; ++k) {
					te = &elements[r->rhs[k]];
					if (!te->generate_epsilon_done)
						break;
					if (!te->can_generate_epsilon)
						break;
				}
				if (k < r->num_rhs)
					continue;

				/* All rhs can gen epsilon */
				assert(k == r->num_rhs);
				progress = true;
				++num_rules_done;
				r->generate_epsilon_done = true;
				r->can_generate_epsilon = true;
			}
			if (num_rules_done < e->num_rules)
				continue;

			/* ele can be satisfied */
			progress = true;
			e->generate_epsilon_done = true;
			e->can_generate_epsilon = false;
			for (j = 0; j < e->num_rules; ++j) {
				r = &e->rules[j];
				assert(r->generate_epsilon_done);
				if (r->can_generate_epsilon == false)
					continue;
				e->can_generate_epsilon = true;
				break;
			}
		}
		if (!progress)
			break;
	}
}

static
bool add_first(int *first, int *num_firsts,
			   const int *sfirsts, const int snum_firsts)
{
	int i, j;
	bool added = false;

	for (i = 0; i < snum_firsts; ++i) {
		for (j = 0; j < num_firsts[0]; ++j) {
			if (first[j] == sfirsts[i])
				break;
		}
		if (j < num_firsts[0])
			continue;
		assert(num_firsts[0] < 1024);
		first[num_firsts[0]++] = sfirsts[i];
		added = true;
	}
	return added;
}

static
void add_edge(struct element *from,
			  struct element *to)
{
	int i;

	for (i = 0; i < from->node.num_outgoing; ++i) {
		if (from->node.outgoing[i] == to->index)
			break;
	}
	if (i == from->node.num_outgoing) {
		from->node.outgoing[i++] = to->index;
		from->node.num_outgoing = i;
	}

	for (i = 0; i < to->node.num_incoming; ++i) {
		if (to->node.incoming[i] == from->index)
			break;
	}
	if (i == to->node.num_incoming) {
		to->node.incoming[i++] = from->index;
		to->node.num_incoming = i;
	}
}

/* Terminals won't have any outgoing edges */
static
void build_find_first_graph()
{
	int i, j, k;
	struct element *e, *te;
	struct rule *r;

	for (i = 0; i < num_elements; ++i) {
		e = &elements[i];
		if (e->is_terminal)
			continue;
		assert(e->num_rules);
		for (j = 0; j < e->num_rules; ++j) {
			r = &e->rules[j];

			/*
			 * e0:e1 e2 e3
			 * add e0->e1
			 */
			assert(r->num_rhs);
			for (k = 0; k < r->num_rhs; ++k) {
				te = &elements[r->rhs[k]];
				if (te == e)
					continue;
				add_edge(e, te);

				// if te cannot gen epsilon, done.
				assert(te->generate_epsilon_done);
				if (te->can_generate_epsilon == false)
					break;
			}
		}
	}
}

struct queue {
	int queue[1024];
	int read, num_elements;
};

static
void q_add(struct queue *q, struct element *e)
{
	int pos;

	if (e->node.is_on_queue)
		return;
	e->node.is_on_queue = true;
	assert(q->num_elements < 1024);
	pos = (q->read + q->num_elements) % 1024;
	q->queue[pos] = e->index;
	++q->num_elements;
}

static
int q_rem(struct queue *q)
{
	int index;
	assert(q->num_elements > 0);
	index = q->queue[q->read];
	q->read = (q->read + 1) % 1024;
	--q->num_elements;
	return index;
}

static
void find_first_bfs()
{
	int i;
	static struct queue q;
	struct element *e, *te;
	bool res;

	q.read = q.num_elements = 0;

	for (i = 0; i < num_elements; ++i) {
		e = &elements[i];
		if (!e->is_terminal)
			continue;
		assert(e->node.num_outgoing == 0);
		//printf("adding %d %s\n", e->index, e->name);
		q_add(&q, e);
	}

	while (q.num_elements) {
		e = &elements[q_rem(&q)];
		assert(e);

		e->node.is_on_queue = false;

		/* For all nodes for which e has incoming edges, update them */
		for (i = 0; i < e->node.num_incoming; ++i) {
			te = &elements[e->node.incoming[i]];
			if (te == e)
				continue;

			res = add_first(te->first, &te->num_firsts, e->first,
							e->num_firsts);

			/* If fresh data added, and te has incoming and te is not on queue,
			 * place te on q
			 */
			if (res && te->node.num_incoming && !te->node.is_on_queue)
				q_add(&q, te);
		}
	}
}

static
int cmpfunc(const void *p0, const void *p1)
{
	const struct element *e[2];
#if 0
	int a[2];
	a[0] = *(int *)p0;
	a[1] = *(int *)p1;
	return a[0] - a[1];
#endif
	e[0] = &elements[*(int *)p0];
	e[1] = &elements[*(int *)p1];
	return strcmp(e[0]->name, e[1]->name);
}

int main(int argc, char **argv)
{
	int i, err, j, len, lhs, epsilon;
	FILE *file;
	static char line[4096];
	struct element *e;
	struct rule *r;
	char *str;

	if (argc != 2) {
		fprintf(stderr, "%s: Usage: %s grammar.txt\n", __func__, argv[0]);
		return -1;
	}

	file = fopen(argv[1], "r");
	if (file == NULL) {
		fprintf(stderr, "%s: Error: Opening %s\n", __func__, argv[1]);
		return -1;
	}

	err = 0;
	while (fgets(line, 4096, file)) {
		if (line[0] == '#' || line[0] == '\n')
			continue;

		len = strlen(line) - 1;	//	\n
		for (i = 0; i < len; ++i) {
			if (line[i] == ':')
				break;
		}
		str = calloc(i + 1, sizeof(char));
		str[i] = 0;
		memcpy(str, &line[0], i);
		assert(line[i] == ':');
		++i;

		lhs = add_element(str);
		e = &elements[lhs];
		assert(e->index == lhs);
		assert(e->is_terminal == false);
		r = &e->rules[e->num_rules++];
		r->lhs = lhs;
		r->num_rhs = 0;
		r->can_generate_epsilon = false;
		r->generate_epsilon_done = false;
		for (; i < len;) {
			assert(line[i] == '\t');
			++i;
			assert(i < len);
			j = i;
			for (; i < len; ++i) {
				if (line[i] == '\t')
					break;
			}
			// [j, i) contains an element. i is either \t or len.
			str = calloc(i - j + 1, sizeof(char));
			str[i - j] = 0;
			memcpy(str, &line[j], i - j);
			j = add_element(str);
			r->rhs[r->num_rhs++] = add_element(str);
		}
	}
	fclose(file);

	/* All terminals */
	epsilon = find_element("epsilon");
	for (i = 0; i < num_elements; ++i) {
		e = &elements[i];
		if (!e->is_terminal)
			continue;
		assert(e->num_rules == 0);
		e->can_generate_epsilon = false;
		e->generate_epsilon_done = true;
		if (e->index == epsilon)
			e->can_generate_epsilon = true;
	}
	calc_generate_epsilon();
	/*
	 * It turns out that only AttributeList (and perhaps BalancedTOken)
	 * can generate epsilon.
	 */
#if 0
	for (i = 0; i < num_elements; ++i)
		print_element(i);
#endif
	/* All terminals */
	for (i = 0; i < num_elements; ++i) {
		e = &elements[i];
		if (!e->is_terminal)
			continue;
		assert(e->num_rules == 0);
		e->num_firsts = 1;
		e->first[0] = e->index;
	}
	build_find_first_graph();
	find_first_bfs();
	for (i = 0; i < num_elements; ++i) {
		e = &elements[i];
		qsort(&e->first, e->num_firsts, sizeof(int), cmpfunc);
	}
	for (i = 0; i < num_elements; ++i)
		print_element(i);
	return err;
}
