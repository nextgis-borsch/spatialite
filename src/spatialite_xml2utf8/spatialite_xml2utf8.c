/* 
/ spatialite_xml2utf8
/
/ a tool converting the charset encoding for any XML as UTF-8
/
/ version 1.0, 2017 August 25
/
/ Author: Sandro Furieri a.furieri@lqt.it
/
/ Copyright (C) 2017  Alessandro Furieri
/
/    This program is free software: you can redistribute it and/or modify
/    it under the terms of the GNU General Public License as published by
/    the Free Software Foundation, either version 3 of the License, or
/    (at your option) any later version.
/
/    This program is distributed in the hope that it will be useful,
/    but WITHOUT ANY WARRANTY; without even the implied warranty of
/    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/    GNU General Public License for more details.
/
/    You should have received a copy of the GNU General Public License
/    along with this program.  If not, see <http://www.gnu.org/licenses/>.
/
*/

#include <stdlib.h>
#include <stdio.h>

#if defined(_WIN32) && !defined(__MINGW32__)
#include "config-msvc.h"
#else
#include "config.h"
#endif

#if defined(__MINGW32__) || defined(_WIN32)
#define LIBICONV_STATIC
#include <iconv.h>
#define LIBCHARSET_STATIC
#ifdef _MSC_VER
/* <localcharset.h> isn't supported on OSGeo4W */
/* applying a tricky workaround to fix this issue */
extern const char *locale_charset (void);
#else /* sane Windows - not OSGeo4W */
#include <localcharset.h>
#endif /* end localcharset */
#else /* not MINGW32 - WIN32 */
#if defined(__APPLE__) || defined(__ANDROID__)
#include <iconv.h>
#include <localcharset.h>
#else /* neither Mac OsX nor Android */
#include <iconv.h>
#include <langinfo.h>
#endif
#endif

static void
do_convert (iconv_t cvt, char *in, char *out, size_t i_len)
{
    size_t i;
    size_t max_len = i_len * 4;
    size_t o_len = max_len;
    char *p_in = in;
    char *p_out = out;
    if (iconv (cvt, &p_in, &i_len, &p_out, &o_len) == (size_t) (-1))
      {
	  fprintf (stderr, "invalid character sequence !!!\n");
	  return;
      }
    for (i = 0; i < max_len - o_len; i++)
	putchar (out[i]);
    putchar ('\n');
}

int
main (int argc, const char *argv[])
{
    int lineno = 0;
    size_t count;
    char *out = malloc (1024 * 1024);
    char *in = malloc (1024 * 1924);
    char *p_in;
    iconv_t cvt;
    const char *charset = NULL;
    if (argc != 2)
      {
	  fprintf (stderr,
		   "usage: spatialite_utf8 input-charset <input >output\n");
	  return -1;
      }
    charset = argv[1];

    cvt = iconv_open ("UTF-8", charset);
    if (cvt == (iconv_t) (-1))
      {
	  fprintf (stderr, "Unknown charset: %s\n", charset);
	  goto stop;
      }

    count = 0;
    p_in = in;
    while (1)
      {
	  int c = getchar ();
	  if (c == EOF || c == '\n')
	    {
		if (lineno > 0)
		    do_convert (cvt, in, out, count);
		else
		    printf ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
		if (c == EOF)
		    break;
		lineno++;
		count = 0;
		p_in = in;
		continue;
	    }
	  *p_in++ = c;
	  count++;
      }

    iconv_close (cvt);

  stop:
    return 0;
}
