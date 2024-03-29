/*
 *  Hydra, an http server
 *  Copyright (C) 1995 Paul Phillips <paulp@go2net.com>
 *  Some changes Copyright (C) 1996 Larry Doolittle <ldoolitt@boa.org>
 *  Some changes Copyright (C) 1996 Russ Nelson <nelson@crynwr.com>
 *  Some changes Copyright (C) 1996-2002 Jon Nelson <jnelson@boa.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 1, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* $Id: alias.c,v 1.22 2006-03-09 18:11:07 nmav Exp $ */

#include "boa.h"

static int init_local_cgi_stuff(request *, int);

/*
 * Name: get_hash_value
 *
 * Description: adds the ASCII values of the file letters
 * and mods by the hashtable size to get the hash value
 * Note: stops at first '/' (or '\0')
 * The returned value must be %= with the table size.
 *
 * This is actually the summing loop. See "boa.h" for the rest.
 */

int get_hash_value(const char *str)
{
   unsigned int hash = 0;
   unsigned int index = 0;

   while (str && str[index] && str[index] != '/')
      hash += (unsigned int) str[index++];
   return hash;
}				/* get_hash_value() */

/*
 * Name: add_alias
 *
 * Description: add an Alias, Redirect, or ScriptAlias to the
 * alias hash table.
 */

void add_alias(char *hostname, char *fakename, char *realname, int type)
{
   alias *old, *new;
   int hash, fakelen, reallen, hostlen;
   virthost *vhost;

   /* sanity checking */
   if (hostname == NULL || fakename == NULL || realname == NULL) {
      DIE("NULL values sent to add_alias");
   }

   hostlen = strlen(hostname);
   vhost = find_virthost(hostname, hostlen);
   if (vhost == NULL) {
      log_error_time();
      fprintf(stderr, "Tried to add Alias for non-existent host %s.\n",
	      hostname);
      exit(1);
   }

   fakelen = strlen(fakename);
   reallen = strlen(realname);
   if (fakelen == 0 || reallen == 0) {
      DIE("empty values sent to add_alias");
   }

   hash = get_alias_hash_value(fakename);
   old = vhost->alias_hashtable[hash];

   if (old) {
      while (old->next) {
	 if (!strcmp(fakename, old->fakename))	/* don't add twice */
	    return;
	 old = old->next;
      }
   }

   new = (alias *) malloc(sizeof(alias));
   if (!new) {
      DIE("out of memory adding alias to hash");
   }

   if (old)
      old->next = new;
   else
      vhost->alias_hashtable[hash] = new;

   if (!(new->fakename = strdup(fakename))) {
      DIE("failed strdup");
   }
   new->fake_len = fakelen;
   /* check for "here" */
   if (!(new->realname = strdup(realname))) {
      DIE("strdup of realname failed");
   }
   new->real_len = reallen;

   new->type = type;
   new->next = NULL;
}				/* add_alias() */

/*
 * Name: find_alias
 *
 * Description: Locates URI in the alias hashtable if it exists.
 *
 * Returns:
 *
 * alias structure or NULL if not found
 */

alias *find_alias(char *hostname, char *uri, int urilen)
{
   alias *current;
   int hash;
   virthost *vhost;

   /* Find ScriptAlias, Alias, or Redirect */

   if ((vhost = find_virthost(hostname, 0)) == NULL) {
      return NULL;
   }

   if (urilen == 0)
      urilen = strlen(uri);
   hash = get_alias_hash_value(uri);

   current = vhost->alias_hashtable[hash];
   while (current) {
#ifdef FASCIST_LOGGING
      fprintf(stderr,
	      "%s:%d - comparing \"%s\" (request) to \"%s\" (alias): ",
	      __FILE__, __LINE__, uri, current->fakename);
#endif
      /*
       * current->fake_len must always be shorter or equal to the URI
       */
      /*
       * when performing matches:
       * If the virtual part of the url ends in '/', and
       * we get a match, stop there.
       * Otherwise, we require '/' or '\0' at the end of the url.
       * We only check if the virtual path does *not* end in '/'
       */
      if (current->fake_len <= urilen &&
	  !memcmp(uri, current->fakename, current->fake_len) &&
	  (current->fakename[current->fake_len - 1] == '/' ||
	   (current->fakename[current->fake_len - 1] != '/' &&
	    (uri[current->fake_len] == '\0' ||
	     uri[current->fake_len] == '/')))) {
#ifdef FASCIST_LOGGING
	 fprintf(stderr, "Got it!\n");
#endif
	 return current;
      }
#ifdef FASCIST_LOGGING
      else
	 fprintf(stderr, "Didn't get it!\n");
#endif
      current = current->next;
   }
   return current;
}				/* find_alias() */

/* Name: is_executable_cgi
 * 
 * Description: Finds if the given filename is an executable CGI.
 *  if it is one, then it setups some required variables, such as
 *  path_info.
 *
 * Returns:
 * -1 error: probably file was not found
 * 0 not executable CGI
 * 1 executable CGI
 */
int is_executable_cgi(request * req, const char *filename)
{
   char *mime_type = get_mime_type(filename);
   action_module_st *hic;

   /* below we support cgis outside of a ScriptAlias */
   if (strcmp(CGI_MIME_TYPE, mime_type) == 0) {	/* cgi */
      int ret;
      ret =
	  init_local_cgi_stuff(req,
			       (req->http_version ==
				HTTP_0_9) ? NPH : CGI);

      if (ret == 0)
	 return -1;

      return 1;
   } else if ((hic = find_cgi_action_appr_module(mime_type, 0)) != 0) {	/* CGI ACTION */
      int ret = 0;

      if (hic->action) {
	 req->action = hic->action;
	 if (req->action == NULL)
	    return -1;

	 ret = init_local_cgi_stuff(req, CGI_ACTION);
      }

      if (ret == 0)
	 return -1;

      return 1;
   }

   return 0;			/* no cgi */
}

/*
 * Name: translate_uri
 *
 * Description: Parse a request's virtual path.
 * Sets query_string, pathname directly.
 * Also sets path_info, path_translated, and script_name via
 *  init_script_alias
 *
 * Note: NPH in user dir is currently broken
 *
 * Note -- this should be broken up.
 *
 * Return values:
 *   0: failure, close it down
 *   1: success, continue
 */
int translate_uri(request * req)
{
   char buffer[MAX_HEADER_LENGTH + 1];
   char *req_urip;
   alias *current;
   char *p;
   int uri_len, ret, len;	/* FIXME-andreou *//* Goes in pair with the one at L263 */

   req_urip = req->request_uri;
   if (req_urip[0] != '/') {
      send_r_bad_request(req);
      return 0;
   }

   uri_len = strlen(req->request_uri);

   current = find_alias(req->hostname, req->request_uri, uri_len);
   if (current) {
      if (current->type == SCRIPTALIAS)	/* Script */
	 return init_script_alias(req, current, uri_len);

      /* not a script alias, therefore begin filling in data */
      /* FIXME-andreou */
      /* { */
      /* int len; */
      len = current->real_len;
      len += uri_len - current->fake_len;
      if (len > MAX_HEADER_LENGTH) {
	 log_error_doc(req);
	 fputs("URI too long!\n", stderr);
	 send_r_bad_request(req);
	 return 0;
      }
      memcpy(buffer, current->realname, current->real_len);
      memcpy(buffer + current->real_len,
	     req->request_uri + current->fake_len,
	     uri_len - current->fake_len + 1);
      /* } */

      if (current->type == REDIRECT) {	/* Redirect */
	 if (req->method == M_POST) {	/* POST to non-script */
	    /* it's not a cgi, but we try to POST??? */
	    send_r_bad_request(req);
	    return 0;		/* not a script alias, therefore begin filling in data */
	 }
	 send_r_moved_temp(req, buffer, "");
	 return 0;
      } else {			/* Alias */
	 req->pathname = strdup(buffer);
	 if (!req->pathname) {
	    send_r_error(req);
	    WARN("unable to strdup buffer onto req->pathname");
	    return 0;
	 }
	 return 1;
      }
   }

   /* The reason why this is *not* an 'else if' is that, 
    * after aliasing, we still have to check for '~' expansion
    */

   if (req->user_dir[0] != 0 && req->request_uri[1] == '~') {
      char *user_homedir;

      req_urip = req->request_uri + 2;

      /* since we have uri_len which is from strlen(req->request_uri) */
      p = memchr(req_urip, '/', uri_len - 2);
      if (p)
	 *p = '\0';

      user_homedir = get_home_dir(req_urip);
      if (p)			/* have to restore request_uri in case of error */
	 *p = '/';

      if (!user_homedir) {	/* no such user */
	 send_r_not_found(req);
	 return 0;
      }
      {
	 int l1 = strlen(user_homedir);
	 int l2 = strlen(req->user_dir);
	 int l3 = (p ? strlen(p) : 0);

	 if (l1 + l2 + l3 + 1 > MAX_HEADER_LENGTH) {
	    log_error_doc(req);
	    fputs("uri too long!\n", stderr);
	    send_r_bad_request(req);
	    return 0;
	 }

	 memcpy(buffer, user_homedir, l1);
	 buffer[l1] = '/';
	 memcpy(buffer + l1 + 1, req->user_dir, l2 + 1);
	 if (p)
	    memcpy(buffer + l1 + 1 + l2, p, l3 + 1);
      }
   } else if (req->document_root[0] != 0) {
      /* no aliasing, no userdir... */
      int l1, l2;

      l1 = strlen(req->document_root);
      l2 = strlen(req->request_uri);

      if (l1 + l2 + 1 > MAX_HEADER_LENGTH) {
	 log_error_doc(req);
	 fputs("uri too long!\n", stderr);
	 send_r_bad_request(req);
	 return 0;
      }

      /* the 'l2 + 1' is there so we copy the '\0' as well */
      memcpy(buffer, req->document_root, l1);
      memcpy(buffer + l1, req->request_uri, l2 + 1);
   } else {
      /* not aliased.  not userdir.  not part of document_root.  BAIL */
      send_r_bad_request(req);
      return 0;
   }

   /* if here,
    * o it may be aliased but it's not a redirect or a script...
    * o it may be a homedir
    * o it may be a document_root resource (with or without virtual host)
    */

   req->pathname = strdup(buffer);
   if (!req->pathname) {
      WARN("Could not strdup buffer for req->pathname!");
      send_r_error(req);
      return 0;
   }

   /* FIXME -- script_name here equals req->request_uri */
   /* script_name could end up as /cgi-bin/bob/extra_path */

   if ((ret = is_executable_cgi(req, buffer)) != 0) {
      if (ret == -1) {
	 send_r_not_found(req);
	 return 0;
      }
      return 1;
   } else if (req->method == M_POST) {	/* POST to non-script */
      /* it's not a cgi, but we try to POST??? */
      send_r_bad_request(req);
      return 0;
   } else			/* we are done!! */
      return 1;
}				/* translate_uri() */

/* Sets the parameters needed in CGIs. This is used in CGIs
 * which are not in aliased directories.
 *
 * Returns:
 * 0 Some error.
 * 1 ok.
 */
static int init_local_cgi_stuff(request * req, int cgi)
{
   char pathname[MAX_HEADER_LENGTH + 1];
   char *p;
   int perms;

#ifdef FASCIST_LOGGING
   log_error_time();
   fprintf(stderr, "%s:%d - buffer is: \"%s\"\n",
	   __FILE__, __LINE__, buffer);
#endif

   if (req->script_name)
      free(req->script_name);
   req->script_name = strdup(req->request_uri);
   if (!req->script_name) {
      WARN("Could not strdup req->request_uri for req->script_name");
      return 0;
   }

   req->is_cgi = cgi;

   /* For PATH_INFO, and PATH_TRANSLATED */
   strcpy(pathname, req->request_uri);
   p = strrchr(pathname, '/');
   if (p)
      *p = 0;



   if (req->path_info)
      free(req->path_info);
   req->path_info = strdup(pathname);
   if (!req->path_info) {
      WARN("unable to strdup pathname for req->path_info");
      return 0;
   }

   strcpy(pathname, req->document_root);
   strcat(pathname, req->request_uri);

   if (req->path_translated)
      free(req->path_translated);
   req->path_translated = strdup(pathname);
   if (!req->path_translated) {
      WARN("unable to strdup pathname for req->path_translated");
      return 0;
   }

   /* Check if the file is accesible */
   if (cgi == CGI_ACTION)
      perms = R_OK;
   else
      perms = R_OK | X_OK;

   if (access(req->path_translated, perms) != 0) {
      if (cgi == CGI) {
	 log_error_time();
	 fprintf(stderr, "Script '%s' is not executable.\n",
		 req->path_translated);
      }
      /* couldn't access file */
      return 0;
   }

   return 1;
}

/*
 * Name: init_script_alias
 *
 * Description: Performs full parsing on a ScriptAlias request
 * Sets path_info and script_name
 *
 * Return values:
 *
 * 0: failure, shut down
 * 1: success, continue
 */
int init_script_alias(request * req, alias * current1, int uri_len)
{
   char pathname[MAX_HEADER_LENGTH + 1];
   struct stat statbuf;
   char buffer[MAX_HEADER_LENGTH + 1];

   int index = 0;
   char c;
   int err;
   virthost *vhost;

   vhost = find_virthost(req->hostname, 0);
   if (vhost == NULL) {
      send_r_not_found(req);
      return 0;
   }

   /* copies the "real" path + the non-alias portion of the
      uri to pathname.
    */

   if (uri_len - current1->fake_len + current1->real_len >
       MAX_HEADER_LENGTH) {
      log_error_doc(req);
      fputs("uri too long!\n", stderr);
      send_r_bad_request(req);
      return 0;
   }

   memcpy(pathname, current1->realname, current1->real_len);
   memcpy(pathname + current1->real_len, &req->request_uri[current1->fake_len], uri_len - current1->fake_len + 1);	/* the +1 copies the NUL */
#ifdef FASCIST_LOGGING
   log_error_time();
   fprintf(stderr,
	   "%s:%d - pathname in init_script_alias is: \"%s\" (\"%s\")\n",
	   __FILE__, __LINE__, pathname, pathname + current1->real_len);
#endif
   if (strncmp("nph-", pathname + current1->real_len, 4) == 0
       || req->http_version == HTTP_0_9)
      req->is_cgi = NPH;
   else
      req->is_cgi = CGI;


   /* start at the beginning of the actual uri...
      (in /cgi-bin/bob, start at the 'b' in bob */
   index = current1->real_len;

   /* go to first and successive '/' and keep checking
    * if it is a full pathname
    * on success (stat (not lstat) of file is a *regular file*)
    */
   do {
      c = pathname[++index];
      if (c == '/') {
	 pathname[index] = '\0';
	 err = stat(pathname, &statbuf);
	 pathname[index] = '/';
	 if (err == -1) {
	    send_r_not_found(req);
	    return 0;
	 }

	 /* is it a dir? */
	 if (!S_ISDIR(statbuf.st_mode)) {
	    /* check access */
	    if (!(statbuf.st_mode & (S_IFREG |	/* regular file */
				     (S_IRUSR | S_IXUSR) |	/* u+rx */
				     (S_IRGRP | S_IXGRP) |	/* g+rx */
				     (S_IROTH | S_IXOTH)))) {	/* o+rx */
	       send_r_forbidden(req);
	       return 0;
	    }
	    /* stop here */
	    break;
	 }
      }
   } while (c != '\0');


   /* start at the beginning of the actual uri...
    *  (in /cgi-bin/bob, start at the 'b' in bob
    */
   index = current1->real_len;

   /* go to first and successive '/' and keep checking
    * if it is a full pathname
    * on success (stat (not lstat) of file is a *regular file*)
    */
   do {
      c = pathname[++index];
      if (c == '/') {
	 pathname[index] = '\0';
	 err = stat(pathname, &statbuf);
	 pathname[index] = '/';
	 if (err == -1) {
	    send_r_not_found(req);
	    return 0;
	 }

	 /* is it a dir? */
	 if (!S_ISDIR(statbuf.st_mode)) {
	    /* check access */
	    if (!(statbuf.st_mode & (S_IFREG |	/* regular file */
				     (S_IRUSR | S_IXUSR) |	/* u+rx */
				     (S_IRGRP | S_IXGRP) |	/* g+rx */
				     (S_IROTH | S_IXOTH)))) {	/* o+rx */
	       send_r_forbidden(req);
	       return 0;
	    }
	    /* stop here */
	    break;
	 }
      }
   } while (c != '\0');

   req->script_name = strdup(req->request_uri);
   if (!req->script_name) {
      send_r_error(req);
      WARN("unable to strdup req->request_uri for req->script_name");
      return 0;
   }

   if (c == '\0') {
      err = stat(pathname, &statbuf);
      if (err == -1) {
	 send_r_not_found(req);
	 return 0;
      }

      /* is it a dir? */
      if (!S_ISDIR(statbuf.st_mode)) {
	 /* check access */
	 if (!(statbuf.st_mode & (S_IFREG |	/* regular file */
				  (S_IRUSR | S_IXUSR) |	/* u+rx */
				  (S_IRGRP | S_IXGRP) |	/* g+rx */
				  (S_IROTH | S_IXOTH)))) {	/* o+rx */
	    send_r_forbidden(req);
	    return 0;
	 }
	 /* stop here */
      } else {
	 send_r_forbidden(req);
	 return 0;
      }
   }

   /* we have path_info if c == '/'... still have to check for query */
   else if (c == '/') {
      int hash;
      alias *current;
      int path_len;

      req->path_info = strdup(pathname + index);
      if (!req->path_info) {
	 send_r_error(req);
	 WARN("unable to strdup pathname + index for req->path_info");
	 return 0;
      }
      pathname[index] = '\0';	/* strip path_info from path */
      path_len = strlen(req->path_info);
      /* we need to fix script_name here */
      /* index points into pathname, which is
       * realname/cginame/foo
       * and index points to the '/foo' part
       */
      req->script_name[strlen(req->script_name) - path_len] = '\0';	/* zap off the /foo part */

      /* FIXME-andreou */
      /* now, we have to re-alias the extra path info....
       * this sucks.
       */
      hash = get_alias_hash_value(req->path_info);
      current = vhost->alias_hashtable[hash];
      while (current && !req->path_translated) {
	 if (!strncmp(req->path_info, current->fakename,
		      current->fake_len)) {

	    if (current->real_len +
		path_len - current->fake_len > MAX_HEADER_LENGTH) {
	       log_error_doc(req);
	       fputs("uri too long!\n", stderr);
	       send_r_bad_request(req);
	       return 0;
	    }

	    memcpy(buffer, current->realname, current->real_len);
	    strcpy(buffer + current->real_len,
		   &req->path_info[current->fake_len]);
	    req->path_translated = strdup(buffer);
	    if (!req->path_translated) {
	       send_r_error(req);
	       WARN("unable to strdup buffer for req->path_translated");
	       return 0;
	    }
	 }
	 current = current->next;
      }
      /* no alias... try userdir */
      if (!req->path_translated && req->user_dir[0] != 0
	  && req->path_info[1] == '~') {
	 char *user_homedir;
	 char *p;

	 p = strchr(pathname + index + 1, '/');
	 if (p)
	    *p = '\0';

	 user_homedir = get_home_dir(pathname + index + 2);
	 if (p)
	    *p = '/';

	 if (!user_homedir) {	/* no such user */
	    send_r_not_found(req);
	    return 0;
	 }
	 {
	    int l1 = strlen(user_homedir);
	    int l2 = strlen(req->user_dir);
	    int l3;
	    if (p)
	       l3 = strlen(p);
	    else
	       l3 = 0;

	    req->path_translated = malloc(l1 + l2 + l3 + 2);
	    if (req->path_translated == NULL) {
	       send_r_error(req);
	       WARN("unable to malloc memory for req->path_translated");
	       return 0;
	    }
	    memcpy(req->path_translated, user_homedir, l1);
	    req->path_translated[l1] = '/';
	    memcpy(req->path_translated + l1 + 1, req->user_dir, l2 + 1);
	    if (p)
	       memcpy(req->path_translated + l1 + 1 + l2, p, l3 + 1);
	 }
      }
      if (!req->path_translated && req->document_root[0] != 0) {
	 /* no userdir, no aliasing... try document root */
	 int l1, l2;
	 l1 = strlen(req->document_root);
	 l2 = path_len;

	 req->path_translated = malloc(l1 + l2 + 1);
	 if (req->path_translated == NULL) {
	    send_r_error(req);
	    WARN("unable to malloc memory for req->path_translated");
	    return 0;
	 }
	 memcpy(req->path_translated, req->document_root, l1);
	 memcpy(req->path_translated + l1, req->path_info, l2 + 1);
      }
   }

   req->pathname = strdup(pathname);
   if (!req->pathname) {
      send_r_error(req);
      WARN("unable to strdup pathname for req->pathname");
      return 0;
   }

   return 1;
}				/* init_script_alias() */

/*
 * Empties the alias hashtable, deallocating any allocated memory.
 */

void dump_alias(virthost * vhost)
{
   int i;
   alias *temp;

   for (i = 0; i < ALIAS_HASHTABLE_SIZE; ++i) {	/* these limits OK? */
      if (vhost->alias_hashtable[i]) {
	 temp = vhost->alias_hashtable[i];
	 while (temp) {
	    alias *temp_next;

	    if (temp->fakename)
	       free(temp->fakename);
	    if (temp->realname)
	       free(temp->realname);
	    temp_next = temp->next;
	    free(temp);
	    temp = temp_next;
	 }
	 vhost->alias_hashtable[i] = NULL;
      }
   }
}				/* dump_alias() */

/* EOF */
