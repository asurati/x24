/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */

/*
 * These are tokens for the parse tree. Included after inc/cpp/tokens.h.
 * All of these are non-terminals.
 */
#if 0
DEF(TRANSLATION_OBJECT)	/* Must be kept first in this file */
DEF(TRANSLATION_UNIT)
DEF(EXTERNAL_DECLARATION)
DEF(PRIMARY_EXPRESSION)
DEF(EXPRESSION)
DEF(GENERIC_SELECTION)
DEF(ASSIGNMENT_EXPRESSION)
DEF(GENERIC_ASSOC_LIST)
DEF(GENERIC_ASSOCIATION)
DEF(TYPE_NAME)
DEF(POSTFIX_EXPRESSION)
DEF(ARGUMENT_EXPRESSION_LIST)
DEF(COMPOUND_LITERAL)
DEF(BRACED_INITIALIZER)
DEF(STORAGE_CLASS_SPECIFIERS)
DEF(STORAGE_CLASS_SPECIFIER)
DEF(UNARY_EXPRESSION)
DEF(UNARY_OPERATOR)
DEF(CAST_EXPRESSION)
DEF(MULTIPLICATIVE_EXPRESSION)
DEF(ADDITIVE_EXPRESSION)
DEF(SHIFT_EXPRESSION)
DEF(RELATIONAL_EXPRESSION)
DEF(EQUALITY_EXPRESSION)
DEF(AND_EXPRESSION)
DEF(EXLUSIVE_OR_EXPRESSION)
DEF(INCLUSIVE_OR_EXPRESSION)
DEF(LOGICAL_AND_EXPRESSION)
DEF(LOGICAL_OR_EXPRESSION)
DEF(CONDITIONAL_EXPRESSION)
DEF(ASSIGNMENT_OPERATOR)
DEF(CONSTANT_EXPRESSION)
DEF(DECLARATION)
DEF(DECLARATION_SPECIFIERS)
DEF(INIT_DECLARATOR_LIST)
DEF(ATTRIBUTE_SPECIFIER_SEQUENCE)
DEF(STATIC_ASSERT_DECLARATION)
DEF(ATTRIBUTE_DECLARATION)
DEF(DECLARATION_SPECIFIER)
DEF(TYPE_SPECIFIER_QUALIFIER)
DEF(FUNCTION_SPECIFIER)
DEF(INIT_DECLARATOR)
DEF(DECLARATOR)
DEF(INITIALIZER)
DEF(TYPE_SPECIFIER)
DEF(ATOMIC_TYPE_SPECIFIER)
DEF(STRUCT_OR_UNION_SPECIFIER)
DEF(ENUM_SPECIFIER)
DEF(TYPE_DEF_NAME)
DEF(TYPE_OF_SPECIFIER)
DEF(STRUCT_OR_UNION)
DEF(MEMBER_DECLARATION_LIST)
DEF(MEMBER_DECLARATION)
DEF(SPECIFIER_QUALIFIER_LIST)
DEF(ALIGNMENT_SPECIFIER)
DEF(MEMBER_DECLARATOR_LIST)
DEF(MEMBER_DECLARATOR)
DEF(ENUMERATOR_LIST)
DEF(ENUM_TYPE_SPECIFIER)
DEF(ENUMERATOR)
DEF(ENUMERATION_CONSTANT)
DEF(TYPE_OF_SPECIFIER_ARGUMENT)
DEF(TYPE_QUALIFIER)
DEF(DIRECT_DECLARATOR)
DEF(ARRAY_DECLARATOR)
DEF(FUNCTION_DECLARATOR)
DEF(PARAMETER_TYPE_LIST)
DEF(TYPE_QUALIFIER_LIST)
DEF(PARAMETER_LIST)
DEF(PARAMETER_DECLARATION)
DEF(ABSTRACT_DECLARATOR)
DEF(POINTER)
DEF(DIRECT_ABSTRACT_DECLARATOR)
DEF(ARRAY_ABSTRACT_DECLARATOR)
DEF(FUNCTION_ABSTRACT_DECLARATOR)
DEF(INITIALIZER_LIST)
DEF(DESIGNATION)
DEF(DESIGNATOR_LIST)
DEF(DESIGNATOR)
DEF(ATTRIBUTE_SPECIFIER)
DEF(ATTRIBUTE_LIST)
DEF(ATTRIBUTE)
DEF(ATTRIBUTE_TOKEN)
DEF(ATTRIBUTE_ARGUMENT_CLAUSE)
DEF(STANDARD_ATTRIBUTE)
DEF(ATTRIBUTE_PREFIXED_TOKEN)
DEF(ATTRIBUTE_PREFIX)
DEF(BALANCED_TOKEN_SEQUENCE)
DEF(BALANCED_TOKEN)
DEF(STATEMENT)
DEF(LABELED_STATEMENT)
DEF(UNLABELED_STATEMENT)
DEF(EXPRESSION_STATEMENT)
DEF(PRIMARY_BLOCK)
DEF(JUMP_STATEMENT)
DEF(SELECTION_STATEMENT)
DEF(COMPOUND_STATEMENT)
DEF(ITERATION_STATEMENT)
DEF(SECONDARY_BLOCK)
DEF(LABEL)
DEF(BLOCK_ITEM_LIST)
DEF(BLOCK_ITEM)
DEF(FUNCTION_DEFINITION)
DEF(FUNCTION_BODY)
#else
/*
 * The grammar non-terminals are commented out. The hand-written parser
 * doesn't need all of them. Those which are needed are here.
 * lr/earley parsers, and txt.to.bin do need them.
 */
NODE(TRANSLATION_UNIT)
NODE(DECLARATION_SPECIFIERS)
NODE(DECLARATOR)
NODE(ABSTRACT_DECLARATOR)
NODE(ATTRIBUTE_DECLARATION)
#endif
/* Grammar (CC) terminals and non-terminals end here */

/*
 * Following are non-terminals that are refinement of some of the non-termianls
 * above. These are not used in the grammar proper (i.e. in grammar.txt). These
 * should be used only when dealing with ast and beyond.
 */
NODE(TYPE_SPECIFIERS)
NODE(TYPE_QUALIFIERS)
NODE(FUNCTION_SPECIFIERS)
NODE(STORAGE_SPECIFIERS)
NODE(ATTRIBUTES)

NODE(SYMBOLS)	/* The symbol table */
NODE(SYMBOL)	/* Objects and Functions */
NODE(SYMBOL_TYPE)
NODE(SYMBOL_TYPE_DEF)	/* Always fully resolved */
NODE(BLOCK)
