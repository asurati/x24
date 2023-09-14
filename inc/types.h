/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef INC_TYPES_H
#define INC_TYPES_H

#include <inc/errno.h>

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define NULL_CHAR	'\0'

/* Defined in main.c */
err_t	mkstemp(int *out_fd,
				const char **out_name);
char	*strdup(const char *str);

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))
/*****************************************************************************/
/*
 * The delete routine receives this->entries[index] */
typedef void fn_ptrq_delete_entry(void *p);
struct ptr_queue {
	void **entries;
	fn_ptrq_delete_entry	*delete;
	int		num_entries;
	int		num_entries_allocated;
	int		read;
};

void	*ptrq_remove_entry(struct ptr_queue *this,
						   const int index);
err_t	ptrq_add_tail(struct ptr_queue *this,
					  void *entry);
err_t	ptrq_add_head(struct ptr_queue *this,
					  void *entry);

#define PTRQ_FOR_EACH(q, ix, e)	\
	for (ix = 0;	\
		 (e = ix < ptrq_num_entries(q) ? ptrq_peek_entry(q, ix) : NULL);	\
		 ++ix)

#define PTRQ_FOR_EACH_WITH_REMOVE(q, e)	\
	while ((e = ptrq_is_empty(q) ? NULL : ptrq_remove_head(q)))

#define PTRQ_FOR_EACH_REVERSE(q, ix, e)	\
	for (ix = ptrq_num_entries(q) - 1;	\
		 e = ix >= 0 ? ptrq_peek_entry(q, ix) : NULL; --ix)

#define PTRQ_FOR_EACH_WITH_REMOVE_REVERSE(q, e)	\
	while ((e = ptrq_is_empty(q) ? NULL : ptrq_remove_tail(q)))

static inline
void ptrq_init(struct ptr_queue *this,
			   fn_ptrq_delete_entry *delete)
{
	this->num_entries_allocated = this->num_entries = this->read = 0;
	this->delete = delete;
	this->entries = NULL;
}

static inline
bool ptrq_is_empty(const struct ptr_queue *this)
{
	return this->num_entries == 0;
}

static inline
bool ptrq_is_full(const struct ptr_queue *this)
{
	return this->num_entries == this->num_entries_allocated;
}

static inline
int ptrq_num_entries(const struct ptr_queue *this)
{
	return this->num_entries;
}

/* Returns this->entries[index] and not &this->entries[index] */
static inline
void *ptrq_peek_entry(const struct ptr_queue *this,
					  const int index)
{
	int pos;
	assert(0 <= index && index < this->num_entries);
	pos = (this->read + index) % this->num_entries_allocated;
	return this->entries[pos];
}

static inline
bool ptrq_find(const struct ptr_queue *this,
			   const void *entry)
{
	int i;
	for (i = 0; i < this->num_entries; ++i) {
		if (this->entries[i] == entry)
			return true;
	}
	return false;
}

static inline
void ptrq_delete_entry(struct ptr_queue *this,
					   const int index)
{
	this->delete(ptrq_peek_entry(this, index));
	ptrq_remove_entry(this, index);
}

static inline
void *ptrq_peek_head(const struct ptr_queue *this)
{
	assert(!ptrq_is_empty(this));
	return ptrq_peek_entry(this, 0);
}

static inline
void *ptrq_peek_tail(const struct ptr_queue *this)
{
	assert(!ptrq_is_empty(this));
	return ptrq_peek_entry(this, this->num_entries - 1);
}

static inline
void *ptrq_remove_head(struct ptr_queue *this)
{
	assert(!ptrq_is_empty(this));
	return ptrq_remove_entry(this, 0);
}

static inline
void *ptrq_remove_tail(struct ptr_queue *this)
{
	assert(!ptrq_is_empty(this));
	return ptrq_remove_entry(this, this->num_entries - 1);
}

static inline
void ptrq_delete_head(struct ptr_queue *this)
{
	assert(!ptrq_is_empty(this));
	ptrq_delete_entry(this, 0);
}

static inline
void ptrq_delete_tail(struct ptr_queue *this)
{
	assert(!ptrq_is_empty(this));
	ptrq_delete_entry(this, this->num_entries - 1);
}

static inline
void ptrq_empty(struct ptr_queue *this)
{
	while (!ptrq_is_empty(this))
		ptrq_delete_head(this);
}

static inline
err_t ptrq_move(struct ptr_queue *this,
				struct ptr_queue *to)
{
	err_t err;
	while (!ptrq_is_empty(this)) {
		err = ptrq_add_tail(to, ptrq_remove_head(this));
		if (err)
			return err;
	}
	return ESUCCESS;
}
/*****************************************************************************/
/*
 * The pointer passed here is &this->entries[index]. The delete routine should
 * not free that pointer.
 */
typedef void fn_valq_delete_entry(void *p);
struct val_queue {
	int	num_entries;
	int	num_entries_allocated;
	int	read;
	int	entry_size;
	char *entries;
	fn_valq_delete_entry	*delete;
};

void	valq_remove_entry(struct val_queue *this,
						  const int index);
err_t	valq_add_tail(struct val_queue *this,
					  const void *entry);
err_t	valq_add_head(struct val_queue *this,
					  const void *entry);

#define valq_copy(dst, q, ix)	\
	memcpy(dst, valq_peek_entry(q, ix), (q)->entry_size)

#define VALQ_FOR_EACH(q, ix, e)	\
	for (ix = 0; ix < valq_num_entries(q) ?	valq_copy(&e, q, ix) : false; ++ix)

#define VALQ_FOR_EACH_WITH_REMOVE(q, e)	\
	while (valq_is_empty(q) ? false :	\
		   valq_copy(&e, q, 0), valq_remove_head(q), true)

#define VALQ_FOR_EACH_REVERSE(q, ix, e)	\
	for (ix = valq_num_entries(q) - 1;	\
		 ix >= 0 ? valq_copy(&e, q, ix) : false; ++ix)

#define VALQ_FOR_EACH_WITH_REMOVE_REVERSE(q, e)	\
	while (valq_is_empty(q) ? false :	\
		   (valq_copy(&e, q, valq_num_entries(q) - 1),	\
			valq_remove_tail(q), true))

static inline
void valq_init(struct val_queue *this,
			   const size_t entry_size,
			   fn_valq_delete_entry *delete)
{
	this->num_entries_allocated = this->num_entries = this->read = 0;
	this->delete = delete;
	this->entry_size = entry_size;
	this->entries = NULL;
}

static inline
bool valq_is_empty(const struct val_queue *this)
{
	return this->num_entries == 0;
}

static inline
bool valq_is_full(const struct val_queue *this)
{
	return this->num_entries == this->num_entries_allocated;
}

static inline
int valq_entry_size(const struct val_queue *this)
{
	return this->entry_size;
}

static inline
int valq_num_entries(const struct val_queue *this)
{
	return this->num_entries;
}

/* Returns &this->entries[index] */
static inline
void *valq_peek_entry(const struct val_queue *this,
					  const int index)
{
	int pos;
	assert(0 <= index && index < this->num_entries);
	pos = (this->read + index) % this->num_entries_allocated;
	return this->entries + pos * this->entry_size;
}

static inline
void valq_delete_entry(struct val_queue *this,
					   const int index)
{
	this->delete(valq_peek_entry(this, index));
	valq_remove_entry(this, index);
}

static inline
void *valq_peek_head(const struct val_queue *this)
{
	assert(!valq_is_empty(this));
	return valq_peek_entry(this, 0);
}

static inline
void *valq_peek_tail(const struct val_queue *this)
{
	assert(!valq_is_empty(this));
	return valq_peek_entry(this, this->num_entries - 1);
}

static inline
void valq_remove_head(struct val_queue *this)
{
	assert(!valq_is_empty(this));
	valq_remove_entry(this, 0);
}

static inline
void valq_remove_tail(struct val_queue *this)
{
	assert(!valq_is_empty(this));
	valq_remove_entry(this, this->num_entries - 1);
}

static inline
void valq_delete_head(struct val_queue *this)
{
	assert(!valq_is_empty(this));
	valq_delete_entry(this, 0);
}

static inline
void valq_delete_tail(struct val_queue *this)
{
	assert(!valq_is_empty(this));
	valq_delete_entry(this, this->num_entries - 1);
}

static inline
void valq_empty(struct val_queue *this)
{
	while (!valq_is_empty(this))
		valq_delete_head(this);
}

static inline
err_t valq_move(struct val_queue *this,
				struct val_queue *to)
{
	err_t err;
	void *entry;

	while (!valq_is_empty(this)) {
		entry = valq_peek_head(this);
		err = valq_add_tail(to, entry);
		if (err)
			return err;
		valq_remove_head(this);
	}
	return ESUCCESS;
}
/*****************************************************************************/
struct ptr_tree {
	void	*parent;
	struct ptr_queue	q;	/* q of children */
};

#define PTRT_FOR_EACH_CHILD(t, ix, c)	PTRQ_FOR_EACH(&(t)->q, ix, c)

static inline
void ptrt_init(struct ptr_tree *this,
			   fn_ptrq_delete_entry *delete)
{
	this->parent = NULL;
	ptrq_init(&this->q, delete);
}

static inline
void *ptrt_parent(const struct ptr_tree *this)
{
	return this->parent;
}

static inline
bool ptrt_has_children(const struct ptr_tree *this)
{
	return !ptrq_is_empty(&this->q);
}

static inline
int ptrt_num_children(const struct ptr_tree *this)
{
	return ptrq_num_entries(&this->q);
}

static inline
void *ptrt_peek_child(const struct ptr_tree *this,
					  const int index)
{
	return ptrq_peek_entry(&this->q, index);
}

static inline
void *ptrt_remove_child(struct ptr_tree *this,
						const int index)
{
	return ptrq_remove_entry(&this->q, index);
}

static inline
void ptrt_delete_child(struct ptr_tree *this,
					   const int index)
{
	ptrq_delete_entry(&this->q, index);
}

static inline
void *ptrt_peek_head_child(const struct ptr_tree *this)
{
	return ptrt_peek_child(this, 0);
}

static inline
void *ptrt_remove_head_child(struct ptr_tree *this)
{
	return ptrt_remove_child(this, 0);
}

static inline
void ptrt_delete_head_child(struct ptr_tree *this)
{
	ptrq_delete_head(&this->q);
}

static inline
void *ptrt_peek_tail_child(const struct ptr_tree *this)
{
	return ptrt_peek_child(this, ptrt_num_children(this) - 1);
}

static inline
void *ptrt_remove_tail_child(struct ptr_tree *this)
{
	return ptrt_remove_child(this, ptrt_num_children(this) - 1);
}

static inline
void ptrt_delete_tail_child(struct ptr_tree *this)
{
	ptrq_delete_tail(&this->q);
}

static inline
err_t ptrt_add_head_child(struct ptr_tree *this,
						  void *child)
{
	return ptrq_add_head(&this->q, child);
}

static inline
err_t ptrt_add_tail_child(struct ptr_tree *this,
						  void *child)
{
	return ptrq_add_tail(&this->q, child);
}

static inline
void ptrt_empty(struct ptr_tree *this)
{
	while (!ptrt_has_children(this))
		ptrt_delete_head_child(this);
}
#endif
