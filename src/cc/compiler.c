/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#include "compiler.h"

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/*****************************************************************************/
err_t compiler_new(const char *path,
				   struct compiler **out)
{
	struct compiler *this;

	this = malloc(sizeof(*this));
	if (this == NULL)
		return ENOMEM;
	this->cpp_tokens_path = path;
	*out = this;
	return ESUCCESS;
}

err_t compiler_delete(struct compiler *this)
{
	assert(this);
	free((void *)this->cpp_tokens_path);
	free(this);
	return ESUCCESS;
}
/*****************************************************************************/
err_t compiler_compile(struct compiler *this)
{
	return ENOTSUP;
	(void)this;
}
