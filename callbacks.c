/*
 * Copyright (c) 2000 Charles Ying. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the same terms as sendmail itself.
 *
 */

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include <pthread.h>

#include "intpools.h"

#include "libmilter/mfapi.h"

#define KEY_CONNECT	newSVpv("connect", 0)
#define KEY_HELO	newSVpv("helo", 0)
#define KEY_ENVFROM	newSVpv("envfrom", 0)
#define KEY_ENVRCPT	newSVpv("envrcpt", 0)
#define KEY_HEADER	newSVpv("header", 0)
#define KEY_EOH		newSVpv("eoh", 0)
#define KEY_BODY	newSVpv("body", 0)
#define KEY_EOM		newSVpv("eom", 0)
#define KEY_ABORT	newSVpv("abort", 0)
#define KEY_CLOSE	newSVpv("close", 0)

#define XPUSHs_Sendmail_Milter_Context	\
	(XPUSHs(sv_2mortal(sv_setref_iv(NEWSV(25, 0), \
		"Sendmail::Milter::Context", (IV) ctx))))

/* A structure for housing callbacks and their mutexes. */

struct callback_t
{
	SV *sv;
	pthread_mutex_t sv_mutex;
};

typedef struct callback_t callback_t;


/* Callback prototypes for first-level callback wrappers. */

sfsistat hook_connect(SMFICTX *, char *, _SOCK_ADDR *);
sfsistat hook_helo(SMFICTX *, char *);
sfsistat hook_envfrom(SMFICTX *, char **);
sfsistat hook_envrcpt(SMFICTX *, char **);
sfsistat hook_header(SMFICTX *, char *, char *);
sfsistat hook_eoh(SMFICTX *);
sfsistat hook_body(SMFICTX *, u_char *, size_t);
sfsistat hook_eom(SMFICTX *);
sfsistat hook_abort(SMFICTX *);
sfsistat hook_close(SMFICTX *);


/* The Milter perl interpreter pool */

static intpool_t I_pool;


/* Callback structures for each callback. */

static callback_t CB_connect;
static callback_t CB_helo;
static callback_t CB_envfrom;
static callback_t CB_envrcpt;
static callback_t CB_header;
static callback_t CB_eoh;
static callback_t CB_body;
static callback_t CB_eom;
static callback_t CB_abort;
static callback_t CB_close;


/* Routines for managing callback_t's. */

void
init_callback(cb_ptr, new_sv)
	callback_t	*cb_ptr;
	SV		*new_sv;
{
	int error;

	memset(cb_ptr, '\0', sizeof(callback_t));

	/* Assign the sv */
	cb_ptr->sv = new_sv;

	/* Initialize the mutex */
	if ((error = pthread_mutex_init(&(cb_ptr->sv_mutex), NULL)) != 0)
		croak("callback pthread_mutex_init failed: %d", error);
}


SV *
get_local_sv(pTHX_ callback_t *cb_ptr)
{
	SV *local_callback;
	int error;

	/*
	**  Okay, heavily undocumented, but here's the dirt...
	**  Turns out from reading dougm's modperl_callback code that
	**  CvPADLIST isn't being properly duplicated. Apparently this sv_dup
	**  seems to fix the problem after poking around sv.c a bit. But I
	**  could be wrong. Very, very, wrong. We shall see.
	*/

	/* Lock CVs */
	if ((error = pthread_mutex_lock(&(cb_ptr->sv_mutex))) != 0)
		croak("mutex_lock failed in locking CV: %d", error);

	if (!SvPOK(cb_ptr->sv))
		local_callback = sv_dup(cb_ptr->sv);
	else
		local_callback = sv_2mortal(newSVsv(cb_ptr->sv));

	/* Unlock interpreter table */
	if ((error = pthread_mutex_unlock(&(cb_ptr->sv_mutex))) != 0)
		croak("mutex_unlock failed in unlocking CV: %d", error);

	return local_callback;
}


/* Main interfaces. */

void
init_callbacks(max_interpreters, max_requests)
	int max_interpreters;
	int max_requests;
{
	init_interpreters(&I_pool, max_interpreters, max_requests);
}


SV *
get_callback(perl_desc, key)
	HV *perl_desc;
	SV *key;
{
	HE *entry;

	entry = hv_fetch_ent(perl_desc, key, 0, 0);

	if (entry == NULL)
		croak("couldn't fetch callback symbol from descriptor.");

	return newSVsv(HeVAL(entry));
}


void
register_callbacks(desc, name, my_callback_table, flags)
	struct smfiDesc		*desc;
	char			*name;
	HV			*my_callback_table;
	int			flags;
{
	memset(desc, '\0', sizeof(struct smfiDesc));

	desc->xxfi_name = strdup(name);
	desc->xxfi_version = SMFI_VERSION;
	desc->xxfi_flags = flags;

	if (hv_exists_ent(my_callback_table, KEY_CONNECT, 0))
	{
		init_callback(&CB_connect,
			get_callback(my_callback_table, KEY_CONNECT));

		desc->xxfi_connect =	hook_connect;
	}

	if (hv_exists_ent(my_callback_table, KEY_HELO, 0))
	{
		init_callback(&CB_helo,
			get_callback(my_callback_table, KEY_HELO));

		desc->xxfi_helo	=	hook_helo;
	}

	if (hv_exists_ent(my_callback_table, KEY_ENVFROM, 0))
	{
		init_callback(&CB_envfrom,
			get_callback(my_callback_table, KEY_ENVFROM));

		desc->xxfi_envfrom =	hook_envfrom;
	}

	if (hv_exists_ent(my_callback_table, KEY_ENVRCPT, 0))
	{
		init_callback(&CB_envrcpt,
			get_callback(my_callback_table, KEY_ENVRCPT));

		desc->xxfi_envrcpt =	hook_envrcpt;
	}

	if (hv_exists_ent(my_callback_table, KEY_HEADER, 0))
	{
		init_callback(&CB_header,
			get_callback(my_callback_table, KEY_HEADER));

		desc->xxfi_header =	hook_header;
	}

	if (hv_exists_ent(my_callback_table, KEY_EOH, 0))
	{
		init_callback(&CB_eoh,
			get_callback(my_callback_table, KEY_EOH));

		desc->xxfi_eoh =	hook_eoh;
	}

	if (hv_exists_ent(my_callback_table, KEY_BODY, 0))
	{
		init_callback(&CB_body,
			get_callback(my_callback_table, KEY_BODY));

		desc->xxfi_body =	hook_body;
	}

	if (hv_exists_ent(my_callback_table, KEY_EOM, 0))
	{
		init_callback(&CB_eom,
			get_callback(my_callback_table, KEY_EOM));

		desc->xxfi_eom =	hook_eom;
	}

	if (hv_exists_ent(my_callback_table, KEY_ABORT, 0))
	{
		init_callback(&CB_abort,
			get_callback(my_callback_table, KEY_ABORT));

		desc->xxfi_abort =	hook_abort;
	}

	if (hv_exists_ent(my_callback_table, KEY_CLOSE, 0))
	{
		init_callback(&CB_close,
			get_callback(my_callback_table, KEY_CLOSE));

		desc->xxfi_close =	hook_close;
	}
}


/* Second-layer callbacks. These do the actual work. */

sfsistat
callback_noargs(pTHX_ callback_t *callback, SMFICTX *ctx)
{
	int n;
	sfsistat retval;
	dSP;
	ENTER;
	SAVETMPS;
	PUSHMARK(SP);

	XPUSHs_Sendmail_Milter_Context;

	PUTBACK;

	n = call_sv(get_local_sv(aTHX_ callback), G_EVAL | G_SCALAR);

	SPAGAIN;

	/* Check the eval first. */
	if (SvTRUE(ERRSV))
	{
		POPs;
		retval = SMFIS_TEMPFAIL;
	}
	else if (n == 1)
	{
		retval = (sfsistat) POPi;
	}
	else
	{
		retval = SMFIS_CONTINUE;
	}

	PUTBACK;
	FREETMPS;
	LEAVE;

	return retval;
}

sfsistat
callback_s(pTHX_ callback_t *callback, SMFICTX *ctx, char *arg1)
{
	int n;
	sfsistat retval;
	dSP;
	ENTER;
	SAVETMPS;
	PUSHMARK(SP);

	XPUSHs_Sendmail_Milter_Context;
	XPUSHs(sv_2mortal(newSVpv(arg1, 0)));

	PUTBACK;

	n = call_sv(get_local_sv(aTHX_ callback), G_EVAL | G_SCALAR);

	SPAGAIN;

	/* Check the eval first. */
	if (SvTRUE(ERRSV))
	{
		POPs;
		retval = SMFIS_TEMPFAIL;
	}
	else if (n == 1)
	{
		retval = (sfsistat) POPi;
	}
	else
	{
		retval = SMFIS_CONTINUE;
	}

	PUTBACK;
	FREETMPS;
	LEAVE;

	return retval;
}

sfsistat
callback_body(pTHX_ callback_t *callback, SMFICTX *ctx,
	            u_char *arg1, size_t arg2)
{
	int n;
	sfsistat retval;
	dSP;
	ENTER;
	SAVETMPS;
	PUSHMARK(SP);

	XPUSHs_Sendmail_Milter_Context;
	XPUSHs(sv_2mortal(newSVpvn(arg1, arg2)));
	XPUSHs(sv_2mortal(newSViv((IV) arg2)));

	PUTBACK;

	n = call_sv(get_local_sv(aTHX_ callback), G_EVAL | G_SCALAR);

	SPAGAIN;

	/* Check the eval first. */
	if (SvTRUE(ERRSV))
	{
		POPs;
		retval = SMFIS_TEMPFAIL;
	}
	else if (n == 1)
	{
		retval = (sfsistat) POPi;
	}
	else
	{
		retval = SMFIS_CONTINUE;
	}

	PUTBACK;
	FREETMPS;
	LEAVE;

	return retval;
}

sfsistat
callback_argv(pTHX_ callback_t *callback, SMFICTX *ctx, char **arg1)
{
	int n;
	sfsistat retval;
	char **iter = arg1;
	dSP;
	ENTER;
	SAVETMPS;
	PUSHMARK(SP);

	XPUSHs_Sendmail_Milter_Context;

	while(iter != NULL)
	{
		if (*iter == NULL)
			break;

		XPUSHs(sv_2mortal(newSVpv(*iter, 0)));
		iter++;
	}

	PUTBACK;

	n = call_sv(get_local_sv(aTHX_ callback), G_EVAL | G_SCALAR);

	SPAGAIN;

	/* Check the eval first. */
	if (SvTRUE(ERRSV))
	{
		POPs;
		retval = SMFIS_TEMPFAIL;
	}
	else if (n == 1)
	{
		retval = (sfsistat) POPi;
	}
	else
	{
		retval = SMFIS_CONTINUE;
	}

	PUTBACK;
	FREETMPS;
	LEAVE;

	return retval;
}

sfsistat
callback_ss(pTHX_ callback_t *callback, SMFICTX *ctx, char *arg1, char *arg2)
{
	int n;
	sfsistat retval;
	dSP;
	ENTER;
	SAVETMPS;
	PUSHMARK(SP);

	XPUSHs_Sendmail_Milter_Context;
	XPUSHs(sv_2mortal(newSVpv(arg1, 0)));
	XPUSHs(sv_2mortal(newSVpv(arg2, 0)));

	PUTBACK;

	n = call_sv(get_local_sv(aTHX_ callback), G_EVAL | G_SCALAR);

	SPAGAIN;

	/* Check the eval first. */
	if (SvTRUE(ERRSV))
	{
		POPs;
		retval = SMFIS_TEMPFAIL;
	}
	else if (n == 1)
	{
		retval = (sfsistat) POPi;
	}
	else
	{
		retval = SMFIS_CONTINUE;
	}

	PUTBACK;
	FREETMPS;
	LEAVE;

	return retval;
}

sfsistat
callback_ssockaddr(pTHX_ callback_t *callback, SMFICTX *ctx, char *arg1,
		   _SOCK_ADDR *arg_sa)
{
	int n;
	sfsistat retval;
	dSP;
	ENTER;
	SAVETMPS;
	PUSHMARK(SP);

	XPUSHs_Sendmail_Milter_Context;

	XPUSHs(sv_2mortal(newSVpv(arg1, 0)));

	/* A Perl sockaddr_in is all we handle right now. */
	if (arg_sa == NULL)
	{
		XPUSHs(sv_2mortal(newSVsv(&PL_sv_undef)));
	}
	else if (arg_sa->sa_family == AF_INET)
	{
		XPUSHs(sv_2mortal(newSVpvn((char *)arg_sa,
					   sizeof(_SOCK_ADDR))));
	}
	else
	{
		XPUSHs(sv_2mortal(newSVsv(&PL_sv_undef)));
	}

	PUTBACK;

	n = call_sv(get_local_sv(aTHX_ callback), G_EVAL | G_SCALAR);

	SPAGAIN;

	/* Check the eval first. */
	if (SvTRUE(ERRSV))
	{
		POPs;
		retval = SMFIS_TEMPFAIL;
	}
	else if (n == 1)
	{
		retval = (sfsistat) POPi;
	}
	else
	{
		retval = SMFIS_CONTINUE;
	}

	PUTBACK;
	FREETMPS;
	LEAVE;

	return retval;
}


/* First-layer callbacks */

sfsistat
hook_connect(ctx, hostname, hostaddr)
	SMFICTX		*ctx;
	char		*hostname;
	_SOCK_ADDR	*hostaddr;
{
	interp_t *interp;
	sfsistat retval;

	if ((interp = lock_interpreter(&I_pool)) == NULL)
		croak("could not lock a new perl interpreter.");

	PERL_SET_CONTEXT(interp->perl);

	retval = callback_ssockaddr(aTHX_ &CB_connect, ctx,
					  hostname, hostaddr);

	unlock_interpreter(&I_pool, interp);

	return retval;
}

sfsistat
hook_helo(ctx, helohost)
	SMFICTX		*ctx;
	char		*helohost;
{
	interp_t *interp;
	sfsistat retval;

	if ((interp = lock_interpreter(&I_pool)) == NULL)
		croak("could not lock a new perl interpreter.");

	PERL_SET_CONTEXT(interp->perl);

	retval = callback_s(aTHX_ &CB_helo, ctx, helohost);

	unlock_interpreter(&I_pool, interp);

	return retval;
}

sfsistat
hook_envfrom(ctx, argv)
	SMFICTX *ctx;
	char **argv;
{
	interp_t *interp;
	sfsistat retval;

	if ((interp = lock_interpreter(&I_pool)) == NULL)
		croak("could not lock a new perl interpreter.");

	PERL_SET_CONTEXT(interp->perl);

	retval = callback_argv(aTHX_ &CB_envfrom, ctx, argv);

	unlock_interpreter(&I_pool, interp);

	return retval;
}

sfsistat
hook_envrcpt(ctx, argv)
	SMFICTX *ctx;
	char **argv;
{
	interp_t *interp;
	sfsistat retval;

	if ((interp = lock_interpreter(&I_pool)) == NULL)
		croak("could not lock a new perl interpreter.");

	PERL_SET_CONTEXT(interp->perl);

	retval = callback_argv(aTHX_ &CB_envrcpt, ctx, argv);

	unlock_interpreter(&I_pool, interp);

	return retval;
}

sfsistat
hook_header(ctx, headerf, headerv)
	SMFICTX *ctx;
	char *headerf;
	char *headerv;
{
	interp_t *interp;
	sfsistat retval;

	if ((interp = lock_interpreter(&I_pool)) == NULL)
		croak("could not lock a new perl interpreter.");

	PERL_SET_CONTEXT(interp->perl);

	retval = callback_ss(aTHX_ &CB_header, ctx, headerf, headerv);

	unlock_interpreter(&I_pool, interp);

	return retval;
}

sfsistat
hook_eoh(ctx)
	SMFICTX *ctx;
{
	interp_t *interp;
	sfsistat retval;

	if ((interp = lock_interpreter(&I_pool)) == NULL)
		croak("could not lock a new perl interpreter.");

	PERL_SET_CONTEXT(interp->perl);

	retval = callback_noargs(aTHX_ &CB_eoh, ctx);

	unlock_interpreter(&I_pool, interp);

	return retval;
}

sfsistat
hook_body(ctx, bodyp, bodylen)
	SMFICTX *ctx;
	u_char *bodyp;
	size_t bodylen;
{
	interp_t *interp;
	sfsistat retval;

	if ((interp = lock_interpreter(&I_pool)) == NULL)
		croak("could not lock a new perl interpreter.");

	PERL_SET_CONTEXT(interp->perl);

	retval = callback_body(aTHX_ &CB_body, ctx, bodyp, bodylen);

	unlock_interpreter(&I_pool, interp);

	return retval;
}

sfsistat
hook_eom(ctx)
	SMFICTX *ctx;
{
	interp_t *interp;
	sfsistat retval;

	if ((interp = lock_interpreter(&I_pool)) == NULL)
		croak("could not lock a new perl interpreter.");

	PERL_SET_CONTEXT(interp->perl);

	retval = callback_noargs(aTHX_ &CB_eom, ctx);

	unlock_interpreter(&I_pool, interp);

	return retval;
}

sfsistat
hook_abort(ctx)
	SMFICTX *ctx;
{
	interp_t *interp;
	sfsistat retval;

	if ((interp = lock_interpreter(&I_pool)) == NULL)
		croak("could not lock a new perl interpreter.");

	PERL_SET_CONTEXT(interp->perl);

	retval = callback_noargs(aTHX_ &CB_abort, ctx);

	unlock_interpreter(&I_pool, interp);

	return retval;
}

sfsistat
hook_close(ctx)
	SMFICTX *ctx;
{
	interp_t *interp;
	sfsistat retval;

	if ((interp = lock_interpreter(&I_pool)) == NULL)
		croak("could not lock a new perl interpreter.");

	PERL_SET_CONTEXT(interp->perl);

	retval = callback_noargs(aTHX_ &CB_close, ctx);

	unlock_interpreter(&I_pool, interp);

	return retval;
}

