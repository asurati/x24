/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#include <inc/types.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <locale.h>

#include <sys/stat.h>

err_t mkstemp(int *out_fd,
			  const char **out_name)
{
	err_t err;
	int i, j, fd, len;
	char *name;
	static char temp_file_name[] = "/tmp/x24.tmp.XXXXXX";
	static const int flags = O_RDWR | O_CREAT | O_EXCL;
	static const mode_t mode = S_IRUSR | S_IWUSR;
	static const char *digits =
		"abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"0123456789"
		"_!=@^-+.#$";

	len = strlen(temp_file_name);
	srand(time(NULL));
	for (i = 0; i < 100; ++i) {
		for (j = 0; j < 6; ++j)
			temp_file_name[len - 6 + j] = digits[rand() % 72];
		fd = open(temp_file_name, flags, mode);
		if (fd < 0) {
			err = errno;
			if (err == EEXIST)
				continue;
			break;
		}
		name = malloc(strlen(temp_file_name) + 1);
		if (name == NULL)
			return ENOMEM;
		strcpy(name, temp_file_name);
		*out_fd = fd;
		*out_name = name;
		return ESUCCESS;
	}
	if (i == 100)
		err = EBUSY;
	return err;
}

char *strdup(const char *str)
{
	char *p = malloc(strlen(str) + 1);
	if (p == NULL)
		return NULL;
	strcpy(p, str);
	return p;
}
/*****************************************************************************/
static
err_t queue_realloc(struct queue *this)
{
	int num_entries_allocated, i, num_allocate;
	size_t size;

	/* first time alloc */
	if (this->read < 0) {
		assert(this->num_entries_allocated == 0);
		assert(this->num_entries == 0);
		assert(this->entries == NULL);
		this->read = 0;
	}
	assert(this->read >= 0);
	/*
	 * a state such as read == 32, num_entries == 80 and
	 * num_entries_allocated == 80 can be reached. We must. Here, there are
	 * 32 objects, indices [0, 31] that are moved by the original loop
	 * below that ran from 0 to this->read. But the allocation only
	 * increases by 16, not by 32, causing write beyond boundaries, heap
	 * overflow. Hence, we must only move the max of
	 * (this->read, 16).
	 *
	 * But this won't work if we are trying to add to the head of the
	 * queue, i.e. to index r-1. In that case the we must allocate
	 * this->read, and not just 16.
	 *
	 * align_up this->read to the next multiple of 16.
	 * (read + 0xf) & (~0xf)
	 * So we allcate max(((this->read + 15) & ~15), 16)
	 */
	num_allocate = (this->read - 0) + 0xf;
	num_allocate &= ~0xf;
	if (num_allocate == 0)
		num_allocate = 16;

	num_entries_allocated = this->num_entries_allocated;	/* save */
	this->num_entries_allocated += num_allocate;
	size = this->num_entries_allocated * sizeof(void *);
	this->entries = realloc(this->entries, size);
	if (this->entries == NULL)
		return ENOMEM;

	/* Move the pointers [0, r-1] to [prev-nia, prev-nia + r - 1] */
	for (i = 0; i < this->read; ++i)
		this->entries[num_entries_allocated + i] = this->entries[i];
	return ESUCCESS;
}
/*
 * queue of pointers. one slot is always kept empty.
 * read pointer is set to 0, and num_entries is set to 0.
 * read pointer points to the entry last read.
 * to add to the tail/backof the queue:
 *    the new position is read + num_entries + 1 mod num_entries_allocated;
 *    if the new position is same as read, we are full, and must reallocate.
 *    if queue is not full, store at r, set r to r-1, incr. num_entries.
 */
err_t queue_add_tail(struct queue *this,
					 void *entry)
{
	err_t err;
	int pos;

	err = ESUCCESS;
	if (queue_is_full(this))
		err = queue_realloc(this);
	if (err)
		return err;
	pos = (this->read + this->num_entries) % this->num_entries_allocated;
	this->entries[pos] = entry;
	++this->num_entries;
	return ESUCCESS;
}

/*
 * to add to the head/front of the queue:
 *    if the queue is full, then expand, store at r and set r to r-1, incr
 *    num_entries.
 */
err_t queue_add_head(struct queue *this,
					 void *entry)
{
	err_t err;

	err = ESUCCESS;
	if (queue_is_full(this))
		err = queue_realloc(this);
	if (err)
		return err;
	--this->read;
	if (this->read == -1)
		this->read += this->num_entries_allocated;
	if (!(0 <= this->read && this->read < this->num_entries_allocated))
		for (;;);
	assert(0 <= this->read && this->read < this->num_entries_allocated);

	this->entries[this->read] = entry;
	++this->num_entries;
	return ESUCCESS;
}

err_t queue_move(struct queue *this,
				 struct queue *out)
{
	void *entry;
	err_t err;

	queue_for_each_with_rem(this, entry) {
		err = queue_add_tail(out, entry);
		if (err)
			return err;
	}
	queue_delete(this);
	return ESUCCESS;
}
