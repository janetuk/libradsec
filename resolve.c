/*
 * Copyright (C) 2006-2009 Stig Venaas <venaas@uninett.no>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

void resolve_freehostport(struct hostportres *hp) {
    if (hp) {
	free(hp->host);
	free(hp->port);
	if (hp->addrinfo)
	    freeaddrinfo(hp->addrinfo);
	free(hp);
    }
}

int resolve_parsehostport(struct hostportres *hp, char *hostport, char *default_port) {
    char *p, *field;
    int ipv6 = 0;

    p = hostport;
    /* allow literal addresses and port, e.g. [2001:db8::1]:1812 */
    if (*p == '[') {
	p++;
	field = p;
	for (; *p && *p != ']' && *p != ' ' && *p != '\t' && *p != '\n'; p++);
	if (*p != ']') {
	    debug(DBG_ERR, "no ] matching initial [");
	    return 0;
	}
	ipv6 = 1;
    } else {
	field = p;
	for (; *p && *p != ':' && *p != ' ' && *p != '\t' && *p != '\n'; p++);
    }
    if (field == p) {
	debug(DBG_ERR, "missing host/address");
	return 0;
    }

    hp->host = stringcopy(field, p - field);
    if (ipv6) {
	p++;
	if (*p && *p != ':' && *p != ' ' && *p != '\t' && *p != '\n') {
	    debug(DBG_ERR, "unexpected character after ]");
	    return 0;
	}
    }
    if (*p == ':') {
	    /* port number or service name is specified */;
	    field = ++p;
	    for (; *p && *p != ' ' && *p != '\t' && *p != '\n'; p++);
	    if (field == p) {
		debug(DBG_ERR, "syntax error, : but no following port");
		return 0;
	    }
	    hp->port = stringcopy(field, p - field);
    } else
	hp->port = default_port ? stringcopy(default_port, 0) : NULL;
    return 1;
}
    
struct hostportres *resolve_newhostport(char *hostport, char *default_port, int socktype, uint8_t passive, uint8_t prefixok) {
    struct hostportres *hp;
    struct addrinfo hints, *res;
    char *slash, *s;
    int plen = 0;

    hp = malloc(sizeof(struct hostportres));
    if (!hp) {
	debug(DBG_ERR, "resolve_newhostport: malloc failed");
	goto errexit;
    }
    memset(hp, 0, sizeof(struct hostportres));

    if (!resolve_parsehostport(hp, hostport, default_port))
	goto errexit;

    if (!strcmp(hp->host, "*")) {
	free(hp->host);
	hp->host = NULL;
    }

    slash = hp->host ? strchr(hp->host, '/') : NULL;
    if (slash) {
	if (!prefixok) {
	    debug(DBG_WARN, "resolve_newhostport: prefix not allowed here", hp->host);
	    goto errexit;
	}
	s = slash + 1;
	if (!*s) {
	    debug(DBG_WARN, "resolve_newhostport: prefix length must be specified after the / in %s", hp->host);
	    goto errexit;
	}
	for (; *s; s++)
	    if (*s < '0' || *s > '9') {
		debug(DBG_WARN, "resolve_newhostport: %s in %s is not a valid prefix length", slash + 1, hp->host);
		goto errexit;
	    }
	plen = atoi(slash + 1);
	if (plen < 0 || plen > 128) {
	    debug(DBG_WARN, "resolve_newhostport: %s in %s is not a valid prefix length", slash + 1, hp->host);
	    goto errexit;
	}
	*slash = '\0';
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = socktype;
    hints.ai_family = AF_UNSPEC;
    if (passive)
	hints.ai_flags = AI_PASSIVE;
    
    if (!hp->host && !hp->port) {
	/* getaddrinfo() doesn't like host and port to be NULL */
	if (getaddrinfo(hp->host, default_port, &hints, &hp->addrinfo)) {
	    debug(DBG_WARN, "resolve_newhostport: can't resolve (null) port (null)");
	    goto errexit;
	}
	for (res = hp->addrinfo; res; res = res->ai_next)
	    port_set(res->ai_addr, 0);
    } else {
	if (slash)
	    hints.ai_flags |= AI_NUMERICHOST;
	if (getaddrinfo(hp->host, hp->port, &hints, &hp->addrinfo)) {
	    debug(DBG_WARN, "resolve_newhostport: can't resolve %s port %s", hp->host ? hp->host : "(null)", hp->port ? hp->port : "(null)");
	    goto errexit;
	}
	if (slash) {
	    *slash = '/';
	    switch (hp->addrinfo->ai_family) {
	    case AF_INET:
		if (plen > 32) {
		    debug(DBG_WARN, "resolve_newhostport: prefix length must be <= 32 in %s", hp->host);
		    goto errexit;
		}
		break;
	    case AF_INET6:
		break;
	    default:
		debug(DBG_WARN, "resolve_newhostport: prefix must be IPv4 or IPv6 in %s", hp->host);
		goto errexit;
	    }
	    hp->prefixlen = (uint8_t)plen;
	} else
	    hp->prefixlen = 255;
    }
    return hp;

 errexit:
    resolve_freehostport(hostportres);
    return NULL;
}	  