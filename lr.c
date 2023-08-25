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
	/* For testing a small grammar */
	"c",
	"d",
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
	print_item_sets();
	return err;
}
