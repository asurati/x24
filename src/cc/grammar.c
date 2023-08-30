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
 * The first is an 32-bit int that gives the # of non-temrinals elements.
 * The non-terminal elements follow according to their index in incr. order.
 * cc_token_type	(32-bit int)
 *  first, 32-bit int num_rules
 *  for each rule,
 *	  num_elements (32-bit int)
 *	  rhs elements array
 */
err_t cc_load_grammar(struct compiler *this)
{
	err_t err;
	int fd, i, j, k, num_elements, num_rules, num_rhs;
	struct cc_grammar_element ge;
	struct cc_grammar_rule gr;
	enum cc_token_type type;

	fd = open("grammar.bin", O_RDONLY);
	if (fd < 0)
		return errno;
	if (read(fd, &num_elements, sizeof(num_elements)) < 0)
		return errno;
	assert(num_elements);	/* # of non-terminal elements */
	/* All elements are ordered by their type in incr. order */
	for (i = 0; i < num_elements; ++i) {
		valq_init(&ge.rules, sizeof(gr), cc_grammar_rule_delete);

		if (read(fd, &ge.type, sizeof(ge.type)) < 0)
			return errno;
		assert(ge.type >= CC_TOKEN_TRANSLATION_OBJECT);
		/* Must be a non-terminal */

		if (read(fd, &num_rules, sizeof(num_rules)) < 0)
			return errno;
		assert(num_rules);

		for (j = 0; j < num_rules; ++j) {
			valq_init(&gr.elements, sizeof(int), NULL);
			err = valq_add_tail(&gr.elements, &ge.type);	/* lhs */
			if (err)
				return err;
			if (read(fd, &num_rhs, sizeof(num_rhs)) < 0)
				return errno;
			assert(num_rhs);
			for (k = 0; k < num_rhs; ++k) {
				if (read(fd, &type, sizeof(type)) < 0)
					return errno;
				err = valq_add_tail(&gr.elements, &type);
				if (err)
					return err;
			}
			err = valq_add_tail(&ge.rules, &gr);
			if (err)
				return err;
		}
		err = valq_add_tail(&this->elements, &ge);
		if (err)
			return err;
	}
	close(fd);
	return ESUCCESS;
}
