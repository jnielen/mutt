/*
 * Copyright (C) 1999-2004 Thomas Roessler <roessler@does-not-exist.org>
 *
 *     This program is free software; you can redistribute it
 *     and/or modify it under the terms of the GNU General Public
 *     License as published by the Free Software Foundation; either
 *     version 2 of the License, or (at your option) any later
 *     version.
 *
 *     This program is distributed in the hope that it will be
 *     useful, but WITHOUT ANY WARRANTY; without even the implied
 *     warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *     PURPOSE.  See the GNU General Public License for more
 *     details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *     Boston, MA  02110-1301, USA.
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "mutt_curses.h"
#include "mutt_menu.h"
#include "attach.h"
#include "mapping.h"
#include "copy.h"
#include "mutt_idna.h"
#include "send.h"

/* some helper functions to verify that we are exclusively operating
 * on message/rfc822 attachments
 */

static short check_msg (BODY * b, short err)
{
  if (!mutt_is_message_type (b->type, b->subtype))
  {
    if (err)
      mutt_error _("You may only bounce message/rfc822 parts.");
    return -1;
  }
  return 0;
}

static short check_all_msg (ATTACH_CONTEXT *actx,
			    BODY * cur, short err)
{
  short i;

  if (cur && check_msg (cur, err) == -1)
    return -1;
  else if (!cur)
  {
    for (i = 0; i < actx->idxlen; i++)
    {
      if (actx->idx[i]->content->tagged)
      {
	if (check_msg (actx->idx[i]->content, err) == -1)
	  return -1;
      }
    }
  }
  return 0;
}


/* can we decode all tagged attachments? */

static short check_can_decode (ATTACH_CONTEXT *actx, BODY * cur)
{
  short i;

  if (cur)
    return mutt_can_decode (cur);

  for (i = 0; i < actx->idxlen; i++)
    if (actx->idx[i]->content->tagged && !mutt_can_decode (actx->idx[i]->content))
      return 0;

  return 1;
}

static short count_tagged (ATTACH_CONTEXT *actx)
{
  short count = 0;
  short i;

  for (i = 0; i < actx->idxlen; i++)
    if (actx->idx[i]->content->tagged)
      count++;

  return count;
}

/* count the number of tagged children below a multipart or message
 * attachment.
 */

static short count_tagged_children (ATTACH_CONTEXT *actx, short i)
{
  short level = actx->idx[i]->level;
  short count = 0;

  while ((++i < actx->idxlen) && (level < actx->idx[i]->level))
    if (actx->idx[i]->content->tagged)
      count++;

  return count;
}



/**
 **
 ** The bounce function, from the attachment menu
 **
 **/

void mutt_attach_bounce (FILE * fp, HEADER * hdr,
                         ATTACH_CONTEXT *actx, BODY * cur)
{
  short i;
  char prompt[STRING];
  char buf[HUGE_STRING];
  char *err = NULL;
  ADDRESS *adr = NULL;
  int ret = 0;
  int p   = 0;

  if (check_all_msg (actx, cur, 1) == -1)
    return;

  /* one or more messages? */
  p = (cur || count_tagged (actx) == 1);

  /* RfC 5322 mandates a From: header, so warn before bouncing
   * messages without one */
  if (cur)
  {
    if (!cur->hdr->env->from)
    {
      mutt_error _("Warning: message contains no From: header");
      mutt_sleep (2);
      mutt_clear_error ();
    }
  }
  else
  {
    for (i = 0; i < actx->idxlen; i++)
    {
      if (actx->idx[i]->content->tagged)
      {
	if (!actx->idx[i]->content->hdr->env->from)
	{
	  mutt_error _("Warning: message contains no From: header");
	  mutt_sleep (2);
	  mutt_clear_error ();
	  break;
	}
      }
    }
  }

  if (p)
    strfcpy (prompt, _("Bounce message to: "), sizeof (prompt));
  else
    strfcpy (prompt, _("Bounce tagged messages to: "), sizeof (prompt));

  buf[0] = '\0';
  if (mutt_get_field (prompt, buf, sizeof (buf), MUTT_ALIAS)
      || buf[0] == '\0')
    return;

  if (!(adr = rfc822_parse_adrlist (adr, buf)))
  {
    mutt_error _("Error parsing address!");
    return;
  }

  adr = mutt_expand_aliases (adr);

  if (mutt_addrlist_to_intl (adr, &err) < 0)
  {
    mutt_error (_("Bad IDN: '%s'"), err);
    FREE (&err);
    rfc822_free_address (&adr);
    return;
  }

  buf[0] = 0;
  rfc822_write_address (buf, sizeof (buf), adr, 1);

#define extra_space (15+7+2)
  /*
   * See commands.c.
   */
  snprintf (prompt, sizeof (prompt) - 4,
            (p ? _("Bounce message to %s") : _("Bounce messages to %s")), buf);

  if (mutt_strwidth (prompt) > MuttMessageWindow->cols - extra_space)
  {
    mutt_format_string (prompt, sizeof (prompt) - 4,
			0, MuttMessageWindow->cols-extra_space, FMT_LEFT, 0,
			prompt, sizeof (prompt), 0);
    safe_strcat (prompt, sizeof (prompt), "...?");
  }
  else
    safe_strcat (prompt, sizeof (prompt), "?");

  if (query_quadoption (OPT_BOUNCE, prompt) != MUTT_YES)
  {
    rfc822_free_address (&adr);
    mutt_window_clearline (MuttMessageWindow, 0);
    mutt_message (p ? _("Message not bounced.") : _("Messages not bounced."));
    return;
  }

  mutt_window_clearline (MuttMessageWindow, 0);

  if (cur)
    ret = mutt_bounce_message (fp, cur->hdr, adr);
  else
  {
    for (i = 0; i < actx->idxlen; i++)
    {
      if (actx->idx[i]->content->tagged)
	if (mutt_bounce_message (actx->idx[i]->fp, actx->idx[i]->content->hdr, adr))
	  ret = 1;
    }
  }

  if (!ret)
    mutt_message (p ? _("Message bounced.") : _("Messages bounced."));
  else
    mutt_error (p ? _("Error bouncing message!") : _("Error bouncing messages!"));

  rfc822_free_address (&adr);
}



/**
 **
 ** resend-message, from the attachment menu
 **
 **
 **/

void mutt_attach_resend (FILE * fp, HEADER * hdr, ATTACH_CONTEXT *actx,
			 BODY * cur)
{
  short i;

  if (check_all_msg (actx, cur, 1) == -1)
    return;

  if (cur)
    mutt_resend_message (fp, Context, cur->hdr);
  else
  {
    for (i = 0; i < actx->idxlen; i++)
      if (actx->idx[i]->content->tagged)
	mutt_resend_message (actx->idx[i]->fp, Context, actx->idx[i]->content->hdr);
  }
}


/**
 **
 ** forward-message, from the attachment menu
 **
 **/

/* try to find a common parent message for the tagged attachments. */

static ATTACHPTR *find_common_parent (ATTACH_CONTEXT *actx, short nattach)
{
  short i;
  short nchildren;

  for (i = 0; i < actx->idxlen; i++)
    if (actx->idx[i]->content->tagged)
      break;

  while (--i >= 0)
  {
    if (mutt_is_message_type (actx->idx[i]->content->type, actx->idx[i]->content->subtype))
    {
      nchildren = count_tagged_children (actx, i);
      if (nchildren == nattach)
	return actx->idx[i];
    }
  }

  return NULL;
}

/*
 * check whether attachment #i is a parent of the attachment
 * pointed to by cur
 *
 * Note: This and the calling procedure could be optimized quite a
 * bit.  For now, it's not worth the effort.
 */

static int is_parent (short i, ATTACH_CONTEXT *actx, BODY *cur)
{
  short level = actx->idx[i]->level;

  while ((++i < actx->idxlen) && actx->idx[i]->level > level)
  {
    if (actx->idx[i]->content == cur)
      return 1;
  }

  return 0;
}

static ATTACHPTR *find_parent (ATTACH_CONTEXT *actx, BODY *cur, short nattach)
{
  short i;
  ATTACHPTR *parent = NULL;

  if (cur)
  {
    for (i = 0; i < actx->idxlen; i++)
    {
      if (mutt_is_message_type (actx->idx[i]->content->type, actx->idx[i]->content->subtype)
	  && is_parent (i, actx, cur))
	parent = actx->idx[i];
      if (actx->idx[i]->content == cur)
	break;
    }
  }
  else if (nattach)
    parent = find_common_parent (actx, nattach);

  return parent;
}

static void include_header (int quote, FILE * ifp,
			    HEADER * hdr, FILE * ofp,
			    char *_prefix)
{
  int chflags = CH_DECODE;
  char prefix[SHORT_STRING];

  if (option (OPTWEED))
    chflags |= CH_WEED | CH_REORDER;

  if (quote)
  {
    if (_prefix)
      strfcpy (prefix, _prefix, sizeof (prefix));
    else if (!option (OPTTEXTFLOWED))
      _mutt_make_string (prefix, sizeof (prefix), NONULL (Prefix),
			 Context, hdr, 0);
    else
      strfcpy (prefix, ">", sizeof (prefix));

    chflags |= CH_PREFIX;
  }

  mutt_copy_header (ifp, hdr, ofp, chflags, quote ? prefix : NULL);
}

/* Attach all the body parts which can't be decoded.
 * This code is shared by forwarding and replying. */

static BODY ** copy_problematic_attachments (BODY **last,
					     ATTACH_CONTEXT *actx,
					     short force)
{
  short i;

  for (i = 0; i < actx->idxlen; i++)
  {
    if (actx->idx[i]->content->tagged &&
	(force || !mutt_can_decode (actx->idx[i]->content)))
    {
      if (mutt_copy_body (actx->idx[i]->fp, last, actx->idx[i]->content) == -1)
	return NULL;		/* XXXXX - may lead to crashes */
      last = &((*last)->next);
    }
  }
  return last;
}

/*
 * forward one or several MIME bodies
 * (non-message types)
 */

static void attach_forward_bodies (FILE * fp, HEADER * hdr,
				   ATTACH_CONTEXT *actx,
				   BODY * cur,
				   short nattach)
{
  short i;
  short mime_fwd_all = 0;
  short mime_fwd_any = 1;
  ATTACHPTR *parent = NULL;
  HEADER *parent_hdr;
  FILE *parent_fp;
  HEADER *tmphdr = NULL;
  BODY **last;
  BUFFER *tmpbody = NULL;
  FILE *tmpfp = NULL;

  char prefix[STRING];

  int rc = 0;

  STATE st;

  /*
   * First, find the parent message.
   * Note: This could be made an option by just
   * putting the following lines into an if block.
   */


  parent = find_parent (actx, cur, nattach);
  if (parent)
  {
    parent_hdr = parent->content->hdr;
    parent_fp = parent->fp;
  }
  else
  {
    parent_hdr = hdr;
    parent_fp = actx->root_fp;
  }


  tmphdr = mutt_new_header ();
  tmphdr->env = mutt_new_envelope ();
  mutt_make_forward_subject (tmphdr->env, Context, parent_hdr);

  tmpbody = mutt_buffer_pool_get ();
  mutt_buffer_mktemp (tmpbody);
  if ((tmpfp = safe_fopen (mutt_b2s (tmpbody), "w")) == NULL)
  {
    mutt_error (_("Can't open temporary file %s."), mutt_b2s (tmpbody));
    goto bail;
  }

  mutt_forward_intro (Context, parent_hdr, tmpfp);

  /* prepare the prefix here since we'll need it later. */

  if (option (OPTFORWQUOTE))
  {
    if (!option (OPTTEXTFLOWED))
      _mutt_make_string (prefix, sizeof (prefix), NONULL (Prefix), Context,
			 parent_hdr, 0);
    else
      strfcpy (prefix, ">", sizeof (prefix));
  }

  include_header (option (OPTFORWQUOTE), parent_fp, parent_hdr,
		  tmpfp, prefix);


  /*
   * Now, we have prepared the first part of the message body: The
   * original message's header.
   *
   * The next part is more interesting: either include the message bodies,
   * or attach them.
   */

  if ((!cur || mutt_can_decode (cur)) &&
      (rc = query_quadoption (OPT_MIMEFWD,
			      _("Forward as attachments?"))) == MUTT_YES)
    mime_fwd_all = 1;
  else if (rc == -1)
    goto bail;

  /*
   * shortcut MIMEFWDREST when there is only one attachment.  Is
   * this intuitive?
   */

  if (!mime_fwd_all && !cur && (nattach > 1)
      && !check_can_decode (actx, cur))
  {
    if ((rc = query_quadoption (OPT_MIMEFWDREST,
                                _("Can't decode all tagged attachments.  MIME-forward the others?"))) == -1)
      goto bail;
    else if (rc == MUTT_NO)
      mime_fwd_any = 0;
  }

  /* initialize a state structure */

  memset (&st, 0, sizeof (st));

  if (option (OPTFORWQUOTE))
    st.prefix = prefix;
  st.flags = MUTT_CHARCONV;
  if (option (OPTWEED))
    st.flags |= MUTT_WEED;
  st.fpout = tmpfp;

  /* where do we append new MIME parts? */
  last = &tmphdr->content;

  if (cur)
  {
    /* single body case */

    if (!mime_fwd_all && mutt_can_decode (cur))
    {
      st.fpin = fp;
      mutt_body_handler (cur, &st);
      state_putc ('\n', &st);
    }
    else
    {
      if (mutt_copy_body (fp, last, cur) == -1)
	goto bail;
      last = &((*last)->next);
    }
  }
  else
  {
    /* multiple body case */

    if (!mime_fwd_all)
    {
      for (i = 0; i < actx->idxlen; i++)
      {
	if (actx->idx[i]->content->tagged && mutt_can_decode (actx->idx[i]->content))
	{
          st.fpin = actx->idx[i]->fp;
	  mutt_body_handler (actx->idx[i]->content, &st);
	  state_putc ('\n', &st);
	}
      }
    }

    if (mime_fwd_any &&
	copy_problematic_attachments (last, actx, mime_fwd_all) == NULL)
      goto bail;
  }

  mutt_forward_trailer (Context, parent_hdr, tmpfp);

  safe_fclose (&tmpfp);
  tmpfp = NULL;

  /* now that we have the template, send it. */
  ci_send_message (0, tmphdr, mutt_b2s (tmpbody), NULL, parent_hdr);

  mutt_buffer_pool_release (&tmpbody);
  return;

bail:
  if (tmpfp)
  {
    safe_fclose (&tmpfp);
    mutt_unlink (mutt_b2s (tmpbody));
  }
  mutt_buffer_pool_release (&tmpbody);

  mutt_free_header (&tmphdr);
}


/*
 * Forward one or several message-type attachments. This
 * is different from the previous function
 * since we want to mimic the index menu's behavior.
 *
 * Code reuse from ci_send_message is not possible here -
 * ci_send_message relies on a context structure to find messages,
 * while, on the attachment menu, messages are referenced through
 * the attachment index.
 */

static void attach_forward_msgs (FILE * fp, HEADER * hdr,
                                 ATTACH_CONTEXT *actx, BODY * cur)
{
  HEADER *curhdr = NULL;
  HEADER *tmphdr = NULL;
  short i;
  int rc;

  BODY **last;
  BUFFER *tmpbody = NULL;
  FILE *tmpfp = NULL;

  int cmflags = 0;
  int chflags = CH_XMIT;

  if (cur)
    curhdr = cur->hdr;
  else
  {
    for (i = 0; i < actx->idxlen; i++)
      if (actx->idx[i]->content->tagged)
      {
	curhdr = actx->idx[i]->content->hdr;
	break;
      }
  }

  tmphdr = mutt_new_header ();
  tmphdr->env = mutt_new_envelope ();
  mutt_make_forward_subject (tmphdr->env, Context, curhdr);


  tmpbody = mutt_buffer_pool_get ();

  if ((rc = query_quadoption (OPT_MIMEFWD,
                              _("Forward MIME encapsulated?"))) == MUTT_NO)
  {

    /* no MIME encapsulation */

    mutt_buffer_mktemp (tmpbody);
    if (!(tmpfp = safe_fopen (mutt_b2s (tmpbody), "w")))
    {
      mutt_error (_("Can't create %s."), mutt_b2s (tmpbody));
      goto cleanup;
    }

    if (option (OPTFORWQUOTE))
    {
      chflags |= CH_PREFIX;
      cmflags |= MUTT_CM_PREFIX;
    }

    if (option (OPTFORWDECODE))
    {
      cmflags |= MUTT_CM_DECODE | MUTT_CM_CHARCONV;
      if (option (OPTWEED))
      {
	chflags |= CH_WEED | CH_REORDER;
	cmflags |= MUTT_CM_WEED;
      }
    }


    if (cur)
    {
      /* mutt_message_hook (cur->hdr, MUTT_MESSAGEHOOK); */
      mutt_forward_intro (Context, cur->hdr, tmpfp);
      _mutt_copy_message (tmpfp, fp, cur->hdr, cur->hdr->content, cmflags, chflags);
      mutt_forward_trailer (Context, cur->hdr, tmpfp);
    }
    else
    {
      for (i = 0; i < actx->idxlen; i++)
      {
	if (actx->idx[i]->content->tagged)
	{
	  /* mutt_message_hook (idx[i]->content->hdr, MUTT_MESSAGEHOOK); */
	  mutt_forward_intro (Context, actx->idx[i]->content->hdr, tmpfp);
	  _mutt_copy_message (tmpfp, actx->idx[i]->fp, actx->idx[i]->content->hdr,
			      actx->idx[i]->content->hdr->content, cmflags, chflags);
	  mutt_forward_trailer (Context, actx->idx[i]->content->hdr, tmpfp);
	}
      }
    }
    safe_fclose (&tmpfp);
  }
  else if (rc == MUTT_YES)	/* do MIME encapsulation - we don't need to do much here */
  {
    last = &tmphdr->content;
    if (cur)
      mutt_copy_body (fp, last, cur);
    else
    {
      for (i = 0; i < actx->idxlen; i++)
	if (actx->idx[i]->content->tagged)
	{
	  mutt_copy_body (actx->idx[i]->fp, last, actx->idx[i]->content);
	  last = &((*last)->next);
	}
    }
  }
  else
    mutt_free_header (&tmphdr);

  ci_send_message (0, tmphdr,
                   mutt_buffer_len (tmpbody) ? mutt_b2s (tmpbody) : NULL,
		   NULL, curhdr);
  tmphdr = NULL;  /* ci_send_message frees this */

cleanup:
  mutt_free_header (&tmphdr);
  mutt_buffer_pool_release (&tmpbody);
}

void mutt_attach_forward (FILE * fp, HEADER * hdr,
			  ATTACH_CONTEXT *actx, BODY * cur)
{
  short nattach;


  if (check_all_msg (actx, cur, 0) == 0)
    attach_forward_msgs (fp, hdr, actx, cur);
  else
  {
    nattach = count_tagged (actx);
    attach_forward_bodies (fp, hdr, actx, cur, nattach);
  }
}

void mutt_attach_mail_sender (FILE *fp, HEADER *hdr, ATTACH_CONTEXT *actx,
                              BODY *cur)
{
  HEADER *tmphdr = NULL;
  short i;

  if (check_all_msg (actx, cur, 0) == -1)
  {
    /* L10N: You will see this error message if you invoke <compose-to-sender>
       when you are on a normal attachment.
    */
    mutt_error _("You may only compose to sender with message/rfc822 parts.");
    return;
  }

  tmphdr = mutt_new_header ();
  tmphdr->env = mutt_new_envelope ();

  if (cur)
  {
    if (mutt_fetch_recips (tmphdr->env, cur->hdr->env, SENDTOSENDER) == -1)
      return;
  }
  else
  {
    for (i = 0; i < actx->idxlen; i++)
    {
      if (actx->idx[i]->content->tagged &&
	  mutt_fetch_recips (tmphdr->env, actx->idx[i]->content->hdr->env,
                             SENDTOSENDER) == -1)
	return;
    }
  }
  ci_send_message (0, tmphdr, NULL, NULL, NULL);
}


/**
 **
 ** the various reply functions, from the attachment menu
 **
 **
 **/

/* Create the envelope defaults for a reply.
 *
 * This function can be invoked in two ways.
 *
 * Either, parent is NULL.  In this case, all tagged bodies are of a message type,
 * and the header information is fetched from them.
 *
 * Or, parent is non-NULL.  In this case, cur is the common parent of all the
 * tagged attachments.
 *
 * Note that this code is horribly similar to envelope_defaults () from send.c.
 */

static int
attach_reply_envelope_defaults (ENVELOPE *env, ATTACH_CONTEXT *actx,
				HEADER *parent, int flags)
{
  ENVELOPE *curenv = NULL;
  HEADER *curhdr = NULL;
  short i;

  if (!parent)
  {
    for (i = 0; i < actx->idxlen; i++)
    {
      if (actx->idx[i]->content->tagged)
      {
	curhdr = actx->idx[i]->content->hdr;
	curenv = curhdr->env;
	break;
      }
    }
  }
  else
  {
    curenv = parent->env;
    curhdr = parent;
  }

  if (curenv == NULL  ||  curhdr == NULL)
  {
    mutt_error _("Can't find any tagged messages.");
    return -1;
  }

  if (parent)
  {
    if (mutt_fetch_recips (env, curenv, flags) == -1)
      return -1;
  }
  else
  {
    for (i = 0; i < actx->idxlen; i++)
    {
      if (actx->idx[i]->content->tagged
	  && mutt_fetch_recips (env, actx->idx[i]->content->hdr->env, flags) == -1)
	return -1;
    }
  }

  if ((flags & SENDLISTREPLY) && !env->to)
  {
    mutt_error _("No mailing lists found!");
    return (-1);
  }

  mutt_fix_reply_recipients (env);
  mutt_make_misc_reply_headers (env, Context, curhdr, curenv);

  if (parent)
    mutt_add_to_reference_headers (env, curenv, NULL, NULL);
  else
  {
    LIST **p = NULL, **q = NULL;

    for (i = 0; i < actx->idxlen; i++)
    {
      if (actx->idx[i]->content->tagged)
	mutt_add_to_reference_headers (env, actx->idx[i]->content->hdr->env, &p, &q);
    }
  }

  return 0;
}


/*  This is _very_ similar to send.c's include_reply(). */

static void attach_include_reply (FILE *fp, FILE *tmpfp, HEADER *cur, int flags)
{
  int cmflags = MUTT_CM_PREFIX | MUTT_CM_DECODE | MUTT_CM_CHARCONV;
  int chflags = CH_DECODE;

  /* mutt_message_hook (cur, MUTT_MESSAGEHOOK); */

  mutt_make_attribution (Context, cur, tmpfp);

  if (!option (OPTHEADER))
    cmflags |= MUTT_CM_NOHEADER;
  if (option (OPTWEED))
  {
    chflags |= CH_WEED;
    cmflags |= MUTT_CM_WEED;
  }

  _mutt_copy_message (tmpfp, fp, cur, cur->content, cmflags, chflags);
  mutt_make_post_indent (Context, cur, tmpfp);
}

void mutt_attach_reply (FILE * fp, HEADER * hdr,
			ATTACH_CONTEXT *actx, BODY * cur,
			int flags)
{
  short mime_reply_any = 0;

  short nattach = 0;
  ATTACHPTR *parent = NULL;
  HEADER *parent_hdr = NULL;
  FILE *parent_fp = NULL;
  HEADER *tmphdr = NULL;
  short i;

  STATE st;
  BUFFER *tmpbody = NULL;
  FILE *tmpfp = NULL;

  char prefix[SHORT_STRING];
  int rc;

  if (check_all_msg (actx, cur, 0) == -1)
  {
    nattach = count_tagged (actx);
    if ((parent = find_parent (actx, cur, nattach)) != NULL)
    {
      parent_hdr = parent->content->hdr;
      parent_fp = parent->fp;
    }
    else
    {
      parent_hdr = hdr;
      parent_fp = actx->root_fp;
    }
  }

  if (nattach > 1 && !check_can_decode (actx, cur))
  {
    if ((rc = query_quadoption (OPT_MIMEFWDREST,
                                _("Can't decode all tagged attachments.  MIME-encapsulate the others?"))) == -1)
      return;
    else if (rc == MUTT_YES)
      mime_reply_any = 1;
  }
  else if (nattach == 1)
    mime_reply_any = 1;

  tmphdr = mutt_new_header ();
  tmphdr->env = mutt_new_envelope ();

  if (attach_reply_envelope_defaults (tmphdr->env, actx,
				      parent_hdr ? parent_hdr : (cur ? cur->hdr : NULL), flags) == -1)
    goto cleanup;

  tmpbody = mutt_buffer_pool_get ();
  mutt_buffer_mktemp (tmpbody);
  if ((tmpfp = safe_fopen (mutt_b2s (tmpbody), "w")) == NULL)
  {
    mutt_error (_("Can't create %s."), mutt_b2s (tmpbody));
    goto cleanup;
  }

  if (!parent_hdr)
  {
    if (cur)
      attach_include_reply (fp, tmpfp, cur->hdr, flags);
    else
    {
      for (i = 0; i < actx->idxlen; i++)
      {
	if (actx->idx[i]->content->tagged)
	  attach_include_reply (actx->idx[i]->fp, tmpfp, actx->idx[i]->content->hdr, flags);
      }
    }
  }
  else
  {
    mutt_make_attribution (Context, parent_hdr, tmpfp);

    memset (&st, 0, sizeof (STATE));
    st.fpout = tmpfp;

    if (!option (OPTTEXTFLOWED))
      _mutt_make_string (prefix, sizeof (prefix), NONULL (Prefix),
			 Context, parent_hdr, 0);
    else
      strfcpy (prefix, ">", sizeof (prefix));

    st.prefix = prefix;
    st.flags  = MUTT_CHARCONV;

    if (option (OPTWEED))
      st.flags |= MUTT_WEED;

    if (option (OPTHEADER))
      include_header (1, parent_fp, parent_hdr, tmpfp, prefix);

    if (cur)
    {
      if (mutt_can_decode (cur))
      {
        st.fpin = fp;
	mutt_body_handler (cur, &st);
	state_putc ('\n', &st);
      }
      else
	mutt_copy_body (fp, &tmphdr->content, cur);
    }
    else
    {
      for (i = 0; i < actx->idxlen; i++)
      {
	if (actx->idx[i]->content->tagged && mutt_can_decode (actx->idx[i]->content))
	{
          st.fpin = actx->idx[i]->fp;
	  mutt_body_handler (actx->idx[i]->content, &st);
	  state_putc ('\n', &st);
	}
      }
    }

    mutt_make_post_indent (Context, parent_hdr, tmpfp);

    if (mime_reply_any && !cur &&
	copy_problematic_attachments (&tmphdr->content, actx, 0) == NULL)
    {
      goto cleanup;
    }
  }

  safe_fclose (&tmpfp);

  if (ci_send_message (flags, tmphdr, mutt_b2s (tmpbody), NULL,
                       parent_hdr ? parent_hdr : (cur ? cur->hdr : NULL)) == 0)
    mutt_set_flag (Context, hdr, MUTT_REPLIED, 1);

  tmphdr = NULL;  /* ci_send_message frees this */

cleanup:
  if (tmpfp)
  {
    safe_fclose (&tmpfp);
    mutt_unlink (mutt_b2s (tmpbody));
  }
  mutt_buffer_pool_release (&tmpbody);
  mutt_free_header (&tmphdr);
}
