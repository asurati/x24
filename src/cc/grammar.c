/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#include "compiler.h"

#include <inc/unicode.h>

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
/*****************************************************************************/
/*
 * format of grammar.bin
 * num_elements (32-bit int)
 * For each element,
 *		type		(32-bit int)
 *		num_rules	(32-bit int)	only for non-terminals
 *		For each rule,
 *			num_elements	(32-bit int)
 *			elements		(each 32-bit int)
 *
 * num_item_sets (32-bit int)
 * For each item_set,
 *		num_kernel_items	(32-bit int)
 *		num_closure_items	(32-bit int)
 *		For each item,
 *			element
 *			rule
 *			dot_pos
 *			jump
 *			num_look_aheads
 *			look_aheads
 */
err_t cc_load_grammar(struct compiler *this)
{
	int fd, n, i, j;
	struct cc_grammar_element *ge;
	struct cc_grammar_rule *gr;
	struct cc_grammar_item_set *gis;
	struct cc_grammar_item *gi;

	fd = open("grammar.bin", O_RDONLY);
	if (fd < 0)
		return errno;
	if (read(fd, &n, sizeof(n)) < 0)
		return errno;
	this->num_elements = n;
	this->elements = calloc(this->num_elements, sizeof(*ge));
	if (this->elements == NULL)
		return ENOMEM;
	for (i = 0; i < this->num_elements; ++i) {
		ge = &this->elements[i];
		if (read(fd, &ge->type, sizeof(ge->type)) < 0)
			return errno;
		if (ge->type < CC_TOKEN_TRANSLATION_OBJECT)
			continue;	/* terminals */
		if (read(fd, &n, sizeof(n)) < 0)
			return errno;
		assert(n);
		ge->num_rules = n;
		ge->rules = calloc(ge->num_rules, sizeof(*gr));
		assert(ge->rules);
		if (ge->rules == NULL)
			return ENOMEM;
		for (j = 0; j < ge->num_rules; ++j) {
			gr = &ge->rules[j];
			if (read(fd, &n, sizeof(n)) < 0)
				return errno;
			gr->num_elements = n;
			gr->elements = calloc(gr->num_elements, sizeof(int));
			assert(gr->elements);
			if (gr->elements == NULL)
				return ENOMEM;
			if (read(fd, gr->elements, n * sizeof(gr->elements[0])) < 0)
				return errno;
		}
	}

	if (read(fd, &n, sizeof(n)) < 0)
		return errno;
	this->num_item_sets = n;
	this->item_sets = calloc(this->num_item_sets, sizeof(*gis));
	if (this->item_sets == NULL)
		return ENOMEM;
	for (i = 0; i < this->num_item_sets; ++i) {
		gis = &this->item_sets[i];
		if (read(fd, &n, sizeof(n)) < 0)
			return errno;
		gis->num_kernel_items = n;
		if (read(fd, &n, sizeof(n)) < 0)
			return errno;
		gis->num_closure_items = n;
		gis->items = calloc(gis->num_kernel_items + gis->num_closure_items,
							sizeof(*gi));
		for (j = 0; j < gis->num_kernel_items + gis->num_closure_items; ++j) {
			gi = &gis->items[j];
			if (read(fd, &gi->element, sizeof(gi->element)) < 0)
				return errno;
			if (read(fd, &gi->rule, sizeof(gi->rule)) < 0)
				return errno;
			if (read(fd, &gi->dot_position, sizeof(gi->dot_position)) < 0)
				return errno;
			if (read(fd, &gi->jump, sizeof(gi->element)) < 0)
				return errno;
			if (read(fd, &n, sizeof(n)) < 0)
				return errno;
			gi->num_look_aheads = n;
			assert(n);
			gi->look_aheads = calloc(gi->num_look_aheads, sizeof(int));
			if (gi->look_aheads == NULL)
				return ENOMEM;
			if (read(fd, gi->look_aheads, n * sizeof(gi->look_aheads[0])) < 0)
				return errno;
		}
	}
	close(fd);
	return ESUCCESS;
}
