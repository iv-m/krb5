/*
 * appl/user_user/server.c
 *
 * Copyright 1991 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 *
 * One end of the user-user client-server pair.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <fcntl.h>

#include "krb5.h"
#include "com_err.h"

extern krb5_flags krb5_kdc_default_options;

/* fd 0 is a tcp socket used to talk to the client */

int main(argc, argv)
int argc;
char *argv[];
{
  krb5_data pname_data, tkt_data;
  int l, sock = 0;
  int retval;
  struct sockaddr_in l_inaddr, f_inaddr;	/* local, foreign address */
  krb5_creds creds, *new_creds;
  krb5_ccache cc;
  krb5_data msgtext, msg;
  krb5_context context;
    krb5_auth_context * auth_context = NULL;

#ifndef DEBUG
  freopen("/tmp/uu-server.log", "w", stderr);
#endif

  krb5_init_context(&context);
  krb5_init_ets(context);

#ifdef DEBUG
    {
	int one = 1;
	int acc;
	struct servent *sp;
	int namelen = sizeof(f_inaddr);

	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
	    com_err("uu-server", errno, "creating socket");
	    exit(3);
	}

	l_inaddr.sin_family = AF_INET;
	l_inaddr.sin_addr.s_addr = 0;
	if (!(sp = getservbyname("uu-sample", "tcp"))) {
	    com_err("uu-server", 0, "can't find uu-sample/tcp service");
	    exit(3);
	}
	(void) setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof (one));
	l_inaddr.sin_port = sp->s_port;
	if (bind(sock, (struct sockaddr *)&l_inaddr, sizeof(l_inaddr))) {
	    com_err("uu-server", errno, "binding socket");
	    exit(3);
	}
	if (listen(sock, 1) == -1) {
	    com_err("uu-server", errno, "listening");
	    exit(3);
	}
	if ((acc = accept(sock, (struct sockaddr *)&f_inaddr, &namelen)) == -1) {
	    com_err("uu-server", errno, "accepting");
	    exit(3);
	}
	dup2(acc, 0);
	close(sock);
	sock = 0;
    }
#endif
  if (retval = krb5_read_message(context, (krb5_pointer) &sock, &pname_data)) {
      com_err ("uu-server", retval, "reading pname");
      return 2;
  }
  if (retval = krb5_read_message(context, (krb5_pointer) &sock, &tkt_data)) {
      com_err ("uu-server", retval, "reading ticket data");
      return 2;
  }

  if (retval = krb5_cc_default(context, &cc))
    {
      com_err("uu-server", retval, "getting credentials cache");
      return 4;
    }

  memset ((char*)&creds, 0, sizeof(creds));
  if (retval = krb5_cc_get_principal(context, cc, &creds.client))
    {
      com_err("uu-client", retval, "getting principal name");
      return 6;
    }

  /* client sends it already null-terminated. */
  printf ("uu-server: client principal is \"%s\".\n", pname_data.data);

  if (retval = krb5_parse_name(context, pname_data.data, &creds.server))
    {
      com_err("uu-server", retval, "parsing client name");
      return 3;
    }
  creds.second_ticket = tkt_data;
  printf ("uu-server: client ticket is %d bytes.\n",
	  creds.second_ticket.length);

  if (retval = krb5_get_credentials(context, KRB5_GC_USER_USER, cc, 
				    &creds, &new_creds))
    {
      com_err("uu-server", retval, "getting user-user ticket");
      return 5;
    }

#ifndef DEBUG
  l = sizeof(f_inaddr);
  if (getpeername(0, (struct sockaddr *)&f_inaddr, &l) == -1)
    {
      com_err("uu-server", errno, "getting client address");
      return 6;
    }
#endif
  l = sizeof(l_inaddr);
  if (getsockname(0, (struct sockaddr *)&l_inaddr, &l) == -1)
    {
      com_err("uu-server", errno, "getting local address");
      return 6;
    }

  /* send a ticket/authenticator to the other side, so it can get the key
     we're using for the krb_safe below. */

    if (retval = krb5_auth_con_init(context, &auth_context)) {
      	com_err("uu-server", retval, "making auth_context");
      	return 8;
    }

    if (retval = krb5_auth_con_setflags(context, auth_context,
					KRB5_AUTH_CONTEXT_DO_SEQUENCE)) {
	com_err("uu-server", retval, "initializing the auth_context flags");
	return 8;
    }

    if (retval = krb5_auth_con_genaddrs(context, auth_context, sock,
				KRB5_AUTH_CONTEXT_GENERATE_LOCAL_FULL_ADDR |
				KRB5_AUTH_CONTEXT_GENERATE_REMOTE_FULL_ADDR)) {
        com_err("uu-server", retval, "generating addrs for auth_context");
        return 9;
    }

#if 1
    if (retval = krb5_mk_req_extended(context, &auth_context, 
				      AP_OPTS_USE_SESSION_KEY, 
				      NULL, new_creds, &msg)) {
      	com_err("uu-server", retval, "making AP_REQ");
      	return 8;
    }
    retval = krb5_write_message(context, (krb5_pointer) &sock, &msg);
#else
    retval = krb5_sendauth(context, &auth_context, (krb5_pointer)&sock,"???", 0,
			   0, AP_OPTS_MUTUAL_REQUIRED | AP_OPTS_USE_SESSION_KEY,
			   NULL, &creds, cc, NULL, NULL, NULL);
#endif
  if (retval)
      goto cl_short_wrt;

  free(msg.data);

  msgtext.length = 32;
  msgtext.data = "Hello, other end of connection.";

  if (retval = krb5_mk_safe(context, auth_context, &msgtext, &msg, NULL))
    {
      com_err("uu-server", retval, "encoding message to client");
      return 6;
    }

  retval = krb5_write_message(context, (krb5_pointer) &sock, &msg);
  if (retval)
    {
    cl_short_wrt:
	com_err("uu-server", retval, "writing message to client");
      return 7;
    }

  return 0;
}
