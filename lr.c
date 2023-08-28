// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2023 Amol Surati
// vim: set noet ts=4 sts=4 sw=4:

// cc -O3 -Wall -Wextra -I. lr.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

enum cc_token_type {
#define DEF(t)	CC_TOKEN_ ## t,
#include <inc/cpp/tokens.h>
#include <inc/cc/tokens.h>
#undef DEF
};

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
struct item {
	int	element;
	int	rule;
	int	dot_pos;
	int *las;	/* look aheads */
	int num_las;
	int	jump;
};

/* With **items, reallocating items does not invalidate pointers to items. */
/* Each item_set is identified by the set of its kernel items
 * two item_sets with the same set of kernel items are identical
 */
struct item_set {
	int index;
	struct item **kernels;	/* The initial + all items with dot_pos != 0 */
	int num_kernels;
	struct item **items;	/* Closure items. All have dot_pos == 0 */
	int num_items;
};

struct item_set **sets;
int num_sets;


/* During closure, we can match the items by excluding the las from teh
 * comparison and then adding the las into the found item.
 * During goto, we must match kernel items exactly - number, productions and
 * las.
 */
static
bool item_set_add_item(struct item_set *set,
					   struct item *item)
{
	int i;
	bool res;
	struct item *ti;

	for (i = 0; i < set->num_items; ++i) {
		ti = set->items[i];
		if (ti->element != item->element)
			continue;
		if (ti->rule != item->rule)
			continue;
		if (ti->dot_pos != item->dot_pos)
			continue;

		/* For closure items, we do not need to match the las. */
		/*
		 * If there's any ele from item.las, that is missing in ti.las,
		 * add it into ti.las.
		 */
		res = add_first(&ti->las, &ti->num_las, item->las, item->num_las);
		free(item->las);
		free(item);
		return res;
	}
	/* item isn't present at all */
	set->items = realloc(set->items, (set->num_items + 1) * sizeof(item));
	set->items[set->num_items++] = item;
	return true;
}

static
bool item_set_add_kernel(struct item_set *set,
						 struct item *item)
{
	int i;
	struct item *ti;

	/* We must not find the item being added */
	for (i = 0; i < set->num_kernels; ++i) {
		ti = set->kernels[i];
		if (ti->element != item->element)
			continue;
		if (ti->rule != item->rule)
			continue;
		if (ti->dot_pos != item->dot_pos)
			continue;
		assert(0);
		return true;
	}
	/* item isn't present at all */
	set->kernels = realloc(set->kernels,
						   (set->num_kernels + 1) * sizeof(item));
	set->kernels[set->num_kernels++] = item;
	return true;
}

static
void print_item(const struct item *item)
{
	int i, j;
	struct element *e;
	struct rule *r;

	e = &elements[item->element];
	r = &e->rules[item->rule];
	assert(r->lhs == e->index);
	printf("[%s ->", e->name);

	for (i = 0; i < r->num_rhs; ++i) {
		j = r->rhs[i];
		e = &elements[j];
		if (item->dot_pos == i)
			printf(" .");
		printf(" %s", e->name);
	}
	if (item->dot_pos == i)
		printf(" .");
	printf("] jump=%d las:", item->jump);
	for (i = 0; i < item->num_las; ++i) {
		j = item->las[i];
		if (j == EOF) {
			printf(" eof");
			continue;
		}
		e = &elements[j];
		printf(" %s", e->name);
	}

	printf("\n");
}

static
void print_item_set(const struct item_set *set)
{
	int i;

	printf("%s: item-set[%4d]:k----------------------\n", __func__,
		   set->index);
	for (i = 0; i < set->num_kernels; ++i)
		print_item(set->kernels[i]);
	if (set->num_items)
		printf("%s: item-set[%4d]:-----------------------\n", __func__,
			   set->index);
	for (i = 0; i < set->num_items; ++i)
		print_item(set->items[i]);
	printf("%s: item-set[%4d]:done-------------------\n", __func__, set->index);
	printf("\n");
}

static
void print_item_sets()
{
	int i;

	for (i = 0; i < num_sets; ++i)
		print_item_set(sets[i]);
}

static
int find_item_set(const struct item_set *set)
{
	int i, j, num_items_matched, k, m, n, num_las_matched;
	const struct item_set *s;
	struct item  *item[2];

	/* All must match exactly */
	for (i = 0; i < num_sets; ++i) {
		s = sets[i];
		if (s->num_kernels != set->num_kernels)
			continue;
		/* Does the kernels have the same items */
		num_items_matched = 0;
		for (j = 0; j < set->num_kernels; ++j) {
			item[0] = set->kernels[j];
			/* Search item[0] in s */
			for (k = 0; k < s->num_kernels; ++k) {
				item[1] = s->kernels[k];
				if (item[0]->element != item[1]->element)
					continue;
				if (item[0]->rule != item[1]->rule)
					continue;
				if (item[0]->dot_pos != item[1]->dot_pos)
					continue;
				if (item[0]->num_las != item[1]->num_las)
					continue;
				num_las_matched = 0;
				for (m = 0; m < item[0]->num_las; ++m) {
					for (n = 0; n < item[1]->num_las; ++n) {
						if (item[0]->las[m] != item[1]->las[n])
							continue;
						++num_las_matched;
						break;
					}
				}
				if (num_las_matched != item[0]->num_las)
					continue;
				/* a full item was matched */
				++num_items_matched;
				break;
			}
		}
		if (num_items_matched != set->num_kernels)
			continue;
		/* Full set was matched */
		return i;
	}
	return EOF;
}

/* For each item, see if it needs to generate more items. */
/* return true if we modified the set */
bool closure_one(struct item_set *set,
				 struct item *item)
{
	int i;
	struct item *ti;
	bool added = false;
	struct element *e, *te;
	struct rule *r;

	e = &elements[item->element];
	r = &e->rules[item->rule];

	/* If this is a reduce item, skip. */
	if (item->dot_pos == r->num_rhs)
		return false;

	/* If the dot-pos rhs item is terminal, skip */
	i = r->rhs[item->dot_pos];
	e = &elements[i];
	if (e->is_terminal)
		return false;

	/*
	 * A -> alpha . B			[L]
	 * A -> alpha . B beta		[L]
	 */

	/* A -> alpha . B			[L] */
	if (item->dot_pos == r->num_rhs - 1) {
		/* We need to add B -> . gamma	[L], for each rule of this ele. */
		/* e points to B */
		for (i = 0; i < e->num_rules; ++i) {
			ti = calloc(1, sizeof(*ti));
			ti->element = e->index;
			ti->rule = i;
			ti->jump = EOF;
			add_first(&ti->las, &ti->num_las, item->las, item->num_las);
			if (item_set_add_item(set, ti))
				added = true;
		}
		return added;
	}

	/* A -> alpha . B	beta		[L] */
	assert(item->dot_pos < r->num_rhs - 1);
	i = r->rhs[item->dot_pos + 1];
	te = &elements[i];

	/* e points to B, te points to beta */
	for (i = 0; i < e->num_rules; ++i) {
		ti = calloc(1, sizeof(*ti));
		ti->element = e->index;
		ti->rule = i;
		ti->jump = EOF;
		assert(te->generate_epsilon_done);
		add_first(&ti->las, &ti->num_las, te->firsts, te->num_firsts);
		if (te->can_generate_epsilon)
			add_first(&ti->las, &ti->num_las, item->las, item->num_las);
		if (item_set_add_item(set, ti))
			added = true;
	}
	return added;
}

void closure(struct item_set *set)
{
	int i, num_items, j, k, added;
	bool res, modified, items_modified;
	struct item *ti, *item;
	struct element *e;
	struct rule *r;
	struct item_set *nset;

	items_modified = false;
	while (true) {
		modified = false;
		for (i = 0; i < set->num_kernels; ++i) {
			item = set->kernels[i];
#if 0
			printf("%s: taking closure of item: ", __func__);
			print_item(item);
#endif
			res = closure_one(set, item);
			modified = res ? true : modified;
#if 0
			printf("%s: closure returned %d\n", __func__, res);
#endif
		}
		if (modified == false)
			break;
		items_modified = true;
	}

	while (items_modified) {
		modified = false;
		for (i = 0; i < set->num_items; ++i) {
			item = set->items[i];
#if 0
			printf("%s: taking closure of item: ", __func__);
			print_item(item);
#endif
			res = closure_one(set, item);
			modified = res ? true : modified;
#if 0
			printf("%s: closure returned %d\n", __func__, res);
#endif
		}
		if (modified == false)
			break;
	}
	//print_item_set(set);

	/* process gotos */
	/* Build an item_set with kernel items and then search */
	while (true) {
		num_items = 0;
		/* Are there any non-reduction items with unresolved jumps */
		for (i = 0; i < set->num_kernels; ++i) {
			item = set->kernels[i];
			e = &elements[item->element];
			r = &e->rules[item->rule];
			if (item->dot_pos == r->num_rhs)
				continue;
			if (item->jump != EOF)
				continue;
			++num_items;
		}
		for (i = 0; i < set->num_items; ++i) {
			item = set->items[i];
			e = &elements[item->element];
			r = &e->rules[item->rule];
			if (item->dot_pos == r->num_rhs)
				continue;
			if (item->jump != EOF)
				continue;
			++num_items;
		}
		if (num_items == 0)
			break;

		/* Find all items with same dot-pos-ele and jump == EOF */
		k = -1;
		nset = calloc(1, sizeof(*nset));
		for (i = 0; i < set->num_kernels + set->num_items; ++i) {
			if (i < set->num_kernels)
				item = set->kernels[i];
			else
				item = set->items[i - set->num_kernels];
			e = &elements[item->element];
			r = &e->rules[item->rule];
			if (item->dot_pos == r->num_rhs)	/* skip reduction items */
				continue;
			if (item->jump != EOF)	/* skip items with valid jumps */
				continue;
			j = item->dot_pos;
			if (k < 0)
				k = r->rhs[j];	/* k is the dot-pos element */
			else if (r->rhs[j] != k)
				continue;

			/* Collect all items with this dot-pos elemeent */
			assert(r->rhs[j] == k);
			ti = calloc(1, sizeof(*ti));
			*ti = *item;
			++ti->dot_pos;
			ti->las = malloc(item->num_las * sizeof(int));
			memcpy(ti->las, item->las, item->num_las * sizeof(int));
			res = item_set_add_kernel(nset, ti);
			assert(res);	/* ti must not be found */
		}
		assert(nset->num_kernels);

		/* find the set matching the kernel */
		added = false;
		i = find_item_set(nset);
		if (i < 0) {
			sets = realloc(sets, (num_sets + 1) * sizeof(nset));
			sets[num_sets++] = nset;
			nset->index = num_sets - 1;
			added = true;
		} else {
			for (j = 0; j < nset->num_kernels; ++j) {
				item = nset->kernels[j];
				free(item->las);
				free(item);
			}
			free(nset->kernels);
			free(nset);
			nset = sets[i];
		}

		/* Fix up the jumps. */
		for (i = 0; i < set->num_kernels + set->num_items; ++i) {
			if (i < set->num_kernels)
				item = set->kernels[i];
			else
				item = set->items[i - set->num_kernels];
			e = &elements[item->element];
			r = &e->rules[item->rule];
			if (item->dot_pos == r->num_rhs)	/* skip reduction items */
				continue;
			if (item->jump != EOF)	/* skip items with valid jumps */
				continue;
			j = item->dot_pos;
			if (r->rhs[j] != k)
				continue;
			/* Fixup all items with this dot-pos elemeent */
			assert(r->rhs[j] == k);
			item->jump = nset->index;
		}
		if (added)
			closure(nset);
	}
}

static
void cleanup()
{
	int i, j;
	struct item_set *set;
	struct item  *item;
	struct element *e;
	struct rule *r;

	for (i = 0; i < num_sets; ++i) {
		set = sets[i];
		for (j = 0; j < set->num_kernels; ++j) {
			item = set->kernels[j];
			free(item->las);
			free(item);
		}
		free(set->kernels);
		for (j = 0; j < set->num_items; ++j) {
			item = set->items[j];
			free(item->las);
			free(item);
		}
		free(set->items);
		free(set);
	}
	free(sets);

	for (i = 0; i < num_elements; ++i) {
		e = &elements[i];
		for (j = 0; j < e->num_rules; ++j) {
			r = &e->rules[j];
			free(r->rhs);
		}
		free((void *)e->name);
		free(e->rules);
		free(e->firsts);
		free(e->incoming);
	}
	free(elements);
}

#define DEF(t) CC_TOKEN_ ## t
enum cc_token_type name_to_type(const char *name)
{
	if (!strcmp(name,"(")) return DEF(LEFT_PAREN);
	if (!strcmp(name,")")) return DEF(RIGHT_PAREN);
	if (!strcmp(name,"_Generic")) return DEF(GENERIC);
	if (!strcmp(name,",")) return DEF(COMMA);
	if (!strcmp(name,":")) return DEF(COLON);
	if (!strcmp(name,"default")) return DEF(DEFAULT);
	if (!strcmp(name,"[")) return DEF(LEFT_BRACKET);
	if (!strcmp(name,"]")) return DEF(RIGHT_BRACKET);
	if (!strcmp(name,".")) return DEF(DOT);
	if (!strcmp(name,"->")) return DEF(ARROW);
	if (!strcmp(name,"++")) return DEF(INCR);
	if (!strcmp(name,"--")) return DEF(DECR);
	if (!strcmp(name,"sizeof")) return DEF(SIZE_OF);
	if (!strcmp(name,"alignof")) return DEF(ALIGN_OF);
	if (!strcmp(name,"&")) return DEF(BITWISE_AND);
	if (!strcmp(name,"*")) return DEF(MUL);
	if (!strcmp(name,"+")) return DEF(PLUS);
	if (!strcmp(name,"-")) return DEF(MINUS);
	if (!strcmp(name,"~")) return DEF(BITWISE_NOT);
	if (!strcmp(name,"!")) return DEF(LOGICAL_NOT);
	if (!strcmp(name,"/")) return DEF(DIV);
	if (!strcmp(name,"%")) return DEF(MOD);
	if (!strcmp(name,"<<")) return DEF(SHIFT_LEFT);
	if (!strcmp(name,">>")) return DEF(SHIFT_RIGHT);
	if (!strcmp(name,"<")) return DEF(LESS_THAN);
	if (!strcmp(name,">")) return DEF(GREATER_THAN);
	if (!strcmp(name,"<=")) return DEF(LESS_THAN_EQUALS);
	if (!strcmp(name,">=")) return DEF(GREATER_THAN_EQUALS);
	if (!strcmp(name,"==")) return DEF(EQUALS);
	if (!strcmp(name,"!=")) return DEF(NOT_EQUALS);
	if (!strcmp(name,"^")) return DEF(BITWISE_XOR);
	if (!strcmp(name,"|")) return DEF(BITWISE_OR);
	if (!strcmp(name,"&&")) return DEF(LOGICAL_AND);
	if (!strcmp(name,"||")) return DEF(LOGICAL_OR);
	if (!strcmp(name,"?")) return DEF(CONDITIONAL);
	if (!strcmp(name,"=")) return DEF(ASSIGN);
	if (!strcmp(name,"*=")) return DEF(MUL_ASSIGN);
	if (!strcmp(name,"/=")) return DEF(DIV_ASSIGN);
	if (!strcmp(name,"%=")) return DEF(MOD_ASSIGN);
	if (!strcmp(name,"+=")) return DEF(PLUS_ASSIGN);
	if (!strcmp(name,"-=")) return DEF(MINUS_ASSIGN);
	if (!strcmp(name,"<<=")) return DEF(SHIFT_LEFT_ASSIGN);
	if (!strcmp(name,">>=")) return DEF(SHIFT_RIGHT_ASSIGN);
	if (!strcmp(name,"&=")) return DEF(BITWISE_AND_ASSIGN);
	if (!strcmp(name,"^=")) return DEF(BITWISE_XOR_ASSIGN);
	if (!strcmp(name,"|=")) return DEF(BITWISE_OR_ASSIGN);
	if (!strcmp(name,";")) return DEF(SEMI_COLON);
	if (!strcmp(name,"auto")) return DEF(AUTO);
	if (!strcmp(name,"constexpr")) return DEF(CONST_EXPR);
	if (!strcmp(name,"extern")) return DEF(EXTERN);
	if (!strcmp(name,"register")) return DEF(REGISTER);
	if (!strcmp(name,"static")) return DEF(STATIC);
	if (!strcmp(name,"thread_local")) return DEF(THREAD_LOCAL);
	if (!strcmp(name,"typedef")) return DEF(TYPE_DEF);
	if (!strcmp(name,"void")) return DEF(VOID);
	if (!strcmp(name,"char")) return DEF(CHAR);
	if (!strcmp(name,"short")) return DEF(SHORT);
	if (!strcmp(name,"int")) return DEF(INT);
	if (!strcmp(name,"long")) return DEF(LONG);
	if (!strcmp(name,"float")) return DEF(FLOAT);
	if (!strcmp(name,"double")) return DEF(DOUBLE);
	if (!strcmp(name,"signed")) return DEF(SIGNED);
	if (!strcmp(name,"unsigned")) return DEF(UNSIGNED);
	if (!strcmp(name,"_BitInt")) return DEF(BIT_INT);
	if (!strcmp(name,"bool")) return DEF(BOOL);
	if (!strcmp(name,"_Complex")) return DEF(COMPLEX);
	if (!strcmp(name,"_Decimal32")) return DEF(DECIMAL_32);
	if (!strcmp(name,"_Decimal64")) return DEF(DECIMAL_64);
	if (!strcmp(name,"_Decimal128")) return DEF(DECIMAL_128);
	if (!strcmp(name,"{")) return DEF(LEFT_BRACE);
	if (!strcmp(name,"}")) return DEF(RIGHT_BRACE);
	if (!strcmp(name,"struct")) return DEF(STRUCT);
	if (!strcmp(name,"union")) return DEF(UNION);
	if (!strcmp(name,"enum")) return DEF(ENUM);
	if (!strcmp(name,"_Atomic")) return DEF(ATOMIC);
	if (!strcmp(name,"typeof")) return DEF(TYPE_OF);
	if (!strcmp(name,"typeof_unqual")) return DEF(TYPE_OF_UNQUAL);
	if (!strcmp(name,"const")) return DEF(CONST);
	if (!strcmp(name,"restrict")) return DEF(RESTRICT);
	if (!strcmp(name,"volatile")) return DEF(VOLATILE);
	if (!strcmp(name,"inline")) return DEF(INLINE);
	if (!strcmp(name,"_Noreturn")) return DEF(NO_RETURN);
	if (!strcmp(name,"alignas")) return DEF(ALIGN_AS);
	if (!strcmp(name,"static")) return DEF(STATIC);
	if (!strcmp(name,"...")) return DEF(ELLIPSIS);
	if (!strcmp(name,"static_assert")) return DEF(STATIC_ASSERT);
	if (!strcmp(name,"::")) return DEF(DOUBLE_COLON);
	if (!strcmp(name,"case")) return DEF(CASE);
	if (!strcmp(name,"if")) return DEF(IF);
	if (!strcmp(name,"switch")) return DEF(SWITCH);
	if (!strcmp(name,"else")) return DEF(ELSE);
	if (!strcmp(name,"while")) return DEF(WHILE);
	if (!strcmp(name,"do")) return DEF(DO);
	if (!strcmp(name,"for")) return DEF(FOR);
	if (!strcmp(name,"goto")) return DEF(GO_TO);
	if (!strcmp(name,"continue")) return DEF(CONTINUE);
	if (!strcmp(name,"break")) return DEF(BREAK);
	if (!strcmp(name,"return")) return DEF(RETURN);
	if (!strcmp(name,"Identifier")) return DEF(IDENTIFIER);
	if (!strcmp(name,"StringLiteral")) return DEF(STRING_LITERAL);
	if (!strcmp(name,"Constant")) return DEF(CONST);

	if (!strcmp(name,"AbstractDeclarator")) return DEF(ABSTRACT_DECLARATOR);
	if (!strcmp(name,"AdditiveExpression")) return DEF(ADDITIVE_EXPRESSION);
	if (!strcmp(name,"AlignmentSpecifier")) return DEF(ALIGNMENT_SPECIFIER);
	if (!strcmp(name,"AndExpression")) return DEF(AND_EXPRESSION);
	if (!strcmp(name,"ArgumentExpressionList")) return DEF(ARGUMENT_EXPRESSION_LIST);
	if (!strcmp(name,"ArrayAbstractDeclarator")) return DEF(ARRAY_ABSTRACT_DECLARATOR);
	if (!strcmp(name,"ArrayDeclarator")) return DEF(ARRAY_DECLARATOR);
	if (!strcmp(name,"AssignmentExpression")) return DEF(ASSIGNMENT_EXPRESSION);
	if (!strcmp(name,"AssignmentOperator")) return DEF(ASSIGNMENT_OPERATOR);
	if (!strcmp(name,"AtomicTypeSpecifier")) return DEF(ATOMIC_TYPE_SPECIFIER);
	if (!strcmp(name,"Attribute")) return DEF(ATTRIBUTE);
	if (!strcmp(name,"AttributeArgumentClause")) return DEF(ATTRIBUTE_ARGUMENT_CLAUSE);
	if (!strcmp(name,"AttributeDeclaration")) return DEF(ATTRIBUTE_DECLARATION);
	if (!strcmp(name,"AttributeList")) return DEF(ATTRIBUTE_LIST);
	if (!strcmp(name,"AttributePrefix")) return DEF(ATTRIBUTE_PREFIX);
	if (!strcmp(name,"AttributePrefixedToken")) return DEF(ATTRIBUTE_PREFIXED_TOKEN);
	if (!strcmp(name,"AttributeSpecifier")) return DEF(ATTRIBUTE_SPECIFIER);
	if (!strcmp(name,"AttributeSpecifierSequence")) return DEF(ATTRIBUTE_SPECIFIER_SEQUENCE);
	if (!strcmp(name,"AttributeToken")) return DEF(ATTRIBUTE_TOKEN);
	if (!strcmp(name,"BalancedToken")) return DEF(BALANCED_TOKEN);
	if (!strcmp(name,"BalancedTokenSequence")) return DEF(BALANCED_TOKEN_SEQUENCE);
	if (!strcmp(name,"BlockItem")) return DEF(BLOCK_ITEM);
	if (!strcmp(name,"BlockItemList")) return DEF(BLOCK_ITEM_LIST);
	if (!strcmp(name,"BracedInitializer")) return DEF(BRACED_INITIALIZER);
	if (!strcmp(name,"CastExpression")) return DEF(CAST_EXPRESSION);
	if (!strcmp(name,"CompoundLiteral")) return DEF(COMPOUND_LITERAL);
	if (!strcmp(name,"CompoundStatement")) return DEF(COMPOUND_STATEMENT);
	if (!strcmp(name,"ConditionalExpression")) return DEF(CONDITIONAL_EXPRESSION);
	if (!strcmp(name,"ConstantExpression")) return DEF(CONSTANT_EXPRESSION);
	if (!strcmp(name,"Declaration")) return DEF(DECLARATION);
	if (!strcmp(name,"DeclarationSpecifier")) return DEF(DECLARATION_SPECIFIER);
	if (!strcmp(name,"DeclarationSpecifiers")) return DEF(DECLARATION_SPECIFIERS);
	if (!strcmp(name,"Declarator")) return DEF(DECLARATOR);
	if (!strcmp(name,"Designation")) return DEF(DESIGNATION);
	if (!strcmp(name,"Designator")) return DEF(DESIGNATOR);
	if (!strcmp(name,"DesignatorList")) return DEF(DESIGNATOR_LIST);
	if (!strcmp(name,"DirectAbstractDeclarator")) return DEF(DIRECT_ABSTRACT_DECLARATOR);
	if (!strcmp(name,"DirectDeclarator")) return DEF(DIRECT_DECLARATOR);
	if (!strcmp(name,"EnumerationConstant")) return DEF(ENUMERATION_CONSTANT);
	if (!strcmp(name,"Enumerator")) return DEF(ENUMERATOR);
	if (!strcmp(name,"EnumeratorList")) return DEF(ENUMERATOR_LIST);
	if (!strcmp(name,"EnumSpecifier")) return DEF(ENUM_SPECIFIER);
	if (!strcmp(name,"EnumTypeSpecifier")) return DEF(ENUM_TYPE_SPECIFIER);
	if (!strcmp(name,"EqualityExpression")) return DEF(EQUALITY_EXPRESSION);
	if (!strcmp(name,"ExclusiveOrExpression")) return DEF(EXLUSIVE_OR_EXPRESSION);
	if (!strcmp(name,"Expression")) return DEF(EXPRESSION);
	if (!strcmp(name,"ExpressionStatement")) return DEF(EXPRESSION_STATEMENT);
	if (!strcmp(name,"ExternalDeclaration")) return DEF(EXTERNAL_DECLARATION);
	if (!strcmp(name,"FunctionAbstractDeclarator")) return DEF(FUNCTION_ABSTRACT_DECLARATOR);
	if (!strcmp(name,"FunctionBody")) return DEF(FUNCTION_BODY);
	if (!strcmp(name,"FunctionDeclarator")) return DEF(FUNCTION_DECLARATOR);
	if (!strcmp(name,"FunctionDefinition")) return DEF(FUNCTION_DEFINITION);
	if (!strcmp(name,"FunctionSpecifier")) return DEF(FUNCTION_SPECIFIER);
	if (!strcmp(name,"GenericAssociation")) return DEF(GENERIC_ASSOCIATION);
	if (!strcmp(name,"GenericAssocList")) return DEF(GENERIC_ASSOC_LIST);
	if (!strcmp(name,"GenericSelection")) return DEF(GENERIC_SELECTION);
	if (!strcmp(name,"InclusiveOrExpression")) return DEF(INCLUSIVE_OR_EXPRESSION);
	if (!strcmp(name,"InitDeclarator")) return DEF(INIT_DECLARATOR);
	if (!strcmp(name,"InitDeclaratorList")) return DEF(INIT_DECLARATOR_LIST);
	if (!strcmp(name,"Initializer")) return DEF(INITIALIZER);
	if (!strcmp(name,"InitializerList")) return DEF(INITIALIZER_LIST);
	if (!strcmp(name,"IterationStatement")) return DEF(ITERATION_STATEMENT);
	if (!strcmp(name,"JumpStatement")) return DEF(JUMP_STATEMENT);
	if (!strcmp(name,"Label")) return DEF(LABEL);
	if (!strcmp(name,"LabeledStatement")) return DEF(LABELED_STATEMENT);
	if (!strcmp(name,"LogicalAndExpression")) return DEF(LOGICAL_AND_EXPRESSION);
	if (!strcmp(name,"LogicalOrExpression")) return DEF(LOGICAL_OR_EXPRESSION);
	if (!strcmp(name,"MemberDeclaration")) return DEF(MEMBER_DECLARATION);
	if (!strcmp(name,"MemberDeclarationList")) return DEF(MEMBER_DECLARATION_LIST);
	if (!strcmp(name,"MemberDeclarator")) return DEF(MEMBER_DECLARATOR);
	if (!strcmp(name,"MemberDeclaratorList")) return DEF(MEMBER_DECLARATOR_LIST);
	if (!strcmp(name,"MultiplicativeExpression")) return DEF(MULTIPLICATIVE_EXPRESSION);
	if (!strcmp(name,"ParameterDeclaration")) return DEF(PARAMETER_DECLARATION);
	if (!strcmp(name,"ParameterList")) return DEF(PARAMETER_LIST);
	if (!strcmp(name,"ParameterTypeList")) return DEF(PARAMETER_TYPE_LIST);
	if (!strcmp(name,"Pointer")) return DEF(POINTER);
	if (!strcmp(name,"PostfixExpression")) return DEF(POSTFIX_EXPRESSION);
	if (!strcmp(name,"PrimaryBlock")) return DEF(PRIMARY_BLOCK);
	if (!strcmp(name,"PrimaryExpression")) return DEF(PRIMARY_EXPRESSION);
	if (!strcmp(name,"RelationalExpression")) return DEF(RELATIONAL_EXPRESSION);
	if (!strcmp(name,"SecondaryBlock")) return DEF(SECONDARY_BLOCK);
	if (!strcmp(name,"SelectionStatement")) return DEF(SELECTION_STATEMENT);
	if (!strcmp(name,"ShiftExpression")) return DEF(SHIFT_EXPRESSION);
	if (!strcmp(name,"SpecifierQualifierList")) return DEF(SPECIFIER_QUALIFIER_LIST);
	if (!strcmp(name,"StandardAttribute")) return DEF(STANDARD_ATTRIBUTE);
	if (!strcmp(name,"Statement")) return DEF(STATEMENT);
	if (!strcmp(name,"StaticAssertDeclaration")) return DEF(STATIC_ASSERT_DECLARATION);
	if (!strcmp(name,"StorageClassSpecifier")) return DEF(STORAGE_CLASS_SPECIFIER);
	if (!strcmp(name,"StorageClassSpecifiers")) return DEF(STORAGE_CLASS_SPECIFIERS);
	if (!strcmp(name,"StructOrUnion")) return DEF(STRUCT_OR_UNION);
	if (!strcmp(name,"StructOrUnionSpecifier")) return DEF(STRUCT_OR_UNION_SPECIFIER);
	if (!strcmp(name,"TranslationObject")) return DEF(TRANSLATION_OBJECT);
	if (!strcmp(name,"TranslationUnit")) return DEF(TRANSLATION_UNIT);
	if (!strcmp(name,"TypedefName")) return DEF(TYPE_DEF_NAME);
	if (!strcmp(name,"TypeName")) return DEF(TYPE_NAME);
	if (!strcmp(name,"TypeofSpecifier")) return DEF(TYPE_OF_SPECIFIER);
	if (!strcmp(name,"TypeofSpecifierArgument")) return DEF(TYPE_OF_SPECIFIER_ARGUMENT);
	if (!strcmp(name,"TypeQualifier")) return DEF(TYPE_QUALIFIER);
	if (!strcmp(name,"TypeQualifierList")) return DEF(TYPE_QUALIFIER_LIST);
	if (!strcmp(name,"TypeSpecifier")) return DEF(TYPE_SPECIFIER);
	if (!strcmp(name,"TypeSpecifierQualifier")) return DEF(TYPE_SPECIFIER_QUALIFIER);
	if (!strcmp(name,"UnaryExpression")) return DEF(UNARY_EXPRESSION);
	if (!strcmp(name,"UnaryOperator")) return DEF(UNARY_OPERATOR);
	if (!strcmp(name,"UnlabeledStatement")) return DEF(UNLABELED_STATEMENT);
	assert(0);
}

/*
 * The first is an 32-bit int that gives the # of elements.
 * The elements follow according to their index in incr. order.
 * cc_token_type	(32-bit int)
 * if non-terminal, then its rules in index order.
 *  first, 32-bit int num_rules
 *  for each rule,
 *	  num_elements (32-bit int)
 *	  rhs elements array
 *
 * Then follows item-sets	in index order incr.
 * The first is 32-bit int that gives # of item-sets.
 *
 * For each item-set,
 * 32-bit integer for # of kernel items
 * 32-bit integer for # of closure (non-kernel) items
 * Then kernel items followed by closure items
 * For each item
 * <element, rule, dot-pos, jump, num_las, las-array>
 */
void serialize()
{
	int i, j, k;
	int fd, num_items;
	const struct element *e;
	const struct rule *r;
	const struct item_set *set;
	const struct item *item;
	enum cc_token_type type;

	fd = open("/tmp/grammar.bin", O_WRONLY | O_TRUNC | O_CREAT,
			  S_IRUSR | S_IWUSR);
	if (fd < 0) {
		perror("open");
		exit(errno);
	}
	assert(fd >= 0);

	write(fd, &num_elements, sizeof(num_elements));
	for (i = 0; i < num_elements; ++i) {
		e = &elements[i];
		type = name_to_type(e->name);
		write(fd, &type, sizeof(type));
		if (e->is_terminal)
			continue;
		assert(e->num_rules);
		write(fd, &e->num_rules, sizeof(e->num_rules));
		for (j = 0; j < e->num_rules; ++j) {
			r = &e->rules[j];
			assert(r->num_rhs);
			write(fd, &r->num_rhs, sizeof(r->num_rhs));
			for (k = 0; k < r->num_rhs; ++k) {
				/* element indices */
				write(fd, &r->rhs[k], sizeof(r->rhs[k]));
			}
		}
	}

	num_items = 0;
	write(fd, &num_sets, sizeof(num_sets));
	for (i = 0; i < num_sets; ++i) {
		set = sets[i];
		write(fd, &set->num_kernels, sizeof(set->num_kernels));
		write(fd, &set->num_items, sizeof(set->num_items));
		for (j = 0; j < set->num_kernels + set->num_items; ++j) {
			if (j < set->num_kernels)
				item = set->kernels[j];
			else
				item = set->items[j - set->num_kernels];
			write(fd, &item->element, sizeof(item->element));
			write(fd, &item->rule, sizeof(item->rule));
			write(fd, &item->dot_pos, sizeof(item->dot_pos));
			write(fd, &item->jump, sizeof(item->jump));
			write(fd, &item->num_las, sizeof(item->num_las));
			for (k = 0; k < item->num_las; ++k)
				write(fd, &item->las[k], sizeof(item->las[k]));
			++num_items;
		}
	}
	close(fd);
	printf("%d\n", num_items);
}

int main(int argc, char **argv)
{
	int i, err, j, len, lhs, epsilon;
	FILE *file;
	static char line[4096];
	struct element *e;
	struct rule *r;
	char *str;
	struct item *item;
	struct item_set *set;

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
		if (e->name != str)
			free(str);
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
			r->rhs[r->num_rhs++] = j = add_element(str);
			if (elements[j].name != str)
				free(str);
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
#if 0
	for (i = 0; i < num_elements; ++i) {
		e = &elements[i];
		qsort(e->firsts, e->num_firsts, sizeof(int), cmpfunc);
	}
#endif
#if 0
	for (i = 0; i < num_elements; ++i)
		print_element(i);
#endif
	num_sets = 1;
	sets = malloc(num_sets * sizeof(set));
	set = calloc(1, sizeof(*set));
	sets[0] = set;
	item = calloc(1, sizeof(*item));
	item->element = find_element("TranslationObject");
	item->las = malloc(sizeof(int));
	item->las[0] = EOF;
	item->num_las = 1;
	item->jump = EOF;	/* No need to jump */
	item_set_add_kernel(set, item);
	closure(set);
	//print_item_sets();
	serialize();
	cleanup();
	return err;
}
