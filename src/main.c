/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#include <inc/cpp/scanner.h>

#include <stdio.h>
#include <locale.h>
/*****************************************************************************/
int main(int argc, char **argv)
{
	err_t err;
	struct scanner *scanner;

	if (argc != 2) {
		printf("Usage: %s path.to.src.c\n", argv[0]);
		return EINVAL;
	}
	setlocale(LC_ALL, "en_US.utf8");
	err = scanner_new(&scanner);
	if (!err)
		err = scanner_scan(scanner, argv[1]);
	scanner_delete(scanner);
	setlocale(LC_ALL, "C");
	return err;
}
