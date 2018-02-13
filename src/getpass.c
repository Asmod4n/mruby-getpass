/*
Copyright 2016 Hendrik Beskow

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
/*
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * Copyright (c) 1988, 1993
 *  The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by the University of
 *  California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <mruby.h>
#include <mruby/error.h>
#ifdef _MSC_VER
#include <conio.h>
#else
#include <signal.h>
#include <termios.h>
#include <paths.h>
#include <errno.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <mruby/string.h>
#include <string.h>

#ifdef _MSC_VER

static mrb_value
mrb_getpass(mrb_state *mrb, mrb_value self)
{
  struct mrb_jmpbuf* prev_jmp = mrb->jmp;
  struct mrb_jmpbuf c_jmp;

  mrb_value buf = mrb_nil_value();

  MRB_TRY(&c_jmp)
  {
    mrb->jmp = &c_jmp;

    const char *prompt = "Password:";

    mrb_get_args(mrb, "|z", &prompt);

    buf = mrb_str_buf_new(mrb, 0);
    memset(RSTRING_PTR(buf), 0, RSTRING_CAPA(buf));

    fputs(prompt, stderr);
    rewind(stderr);      /* implied flush */

    int ch;
    while ((ch = _getch()) != '\003' && ch != '\r') {
      mrb_str_cat(mrb, buf, (const char *) &ch, 1);
    }

    if (ch == '\003') {
      memset(RSTRING_PTR(buf), 0, RSTRING_CAPA(buf));
      buf = mrb_nil_value();
    }

    fputs("\n", stderr);

    mrb->jmp = prev_jmp;
  }
  MRB_CATCH(&c_jmp)
  {
    mrb->jmp = prev_jmp;
    if (mrb_string_p(buf)) {
      memset(RSTRING_PTR(buf), 0, RSTRING_CAPA(buf));
    }
    MRB_THROW(mrb->jmp);
  }
  MRB_END_EXC(&c_jmp);

  return buf;

}

#else

#ifndef TCSASOFT
#define TCSASOFT 0
#endif

struct passinfo {
  sigset_t stop;
  struct termios term;
  FILE *fp, *outfp;
  mrb_value buf;
  mrb_bool echo;
};

static mrb_value
body(mrb_state *mrb, mrb_value pass_val)
{
  struct passinfo *p = (struct passinfo*)mrb_cptr(pass_val);
  const char prompt[] = "Password:";
  int ch;

  /*
   * read and write to /dev/tty if possible; else read from
   * stdin and write to stderr.
   */
  errno = 0;
  if ((p->outfp = p->fp = fopen(_PATH_TTY, "w+")) == NULL) {
    if (errno == ENOMEM) {
      mrb_exc_raise(mrb, mrb_obj_value(mrb->nomem_err));
    }
    p->fp = stdin;
    p->outfp = stderr;
  }

  // we require a tty
  if (!isatty(fileno(p->fp))||!isatty(fileno(p->outfp))) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "sorry, you must have a tty");
  }

  mrb_get_args(mrb, "|z", &prompt);

  p->buf = mrb_str_buf_new(mrb, 0);
  memset(RSTRING_PTR(p->buf), 0, RSTRING_CAPA(p->buf));

  // disable echoing of the password, if it was enabled
  tcgetattr(fileno(p->fp), &p->term);
  if ((p->echo = (p->term.c_lflag & ECHO))) {
    p->term.c_lflag &= ~ECHO;
    tcsetattr(fileno(p->fp), TCSAFLUSH|TCSASOFT, &p->term);
  }

  fputs(prompt, p->outfp);
  rewind(p->outfp);      /* implied flush */

  while ((ch = fgetc(p->fp)) != EOF && ch != '\n') {
    mrb_str_cat(mrb, p->buf, (const char *) &ch, 1);
  }
  if (feof(p->fp)) {
    memset(RSTRING_PTR(p->buf), 0, RSTRING_CAPA(p->buf));
    p->buf = mrb_nil_value();
  }
  fputs("\n", p->outfp);

  // enable echoing again
  if (p->echo) {
    p->term.c_lflag |= ECHO;
    tcsetattr(fileno(p->fp), TCSAFLUSH|TCSASOFT, &p->term);
  }
  if (p->fp != stdin) {
    fclose(p->fp);
  }

  return p->buf;
}

static mrb_value
rescue(mrb_state *mrb, mrb_value pass_val)
{
  struct passinfo *p = (struct passinfo*)mrb_cptr(pass_val);

  if (p->echo) {
    p->term.c_lflag |= ECHO;
    tcsetattr(fileno(p->fp), TCSAFLUSH|TCSASOFT, &p->term);
  }
  if (p->fp && p->fp != stdin) {
    fclose(p->fp);
  }
  if (mrb_string_p(p->buf)) {
    memset(RSTRING_PTR(p->buf), 0, RSTRING_CAPA(p->buf));
  }

  return p->buf;
}

static mrb_value
mrb_getpass(mrb_state *mrb, mrb_value self)
{
  struct passinfo pass;
  mrb_value pass_val = mrb_cptr_value(mrb, &pass);

  pass.buf = mrb_nil_value();
  pass.fp = NULL;
  pass.outfp = NULL;
  pass.echo = FALSE;

  sigemptyset (&pass.stop);
  sigaddset(&pass.stop, SIGINT);
  sigaddset(&pass.stop, SIGTSTP);

  /*
   * note - blocking signals isn't necessarily the
   * right thing, but we leave it for now.
   */
  sigprocmask(SIG_BLOCK, &pass.stop, NULL);

  memset(&pass.term, 0, sizeof(pass.term));

  mrb_rescue_exceptions(mrb, body, pass_val, rescue, pass_val, 1, &mrb->eException_class);

  sigprocmask(SIG_UNBLOCK, &pass.stop, NULL);
  return pass.buf;
}

#endif

void
mrb_mruby_getpass_gem_init(mrb_state* mrb)
{
  mrb_define_module_function(mrb, mrb->kernel_module, "getpass", mrb_getpass, MRB_ARGS_OPT(1));
}

void mrb_mruby_getpass_gem_final(mrb_state* mrb) {}
