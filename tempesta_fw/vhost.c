/**
 *		Tempesta FW
 *
 * Copyright (C) 2016 Tempesta Technologies, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "tempesta_fw.h"
#include "http_match.h"
#include "vhost.h"
#include "str.h"

/* Mappings for match operators. */
static const TfwCfgEnum const __read_mostly tfw_match_enum[] = {
	{ "*",		TFW_HTTP_MATCH_O_WILDCARD },
	{ "eq",		TFW_HTTP_MATCH_O_EQ },
	{ "prefix",	TFW_HTTP_MATCH_O_PREFIX },
	{ "suffix",	TFW_HTTP_MATCH_O_SUFFIX },
	{}
};

/*
 * All cache policy directives are put into a fixed size array.
 * The directives are deduplicated when put into the array.
 * Individual directives are linked to from lists of cache policy
 * directives for specific location sections.
 */
#define TFW_CAPOLICY_ARRAY_SZ	(64)

static TfwCaPolicy	tfw_capolicy[TFW_CAPOLICY_ARRAY_SZ];
static unsigned int	tfw_capolicy_sz = 0;	/* Current size. */

/*
 * All 'location' directives are put into a fixed size array.
 * Duplicate directives are not allowed.
 */
#define TFW_LOCATION_ARRAY_SZ	(64)

static TfwLocation	tfw_location[TFW_LOCATION_ARRAY_SZ];
static unsigned int	tfw_location_sz = 0;	/* Current size. */

/*
 * Default location is a wildcard location. It matches any URI.
 * It may (or may not) contain a set of cache matching directives.
 */
static TfwCaPolicy *tfw_capolicy_dflt[TFW_CAPOLICY_ARRAY_SZ];

static TfwLocation tfw_location_dflt = {
	.op = TFW_HTTP_MATCH_O_WILDCARD,
	.arg = "*",
	.len = 1,
	.capo = tfw_capolicy_dflt,
	.capo_sz = 0,
};

/*
 * IP addresses that make the ACL for cache purge operations are put
 * into a fixed size array. The IP addresses are kept in form of an
 * IPv6 address and the prefix size. sockaddr_in6.sin6_scope_id is
 * used to store the prefix size.
 */
#define TFW_CAPUACL_ARRAY_SZ	(32)

static TfwAddr		tfw_capuacl[TFW_CAPUACL_ARRAY_SZ];

/*
 * Default vhost is a wildcard vhost. It matches any URI.
 * It may (or may not) ontain a set of various directives.
 *
 * Note that @loc_dflt in the default vhost serves as global
 * default caching policy.
 */
static const char __read_mostly s_hdr_via_dflt[] =
	"tempesta_fw" " (" TFW_NAME " " TFW_VERSION ")";

static TfwVhost		tfw_vhost_dflt = {
	.hdr_via	= s_hdr_via_dflt,
	.hdr_via_len	= sizeof(s_hdr_via_dflt) - 1,
	.loc		= tfw_location,
	.loc_dflt	= &tfw_location_dflt,
	.loc_dflt_sz	= 1,
	.capuacl	= tfw_capuacl,
};

/*
 * Match the IP address @addr against the addresses in the ACL list.
 * The addresses are compared according to the prefix length stored
 * with each address in the ACL list.
 * True is returned if the match is found.
 * False is returned otherwise.
 */
bool
tfw_capuacl_match(TfwVhost *vhost, TfwAddr *addr)
{
	int i;
	struct in6_addr *inaddr = &addr->v6.sin6_addr;

	for (i = 0; i < vhost->capuacl_sz; ++i) {
		TfwAddr *acl_addr = &vhost->capuacl[i];
		if (ipv6_prefix_equal(inaddr, &acl_addr->v6.sin6_addr,
					      acl_addr->in6_prefix))
			return true;
	}
	return false;
}

/*
 * Matching functions for match operators. A TfwStr{} is compared
 * with a plain C string according to a specified match operator.
 * The functions are generic.
 */
static bool
__tfw_match_wildcard(tfw_match_t op, const char *cstr, int len, TfwStr *arg)
{
	return ((op == TFW_HTTP_MATCH_O_WILDCARD)
		&& (len == 1) && (*cstr == '*'));
}

static bool
__tfw_match_suffix(tfw_match_t op, const char *cstr, int len, TfwStr *arg)
{
	tfw_str_eq_flags_t flags = TFW_STR_EQ_DEFAULT | TFW_STR_EQ_CASEI;
	return tfw_str_eq_cstr_off(arg, arg->len - len, cstr, len, flags);
}

static bool
__tfw_match_eq(tfw_match_t op, const char *cstr, int len, TfwStr *arg)
{
	tfw_str_eq_flags_t flags = TFW_STR_EQ_DEFAULT | TFW_STR_EQ_CASEI;
	return tfw_str_eq_cstr(arg, cstr, len, flags);
}

static bool
__tfw_match_prefix(tfw_match_t op, const char *cstr, int len, TfwStr *arg)
{
	tfw_str_eq_flags_t flags = TFW_STR_EQ_PREFIX | TFW_STR_EQ_CASEI;
	return tfw_str_eq_cstr(arg, cstr, len, flags);
}

typedef bool (*__tfw_match_fn)(tfw_match_t, const char *, int, TfwStr *);

static const __tfw_match_fn const __read_mostly __tfw_match_fn_tbl[] = {
	[0 ... _TFW_HTTP_MATCH_O_COUNT] = NULL,
	[TFW_HTTP_MATCH_O_WILDCARD]	= __tfw_match_wildcard,
	[TFW_HTTP_MATCH_O_EQ]		= __tfw_match_eq,
	[TFW_HTTP_MATCH_O_PREFIX]	= __tfw_match_prefix,
	[TFW_HTTP_MATCH_O_SUFFIX]	= __tfw_match_suffix,
};

/*
 * Find a matching cache policy directive. Strings are compared
 * according to the match operator in the directive. A pointer
 * to the matching TfwCaPolicy structure is returned if the
 * match is found. Null is returned if there's no match.
 */
static inline bool
__tfw_capolicy_match_fn(TfwCaPolicy *capo, TfwStr *arg)
{
	__tfw_match_fn match_fn = __tfw_match_fn_tbl[capo->op];
	BUG_ON(!match_fn);

	return match_fn(capo->op, capo->arg, capo->len, arg);
}

TfwCaPolicy *
tfw_capolicy_match(TfwLocation *loc, TfwStr *arg)
{
	int i;

	BUG_ON(!loc);
	BUG_ON(!arg);

	for (i = 0; i < loc->capo_sz; ++i) {
		TfwCaPolicy *capo = loc->capo[i];
		if (__tfw_capolicy_match_fn(capo, arg))
			return capo;
	}
	return NULL;
}

/*
 * Find a matching location directive within specified vhost.
 * A pointer to the matching TfwLocation structure is returned
 * if the match is found. NULL is returned if there's no match.
 */
static inline bool
__tfw_location_match(TfwLocation *loc, TfwStr *arg)
{
	__tfw_match_fn match_fn = __tfw_match_fn_tbl[loc->op];
	BUG_ON(!match_fn);

	return match_fn(loc->op, loc->arg, loc->len, arg);
}

TfwLocation *
tfw_location_match(TfwVhost *vhost, TfwStr *arg)
{
	int i;

	BUG_ON(!vhost);
	BUG_ON(!arg);

	for (i = 0; i < vhost->loc_sz; ++i) {
		TfwLocation *loc = &vhost->loc[i];
		if (__tfw_location_match(loc, arg))
			return loc;
	}
	return NULL;
}

/*
 * Find a matching vhost directive. Strings are compared according
 * to the match operator in the directive. A pointer to the matching
 * TfwVhost structure is returned if the match is found. A pointer
 * to the default vhost structure is returned if there's no match.
 */
TfwVhost *
tfw_vhost_get_default(void)
{
	return &tfw_vhost_dflt;
}

TfwVhost *
tfw_vhost_match(TfwStr *arg)
{
	BUG_ON(!arg);

	/* For now there's just the default vhost. */
	return &tfw_vhost_dflt;
}

/*
 * Configuration processing.
 */

/*
 * Pointer to the current location structure.
 * The pointer is shared among several functions below.
 */
static TfwLocation *tfwcfg_this_location;

/*
 * Find a cache policy directive entry. The entry is looked up
 * in the array that holds all cache policy directives from all
 * location sections.
 */
static TfwCaPolicy *
tfw_capolicy_lookup(const short cmd, const short op, const char *arg, int len)
{
	int i;

	for (i = 0; i < tfw_capolicy_sz; ++i) {
		TfwCaPolicy *capo = &tfw_capolicy[i];
		if ((capo->cmd == cmd) && (capo->op == op) && (capo->len == len)
		    && !strncasecmp(capo->arg, arg, len))
			return capo;
	}

	return NULL;
}

/*
 * Create and initialize a new cache policy entry. The entry is placed
 * in the array for all cache policy entries from all location sections.
 */
static TfwCaPolicy *
tfw_capolicy_new(const short cmd, const short op, const char *arg, int len)
{
	char *argmem;
	TfwCaPolicy *capo;

	if (tfw_capolicy_sz == TFW_CAPOLICY_ARRAY_SZ)
		return NULL;

	if ((argmem = kmalloc(len + 1, GFP_KERNEL)) == NULL)
		return NULL;

	capo = &tfw_capolicy[tfw_capolicy_sz++];
	capo->cmd = cmd;
	capo->op = op;
	capo->arg = argmem;
	capo->len = len;
	memcpy((void *)capo->arg, (void *)arg, len + 1);

	return capo;
}

/*
 * Add a new cache policy entry to the given location structure.
 * The entry is added as a pointer into the array for all cache
 * policy entries.
 */
static TfwCaPolicy *
tfw_capolicy_add(TfwLocation *loc, TfwCaPolicy *capo)
{
	if (loc->capo_sz == TFW_CAPOLICY_ARRAY_SZ)
		return NULL;
	loc->capo[loc->capo_sz++] = capo;
	return capo;
}

/*
 * Process a cache policy directive. The directive is added to the
 * current location structure. Duplicate directives are ignored but
 * a warning is produced in that case. if a directive lists several
 * strings to match, then an identical directive is added for each
 * string that is listed.
 */
static int
tfw_handle_capolicy(TfwCfgSpec *cs, TfwCfgEntry *ce, const short cmd)
{
	int i, ret, in_len;
	tfw_match_t op;
	const char *in_op, *in_arg;

	BUG_ON(!tfwcfg_this_location);
	BUG_ON((cmd != TFW_D_CACHE_BYPASS) && (cmd != TFW_D_CACHE_FULFILL));

	if (ce->attr_n) {
		TFW_ERR("%s: Arguments may not have the \'=\' sign\n",
			cs->name);
		return -EINVAL;
	}
	if (ce->val_n < 2) {
		TFW_ERR("%s: Invalid number of arguments: %d\n",
			cs->name, (int)ce->val_n);
		return -EINVAL;
	}

	in_op = ce->vals[0];	/* Match operator. */

	/* Convert the match operator string to the enum value. */
	ret = tfw_cfg_map_enum(tfw_match_enum, in_op, &op);
	if (ret) {
		TFW_ERR("Unknown match OP: '%s %s'\n", cs->name, in_op);
		return -EINVAL;
	}

	/* Add each match string in the directive to the array.*/
	for (i = 1; i < ce->val_n; ++i) {
		TfwCaPolicy *capo;

		in_arg = ce->vals[i];
		in_len = strlen(in_arg);

		/* Get the cache policy entry. */
		capo = tfw_capolicy_lookup(cmd, op, in_arg, in_len);
		if (capo) {
			TFW_WARN("%s: Duplicate entry: '%s %s %s'\n",
				 cs->name, cs->name, in_op, in_arg);
			continue;
		}
		capo = tfw_capolicy_new(cmd, op, in_arg, in_len);
		if (!capo)
			return -ENOMEM;
		/* Link the cache policy entry with the location entry. */
		if (!tfw_capolicy_add(tfwcfg_this_location, capo))
			return -ENOENT;
	}

        return 0;
}

/*
 * The configuration parser has recognized the cache policy directive
 * already, so there's no need to spend cycles and convert it again
 * from the string to the enum value. The functions below are for
 * each directive inside the location section, and for each directive
 * outside of any location section.
 */
static int
tfw_handle_in_cache_fulfill(TfwCfgSpec *cs, TfwCfgEntry *ce)
{
	return tfw_handle_capolicy(cs, ce, TFW_D_CACHE_FULFILL);
}

static int
tfw_handle_in_cache_bypass(TfwCfgSpec *cs, TfwCfgEntry *ce)
{
	return tfw_handle_capolicy(cs, ce, TFW_D_CACHE_BYPASS);
}

static int
tfw_handle_out_cache_fulfill(TfwCfgSpec *cs, TfwCfgEntry *ce)
{
	if (!tfwcfg_this_location)
		tfwcfg_this_location = &tfw_location_dflt;
	return tfw_handle_capolicy(cs, ce, TFW_D_CACHE_FULFILL);
}

static int
tfw_handle_out_cache_bypass(TfwCfgSpec *cs, TfwCfgEntry *ce)
{
	if (!tfwcfg_this_location)
		tfwcfg_this_location = &tfw_location_dflt;
	return tfw_handle_capolicy(cs, ce, TFW_D_CACHE_BYPASS);
}

/*
 * Find a location directive entry. The entry is looked up
 * in the array that holds all location directives.
 */
static TfwLocation *
tfw_location_lookup(tfw_match_t op, const char *arg, int len)
{
	int i;

	for (i = 0; i < tfw_location_sz; ++i) {
		TfwLocation *loc = &tfw_location[i];
		if ((loc->op == op) && (loc->len == len)
		    && !strncasecmp(loc->arg, arg, len))
			return loc;
	}

	return NULL;
}

/*
 * Create and initialize a new entry for a location directive.
 * The entry is placed in the array that holds all location directives.
 */
static TfwLocation *
tfw_location_new(tfw_match_t op, const char *arg, int len)
{
	char *argmem;
	TfwLocation *loc;
	TfwCaPolicy **capo;
	size_t size = sizeof(TfwCaPolicy *) * TFW_CAPOLICY_ARRAY_SZ;

	if (tfw_location_sz == TFW_LOCATION_ARRAY_SZ)
		return NULL;

	if ((argmem = kmalloc(len + 1, GFP_KERNEL)) == NULL)
		return NULL;
	if ((capo = kmalloc(size, GFP_KERNEL)) == NULL) {
		kfree(argmem);
		return NULL;
	}

	loc = &tfw_location[tfw_location_sz++];
	loc->op = op;
	loc->arg = argmem;
	loc->len = len;
	loc->capo = capo;
	loc->capo_sz = 0;
	memcpy((void *)loc->arg, (void *)arg, len + 1);

	return loc;
}

/*
 * Process the location directive that opens a section for cache
 * policy directives in the configuration.
 */
static int
tfw_begin_location(TfwCfgSpec *cs, TfwCfgEntry *ce)
{
	int ret, in_len;
	tfw_match_t op;
	const char *in_op, *in_arg;

	if (ce->attr_n) {
		TFW_ERR("%s: Arguments may not have the \'=\' sign\n",
			cs->name);
		return -EINVAL;
	}
	if (ce->val_n != 2) {
		TFW_ERR("%s: Invalid number of arguments: %d\n",
			cs->name, (int)ce->val_n);
		return -EINVAL;
	}

	/* Get the values of the 'location' directive. */
	in_op = ce->vals[0];	/* Match operator. */
	in_arg = ce->vals[1];	/* String for the match operator. */
	in_len = strlen(in_arg);

	/* Convert the match operator string to the enum value. */
	ret = tfw_cfg_map_enum(tfw_match_enum, in_op, &op);
	if (ret) {
		TFW_ERR("%s: Unknown match OP: '%s %s %s'\n",
			cs->name, cs->name, in_op, in_arg);
		return -EINVAL;
	}

	/* Make sure the location is not a duplicate. */
	if (tfw_location_lookup(op, in_arg, in_len)) {
		TFW_ERR("%s: Duplicate entry: '%s %s %s'\n",
			cs->name, cs->name, in_op, in_arg);
		return -EINVAL;
	}

	/* Add new location and set it to be the current one. */
	tfwcfg_this_location = tfw_location_new(op, in_arg, in_len);
	if (tfwcfg_this_location == NULL) {
		TFW_ERR("%s: Unable to add new location: '%s %s %s'\n",
			cs->name, cs->name, in_op, in_arg);
		return -EINVAL;
	}

	return 0;
}

/*
 * Close the section for a location directive.
 */
static int
tfw_finish_location(TfwCfgSpec *cs)
{
	BUG_ON(!tfwcfg_this_location);
	tfwcfg_this_location = NULL;
	return 0;
}

/*
 * Free only the memory that has been allocated while processing
 * configuration directives. Make sure the memory is not freed twice.
 */
static void
__tfw_cleanup_locache(void)
{
	int i;

	for (i = 0; i < tfw_location_sz; ++i) {
		TfwLocation *loc = &tfw_location[i];
		if (loc->arg) {
			kfree(loc->arg);
			loc->arg = NULL;
		}
		if (loc->capo) {
			kfree(loc->capo);
			loc->capo = NULL;
		}
	}
	for (i = 0; i < tfw_capolicy_sz; ++i) {
		TfwCaPolicy *capo = &tfw_capolicy[i];
		if (capo->arg) {
			kfree(capo->arg);
			capo->arg = NULL;
		}
	}
}

static void
tfw_cleanup_locache(TfwCfgSpec *cs)
{
	__tfw_cleanup_locache();
}

/*
 *  Match the ip address against the ACL list.
 */
static bool
tfw_capuacl_lookup(TfwVhost *vhost, TfwAddr *addr)
{
	int i;
	struct in6_addr *inaddr = &addr->v6.sin6_addr;

	for (i = 0; i < vhost->capuacl_sz; ++i) {
		struct in6_addr *acl_inaddr = &vhost->capuacl[i].v6.sin6_addr;
		if (ipv6_prefix_equal(inaddr, acl_inaddr, addr->in6_prefix))
			return true;
	}
	return false;
}

/*
 * Process the cache_purge_acl directive.
 */
static int
tfw_handle_cache_purge_acl(TfwCfgSpec *cs, TfwCfgEntry *ce)
{
	unsigned int i;
	const char *val;
	TfwVhost *vhost = &tfw_vhost_dflt;

	if (ce->attr_n) {
		TFW_ERR("%s: Arguments may not have the \'=\' sign\n",
			cs->name);
		return -EINVAL;
	}
	TFW_CFG_ENTRY_FOR_EACH_VAL(ce, i, val) {
		TfwAddr addr = {};

		if (tfw_addr_pton_cidr(val, &addr)) {
			TFW_ERR("%s: Invalid ACL entry: '%s'\n",
				cs->name, val);
			return -EINVAL;
		}
		/* Make sure the address is not a duplicate. */
		if (tfw_capuacl_lookup(vhost, &addr)) {
			TFW_ERR("%s: Duplicate IP address or prefix: '%s'\n",
				cs->name, val);
			return -EINVAL;
		}
		/* Add new ACL entry. */
		if (vhost->capuacl_sz == TFW_CAPUACL_ARRAY_SZ) {
			TFW_ERR("%s: Unable to add new ACL: '%s'\n",
				cs->name, val);
			return -EINVAL;
		}
		vhost->capuacl[vhost->capuacl_sz++] = addr;
	}
	vhost->cache_purge_acl = 1;

	return 0;
}

/*
 * Process the cache_purge directive.
 */
static int
tfw_handle_cache_purge(TfwCfgSpec *cs, TfwCfgEntry *ce)
{
	unsigned int i;
	const char *val;
	TfwVhost *vhost = &tfw_vhost_dflt;

	if (ce->attr_n) {
		TFW_ERR("%s: Arguments may not have the \'=\' sign\n",
			cs->name);
		return -EINVAL;
	}
	if (!ce->val_n) {
		/* Default value for the cache_purge directive. */
		vhost->cache_purge_mode = TFW_D_CACHE_PURGE_INVALIDATE;
		goto done;
	}
	TFW_CFG_ENTRY_FOR_EACH_VAL(ce, i, val) {
		if (!strcasecmp(val, "invalidate")) {
			vhost->cache_purge_mode = TFW_D_CACHE_PURGE_INVALIDATE;
		} else {
			TFW_ERR("%s: unsupported argument: '%s'\n",
				cs->name, val);
			return -EINVAL;
		}
	}
done:
	vhost->cache_purge = 1;

	return 0;
}

/*
 * Process hdr_via directive.
 * Default value is preset statically.
 */
static int
tfw_handle_hdr_via(TfwCfgSpec *cs, TfwCfgEntry *ce)
{
	int len;
	TfwVhost *vhost = &tfw_vhost_dflt;

	if (ce->attr_n) {
		TFW_ERR("%s: Arguments may not have the \'=\' sign\n",
			cs->name);
		return -EINVAL;
	}
	if (ce->val_n != 1) {
		TFW_ERR("%s: Invalid number of arguments: %d\n",
			cs->name, (int)ce->val_n);
		return -EINVAL;
	}

	/*
	 * If a value is specified in the configuration file, then
	 * the default value is not used, even if the processing of
	 * the specified value results in an error.
	 */
	len = strlen(ce->vals[0]);
	if ((vhost->hdr_via = kmalloc(len + 1, GFP_KERNEL)) == NULL)
		return -ENOMEM;
	memcpy((void *)vhost->hdr_via, (void *)ce->vals[0], len + 1);
	vhost->hdr_via_len = len;

	return 0;
}

static void
__tfw_cleanup_hdrvia(void)
{
	TfwVhost *vhost = &tfw_vhost_dflt;
	if (vhost->hdr_via && (vhost->hdr_via != s_hdr_via_dflt))
		kfree(vhost->hdr_via);
}

static void
tfw_cleanup_hdrvia(TfwCfgSpec *cs)
{
	__tfw_cleanup_hdrvia();
}

static int
tfw_vhost_cfg_start(void)
{
	if (tfw_vhost_dflt.cache_purge && !tfw_vhost_dflt.cache_purge_acl)
		TFW_WARN("cache_purge directive works only in combination"
			 " with cache_purge_acl directive.\n");
	tfw_vhost_dflt.loc_sz = tfw_location_sz;
	return 0;
}

static void
tfw_vhost_cfg_stop(void)
{
	__tfw_cleanup_hdrvia();
	__tfw_cleanup_locache();
}

static TfwCfgSpec tfw_location_specs[] = {
        {
		"cache_bypass", NULL,
		tfw_handle_in_cache_bypass,
		.allow_none = true,
		.allow_repeat = true,
		.cleanup = tfw_cleanup_locache
        },
        {
		"cache_fulfill", NULL,
		tfw_handle_in_cache_fulfill,
		.allow_none = true,
		.allow_repeat = true,
		.cleanup = tfw_cleanup_locache
        },
        {}
};

static TfwCfgSpec tfw_vhost_cfg_specs[] = {
	{
		"hdr_via", NULL,
		tfw_handle_hdr_via,
		.allow_none = true,
		.allow_repeat = false,
		.cleanup = tfw_cleanup_hdrvia
	},
	{
		"cache_purge",
		NULL,
		tfw_handle_cache_purge,
		.allow_none = true,
		.allow_repeat = false,
	},
	{
		"cache_purge_acl",
		NULL,
		tfw_handle_cache_purge_acl,
		.allow_none = true,
		.allow_repeat = true,
	},
	{
		"cache_bypass", NULL,
		tfw_handle_out_cache_bypass,
		.allow_none = true,
		.allow_repeat = true,
		.cleanup = tfw_cleanup_locache
	},
        {
		"cache_fulfill", NULL,
		tfw_handle_out_cache_fulfill,
		.allow_none = true,
		.allow_repeat = true,
		.cleanup = tfw_cleanup_locache
        },
	{
		"location", NULL,
		tfw_cfg_handle_children,
		tfw_location_specs,
		&(TfwCfgSpecChild) {
			.begin_hook = tfw_begin_location,
			.finish_hook = tfw_finish_location
		},
		.allow_none = true,
		.allow_repeat = true,
		/* .cleanup function in a section with
		   children causes a BUG_ON in cfg.c. */
	},
	{},
};

TfwCfgMod tfw_vhost_cfg_mod = {
	.name	= "vhost",
	.start	= tfw_vhost_cfg_start,
	.stop	= tfw_vhost_cfg_stop,
	.specs	= tfw_vhost_cfg_specs,
};

int
tfw_vhost_init(void)
{
	return 0;
}

void
tfw_vhost_exit(void)
{
	int i;

	for (i = 0; i < tfw_location_sz; ++i)
		if (tfw_location[i].capo)
			kfree(tfw_location[i].capo);
}
