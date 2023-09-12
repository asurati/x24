/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef INC_TREE_H
#define INC_TREE_H

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

#ifndef container_of
#define container_of(p, t, m)	((t *)(void *)((char *)p - offsetof(t, m)))
#endif

struct tree_entry {
	struct tree_entry	*parent;
	struct tree_entry	*next_sibling;
	struct tree_entry	*last_sibling;
	struct tree_entry	*child;		/* first child */
};

static inline
void tree_init(struct tree_entry *this)
{
	this->parent = this->child = NULL;
	this->next_sibling = this->last_sibling = NULL;
}

static inline
void tree_add_sibling(struct tree_entry *this,
					  struct tree_entry *entry)
{
	if (this->next_sibling == NULL) {
		assert(this->last_sibling == NULL);
		this->next_sibling = this->last_sibling = entry;
		return;
	}
	assert(this->last_sibling);
	assert(this->last_sibling->next_sibling == NULL);
	this->last_sibling->next_sibling = entry;
	this->last_sibling = entry;
}

static inline
void tree_add_child(struct tree_entry *this,
					struct tree_entry *entry)
{
	entry->parent = this;
	if (this->child == NULL)
		this->child = entry;
	else
		tree_add_sibling(this->child, entry);
}

static inline
bool tree_has_sibling(const struct tree_entry *this)
{
	return this->next_sibling;
}

static inline
bool tree_has_child(const struct tree_entry *this)
{
	return this->child;
}

static inline
struct tree_entry *tree_peek_child(struct tree_entry *this)
{
	assert(this->child);
	return this->child;
}

static inline
struct tree_entry *tree_peek_sibling(struct tree_entry *this)
{
	assert(this->next_sibling);
	return this->next_sibling;
}

static inline
struct tree_entry *tree_delete_child(struct tree_entry *this)
{
	struct tree_entry *child = this->child;
	assert(child);
	this->child = NULL;
	if (tree_has_sibling(child))
		this->child = child->next_sibling;
	return child;
}
#endif
