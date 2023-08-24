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
///////////////////////////////////////////////////////////////////////////////
struct rule {
	int	lhs;	/* index into elements to a non-terminal */
	int	*rhs;
	int	num_rhs;
	bool can_generate_epsilon;
	bool generate_epsilon_done;
};

struct element {
	int	*incoming;	/* edges */
	int	num_incoming;
	bool	is_on_queue;

	int	index;
	bool	is_terminal;
	const char	*name;

	struct rule	*rules;
	int	num_rules;
	int	*firsts;
	int	num_firsts;

	bool can_generate_epsilon;
	bool generate_epsilon_done;
};

struct element *elements;
int num_elements;
///////////////////////////////////////////////////////////////////////////////
static
void print_rule(const struct rule *r)
{
	int i, j;
	const struct element *e;

	e = &elements[r->lhs];
	printf("%s(%d,%d):", e->name,
		   r->generate_epsilon_done, r->can_generate_epsilon);

	for (i = 0; i < r->num_rhs; ++i) {
		j = r->rhs[i];
		e = &elements[j];
		printf(" %s", e->name);
	}
	printf("\n");
}

static
void print_element(const int index)
{
	int i, j;
	const struct element *e;

	e = &elements[index];
	if (e->is_terminal)
		return;

	for (i = 0; i < e->num_rules; ++i)
		print_rule(&e->rules[i]);

	/* firsts */
	printf("%s firsts:", e->name);
	for (i = 0; i < e->num_firsts; ++i) {
		j = e->firsts[i];
		printf(" %s", elements[j].name);
		if (j != e->num_firsts - 1)
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
	elements = realloc(elements, (num_elements + 1) * sizeof(*e));
	e = &elements[num_elements++];
	memset(e, 0, sizeof(*e));
	e->index = num_elements - 1;
	e->is_terminal = is_terminal(name);
	e->name = name;
	return e->index;
}

static
void calc_generate_epsilon()
{
	int i, j, k, l, num_rules_done;
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
					l = r->rhs[k];
					te = &elements[l];
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
					l = r->rhs[k];
					te = &elements[l];
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
///////////////////////////////////////////////////////////////////////////////
static
bool add_first(int **firsts, int *num_firsts,
			   const int *sfirsts, const int snum_firsts)
{
	int i, j;
	bool added = false;
	int *f = *firsts;
	int nf = *num_firsts;


	for (i = 0; i < snum_firsts; ++i) {
		for (j = 0; j < nf; ++j) {
			if (f[j] == sfirsts[i])
				break;
		}
		if (j < nf)
			continue;
		f = realloc(f, (nf + 1) * sizeof(int));
		f[nf++] = sfirsts[i];
		added = true;
	}
	*firsts = f;
	*num_firsts = nf;
	return added;
}

static
void add_edge(struct element *from,
			  struct element *to)
{
	int i;

	for (i = 0; i < to->num_incoming; ++i) {
		if (to->incoming[i] == from->index)
			break;
	}
	if (i < to->num_incoming)
		return;
	to->incoming = realloc(to->incoming, (to->num_incoming + 1) * sizeof(int));
	to->incoming[to->num_incoming++] = from->index;
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

	if (e->is_on_queue)
		return;
	e->is_on_queue = true;
	assert(q->num_elements < 1024);
	pos = (q->read + q->num_elements) % 1024;
	q->queue[pos] = e->index;
	++q->num_elements;
}

static
int q_rem(struct queue *q)
{
	int index;
	struct element *e;
	assert(q->num_elements > 0);
	index = q->queue[q->read];
	q->read = (q->read + 1) % 1024;
	--q->num_elements;
	e = &elements[index];
	e->is_on_queue = false;
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
		//printf("adding %d %s\n", e->index, e->name);
		q_add(&q, e);
	}

	while (q.num_elements) {
		e = &elements[q_rem(&q)];
		assert(e);

		/* For all nodes for which e has incoming edges, update them */
		for (i = 0; i < e->num_incoming; ++i) {
			te = &elements[e->incoming[i]];
			if (te == e)
				continue;

			res = add_first(&te->firsts, &te->num_firsts, e->firsts,
							e->num_firsts);

			/* If fresh data added, and te has incoming and te is not on queue,
			 * place te on q
			 */
			if (res && te->num_incoming && !te->is_on_queue)
				q_add(&q, te);
		}
	}
}
///////////////////////////////////////////////////////////////////////////////
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
///////////////////////////////////////////////////////////////////////////////
/*
 *  lr1 dotted item
 *  For a rule, num_rhs = 4, item starts at dot_pos == 0.
 *  if dot_pos == num_rhs then the item is complete and allows reduction.
 *  e0: . e1 e2 e3 e4		[las]
 */
#if 0
struct item {
	int	element;
	int	rule;
	int	dot_pos;
	int *las;	/* look aheads */
	int num_las;
	int	jump;
};

struct item_set {
	int index;
	struct array items;
};

struct item_set *set;
int num_sets;

int find_item(const struct item_set *set,
			  const struct item *item)
{
	int i, j, k;
	const struct item *ti;

	for (i = 0; i < set->num_items; ++i) {
		ti = set->items[i];
		if (ti->element != item->element)
			continue;
		if (ti->rule != item->rule)
			continue;
		if (ti->dot_pos != item->dot_pos)
			continue;
		if (ti->num_las != item->num_las)
			continue;
		for (j = 0; j < ti->num_las; ++j) {
			for (k = 0; k < item->num_las; ++k) {
				if (ti->las[j] == item->las[j])
					break;
			}
			if (k == item->num_las)
				return -1;
		}
		return 0;
	}
	return -1;
}

void closure(struct item_set *set)
{
	int i, j, k;
	bool modified = true;
	struct item *item, *ti;
	struct element *e, *te;
	struct rule *r;
	int *las;

	for (i = 0; i < set->num_items; ++i) {
		item = set->items[i];
		e = &elements[item->element];
		r = &e->rules[item->rule];

		/* e0: e1 e2 e3 . A reduce item. skip */
		if (item->dot_pos == r->num_rhs)
			continue;
		assert(item->dot_pos >= 0 && item->dot_pos < r->num_rhs);
		te = &elements[r->rhs[item->dot_pos]];
		if (te->is_terminal)
			continue;
		assert(te->num_rules);
		ti = malloc(sizeof(*ti));
		assert(ti);
		ti->element = te->index;
		ti->dot_pos = 0;
		ti->num_las = e->num_las;
		las = malloc(ti->num_las * sizeof(int));
		assert(las);
		memcpy(las, e->las, ti->num_las * sizeof(int));

	int	element;
	int	rule;
	int	dot_pos;
	int *las;	/* look aheads */
	int num_las;
		if (item->dot_pos < r->num_rhs - 1) {
			te = &elements[r->rhs[item->dot_pos + 1]];
			assert(te->generate_epsilon_done);
			if (!te->can_generate_epsilon) {
				free(las);
				ti->num_las = 0;
				las = NULL;
			}
			ti->num_las += te->num_firsts;
			las = realloc(las, ti->num_las * sizeof(int));
			ti->num_las -= te->num_firsts;
			assert(las);
		}
	}
}

/* adds an item and computes the closure. if look_ahead changes, compute */
void add_item(struct item_set *set,
			  struct item *item)
{
	int i, j;
	struct item *ti;
	bool added = false;

	i = find_item(set, item);
	if (i < 0)
		goto add;
	return;
add:
	set->items = realloc(set->items, (set->num_items + 1) * sizeof(void *));
	assert(set->items);
	set->items[set->num_items++] = item;

	/* closure */
}
#endif
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
		e->rules = realloc(e->rules, (e->num_rules + 1) * sizeof(*r));
		r = &e->rules[e->num_rules++];
		memset(r, 0, sizeof(*r));
		r->lhs = lhs;
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
			r->rhs = realloc(r->rhs, (r->num_rhs + 1) * sizeof(int));
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
	/* All terminals */
	for (i = 0; i < num_elements; ++i) {
		e = &elements[i];
		if (!e->is_terminal)
			continue;
		assert(e->num_rules == 0);
		e->firsts = realloc(e->firsts, (e->num_firsts + 1) * sizeof(int));
		e->firsts[e->num_firsts++] = e->index;
	}
	build_find_first_graph();
	find_first_bfs();
	for (i = 0; i < num_elements; ++i) {
		e = &elements[i];
		qsort(e->firsts, e->num_firsts, sizeof(int), cmpfunc);
	}
	for (i = 0; i < num_elements; ++i)
		print_element(i);
	return err;
}