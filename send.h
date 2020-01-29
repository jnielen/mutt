/*
 * Copyright (C) 2020 Kevin J. McCarthy <kevin@8t8.us>
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _SEND_H
#define _SEND_H 1

ADDRESS *mutt_remove_xrefs (ADDRESS *, ADDRESS *);
int mutt_edit_address (ADDRESS **, const char *, int);
void mutt_forward_intro (CONTEXT *ctx, HEADER *cur, FILE *fp);
void mutt_forward_trailer (CONTEXT *ctx, HEADER *cur, FILE *fp);
void mutt_make_attribution (CONTEXT *ctx, HEADER *cur, FILE *out);
void mutt_make_post_indent (CONTEXT *ctx, HEADER *cur, FILE *out);
int mutt_fetch_recips (ENVELOPE *out, ENVELOPE *in, int flags);
void mutt_fix_reply_recipients (ENVELOPE *env);
void mutt_make_forward_subject (ENVELOPE *env, CONTEXT *ctx, HEADER *cur);
void mutt_make_misc_reply_headers (ENVELOPE *env, CONTEXT *ctx, HEADER *cur, ENVELOPE *curenv);
void mutt_add_to_reference_headers (ENVELOPE *env, ENVELOPE *curenv, LIST ***pp, LIST ***qq);
void mutt_set_followup_to (ENVELOPE *);
ADDRESS *mutt_default_from (void);
void mutt_encode_descriptions (BODY *, short);
int mutt_resend_message (FILE *, CONTEXT *, HEADER *);
int ci_send_message (int, HEADER *, const char *, CONTEXT *, HEADER *);

#endif
