/*-
 * Copyright (c) 2008-2012 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

/*
 * __wt_config_check--
 *	Check that all keys in an application-supplied config string match
 *	what is specified in the check string.
 *	The final parameter is optional, and allows for passing in strings
 *	that are not NULL terminated.
 *
 * All check strings are generated by dist/config.py from the constraints given
 * in dist/api_data.py
 */
int
__wt_config_check(WT_SESSION_IMPL *session,
    WT_CONFIG_CHECK checks[], const char *config, size_t config_len)
{
	WT_CONFIG parser, cparser, sparser;
	WT_CONFIG_ITEM k, v, ck, cv, dummy;
	WT_DECL_RET;
	int badtype, found, i;

	/* It is always okay to pass NULL. */
	if (config == NULL)
		return (0);

	if (config_len == 0)
		WT_RET(__wt_config_init(session, &parser, config));
	else
		WT_RET(__wt_config_initn(session, &parser, config, config_len));
	while ((ret = __wt_config_next(&parser, &k, &v)) == 0) {
		if (k.type != ITEM_STRING && k.type != ITEM_ID)
			WT_RET_MSG(session, EINVAL,
			    "Invalid configuration key found: '%.*s'",
			    (int)k.len, k.str);

		/* The config check array is sorted, so exit on first found */
		for (i = 0, found = 0; checks[i].name != NULL; i++) {
			if (WT_STRING_CASE_MATCH(
			    checks[i].name, k.str, k.len)) {
				found = 1;
				break;
			}
		}

		if (!found)
			WT_RET_MSG(session, EINVAL,
			    "Unknown configuration key found: '%.*s'",
			    (int)k.len, k.str);

		if (strcmp(checks[i].type, "int") == 0)
			badtype = (v.type != ITEM_NUM);
		else if (strcmp(checks[i].type, "boolean") == 0)
			badtype = (v.type != ITEM_NUM ||
			    (v.val != 0 && v.val != 1));
		else if (strcmp(checks[i].type, "list") == 0)
			badtype = (v.len > 0 && v.type != ITEM_STRUCT);
		else if (strcmp(checks[i].type, "category") == 0) {
			/*
			 * Deal with categories that could are of the form:
			 * XXX=(XXX=blah)
			 */
			ret = __wt_config_check(session,
			    checks[i].subconfigs,
			    k.str + strlen(checks[i].name) + 1, v.len);
			if (ret != EINVAL)
				badtype = 0;
			else
				badtype = 1;
		} else
			badtype = 0;

		if (badtype)
			WT_RET_MSG(session, EINVAL,
			    "Invalid value type for key '%.*s': expected a %s",
			    (int)k.len, k.str, checks[i].type);

		if (checks[i].checks == NULL)
			continue;

		/* Setup an iterator for the check string. */
		WT_RET(__wt_config_init(session, &cparser, checks[i].checks));
		while ((ret = __wt_config_next(&cparser, &ck, &cv)) == 0) {
			if (WT_STRING_MATCH("min", ck.str, ck.len)) {
				if (v.val < cv.val)
					WT_RET_MSG(session, EINVAL,
					    "Value too small for key '%.*s' "
					    "the minimum is %.*s",
					    (int)k.len, k.str,
					    (int)cv.len, cv.str);
			} else if (WT_STRING_MATCH("max", ck.str, ck.len)) {
				if (v.val > cv.val)
					WT_RET_MSG(session, EINVAL,
					    "Value too large for key '%.*s' "
					    "the maximum is %.*s",
					    (int)k.len, k.str,
					    (int)cv.len, cv.str);
			} else if (WT_STRING_MATCH("choices", ck.str, ck.len)) {
				if (v.len == 0)
					WT_RET_MSG(session, EINVAL,
					    "Key '%.*s' requires a value",
					    (int)k.len, k.str);
				if (v.type == ITEM_STRUCT) {
					/*
					 * Handle the 'verbose' case of a list
					 * containing restricted choices.
					 */
					WT_RET(__wt_config_subinit(session,
					    &sparser, &v));
					found = 1;
					while (found &&
					    (ret = __wt_config_next(&sparser,
					    &v, &dummy)) == 0) {
						ret = __wt_config_subgetraw(
						    session, &cv, &v, &dummy);
						found = (ret == 0);
					}
				} else  {
					ret = __wt_config_subgetraw(session,
					    &cv, &v, &dummy);
					found = (ret == 0);
				}

				if (ret != 0 && ret != WT_NOTFOUND)
					return (ret);
				if (!found)
					WT_RET_MSG(session, EINVAL,
					    "Value '%.*s' not a "
					    "permitted choice for key '%.*s'",
					    (int)v.len, v.str,
					    (int)k.len, k.str);
			} else
				WT_RET_MSG(session, EINVAL,
				    "unexpected configuration description "
				    "keyword %.*s", (int)ck.len, ck.str);
		}
	}

	if (ret == WT_NOTFOUND)
		ret = 0;

	return (ret);
}
