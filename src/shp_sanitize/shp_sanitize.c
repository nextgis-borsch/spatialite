/* 
/ shp_sanitize
/
/ an analysis / sanitizing tool for  broken SHAPEFILES
/
/ version 1.0, 2016 April 25
/
/ Author: Sandro Furieri a.furieri@lqt.it
/
/ Copyright (C) 2016  Alessandro Furieri
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

#ifndef _WIN32
#include <unistd.h>
#endif

#if defined(_WIN32) && !defined(__MINGW32__)
/* MSVC strictly requires this include [off_t] */
#include <sys/types.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <sys/types.h>
#if defined(_WIN32) && !defined(__MINGW32__)
#include <io.h>
#include <direct.h>
#else
#include <dirent.h>
#endif

#if defined(_WIN32) && !defined(__MINGW32__)
#include "config-msvc.h"
#else
#include "config.h"
#endif

#ifdef SPATIALITE_AMALGAMATION
#include <spatialite/sqlite3.h>
#else
#include <sqlite3.h>
#endif

#include <spatialite/gaiageo.h>
#include <spatialite.h>

#define ARG_NONE		0
#define ARG_IN_DIR		1
#define ARG_OUT_DIR		2

#define SUFFIX_DISCARD	0
#define SUFFIX_SHP		1
#define SUFFIX_SHX		2
#define SUFFIX_DBF		3

#define SHAPEFILE_NO_DATA 1e-38

#if defined(_WIN32) && !defined(__MINGW32__)
#define strcasecmp	_stricmp
#endif /* not WIN32 */

struct shp_entry
{
/* an item of the SHP list */
    char *base_name;
    char *file_name;
    int has_shp;
    int has_shx;
    int has_dbf;
    struct shp_entry *next;
};

struct shp_list
{
/* the SHP list */
    struct shp_entry *first;
    struct shp_entry *last;
};

static struct shp_list *
alloc_shp_list (void)
{
/* allocating an empty SHP list */
    struct shp_list *list = malloc (sizeof (struct shp_list));
    list->first = NULL;
    list->last = NULL;
    return list;
}

static void
free_shp_list (struct shp_list *list)
{
/* memory cleanup: freeing an SHP list */
    struct shp_entry *pi;
    struct shp_entry *pin;
    if (list == NULL)
	return;

    pi = list->first;
    while (pi != NULL)
      {
	  pin = pi->next;
	  if (pi->base_name != NULL)
	      sqlite3_free (pi->base_name);
	  if (pi->file_name != NULL)
	      sqlite3_free (pi->file_name);
	  free (pi);
	  pi = pin;
      }
    free (list);
}

static void
do_add_shapefile (struct shp_list *list, char *base_name, char *file_name,
		  int suffix)
{
/* adding a possible SHP to the list */
    struct shp_entry *pi;
    if (list == NULL)
	return;

    pi = list->first;
    while (pi != NULL)
      {
	  /* searching if already defined */
	  if (strcmp (pi->base_name, base_name) == 0)
	    {
		switch (suffix)
		  {
		  case SUFFIX_SHP:
		      pi->has_shp = 1;
		      break;
		  case SUFFIX_SHX:
		      pi->has_shx = 1;
		      break;
		  case SUFFIX_DBF:
		      pi->has_dbf = 1;
		      break;
		  };
		sqlite3_free (base_name);
		sqlite3_free (file_name);
		return;
	    }
	  pi = pi->next;
      }

/* adding a new SHP entry */
    pi = malloc (sizeof (struct shp_entry));
    pi->base_name = base_name;
    pi->file_name = file_name;
    pi->has_shp = 0;
    pi->has_shx = 0;
    pi->has_dbf = 0;
    pi->next = NULL;

    switch (suffix)
      {
      case SUFFIX_SHP:
	  pi->has_shp = 1;
	  break;
      case SUFFIX_SHX:
	  pi->has_shx = 1;
	  break;
      case SUFFIX_DBF:
	  pi->has_dbf = 1;
	  break;
      };

    if (list->first == NULL)
	list->first = pi;
    if (list->last != NULL)
	list->last->next = pi;
    list->last = pi;
}

static int
test_valid_shp (struct shp_entry *p)
{
/* testing for a valid SHP candidate */
    if (p == NULL)
	return 0;
    if (p->has_shp && p->has_shx && p->has_dbf)
	return 1;
    return 0;
}

static gaiaShapefilePtr
allocShapefile ()
{
/* allocates and initializes the Shapefile object */
    gaiaShapefilePtr shp = malloc (sizeof (gaiaShapefile));
    shp->endian_arch = 1;
    shp->Path = NULL;
    shp->Shape = -1;
    shp->EffectiveType = GAIA_UNKNOWN;
    shp->EffectiveDims = GAIA_XY;
    shp->flShp = NULL;
    shp->flShx = NULL;
    shp->flDbf = NULL;
    shp->Dbf = NULL;
    shp->ShpBfsz = 0;
    shp->BufShp = NULL;
    shp->BufDbf = NULL;
    shp->DbfHdsz = 0;
    shp->DbfReclen = 0;
    shp->DbfSize = 0;
    shp->DbfRecno = 0;
    shp->ShpSize = 0;
    shp->ShxSize = 0;
    shp->MinX = DBL_MAX;
    shp->MinY = DBL_MAX;
    shp->MaxX = -DBL_MAX;
    shp->MaxY = -DBL_MAX;
    shp->Valid = 0;
    shp->IconvObj = NULL;
    shp->LastError = NULL;
    return shp;
}

static void
freeShapefile (gaiaShapefilePtr shp)
{
/* frees all memory allocations related to the Shapefile object */
    if (shp->Path)
	free (shp->Path);
    if (shp->flShp)
	fclose (shp->flShp);
    if (shp->flShx)
	fclose (shp->flShx);
    if (shp->flDbf)
	fclose (shp->flDbf);
    if (shp->Dbf)
	gaiaFreeDbfList (shp->Dbf);
    if (shp->BufShp)
	free (shp->BufShp);
    if (shp->BufDbf)
	free (shp->BufDbf);
    if (shp->LastError)
	free (shp->LastError);
    free (shp);
}

static void
openShpRead (gaiaShapefilePtr shp, const char *path, double *MinX, double *MinY,
	     double *MaxX, double *MaxY, int *mismatching)
{
/* trying to open the shapefile and initial checkings */
    FILE *fl_shx = NULL;
    FILE *fl_shp = NULL;
    FILE *fl_dbf = NULL;
    char xpath[1024];
    int rd;
    unsigned char buf_shx[256];
    unsigned char *buf_shp = NULL;
    int buf_size = 1024;
    int shape;
    unsigned char bf[1024];
    int dbf_size;
    int dbf_reclen = 0;
    int off_dbf;
    int ind;
    char field_name[2048];
    char *sys_err;
    char errMsg[1024];
    double minx;
    double miny;
    double maxx;
    double maxy;
    int len;
    int endian_arch = gaiaEndianArch ();
    gaiaDbfListPtr dbf_list = NULL;
    if (shp->flShp != NULL || shp->flShx != NULL || shp->flDbf != NULL)
      {
	  sprintf (errMsg,
		   "attempting to reopen an already opened Shapefile\n");
	  goto unsupported_conversion;
      }
    sprintf (xpath, "%s.shx", path);
    fl_shx = fopen (xpath, "rb");
    if (!fl_shx)
      {
	  sys_err = strerror (errno);
	  sprintf (errMsg, "unable to open '%s' for reading: %s", xpath,
		   sys_err);
	  goto no_file;
      }
    sprintf (xpath, "%s.shp", path);
    fl_shp = fopen (xpath, "rb");
    if (!fl_shp)
      {
	  sys_err = strerror (errno);
	  sprintf (errMsg, "unable to open '%s' for reading: %s", xpath,
		   sys_err);
	  goto no_file;
      }
    sprintf (xpath, "%s.dbf", path);
    fl_dbf = fopen (xpath, "rb");
    if (!fl_dbf)
      {
	  sys_err = strerror (errno);
	  sprintf (errMsg, "unable to open '%s' for reading: %s", xpath,
		   sys_err);
	  goto no_file;
      }
/* reading SHX file header */
    rd = fread (buf_shx, sizeof (unsigned char), 100, fl_shx);
    if (rd != 100)
	goto error;
    if (gaiaImport32 (buf_shx + 0, GAIA_BIG_ENDIAN, endian_arch) != 9994)	/* checks the SHX magic number */
	goto error;
    *MinX = gaiaImport64 (buf_shx + 36, GAIA_LITTLE_ENDIAN, endian_arch);
    *MinY = gaiaImport64 (buf_shx + 44, GAIA_LITTLE_ENDIAN, endian_arch);
    *MaxX = gaiaImport64 (buf_shx + 52, GAIA_LITTLE_ENDIAN, endian_arch);
    *MaxY = gaiaImport64 (buf_shx + 60, GAIA_LITTLE_ENDIAN, endian_arch);
/* reading SHP file header */
    buf_shp = malloc (sizeof (unsigned char) * buf_size);
    rd = fread (buf_shp, sizeof (unsigned char), 100, fl_shp);
    if (rd != 100)
	goto error;
    if (gaiaImport32 (buf_shp + 0, GAIA_BIG_ENDIAN, endian_arch) != 9994)	/* checks the SHP magic number */
	goto error;
    minx = gaiaImport64 (buf_shp + 36, GAIA_LITTLE_ENDIAN, endian_arch);
    miny = gaiaImport64 (buf_shp + 44, GAIA_LITTLE_ENDIAN, endian_arch);
    maxx = gaiaImport64 (buf_shp + 52, GAIA_LITTLE_ENDIAN, endian_arch);
    maxy = gaiaImport64 (buf_shp + 60, GAIA_LITTLE_ENDIAN, endian_arch);
    *mismatching = 0;
    if (*MinX != minx || *MinY != miny || *MaxX != maxx || *MaxY != maxy)
      {
	  fprintf (stderr,
		   "\t\tHEADERS: found mismatching BBOX between .shx and .shp\n");
	  *mismatching = 1;
      }
    shape = gaiaImport32 (buf_shp + 32, GAIA_LITTLE_ENDIAN, endian_arch);
    if (shape == GAIA_SHP_POINT || shape == GAIA_SHP_POINTZ
	|| shape == GAIA_SHP_POINTM || shape == GAIA_SHP_POLYLINE
	|| shape == GAIA_SHP_POLYLINEZ || shape == GAIA_SHP_POLYLINEM
	|| shape == GAIA_SHP_POLYGON || shape == GAIA_SHP_POLYGONZ
	|| shape == GAIA_SHP_POLYGONM || shape == GAIA_SHP_MULTIPOINT
	|| shape == GAIA_SHP_MULTIPOINTZ || shape == GAIA_SHP_MULTIPOINTM)
	;
    else
	goto unsupported;
/* reading DBF file header */
    rd = fread (bf, sizeof (unsigned char), 32, fl_dbf);
    if (rd != 32)
	goto error;
    switch (*bf)
      {
	  /* checks the DBF magic number */
      case 0x03:
      case 0x83:
	  break;
      case 0x02:
      case 0xF8:
	  sprintf (errMsg, "'%s'\ninvalid magic number %02x [FoxBASE format]",
		   path, *bf);
	  goto dbf_bad_magic;
      case 0xF5:
	  sprintf (errMsg,
		   "'%s'\ninvalid magic number %02x [FoxPro 2.x (or earlier) format]",
		   path, *bf);
	  goto dbf_bad_magic;
      case 0x30:
      case 0x31:
      case 0x32:
	  sprintf (errMsg,
		   "'%s'\ninvalid magic number %02x [Visual FoxPro format]",
		   path, *bf);
	  goto dbf_bad_magic;
      case 0x43:
      case 0x63:
      case 0xBB:
      case 0xCB:
	  sprintf (errMsg, "'%s'\ninvalid magic number %02x [dBASE IV format]",
		   path, *bf);
	  goto dbf_bad_magic;
      default:
	  sprintf (errMsg, "'%s'\ninvalid magic number %02x [unknown format]",
		   path, *bf);
	  goto dbf_bad_magic;
      };
    dbf_size = gaiaImport16 (bf + 8, GAIA_LITTLE_ENDIAN, endian_arch);
    dbf_reclen = gaiaImport16 (bf + 10, GAIA_LITTLE_ENDIAN, endian_arch);
    dbf_size--;
    off_dbf = 0;
    dbf_list = gaiaAllocDbfList ();
    for (ind = 32; ind < dbf_size; ind += 32)
      {
	  /* fetches DBF fields definitions */
	  rd = fread (bf, sizeof (unsigned char), 32, fl_dbf);
	  if (rd != 32)
	      goto error;
	  if (*(bf + 11) == 'M')
	    {
		/* skipping any MEMO field */
		memcpy (field_name, bf, 11);
		field_name[11] = '\0';
		off_dbf += *(bf + 16);
		fprintf (stderr,
			 "WARNING: column \"%s\" is of the MEMO type and will be ignored\n",
			 field_name);
		continue;
	    }
	  memcpy (field_name, bf, 11);
	  field_name[11] = '\0';
	  gaiaAddDbfField (dbf_list, field_name, *(bf + 11), off_dbf,
			   *(bf + 16), *(bf + 17));
	  off_dbf += *(bf + 16);
      }
    if (!gaiaIsValidDbfList (dbf_list))
      {
	  /* invalid DBF */
	  goto illegal_dbf;
      }
    len = strlen (path);
    shp->Path = malloc (len + 1);
    strcpy (shp->Path, path);
    shp->ReadOnly = 1;
    shp->Shape = shape;
    switch (shape)
      {
	  /* setting up a prudential geometry type */
      case GAIA_SHP_POINT:
      case GAIA_SHP_POINTZ:
      case GAIA_SHP_POINTM:
	  shp->EffectiveType = GAIA_POINT;
	  break;
      case GAIA_SHP_POLYLINE:
      case GAIA_SHP_POLYLINEZ:
      case GAIA_SHP_POLYLINEM:
	  shp->EffectiveType = GAIA_MULTILINESTRING;
	  break;
      case GAIA_SHP_POLYGON:
      case GAIA_SHP_POLYGONZ:
      case GAIA_SHP_POLYGONM:
	  shp->EffectiveType = GAIA_MULTIPOLYGON;
	  break;
      case GAIA_SHP_MULTIPOINT:
      case GAIA_SHP_MULTIPOINTZ:
      case GAIA_SHP_MULTIPOINTM:
	  shp->EffectiveType = GAIA_MULTIPOINT;
	  break;
      }
    switch (shape)
      {
	  /* setting up a prudential dimension model */
      case GAIA_SHP_POINTZ:
      case GAIA_SHP_POLYLINEZ:
      case GAIA_SHP_POLYGONZ:
      case GAIA_SHP_MULTIPOINTZ:
	  shp->EffectiveDims = GAIA_XY_Z_M;
	  break;
      case GAIA_SHP_POINTM:
      case GAIA_SHP_POLYLINEM:
      case GAIA_SHP_POLYGONM:
      case GAIA_SHP_MULTIPOINTM:
	  shp->EffectiveDims = GAIA_XY_M;
	  break;
      default:
	  shp->EffectiveDims = GAIA_XY;
	  break;
      }
    shp->flShp = fl_shp;
    shp->flShx = fl_shx;
    shp->flDbf = fl_dbf;
    shp->Dbf = dbf_list;
/* saving the SHP buffer */
    shp->BufShp = buf_shp;
    shp->ShpBfsz = buf_size;
/* allocating DBF buffer */
    shp->BufDbf = malloc (sizeof (unsigned char) * dbf_reclen);
    shp->DbfHdsz = dbf_size + 1;
    shp->DbfReclen = dbf_reclen;
    shp->Valid = 1;
    shp->endian_arch = endian_arch;
    return;
  unsupported_conversion:
/* illegal charset */
    if (shp->LastError)
	free (shp->LastError);
    len = strlen (errMsg);
    shp->LastError = malloc (len + 1);
    strcpy (shp->LastError, errMsg);
    return;
  no_file:
/* one of shapefile's files can't be accessed */
    if (shp->LastError)
	free (shp->LastError);
    len = strlen (errMsg);
    shp->LastError = malloc (len + 1);
    strcpy (shp->LastError, errMsg);
    if (fl_shx)
	fclose (fl_shx);
    if (fl_shp)
	fclose (fl_shp);
    if (fl_dbf)
	fclose (fl_dbf);
    return;
  dbf_bad_magic:
/* the DBF has an invalid magin number */
    if (shp->LastError)
	free (shp->LastError);
    len = strlen (errMsg);
    shp->LastError = malloc (len + 1);
    strcpy (shp->LastError, errMsg);
    gaiaFreeDbfList (dbf_list);
    if (buf_shp)
	free (buf_shp);
    fclose (fl_shx);
    fclose (fl_shp);
    fclose (fl_dbf);
    return;
  error:
/* the shapefile is invalid or corrupted */
    if (shp->LastError)
	free (shp->LastError);
    sprintf (errMsg, "'%s' is corrupted / has invalid format", path);
    len = strlen (errMsg);
    shp->LastError = malloc (len + 1);
    strcpy (shp->LastError, errMsg);
    gaiaFreeDbfList (dbf_list);
    if (buf_shp)
	free (buf_shp);
    fclose (fl_shx);
    fclose (fl_shp);
    fclose (fl_dbf);
    return;
  unsupported:
/* the shapefile has an unrecognized shape type */
    if (shp->LastError)
	free (shp->LastError);
    sprintf (errMsg, "'%s' shape=%d is not supported", path, shape);
    len = strlen (errMsg);
    shp->LastError = malloc (len + 1);
    strcpy (shp->LastError, errMsg);
    gaiaFreeDbfList (dbf_list);
    if (buf_shp)
	free (buf_shp);
    fclose (fl_shx);
    fclose (fl_shp);
    if (fl_dbf)
	fclose (fl_dbf);
    return;
  illegal_dbf:
/* the DBF-file contains unsupported data types */
    if (shp->LastError)
	free (shp->LastError);
    sprintf (errMsg, "'%s.dbf' contains unsupported data types", path);
    len = strlen (errMsg);
    shp->LastError = malloc (len + 1);
    strcpy (shp->LastError, errMsg);
    gaiaFreeDbfList (dbf_list);
    if (buf_shp)
	free (buf_shp);
    fclose (fl_shx);
    fclose (fl_shp);
    if (fl_dbf)
	fclose (fl_dbf);
    return;
}

static int
readShpEntity (gaiaShapefilePtr shp, int current_row, int *shplen, double *minx,
	       double *miny, double *maxx, double *maxy)
{
/* trying to read an entity from shapefile */
    unsigned char buf[512];
    int len;
    int rd;
    int skpos;
    int offset;
    int off_shp;
    int sz;
    char errMsg[1024];
    int shape;
    int endian_arch = gaiaEndianArch ();

/* positioning and reading the SHX file */
    offset = 100 + (current_row * 8);	/* 100 bytes for the header + current row displacement; each SHX row = 8 bytes */
    skpos = fseek (shp->flShx, offset, SEEK_SET);
    if (skpos != 0)
	goto eof;
    rd = fread (buf, sizeof (unsigned char), 8, shp->flShx);
    if (rd != 8)
	goto eof;
    off_shp = gaiaImport32 (buf, GAIA_BIG_ENDIAN, shp->endian_arch);
/* positioning and reading the DBF file */
    offset = shp->DbfHdsz + (current_row * shp->DbfReclen);
    skpos = fseek (shp->flDbf, offset, SEEK_SET);
    if (skpos != 0)
	goto error;
    rd = fread (shp->BufDbf, sizeof (unsigned char), shp->DbfReclen,
		shp->flDbf);
    if (rd != shp->DbfReclen)
	goto error;
    if (*(shp->BufDbf) == '*')
	goto dbf_deleted;
/* positioning and reading corresponding SHP entity - geometry */
    offset = off_shp * 2;
    skpos = fseek (shp->flShp, offset, SEEK_SET);
    if (skpos != 0)
	goto error;
    rd = fread (buf, sizeof (unsigned char), 8, shp->flShp);
    if (rd != 8)
	goto error;
    sz = gaiaImport32 (buf + 4, GAIA_BIG_ENDIAN, shp->endian_arch);
    if ((sz * 2) > shp->ShpBfsz)
      {
	  /* current buffer is too small; we need to allocate a bigger buffer */
	  free (shp->BufShp);
	  shp->ShpBfsz = sz * 2;
	  shp->BufShp = malloc (sizeof (unsigned char) * shp->ShpBfsz);
      }
    /* reading the raw Geometry */
    rd = fread (shp->BufShp, sizeof (unsigned char), sz * 2, shp->flShp);
    if (rd != sz * 2)
	goto error;
    *shplen = rd;

/* retrieving the feature's BBOX */
    shape = gaiaImport32 (shp->BufShp + 0, GAIA_LITTLE_ENDIAN, endian_arch);
    *minx = DBL_MAX;
    *miny = DBL_MAX;
    *maxx = DBL_MAX;
    *maxy = DBL_MAX;
    if (shape == GAIA_SHP_POINT || shape == GAIA_SHP_POINTZ
	|| shape == GAIA_SHP_POINTM)
      {
	  *minx =
	      gaiaImport64 (shp->BufShp + 4, GAIA_LITTLE_ENDIAN, endian_arch);
	  *maxx = *minx;
	  *miny =
	      gaiaImport64 (shp->BufShp + 12, GAIA_LITTLE_ENDIAN, endian_arch);
	  *maxy = *miny;
      }
    if (shape == GAIA_SHP_POLYLINE || shape == GAIA_SHP_POLYLINEZ
	|| shape == GAIA_SHP_POLYLINEM || shape == GAIA_SHP_POLYGON
	|| shape == GAIA_SHP_POLYGONZ || shape == GAIA_SHP_POLYGONM
	|| shape == GAIA_SHP_MULTIPOINT || shape == GAIA_SHP_MULTIPOINTZ
	|| shape == GAIA_SHP_MULTIPOINTM)
      {
	  *minx =
	      gaiaImport64 (shp->BufShp + 4, GAIA_LITTLE_ENDIAN, endian_arch);
	  *miny =
	      gaiaImport64 (shp->BufShp + 12, GAIA_LITTLE_ENDIAN, endian_arch);
	  *maxx =
	      gaiaImport64 (shp->BufShp + 20, GAIA_LITTLE_ENDIAN, endian_arch);
	  *maxy =
	      gaiaImport64 (shp->BufShp + 28, GAIA_LITTLE_ENDIAN, endian_arch);
      }
    return 1;

  eof:
    if (shp->LastError)
	free (shp->LastError);
    shp->LastError = NULL;
    return 0;
  error:
    if (shp->LastError)
	free (shp->LastError);
    sprintf (errMsg, "'%s' is corrupted / has invalid format", shp->Path);
    len = strlen (errMsg);
    shp->LastError = malloc (len + 1);
    strcpy (shp->LastError, errMsg);
    return 0;
  dbf_deleted:
    if (shp->LastError)
	free (shp->LastError);
    shp->LastError = NULL;
    return -1;
}

struct shp_ring_item
{
/* a RING item [to be reassembled into a (Multi)Polygon] */
    gaiaRingPtr Ring;
    int IsExterior;
    gaiaRingPtr Mother;
    struct shp_ring_item *Next;
};

struct shp_ring_collection
{
/* a collection of RING items */
    struct shp_ring_item *First;
    struct shp_ring_item *Last;
};

static void
shp_free_rings (struct shp_ring_collection *ringsColl)
{
/* memory cleanup: rings collection */
    struct shp_ring_item *p;
    struct shp_ring_item *pN;
    p = ringsColl->First;
    while (p)
      {
	  pN = p->Next;
	  if (p->Ring)
	      gaiaFreeRing (p->Ring);
	  free (p);
	  p = pN;
      }
}

static void
shp_add_ring (struct shp_ring_collection *ringsColl, gaiaRingPtr ring)
{
/* inserting a ring into the rings collection */
    struct shp_ring_item *p = malloc (sizeof (struct shp_ring_item));
    p->Ring = ring;
    gaiaMbrRing (ring);
    gaiaClockwise (ring);
/* accordingly to SHP rules interior/exterior depends on direction */
    p->IsExterior = ring->Clockwise;
    p->Mother = NULL;
    p->Next = NULL;
/* updating the linked list */
    if (ringsColl->First == NULL)
	ringsColl->First = p;
    if (ringsColl->Last != NULL)
	ringsColl->Last->Next = p;
    ringsColl->Last = p;
}

static int
shp_check_rings (gaiaRingPtr exterior, gaiaRingPtr candidate)
{
/* 
/ speditively checks if the candidate could be an interior Ring
/ contained into the exterior Ring
*/
    double z;
    double m;
    double x0;
    double y0;
    double x1;
    double y1;
    int mid;
    int ret0;
    int ret1;
    if (candidate->DimensionModel == GAIA_XY_Z)
      {
	  gaiaGetPointXYZ (candidate->Coords, 0, &x0, &y0, &z);
      }
    else if (candidate->DimensionModel == GAIA_XY_M)
      {
	  gaiaGetPointXYM (candidate->Coords, 0, &x0, &y0, &m);
      }
    else if (candidate->DimensionModel == GAIA_XY_Z_M)
      {
	  gaiaGetPointXYZM (candidate->Coords, 0, &x0, &y0, &z, &m);
      }
    else
      {
	  gaiaGetPoint (candidate->Coords, 0, &x0, &y0);
      }
    mid = candidate->Points / 2;
    if (candidate->DimensionModel == GAIA_XY_Z)
      {
	  gaiaGetPointXYZ (candidate->Coords, mid, &x1, &y1, &z);
      }
    else if (candidate->DimensionModel == GAIA_XY_M)
      {
	  gaiaGetPointXYM (candidate->Coords, mid, &x1, &y1, &m);
      }
    else if (candidate->DimensionModel == GAIA_XY_Z_M)
      {
	  gaiaGetPointXYZM (candidate->Coords, mid, &x1, &y1, &z, &m);
      }
    else
      {
	  gaiaGetPoint (candidate->Coords, mid, &x1, &y1);
      }

/* testing if the first point falls on the exterior ring surface */
    ret0 = gaiaIsPointOnRingSurface (exterior, x0, y0);
/* testing if the second point falls on the exterior ring surface */
    ret1 = gaiaIsPointOnRingSurface (exterior, x1, y1);
    if (ret0 || ret1)
	return 1;
    return 0;
}

static int
shp_mbr_contains (gaiaRingPtr r1, gaiaRingPtr r2)
{
/* checks if the first Ring contains the second one - MBR based */
    int ok_1 = 0;
    int ok_2 = 0;
    int ok_3 = 0;
    int ok_4 = 0;
    if (r2->MinX >= r1->MinX && r2->MinX <= r1->MaxX)
	ok_1 = 1;
    if (r2->MaxX >= r1->MinX && r2->MaxX <= r1->MaxX)
	ok_2 = 1;
    if (r2->MinY >= r1->MinY && r2->MinY <= r1->MaxY)
	ok_3 = 1;
    if (r2->MaxY >= r1->MinY && r2->MaxY <= r1->MaxY)
	ok_4 = 1;
    if (ok_1 && ok_2 && ok_3 && ok_4)
	return 1;
    return 0;
}

static void
shp_arrange_rings (struct shp_ring_collection *ringsColl)
{
/* 
/ arranging Rings so to associate any interior ring
/ to the containing exterior ring
*/
    struct shp_ring_item *pInt;
    struct shp_ring_item *pExt;
    pExt = ringsColl->First;
    while (pExt != NULL)
      {
	  /* looping on Exterior Rings */
	  if (pExt->IsExterior)
	    {
		pInt = ringsColl->First;
		while (pInt != NULL)
		  {
		      /* looping on Interior Rings */
		      if (pInt->IsExterior == 0 && pInt->Mother == NULL
			  && shp_mbr_contains (pExt->Ring, pInt->Ring))
			{
			    /* ok, matches */
			    if (shp_check_rings (pExt->Ring, pInt->Ring))
				pInt->Mother = pExt->Ring;
			}
		      pInt = pInt->Next;
		  }
	    }
	  pExt = pExt->Next;
      }
    pExt = ringsColl->First;
    while (pExt != NULL)
      {
	  if (pExt->IsExterior == 0 && pExt->Mother == NULL)
	    {
		/* orphan ring: promoting to Exterior */
		pExt->IsExterior = 1;
	    }
	  pExt = pExt->Next;
      }
}

static void
shp_build_area (struct shp_ring_collection *ringsColl, gaiaGeomCollPtr geom)
{
/* building the final (Multi)Polygon Geometry */
    gaiaPolygonPtr polyg;
    struct shp_ring_item *pExt;
    struct shp_ring_item *pInt;
    pExt = ringsColl->First;
    while (pExt != NULL)
      {
	  if (pExt->IsExterior)
	    {
		/* creating a new Polygon */
		polyg = gaiaInsertPolygonInGeomColl (geom, pExt->Ring);
		pInt = ringsColl->First;
		while (pInt != NULL)
		  {
		      if (pExt->Ring == pInt->Mother)
			{
			    /* adding an interior ring to current POLYGON */
			    gaiaAddRingToPolyg (polyg, pInt->Ring);
			    /* releasing Ring ownership */
			    pInt->Ring = NULL;
			}
		      pInt = pInt->Next;
		  }
		/* releasing Ring ownership */
		pExt->Ring = NULL;
	    }
	  pExt = pExt->Next;
      }
}

static gaiaGeomCollPtr
do_parse_geometry (const unsigned char *bufshp, int buflen, int eff_dims,
		   int eff_type, int *nullshape)
{
/* attempting to parse a Geometry from the SHP */
    gaiaGeomCollPtr geom = NULL;
    int shape;
    double x;
    double y;
    double z;
    double m;
    int points;
    int n;
    int n1;
    int base;
    int baseZ;
    int baseM;
    int start;
    int end;
    int iv;
    int ind;
    int max_size;
    int min_size;
    int hasM;
    int sz;
    gaiaLinestringPtr line = NULL;
    gaiaRingPtr ring = NULL;
    int endian_arch = gaiaEndianArch ();
    struct shp_ring_collection ringsColl;
/* initializing the RING collection */
    ringsColl.First = NULL;
    ringsColl.Last = NULL;

    shape = gaiaImport32 (bufshp + 0, GAIA_LITTLE_ENDIAN, endian_arch);
    if (shape == GAIA_SHP_NULL)
      {
	  *nullshape = 1;
	  return NULL;
      }
    *nullshape = 0;

    if (shape == GAIA_SHP_POINT)
      {
	  /* shape point */
	  x = gaiaImport64 (bufshp + 4, GAIA_LITTLE_ENDIAN, endian_arch);
	  y = gaiaImport64 (bufshp + 12, GAIA_LITTLE_ENDIAN, endian_arch);
	  if (eff_dims == GAIA_XY_Z)
	    {
		geom = gaiaAllocGeomCollXYZ ();
		gaiaAddPointToGeomCollXYZ (geom, x, y, 0.0);
	    }
	  else if (eff_dims == GAIA_XY_M)
	    {
		geom = gaiaAllocGeomCollXYM ();
		gaiaAddPointToGeomCollXYM (geom, x, y, 0.0);
	    }
	  else if (eff_dims == GAIA_XY_Z_M)
	    {
		geom = gaiaAllocGeomCollXYZM ();
		gaiaAddPointToGeomCollXYZM (geom, x, y, 0.0, 0.0);
	    }
	  else
	    {
		geom = gaiaAllocGeomColl ();
		gaiaAddPointToGeomColl (geom, x, y);
	    }
	  geom->DeclaredType = GAIA_POINT;
      }
    if (shape == GAIA_SHP_POINTZ)
      {
	  /* shape point Z */
	  x = gaiaImport64 (bufshp + 4, GAIA_LITTLE_ENDIAN, endian_arch);
	  y = gaiaImport64 (bufshp + 12, GAIA_LITTLE_ENDIAN, endian_arch);
	  z = gaiaImport64 (bufshp + 20, GAIA_LITTLE_ENDIAN, endian_arch);
	  if (buflen == 28)
	      m = 0.0;
	  else
	      m = gaiaImport64 (bufshp + 28, GAIA_LITTLE_ENDIAN, endian_arch);
	  if (eff_dims == GAIA_XY_Z)
	    {
		geom = gaiaAllocGeomCollXYZ ();
		gaiaAddPointToGeomCollXYZ (geom, x, y, z);
	    }
	  else if (eff_dims == GAIA_XY_M)
	    {
		geom = gaiaAllocGeomCollXYM ();
		gaiaAddPointToGeomCollXYM (geom, x, y, m);
	    }
	  else if (eff_dims == GAIA_XY_Z_M)
	    {
		geom = gaiaAllocGeomCollXYZM ();
		gaiaAddPointToGeomCollXYZM (geom, x, y, z, m);
	    }
	  else
	    {
		geom = gaiaAllocGeomColl ();
		gaiaAddPointToGeomColl (geom, x, y);
	    }
	  geom->DeclaredType = GAIA_POINT;
      }
    if (shape == GAIA_SHP_POINTM)
      {
	  /* shape point M */
	  x = gaiaImport64 (bufshp + 4, GAIA_LITTLE_ENDIAN, endian_arch);
	  y = gaiaImport64 (bufshp + 12, GAIA_LITTLE_ENDIAN, endian_arch);
	  m = gaiaImport64 (bufshp + 20, GAIA_LITTLE_ENDIAN, endian_arch);
	  if (eff_dims == GAIA_XY_Z)
	    {
		geom = gaiaAllocGeomCollXYZ ();
		gaiaAddPointToGeomCollXYZ (geom, x, y, 0.0);
	    }
	  else if (eff_dims == GAIA_XY_M)
	    {
		geom = gaiaAllocGeomCollXYM ();
		gaiaAddPointToGeomCollXYM (geom, x, y, m);
	    }
	  else if (eff_dims == GAIA_XY_Z_M)
	    {
		geom = gaiaAllocGeomCollXYZM ();
		gaiaAddPointToGeomCollXYZM (geom, x, y, 0.0, m);
	    }
	  else
	    {
		geom = gaiaAllocGeomColl ();
		gaiaAddPointToGeomColl (geom, x, y);
	    }
	  geom->DeclaredType = GAIA_POINT;
      }
    if (shape == GAIA_SHP_POLYLINE)
      {
	  /* shape polyline */
	  n = gaiaImport32 (bufshp + 36, GAIA_LITTLE_ENDIAN, endian_arch);
	  n1 = gaiaImport32 (bufshp + 40, GAIA_LITTLE_ENDIAN, endian_arch);
	  base = 44 + (n * 4);
	  start = 0;
	  for (ind = 0; ind < n; ind++)
	    {
		if (ind < (n - 1))
		    end =
			gaiaImport32 (bufshp + 44 + ((ind + 1) * 4),
				      GAIA_LITTLE_ENDIAN, endian_arch);
		else
		    end = n1;
		points = end - start;
		if (eff_dims == GAIA_XY_Z)
		    line = gaiaAllocLinestringXYZ (points);
		else if (eff_dims == GAIA_XY_M)
		    line = gaiaAllocLinestringXYM (points);
		else if (eff_dims == GAIA_XY_Z_M)
		    line = gaiaAllocLinestringXYZM (points);
		else
		    line = gaiaAllocLinestring (points);
		points = 0;
		for (iv = start; iv < end; iv++)
		  {
		      x = gaiaImport64 (bufshp + base + (iv * 16),
					GAIA_LITTLE_ENDIAN, endian_arch);
		      y = gaiaImport64 (bufshp + base + (iv * 16) +
					8, GAIA_LITTLE_ENDIAN, endian_arch);
		      if (eff_dims == GAIA_XY_Z)
			{
			    gaiaSetPointXYZ (line->Coords, points, x, y, 0.0);
			}
		      else if (eff_dims == GAIA_XY_M)
			{
			    gaiaSetPointXYM (line->Coords, points, x, y, 0.0);
			}
		      else if (eff_dims == GAIA_XY_Z_M)
			{
			    gaiaSetPointXYZM (line->Coords, points, x, y,
					      0.0, 0.0);
			}
		      else
			{
			    gaiaSetPoint (line->Coords, points, x, y);
			}
		      start++;
		      points++;
		  }
		if (!geom)
		  {
		      if (eff_dims == GAIA_XY_Z)
			  geom = gaiaAllocGeomCollXYZ ();
		      else if (eff_dims == GAIA_XY_M)
			  geom = gaiaAllocGeomCollXYM ();
		      else if (eff_dims == GAIA_XY_Z_M)
			  geom = gaiaAllocGeomCollXYZM ();
		      else
			  geom = gaiaAllocGeomColl ();
		      if (eff_type == GAIA_LINESTRING)
			  geom->DeclaredType = GAIA_LINESTRING;
		      else
			  geom->DeclaredType = GAIA_MULTILINESTRING;
		  }
		gaiaInsertLinestringInGeomColl (geom, line);
	    }
      }
    if (shape == GAIA_SHP_POLYLINEZ)
      {
	  /* shape polyline Z */
	  n = gaiaImport32 (bufshp + 36, GAIA_LITTLE_ENDIAN, endian_arch);
	  n1 = gaiaImport32 (bufshp + 40, GAIA_LITTLE_ENDIAN, endian_arch);
	  hasM = 0;
	  max_size = 38 + (2 * n) + (n1 * 16);	/* size [in 16 bits words !!!] ZM */
	  min_size = 30 + (2 * n) + (n1 * 12);	/* size [in 16 bits words !!!] Z-only */
	  sz = buflen / 2;
	  if (sz < min_size)
	      goto error;
	  if (sz == max_size)
	      hasM = 1;
	  base = 44 + (n * 4);
	  baseZ = base + (n1 * 16) + 16;
	  baseM = baseZ + (n1 * 8) + 16;
	  start = 0;
	  for (ind = 0; ind < n; ind++)
	    {
		if (ind < (n - 1))
		    end =
			gaiaImport32 (bufshp + 44 + ((ind + 1) * 4),
				      GAIA_LITTLE_ENDIAN, endian_arch);
		else
		    end = n1;
		points = end - start;
		if (eff_dims == GAIA_XY_Z)
		    line = gaiaAllocLinestringXYZ (points);
		else if (eff_dims == GAIA_XY_M)
		    line = gaiaAllocLinestringXYM (points);
		else if (eff_dims == GAIA_XY_Z_M)
		    line = gaiaAllocLinestringXYZM (points);
		else
		    line = gaiaAllocLinestring (points);
		points = 0;
		for (iv = start; iv < end; iv++)
		  {
		      x = gaiaImport64 (bufshp + base + (iv * 16),
					GAIA_LITTLE_ENDIAN, endian_arch);
		      y = gaiaImport64 (bufshp + base + (iv * 16) +
					8, GAIA_LITTLE_ENDIAN, endian_arch);
		      z = gaiaImport64 (bufshp + baseZ + (iv * 8),
					GAIA_LITTLE_ENDIAN, endian_arch);
		      if (hasM)
			  m = gaiaImport64 (bufshp + baseM +
					    (iv * 8), GAIA_LITTLE_ENDIAN,
					    endian_arch);
		      else
			  m = 0.0;
		      if (m < SHAPEFILE_NO_DATA)
			  m = 0.0;
		      if (eff_dims == GAIA_XY_Z)
			{
			    gaiaSetPointXYZ (line->Coords, points, x, y, z);
			}
		      else if (eff_dims == GAIA_XY_M)
			{
			    gaiaSetPointXYM (line->Coords, points, x, y, m);
			}
		      else if (eff_dims == GAIA_XY_Z_M)
			{
			    gaiaSetPointXYZM (line->Coords, points, x, y, z, m);
			}
		      else
			{
			    gaiaSetPoint (line->Coords, points, x, y);
			}
		      start++;
		      points++;
		  }
		if (!geom)
		  {
		      if (eff_dims == GAIA_XY_Z)
			  geom = gaiaAllocGeomCollXYZ ();
		      else if (eff_dims == GAIA_XY_M)
			  geom = gaiaAllocGeomCollXYM ();
		      else if (eff_dims == GAIA_XY_Z_M)
			  geom = gaiaAllocGeomCollXYZM ();
		      else
			  geom = gaiaAllocGeomColl ();
		      if (eff_type == GAIA_LINESTRING)
			  geom->DeclaredType = GAIA_LINESTRING;
		      else
			  geom->DeclaredType = GAIA_MULTILINESTRING;
		  }
		gaiaInsertLinestringInGeomColl (geom, line);
	    }
      }
    if (shape == GAIA_SHP_POLYLINEM)
      {
	  /* shape polyline M */
	  n = gaiaImport32 (bufshp + 36, GAIA_LITTLE_ENDIAN, endian_arch);
	  n1 = gaiaImport32 (bufshp + 40, GAIA_LITTLE_ENDIAN, endian_arch);
	  hasM = 0;
	  max_size = 30 + (2 * n) + (n1 * 12);	/* size [in 16 bits words !!!] M */
	  min_size = 22 + (2 * n) + (n1 * 8);	/* size [in 16 bits words !!!] no-M */
	  sz = buflen / 2;
	  if (sz < min_size)
	      goto error;
	  if (sz == max_size)
	      hasM = 1;
	  base = 44 + (n * 4);
	  baseM = base + (n1 * 16) + 16;
	  start = 0;
	  for (ind = 0; ind < n; ind++)
	    {
		if (ind < (n - 1))
		    end =
			gaiaImport32 (bufshp + 44 + ((ind + 1) * 4),
				      GAIA_LITTLE_ENDIAN, endian_arch);
		else
		    end = n1;
		points = end - start;
		if (eff_dims == GAIA_XY_Z)
		    line = gaiaAllocLinestringXYZ (points);
		else if (eff_dims == GAIA_XY_M)
		    line = gaiaAllocLinestringXYM (points);
		else if (eff_dims == GAIA_XY_Z_M)
		    line = gaiaAllocLinestringXYZM (points);
		else
		    line = gaiaAllocLinestring (points);
		points = 0;
		for (iv = start; iv < end; iv++)
		  {
		      x = gaiaImport64 (bufshp + base + (iv * 16),
					GAIA_LITTLE_ENDIAN, endian_arch);
		      y = gaiaImport64 (bufshp + base + (iv * 16) +
					8, GAIA_LITTLE_ENDIAN, endian_arch);
		      if (hasM)
			  m = gaiaImport64 (bufshp + baseM +
					    (iv * 8), GAIA_LITTLE_ENDIAN,
					    endian_arch);
		      else
			  m = 0.0;
		      if (m < SHAPEFILE_NO_DATA)
			  m = 0.0;
		      if (eff_dims == GAIA_XY_Z)
			{
			    gaiaSetPointXYZ (line->Coords, points, x, y, 0.0);
			}
		      else if (eff_dims == GAIA_XY_M)
			{
			    gaiaSetPointXYM (line->Coords, points, x, y, m);
			}
		      else if (eff_dims == GAIA_XY_Z_M)
			{
			    gaiaSetPointXYZM (line->Coords, points, x, y,
					      0.0, m);
			}
		      else
			{
			    gaiaSetPoint (line->Coords, points, x, y);
			}
		      start++;
		      points++;
		  }
		if (!geom)
		  {
		      if (eff_dims == GAIA_XY_Z)
			  geom = gaiaAllocGeomCollXYZ ();
		      else if (eff_dims == GAIA_XY_M)
			  geom = gaiaAllocGeomCollXYM ();
		      else if (eff_dims == GAIA_XY_Z_M)
			  geom = gaiaAllocGeomCollXYZM ();
		      else
			  geom = gaiaAllocGeomColl ();
		      if (eff_type == GAIA_LINESTRING)
			  geom->DeclaredType = GAIA_LINESTRING;
		      else
			  geom->DeclaredType = GAIA_MULTILINESTRING;
		  }
		gaiaInsertLinestringInGeomColl (geom, line);
	    }
      }
    if (shape == GAIA_SHP_POLYGON)
      {
	  /* shape polygon */
	  n = gaiaImport32 (bufshp + 36, GAIA_LITTLE_ENDIAN, endian_arch);
	  n1 = gaiaImport32 (bufshp + 40, GAIA_LITTLE_ENDIAN, endian_arch);
	  base = 44 + (n * 4);
	  start = 0;
	  for (ind = 0; ind < n; ind++)
	    {
		if (ind < (n - 1))
		    end =
			gaiaImport32 (bufshp + 44 + ((ind + 1) * 4),
				      GAIA_LITTLE_ENDIAN, endian_arch);
		else
		    end = n1;
		points = end - start;
		if (eff_dims == GAIA_XY_Z)
		    ring = gaiaAllocRingXYZ (points);
		else if (eff_dims == GAIA_XY_M)
		    ring = gaiaAllocRingXYM (points);
		else if (eff_dims == GAIA_XY_Z_M)
		    ring = gaiaAllocRingXYZM (points);
		else
		    ring = gaiaAllocRing (points);
		points = 0;
		for (iv = start; iv < end; iv++)
		  {
		      x = gaiaImport64 (bufshp + base + (iv * 16),
					GAIA_LITTLE_ENDIAN, endian_arch);
		      y = gaiaImport64 (bufshp + base + (iv * 16) +
					8, GAIA_LITTLE_ENDIAN, endian_arch);
		      if (eff_dims == GAIA_XY_Z)
			{
			    gaiaSetPointXYZ (ring->Coords, points, x, y, 0.0);
			}
		      else if (eff_dims == GAIA_XY_M)
			{
			    gaiaSetPointXYM (ring->Coords, points, x, y, 0.0);
			}
		      else if (eff_dims == GAIA_XY_Z_M)
			{
			    gaiaSetPointXYZM (ring->Coords, points, x, y,
					      0.0, 0.0);
			}
		      else
			{
			    gaiaSetPoint (ring->Coords, points, x, y);
			}
		      start++;
		      points++;
		  }
		shp_add_ring (&ringsColl, ring);
	    }
	  shp_arrange_rings (&ringsColl);
	  /* allocating the final geometry */
	  if (eff_dims == GAIA_XY_Z)
	      geom = gaiaAllocGeomCollXYZ ();
	  else if (eff_dims == GAIA_XY_M)
	      geom = gaiaAllocGeomCollXYM ();
	  else if (eff_dims == GAIA_XY_Z_M)
	      geom = gaiaAllocGeomCollXYZM ();
	  else
	      geom = gaiaAllocGeomColl ();
	  if (eff_type == GAIA_POLYGON)
	      geom->DeclaredType = GAIA_POLYGON;
	  else
	      geom->DeclaredType = GAIA_MULTIPOLYGON;
	  shp_build_area (&ringsColl, geom);
      }
    if (shape == GAIA_SHP_POLYGONZ)
      {
	  /* shape polygon Z */
	  n = gaiaImport32 (bufshp + 36, GAIA_LITTLE_ENDIAN, endian_arch);
	  n1 = gaiaImport32 (bufshp + 40, GAIA_LITTLE_ENDIAN, endian_arch);
	  hasM = 0;
	  max_size = 38 + (2 * n) + (n1 * 16);	/* size [in 16 bits words !!!] ZM */
	  min_size = 30 + (2 * n) + (n1 * 12);	/* size [in 16 bits words !!!] Z-only */
	  sz = buflen / 2;
	  if (sz < min_size)
	      goto error;
	  if (sz == max_size)
	      hasM = 1;
	  base = 44 + (n * 4);
	  baseZ = base + (n1 * 16) + 16;
	  baseM = baseZ + (n1 * 8) + 16;
	  start = 0;
	  for (ind = 0; ind < n; ind++)
	    {
		if (ind < (n - 1))
		    end =
			gaiaImport32 (bufshp + 44 + ((ind + 1) * 4),
				      GAIA_LITTLE_ENDIAN, endian_arch);
		else
		    end = n1;
		points = end - start;
		if (eff_dims == GAIA_XY_Z)
		    ring = gaiaAllocRingXYZ (points);
		else if (eff_dims == GAIA_XY_M)
		    ring = gaiaAllocRingXYM (points);
		else if (eff_dims == GAIA_XY_Z_M)
		    ring = gaiaAllocRingXYZM (points);
		else
		    ring = gaiaAllocRing (points);
		points = 0;
		for (iv = start; iv < end; iv++)
		  {
		      x = gaiaImport64 (bufshp + base + (iv * 16),
					GAIA_LITTLE_ENDIAN, endian_arch);
		      y = gaiaImport64 (bufshp + base + (iv * 16) +
					8, GAIA_LITTLE_ENDIAN, endian_arch);
		      z = gaiaImport64 (bufshp + baseZ + (iv * 8),
					GAIA_LITTLE_ENDIAN, endian_arch);
		      if (hasM)
			  m = gaiaImport64 (bufshp + baseM +
					    (iv * 8), GAIA_LITTLE_ENDIAN,
					    endian_arch);
		      else
			  m = 0.0;
		      if (m < SHAPEFILE_NO_DATA)
			  m = 0.0;
		      if (eff_dims == GAIA_XY_Z)
			{
			    gaiaSetPointXYZ (ring->Coords, points, x, y, z);
			}
		      else if (eff_dims == GAIA_XY_M)
			{
			    gaiaSetPointXYM (ring->Coords, points, x, y, m);
			}
		      else if (eff_dims == GAIA_XY_Z_M)
			{
			    gaiaSetPointXYZM (ring->Coords, points, x, y, z, m);
			}
		      else
			{
			    gaiaSetPoint (ring->Coords, points, x, y);
			}
		      start++;
		      points++;
		  }
		shp_add_ring (&ringsColl, ring);
	    }
	  shp_arrange_rings (&ringsColl);
	  /* allocating the final geometry */
	  if (eff_dims == GAIA_XY_Z)
	      geom = gaiaAllocGeomCollXYZ ();
	  else if (eff_dims == GAIA_XY_M)
	      geom = gaiaAllocGeomCollXYM ();
	  else if (eff_dims == GAIA_XY_Z_M)
	      geom = gaiaAllocGeomCollXYZM ();
	  else
	      geom = gaiaAllocGeomColl ();
	  if (eff_type == GAIA_POLYGON)
	      geom->DeclaredType = GAIA_POLYGON;
	  else
	      geom->DeclaredType = GAIA_MULTIPOLYGON;
	  shp_build_area (&ringsColl, geom);
      }
    if (shape == GAIA_SHP_POLYGONM)
      {
	  /* shape polygon M */
	  n = gaiaImport32 (bufshp + 36, GAIA_LITTLE_ENDIAN, endian_arch);
	  n1 = gaiaImport32 (bufshp + 40, GAIA_LITTLE_ENDIAN, endian_arch);
	  hasM = 0;
	  max_size = 30 + (2 * n) + (n1 * 12);	/* size [in 16 bits words !!!] M */
	  min_size = 22 + (2 * n) + (n1 * 8);	/* size [in 16 bits words !!!] no-M */
	  sz = buflen / 2;
	  if (sz < min_size)
	      goto error;
	  if (sz == max_size)
	      hasM = 1;
	  base = 44 + (n * 4);
	  baseM = base + (n1 * 16) + 16;
	  start = 0;
	  for (ind = 0; ind < n; ind++)
	    {
		if (ind < (n - 1))
		    end =
			gaiaImport32 (bufshp + 44 + ((ind + 1) * 4),
				      GAIA_LITTLE_ENDIAN, endian_arch);
		else
		    end = n1;
		points = end - start;
		if (eff_dims == GAIA_XY_Z)
		    ring = gaiaAllocRingXYZ (points);
		else if (eff_dims == GAIA_XY_M)
		    ring = gaiaAllocRingXYM (points);
		else if (eff_dims == GAIA_XY_Z_M)
		    ring = gaiaAllocRingXYZM (points);
		else
		    ring = gaiaAllocRing (points);
		points = 0;
		for (iv = start; iv < end; iv++)
		  {
		      x = gaiaImport64 (bufshp + base + (iv * 16),
					GAIA_LITTLE_ENDIAN, endian_arch);
		      y = gaiaImport64 (bufshp + base + (iv * 16) +
					8, GAIA_LITTLE_ENDIAN, endian_arch);
		      if (hasM)
			  m = gaiaImport64 (bufshp + baseM +
					    (iv * 8), GAIA_LITTLE_ENDIAN,
					    endian_arch);
		      m = 0.0;
		      if (m < SHAPEFILE_NO_DATA)
			  m = 0.0;
		      if (eff_dims == GAIA_XY_Z)
			{
			    gaiaSetPointXYZ (ring->Coords, points, x, y, 0.0);
			}
		      else if (eff_dims == GAIA_XY_M)
			{
			    gaiaSetPointXYM (ring->Coords, points, x, y, m);
			}
		      else if (eff_dims == GAIA_XY_Z_M)
			{
			    gaiaSetPointXYZM (ring->Coords, points, x, y,
					      0.0, m);
			}
		      else
			{
			    gaiaSetPoint (ring->Coords, points, x, y);
			}
		      start++;
		      points++;
		  }
		shp_add_ring (&ringsColl, ring);
	    }
	  shp_arrange_rings (&ringsColl);
	  /* allocating the final geometry */
	  if (eff_dims == GAIA_XY_Z)
	      geom = gaiaAllocGeomCollXYZ ();
	  else if (eff_dims == GAIA_XY_M)
	      geom = gaiaAllocGeomCollXYM ();
	  else if (eff_dims == GAIA_XY_Z_M)
	      geom = gaiaAllocGeomCollXYZM ();
	  else
	      geom = gaiaAllocGeomColl ();
	  if (eff_type == GAIA_POLYGON)
	      geom->DeclaredType = GAIA_POLYGON;
	  else
	      geom->DeclaredType = GAIA_MULTIPOLYGON;
	  shp_build_area (&ringsColl, geom);
      }
    if (shape == GAIA_SHP_MULTIPOINT)
      {
	  /* shape multipoint */
	  n = gaiaImport32 (bufshp + 36, GAIA_LITTLE_ENDIAN, endian_arch);
	  if (eff_dims == GAIA_XY_Z)
	      geom = gaiaAllocGeomCollXYZ ();
	  else if (eff_dims == GAIA_XY_M)
	      geom = gaiaAllocGeomCollXYM ();
	  else if (eff_dims == GAIA_XY_Z_M)
	      geom = gaiaAllocGeomCollXYZM ();
	  else
	      geom = gaiaAllocGeomColl ();
	  geom->DeclaredType = GAIA_MULTIPOINT;
	  for (iv = 0; iv < n; iv++)
	    {
		x = gaiaImport64 (bufshp + 40 + (iv * 16),
				  GAIA_LITTLE_ENDIAN, endian_arch);
		y = gaiaImport64 (bufshp + 40 + (iv * 16) + 8,
				  GAIA_LITTLE_ENDIAN, endian_arch);
		if (eff_dims == GAIA_XY_Z)
		    gaiaAddPointToGeomCollXYZ (geom, x, y, 0.0);
		else if (eff_dims == GAIA_XY_M)
		    gaiaAddPointToGeomCollXYM (geom, x, y, 0.0);
		else if (eff_dims == GAIA_XY_Z_M)
		    gaiaAddPointToGeomCollXYZM (geom, x, y, 0.0, 0.0);
		else
		    gaiaAddPointToGeomColl (geom, x, y);
	    }
      }
    if (shape == GAIA_SHP_MULTIPOINTZ)
      {
	  /* shape multipoint Z */
	  n = gaiaImport32 (bufshp + 36, GAIA_LITTLE_ENDIAN, endian_arch);
	  hasM = 0;
	  max_size = 36 + (n * 16);	/* size [in 16 bits words !!!] ZM */
	  min_size = 28 + (n * 12);	/* size [in 16 bits words !!!] Z-only */
	  sz = buflen / 2;
	  if (sz < min_size)
	      goto error;
	  if (sz == max_size)
	      hasM = 1;
	  baseZ = 40 + (n * 16) + 16;
	  baseM = baseZ + (n * 8) + 16;
	  if (eff_dims == GAIA_XY_Z)
	      geom = gaiaAllocGeomCollXYZ ();
	  else if (eff_dims == GAIA_XY_M)
	      geom = gaiaAllocGeomCollXYM ();
	  else if (eff_dims == GAIA_XY_Z_M)
	      geom = gaiaAllocGeomCollXYZM ();
	  else
	      geom = gaiaAllocGeomColl ();
	  geom->DeclaredType = GAIA_MULTIPOINT;
	  for (iv = 0; iv < n; iv++)
	    {
		x = gaiaImport64 (bufshp + 40 + (iv * 16),
				  GAIA_LITTLE_ENDIAN, endian_arch);
		y = gaiaImport64 (bufshp + 40 + (iv * 16) + 8,
				  GAIA_LITTLE_ENDIAN, endian_arch);
		z = gaiaImport64 (bufshp + baseZ + (iv * 8),
				  GAIA_LITTLE_ENDIAN, endian_arch);
		if (hasM)
		    m = gaiaImport64 (bufshp + baseM + (iv * 8),
				      GAIA_LITTLE_ENDIAN, endian_arch);
		else
		    m = 0.0;
		if (m < SHAPEFILE_NO_DATA)
		    m = 0.0;
		if (eff_dims == GAIA_XY_Z)
		    gaiaAddPointToGeomCollXYZ (geom, x, y, z);
		else if (eff_dims == GAIA_XY_M)
		    gaiaAddPointToGeomCollXYM (geom, x, y, m);
		else if (eff_dims == GAIA_XY_Z_M)
		    gaiaAddPointToGeomCollXYZM (geom, x, y, z, m);
		else
		    gaiaAddPointToGeomColl (geom, x, y);
	    }
      }
    if (shape == GAIA_SHP_MULTIPOINTM)
      {
	  /* shape multipoint M */
	  n = gaiaImport32 (bufshp + 36, GAIA_LITTLE_ENDIAN, endian_arch);
	  hasM = 0;
	  max_size = 28 + (n * 12);	/* size [in 16 bits words !!!] M */
	  min_size = 20 + (n * 8);	/* size [in 16 bits words !!!] no-M */
	  sz = buflen / 2;
	  if (sz < min_size)
	      goto error;
	  if (sz == max_size)
	      hasM = 1;
	  baseM = 40 + (n * 16) + 16;
	  if (eff_dims == GAIA_XY_Z)
	      geom = gaiaAllocGeomCollXYZ ();
	  else if (eff_dims == GAIA_XY_M)
	      geom = gaiaAllocGeomCollXYM ();
	  else if (eff_dims == GAIA_XY_Z_M)
	      geom = gaiaAllocGeomCollXYZM ();
	  else
	      geom = gaiaAllocGeomColl ();
	  geom->DeclaredType = GAIA_MULTIPOINT;
	  for (iv = 0; iv < n; iv++)
	    {
		x = gaiaImport64 (bufshp + 40 + (iv * 16),
				  GAIA_LITTLE_ENDIAN, endian_arch);
		y = gaiaImport64 (bufshp + 40 + (iv * 16) + 8,
				  GAIA_LITTLE_ENDIAN, endian_arch);
		if (hasM)
		    m = gaiaImport64 (bufshp + baseM + (iv * 8),
				      GAIA_LITTLE_ENDIAN, endian_arch);
		else
		    m = 0.0;
		if (m < SHAPEFILE_NO_DATA)
		    m = 0.0;
		if (eff_dims == GAIA_XY_Z)
		    gaiaAddPointToGeomCollXYZ (geom, x, y, 0.0);
		else if (eff_dims == GAIA_XY_M)
		    gaiaAddPointToGeomCollXYM (geom, x, y, m);
		else if (eff_dims == GAIA_XY_Z_M)
		    gaiaAddPointToGeomCollXYZM (geom, x, y, 0.0, m);
		else
		    gaiaAddPointToGeomColl (geom, x, y);
	    }
      }

    if (geom != NULL)
	gaiaMbrGeometry (geom);
    shp_free_rings (&ringsColl);
    return geom;

  error:
    fprintf (stderr, "\tcorrupted shapefile / invalid format");
    shp_free_rings (&ringsColl);
    return NULL;
}

static int
do_read_shp (const void *cache, const char *shp_path, int validate, int esri,
	     int *invalid)
{
/* reading some Shapefile and testing for validity */
    int current_row;
    gaiaShapefilePtr shp = NULL;
    int ret;
    double minx;
    double miny;
    double maxx;
    double maxy;
    double MinX = DBL_MAX;
    double MinY = DBL_MAX;
    double MaxX = 0.0 - DBL_MAX;
    double MaxY = 0.0 - DBL_MAX;
    double hMinX;
    double hMinY;
    double hMaxX;
    double hMaxY;
    int mismatching;

    *invalid = 0;
    shp = allocShapefile ();
    openShpRead (shp, shp_path, &hMinX, &hMinY, &hMaxX, &hMaxY, &mismatching);
    if (!(shp->Valid))
      {
	  char extra[512];
	  *extra = '\0';
	  if (shp->LastError)
	      sprintf (extra, "\n\tcause: %s\n", shp->LastError);
	  fprintf (stderr,
		   "\terror: cannot open shapefile '%s'%s", shp_path, extra);
	  freeShapefile (shp);
	  return 0;
      }
    if (mismatching)
	*invalid += 1;

    current_row = 0;
    while (1)
      {
	  /* reading rows from shapefile */
	  int shplen;
	  ret =
	      readShpEntity (shp, current_row, &shplen, &minx, &miny, &maxx,
			     &maxy);
	  if (ret < 0)
	    {
		/* found a DBF deleted record */
		fprintf (stderr, "\t\trow #%d: logical deletion found\n",
			 current_row);
		current_row++;
		*invalid += 1;
		continue;
	    }
	  if (!ret)
	    {
		if (!(shp->LastError))	/* normal SHP EOF */
		    break;
		fprintf (stderr, "\tERROR: %s\n", shp->LastError);
		goto stop;
	    }

	  if (validate)
	    {
		int nullshape;
		gaiaGeomCollPtr geom =
		    do_parse_geometry (shp->BufShp, shplen, shp->EffectiveDims,
				       shp->EffectiveType, &nullshape);
		if (nullshape)
		    ;
		else
		  {
		      if (geom == NULL)
			{
			    fprintf (stderr,
				     "\t\trow #%d: unable to get a Geometry\n",
				     current_row);
			    *invalid += 1;
			}
		      else
			{
			    if (geom->MinX != minx || geom->MinY != miny
				|| geom->MaxX != maxx || geom->MaxY != maxy)
			      {
				  fprintf (stderr,
					   "\t\trow #%d: mismatching BBOX\n",
					   current_row);
				  *invalid += 1;
			      }
			    if (esri)
			      {
				  /* checking invalid geometries in ESRI mode */
				  gaiaGeomCollPtr detail;
				  detail =
				      gaiaIsValidDetailEx_r (cache, geom, 1);
				  if (detail == NULL)
				    {
					/* extra checks */
					int extra = 0;
					if (gaiaIsToxic_r (cache, geom))
					    extra = 1;
					if (gaiaIsNotClosedGeomColl_r
					    (cache, geom))
					    extra = 1;
					if (extra)
					  {
					      char *reason =
						  gaiaIsValidReason_r (cache,
								       geom);
					      if (reason == NULL)
						  fprintf (stderr,
							   "\t\trow #%d: invalid Geometry (unknown reason)\n",
							   current_row);
					      else
						{
						    fprintf (stderr,
							     "\t\trow #%d: %s\n",
							     current_row,
							     reason);
						    free (reason);
						}
					      *invalid += 1;
					  }
				    }
				  else
				    {
					char *reason =
					    gaiaIsValidReason_r (cache, geom);
					if (reason == NULL)
					    fprintf (stderr,
						     "\t\trow #%d: invalid Geometry (unknown reason)\n",
						     current_row);
					else
					  {
					      fprintf (stderr,
						       "\t\trow #%d: %s\n",
						       current_row, reason);
					      free (reason);
					  }
					*invalid += 1;
					gaiaFreeGeomColl (detail);
				    }
			      }
			    else
			      {
				  /* checking invalid geometries in ISO/OGC mode */
				  if (gaiaIsValid_r (cache, geom) != 1)
				    {
					char *reason =
					    gaiaIsValidReason_r (cache, geom);
					if (reason == NULL)
					    fprintf (stderr,
						     "\t\trow #%d: invalid Geometry (unknown reason)\n",
						     current_row);
					else
					  {
					      fprintf (stderr,
						       "\t\trow #%d: %s\n",
						       current_row, reason);
					      free (reason);
					  }
					*invalid += 1;
				    }
			      }
			    gaiaFreeGeomColl (geom);
			}
		  }
	    }
	  if (minx != DBL_MAX && miny != DBL_MAX && maxx != DBL_MAX
	      && maxy != DBL_MAX)
	    {
		if (minx < MinX)
		    MinX = minx;
		if (miny < MinY)
		    MinY = miny;
		if (maxx > MaxX)
		    MaxX = maxx;
		if (maxy > MaxY)
		    MaxY = maxy;
	    }
	  current_row++;
      }
    freeShapefile (shp);

    if (MinX != hMinX || MinY != hMinY || MaxX != hMaxX || MaxY != hMaxY)
      {
	  fprintf (stderr, "\t\tHEADERS: found invalid BBOX\n");
	  *invalid += 1;
      }

    return 1;

  stop:
    freeShapefile (shp);
    fprintf (stderr, "\tMalformed shapefile: quitting\n");
    return 0;
}

static void
do_clen_files (const char *out_path, const char *name)
{
/* removing an invalid Shapefile (not properly repaired) */
    char path[1024];

    sprintf (path, "%s/%s.shx", out_path, name);
#ifdef _WIN32
    _unlink (path);
#else
    unlink (path);
#endif

    sprintf (path, "%s/%s.shp", out_path, name);
#ifdef _WIN32
    _unlink (path);
#else
    unlink (path);
#endif

    sprintf (path, "%s/%s.dbf", out_path, name);
#ifdef _WIN32
    _unlink (path);
#else
    unlink (path);
#endif
}

static void
openShpWrite (gaiaShapefilePtr shp, const char *path, int shape,
	      gaiaDbfListPtr dbf_list)
{
/* trying to create the shapefile */
    FILE *fl_shx = NULL;
    FILE *fl_shp = NULL;
    FILE *fl_dbf = NULL;
    char xpath[1024];
    unsigned char *buf_shp = NULL;
    int buf_size = 1024;
    unsigned char *dbf_buf = NULL;
    gaiaDbfFieldPtr fld;
    char *sys_err;
    char errMsg[1024];
    short dbf_reclen = 0;
    int shp_size = 0;
    int shx_size = 0;
    unsigned short dbf_size = 0;
    int len;
    int endian_arch = gaiaEndianArch ();
    char buf[2048];
    if (shp->flShp != NULL || shp->flShx != NULL || shp->flDbf != NULL)
      {
	  sprintf (errMsg,
		   "attempting to reopen an already opened Shapefile\n");
	  goto unsupported_conversion;
      }
    buf_shp = malloc (buf_size);
/* trying to open shapefile files */
    sprintf (xpath, "%s.shx", path);
    fl_shx = fopen (xpath, "wb");
    if (!fl_shx)
      {
	  sys_err = strerror (errno);
	  sprintf (errMsg, "unable to open '%s' for writing: %s", xpath,
		   sys_err);
	  goto no_file;
      }
    sprintf (xpath, "%s.shp", path);
    fl_shp = fopen (xpath, "wb");
    if (!fl_shp)
      {
	  sys_err = strerror (errno);
	  sprintf (errMsg, "unable to open '%s' for writing: %s", xpath,
		   sys_err);
	  goto no_file;
      }
    sprintf (xpath, "%s.dbf", path);
    fl_dbf = fopen (xpath, "wb");
    if (!fl_dbf)
      {
	  sys_err = strerror (errno);
	  sprintf (errMsg, "unable to open '%s' for writing: %s", xpath,
		   sys_err);
	  goto no_file;
      }
/* allocating DBF buffer */
    dbf_reclen = 1;		/* an extra byte is needed because in DBF rows first byte is a marker for deletion */
    fld = dbf_list->First;
    while (fld)
      {
	  /* computing the DBF record length */
	  dbf_reclen += fld->Length;
	  fld = fld->Next;
      }
    dbf_buf = malloc (dbf_reclen);
/* writing an empty SHP file header */
    memset (buf_shp, 0, 100);
    fwrite (buf_shp, 1, 100, fl_shp);
    shp_size = 50;		/* note: shapefile [SHP and SHX] counts sizes in WORDS of 16 bits, not in bytes of 8 bits !!!! */
/* writing an empty SHX file header */
    memset (buf_shp, 0, 100);
    fwrite (buf_shp, 1, 100, fl_shx);
    shx_size = 50;
/* writing the DBF file header */
    memset (buf_shp, '\0', 32);
    fwrite (buf_shp, 1, 32, fl_dbf);
    dbf_size = 32;		/* note: DBF counts sizes in bytes */
    fld = dbf_list->First;
    while (fld)
      {
	  /* exporting DBF Fields specifications */
	  memset (buf_shp, 0, 32);
	  strcpy (buf, fld->Name);
	  memcpy (buf_shp, buf, strlen (buf));
	  *(buf_shp + 11) = fld->Type;
	  *(buf_shp + 16) = fld->Length;
	  *(buf_shp + 17) = fld->Decimals;
	  fwrite (buf_shp, 1, 32, fl_dbf);
	  dbf_size += 32;
	  fld = fld->Next;
      }
    fwrite ("\r", 1, 1, fl_dbf);	/* this one is a special DBF delimiter that closes file header */
    dbf_size++;
/* setting up the SHP struct */
    len = strlen (path);
    shp->Path = malloc (len + 1);
    strcpy (shp->Path, path);
    shp->ReadOnly = 0;
    shp->Shape = shape;
    shp->flShp = fl_shp;
    shp->flShx = fl_shx;
    shp->flDbf = fl_dbf;
    shp->Dbf = dbf_list;
    shp->BufShp = buf_shp;
    shp->ShpBfsz = buf_size;
    shp->BufDbf = dbf_buf;
    shp->DbfHdsz = dbf_size + 1;
    shp->DbfReclen = dbf_reclen;
    shp->DbfSize = dbf_size;
    shp->DbfRecno = 0;
    shp->ShpSize = shp_size;
    shp->ShxSize = shx_size;
    shp->MinX = DBL_MAX;
    shp->MinY = DBL_MAX;
    shp->MaxX = -DBL_MAX;
    shp->MaxY = -DBL_MAX;
    shp->Valid = 1;
    shp->endian_arch = endian_arch;
    return;
  unsupported_conversion:
/* illegal charset */
    if (shp->LastError)
	free (shp->LastError);
    len = strlen (errMsg);
    shp->LastError = malloc (len + 1);
    strcpy (shp->LastError, errMsg);
    return;
  no_file:
/* one of shapefile's files can't be created/opened */
    if (shp->LastError)
	free (shp->LastError);
    len = strlen (errMsg);
    shp->LastError = malloc (len + 1);
    strcpy (shp->LastError, errMsg);
    if (buf_shp)
	free (buf_shp);
    if (fl_shx)
	fclose (fl_shx);
    if (fl_shp)
	fclose (fl_shp);
    if (fl_dbf)
	fclose (fl_dbf);
    return;
}

static int
writeShpEntity (gaiaShapefilePtr shp, const unsigned char *bufshp, int shplen,
		const unsigned char *bufdbf, int dbflen)
{
/* trying to write an entity into shapefile */
    unsigned char buf[64];
    int endian_arch = shp->endian_arch;
    int shape;
    double minx;
    double maxx;
    double miny;
    double maxy;

/* inserting entity in SHX file */
    gaiaExport32 (buf, shp->ShpSize, GAIA_BIG_ENDIAN, endian_arch);	/* exports current SHP file position */
    gaiaExport32 (buf + 4, shplen / 2, GAIA_BIG_ENDIAN, endian_arch);	/* exports entitiy size [in 16 bits words !!!] */
    fwrite (buf, 1, 8, shp->flShx);
    (shp->ShxSize) += 4;	/* updating current SHX file position [in 16 bits words !!!] */

/* inserting entity in SHP file */
    gaiaExport32 (buf, shp->DbfRecno + 1, GAIA_BIG_ENDIAN, endian_arch);	/* exports entity ID */
    gaiaExport32 (buf + 4, shplen / 2, GAIA_BIG_ENDIAN, endian_arch);	/* exports entity size [in 16 bits words !!!] */
    fwrite (buf, 1, 8, shp->flShp);
    (shp->ShpSize) += 4;
    fwrite (bufshp, 1, shplen, shp->flShp);
    (shp->ShpSize) += shplen / 2;	/* updating current SHP file position [in 16 bits words !!!] */

/* inserting entity in DBF file */
    fwrite (bufdbf, 1, dbflen, shp->flDbf);
    (shp->DbfRecno)++;

/* updating the full extent BBOX */
    shape = gaiaImport32 (bufshp + 0, GAIA_LITTLE_ENDIAN, endian_arch);
    if (shape == GAIA_SHP_POINT || shape == GAIA_SHP_POINTZ
	|| shape == GAIA_SHP_POINTM)
      {
	  minx = gaiaImport64 (bufshp + 4, GAIA_LITTLE_ENDIAN, endian_arch);
	  maxx = minx;
	  miny = gaiaImport64 (bufshp + 12, GAIA_LITTLE_ENDIAN, endian_arch);
	  maxy = miny;
	  if (minx < shp->MinX)
	      shp->MinX = minx;
	  if (maxx > shp->MaxX)
	      shp->MaxX = maxx;
	  if (miny < shp->MinY)
	      shp->MinY = miny;
	  if (maxy > shp->MaxY)
	      shp->MaxY = maxy;
      }
    if (shape == GAIA_SHP_POLYLINE || shape == GAIA_SHP_POLYLINEZ
	|| shape == GAIA_SHP_POLYLINEM || shape == GAIA_SHP_POLYGON
	|| shape == GAIA_SHP_POLYGONZ || shape == GAIA_SHP_POLYGONM
	|| shape == GAIA_SHP_MULTIPOINT || shape == GAIA_SHP_MULTIPOINTZ
	|| shape == GAIA_SHP_MULTIPOINTM)
      {
	  minx = gaiaImport64 (bufshp + 4, GAIA_LITTLE_ENDIAN, endian_arch);
	  miny = gaiaImport64 (bufshp + 12, GAIA_LITTLE_ENDIAN, endian_arch);
	  maxx = gaiaImport64 (bufshp + 20, GAIA_LITTLE_ENDIAN, endian_arch);
	  maxy = gaiaImport64 (bufshp + 28, GAIA_LITTLE_ENDIAN, endian_arch);
	  if (minx < shp->MinX)
	      shp->MinX = minx;
	  if (maxx > shp->MaxX)
	      shp->MaxX = maxx;
	  if (miny < shp->MinY)
	      shp->MinY = miny;
	  if (maxy > shp->MaxY)
	      shp->MaxY = maxy;
      }
    return 1;
}

static int
check_geometry (gaiaGeomCollPtr geom, int shape)
{
/* cheching the geometry type and dims */
    int pts = 0;
    int lns = 0;
    int pgs = 0;
    gaiaPointPtr pt;
    gaiaLinestringPtr ln;
    gaiaPolygonPtr pg;

    pt = geom->FirstPoint;
    while (pt != NULL)
      {
	  pts++;
	  pt = pt->Next;
      }
    ln = geom->FirstLinestring;
    while (ln != NULL)
      {
	  lns++;
	  ln = ln->Next;
      }
    pg = geom->FirstPolygon;
    while (pg != NULL)
      {
	  pgs++;
	  pg = pg->Next;
      }

    if (pts == 1 && lns == 0 && pgs == 0)
      {
	  if (shape == GAIA_SHP_POINT && geom->DimensionModel == GAIA_XY)
	      return 1;
	  if (shape == GAIA_SHP_POINTZ
	      && (geom->DimensionModel == GAIA_XY_Z
		  || geom->DimensionModel == GAIA_XY_Z_M))
	      return 1;
	  if (shape == GAIA_SHP_POINTM && geom->DimensionModel == GAIA_XY_M)
	      return 1;
	  if (shape == GAIA_SHP_MULTIPOINT && geom->DimensionModel == GAIA_XY)
	      return 1;
	  if (shape == GAIA_SHP_MULTIPOINTZ
	      && (geom->DimensionModel == GAIA_XY_Z
		  || geom->DimensionModel == GAIA_XY_Z_M))
	      return 1;
	  if (shape == GAIA_SHP_MULTIPOINTM
	      && geom->DimensionModel == GAIA_XY_M)
	      return 1;
      }
    if (pts > 1 && lns == 0 && pgs == 0)
      {
	  if (shape == GAIA_SHP_MULTIPOINT && geom->DimensionModel == GAIA_XY)
	      return 1;
	  if (shape == GAIA_SHP_MULTIPOINTZ
	      && (geom->DimensionModel == GAIA_XY_Z
		  || geom->DimensionModel == GAIA_XY_Z_M))
	      return 1;
	  if (shape == GAIA_SHP_MULTIPOINTM
	      && geom->DimensionModel == GAIA_XY_M)
	      return 1;
      }
    if (pts == 0 && lns > 0 && pgs == 0)
      {
	  if (shape == GAIA_SHP_POLYLINE && geom->DimensionModel == GAIA_XY)
	      return 1;
	  if (shape == GAIA_SHP_POLYLINEZ
	      && (geom->DimensionModel == GAIA_XY_Z
		  || geom->DimensionModel == GAIA_XY_Z_M))
	      return 1;
	  if (shape == GAIA_SHP_POLYLINEM && geom->DimensionModel == GAIA_XY_M)
	      return 1;
      }
    if (pts == 0 && lns == 0 && pgs > 0)
      {
	  if (shape == GAIA_SHP_POLYGON && geom->DimensionModel == GAIA_XY)
	      return 1;
	  if (shape == GAIA_SHP_POLYGONZ
	      && (geom->DimensionModel == GAIA_XY_Z
		  || geom->DimensionModel == GAIA_XY_Z_M))
	      return 1;
	  if (shape == GAIA_SHP_POLYGONM && geom->DimensionModel == GAIA_XY_M)
	      return 1;
      }

    return 0;
}

static int
check_geometry_verbose (gaiaGeomCollPtr geom, int shape, char **expected,
			char **actual)
{
/* cheching the geometry type and dims - verbose */
    int pts = 0;
    int lns = 0;
    int pgs = 0;
    gaiaPointPtr pt;
    gaiaLinestringPtr ln;
    gaiaPolygonPtr pg;
    const char *str;
    int len;

    *expected = NULL;
    *actual = NULL;
    pt = geom->FirstPoint;
    while (pt != NULL)
      {
	  pts++;
	  pt = pt->Next;
      }
    ln = geom->FirstLinestring;
    while (ln != NULL)
      {
	  lns++;
	  ln = ln->Next;
      }
    pg = geom->FirstPolygon;
    while (pg != NULL)
      {
	  pgs++;
	  pg = pg->Next;
      }

    if (pts == 1 && lns == 0 && pgs == 0)
      {
	  if (shape == GAIA_SHP_POINT && geom->DimensionModel == GAIA_XY)
	      return 1;
	  if (shape == GAIA_SHP_POINTZ
	      && (geom->DimensionModel == GAIA_XY_Z
		  || geom->DimensionModel == GAIA_XY_Z_M))
	      return 1;
	  if (shape == GAIA_SHP_POINTM && geom->DimensionModel == GAIA_XY_M)
	      return 1;
	  if (shape == GAIA_SHP_MULTIPOINT && geom->DimensionModel == GAIA_XY)
	      return 1;
	  if (shape == GAIA_SHP_MULTIPOINTZ
	      && (geom->DimensionModel == GAIA_XY_Z
		  || geom->DimensionModel == GAIA_XY_Z_M))
	      return 1;
	  if (shape == GAIA_SHP_MULTIPOINTM
	      && geom->DimensionModel == GAIA_XY_M)
	      return 1;
      }
    if (pts > 1 && lns == 0 && pgs == 0)
      {
	  if (shape == GAIA_SHP_MULTIPOINT && geom->DimensionModel == GAIA_XY)
	      return 1;
	  if (shape == GAIA_SHP_MULTIPOINTZ
	      && (geom->DimensionModel == GAIA_XY_Z
		  || geom->DimensionModel == GAIA_XY_Z_M))
	      return 1;
	  if (shape == GAIA_SHP_MULTIPOINTM
	      && geom->DimensionModel == GAIA_XY_M)
	      return 1;
      }
    if (pts == 0 && lns > 0 && pgs == 0)
      {
	  if (shape == GAIA_SHP_POLYLINE && geom->DimensionModel == GAIA_XY)
	      return 1;
	  if (shape == GAIA_SHP_POLYLINEZ
	      && (geom->DimensionModel == GAIA_XY_Z
		  || geom->DimensionModel == GAIA_XY_Z_M))
	      return 1;
	  if (shape == GAIA_SHP_POLYLINEM && geom->DimensionModel == GAIA_XY_M)
	      return 1;
      }
    if (pts == 0 && lns == 0 && pgs > 0)
      {
	  if (shape == GAIA_SHP_POLYGON && geom->DimensionModel == GAIA_XY)
	      return 1;
	  if (shape == GAIA_SHP_POLYGONZ
	      && (geom->DimensionModel == GAIA_XY_Z
		  || geom->DimensionModel == GAIA_XY_Z_M))
	      return 1;
	  if (shape == GAIA_SHP_POLYGONM && geom->DimensionModel == GAIA_XY_M)
	      return 1;
      }

    switch (shape)
      {
      case GAIA_SHP_POINT:
	  str = "POINT";
	  break;
      case GAIA_SHP_POINTZ:
	  str = "POINT-M";
	  break;
      case GAIA_SHP_POINTM:
	  str = "POINT-Z";
	  break;
      case GAIA_SHP_POLYLINE:
	  str = "POLYLINE";
	  break;
      case GAIA_SHP_POLYLINEZ:
	  str = "POLYLINE-Z";
	  break;
      case GAIA_SHP_POLYLINEM:
	  str = "POLYLINE-M";
	  break;
      case GAIA_SHP_POLYGON:
	  str = "POLYGON";
	  break;
      case GAIA_SHP_POLYGONZ:
	  str = "POLYGON-Z";
	  break;
      case GAIA_SHP_POLYGONM:
	  str = "POLYGON-M";
	  break;
      case GAIA_SHP_MULTIPOINT:
	  str = "MULTIPOINT";
	  break;
      case GAIA_SHP_MULTIPOINTZ:
	  str = "MULTIPOINT-Z";
	  break;
      case GAIA_SHP_MULTIPOINTM:
	  str = "MULTIPOINT-M";
	  break;
      default:
	  str = "UNKNOWN";
	  break;
      };
    len = strlen (str);
    *expected = malloc (len + 1);
    strcpy (*expected, str);

    str = "UNKNOWN";
    if (pts == 1 && lns == 0 && pgs == 0)
      {
	  if (geom->DimensionModel == GAIA_XY)
	      str = "POINT";
	  if (geom->DimensionModel == GAIA_XY_Z
	      || geom->DimensionModel == GAIA_XY_Z_M)
	      str = "POINT-Z";
	  if (geom->DimensionModel == GAIA_XY_M)
	      str = "POINT-M";
      }
    if (pts > 1 && lns == 0 && pgs == 0)
      {
	  if (geom->DimensionModel == GAIA_XY)
	      str = "MULTIPOINT";
	  if (geom->DimensionModel == GAIA_XY_Z
	      || geom->DimensionModel == GAIA_XY_Z_M)
	      str = "MULTIPOINT-Z";
	  if (geom->DimensionModel == GAIA_XY_M)
	      str = "MULTIPOINT-M";
      }
    if (pts == 0 && lns > 0 && pgs == 0)
      {
	  if (geom->DimensionModel == GAIA_XY)
	      str = "POLYLINE";
	  if (geom->DimensionModel == GAIA_XY_Z
	      || geom->DimensionModel == GAIA_XY_Z_M)
	      str = "POLYLINE-Z";
	  if (geom->DimensionModel == GAIA_XY_M)
	      str = "POLYLINE-M";
      }
    if (pts == 0 && lns == 0 && pgs > 0)
      {
	  if (geom->DimensionModel == GAIA_XY)
	      str = "POLYGON";
	  if (geom->DimensionModel == GAIA_XY_Z
	      || geom->DimensionModel == GAIA_XY_Z_M)
	      str = "POLYGON-Z";
	  return 1;
	  if (geom->DimensionModel == GAIA_XY_M)
	      str = "POLYGON-M";
	  return 1;
      }
    len = strlen (str);
    *actual = malloc (len + 1);
    strcpy (*actual, str);

    return 0;
}

static void
gaiaSaneClockwise (gaiaPolygonPtr polyg)
{
/*
/ when exporting POLYGONs to SHAPEFILE, we must guarantee that:
/ - all EXTERIOR RING must be clockwise
/ - all INTERIOR RING must be anti-clockwise
/
/ this function checks for the above conditions,
/ and if needed inverts the rings
*/
    int ib;
    int iv;
    int iv2;
    double x;
    double y;
    double z;
    double m;
    gaiaRingPtr new_ring;
    gaiaRingPtr ring = polyg->Exterior;
    gaiaClockwise (ring);
    if (!(ring->Clockwise))
      {
	  /* exterior ring needs inversion */
	  if (ring->DimensionModel == GAIA_XY_Z)
	      new_ring = gaiaAllocRingXYZ (ring->Points);
	  else if (ring->DimensionModel == GAIA_XY_M)
	      new_ring = gaiaAllocRingXYM (ring->Points);
	  else if (ring->DimensionModel == GAIA_XY_Z_M)
	      new_ring = gaiaAllocRingXYZM (ring->Points);
	  else
	      new_ring = gaiaAllocRing (ring->Points);
	  iv2 = 0;
	  for (iv = ring->Points - 1; iv >= 0; iv--)
	    {
		if (ring->DimensionModel == GAIA_XY_Z)
		  {
		      gaiaGetPointXYZ (ring->Coords, iv, &x, &y, &z);
		      gaiaSetPointXYZ (new_ring->Coords, iv2, x, y, z);
		  }
		else if (ring->DimensionModel == GAIA_XY_M)
		  {
		      gaiaGetPointXYM (ring->Coords, iv, &x, &y, &m);
		      gaiaSetPointXYM (new_ring->Coords, iv2, x, y, m);
		  }
		else if (ring->DimensionModel == GAIA_XY_Z_M)
		  {
		      gaiaGetPointXYZM (ring->Coords, iv, &x, &y, &z, &m);
		      gaiaSetPointXYZM (new_ring->Coords, iv2, x, y, z, m);
		  }
		else
		  {
		      gaiaGetPoint (ring->Coords, iv, &x, &y);
		      gaiaSetPoint (new_ring->Coords, iv2, x, y);
		  }
		iv2++;
	    }
	  polyg->Exterior = new_ring;
	  gaiaFreeRing (ring);
      }
    for (ib = 0; ib < polyg->NumInteriors; ib++)
      {
	  ring = polyg->Interiors + ib;
	  gaiaClockwise (ring);
	  if (ring->Clockwise)
	    {
		/* interior ring needs inversion */
		if (ring->DimensionModel == GAIA_XY_Z)
		    new_ring = gaiaAllocRingXYZ (ring->Points);
		else if (ring->DimensionModel == GAIA_XY_M)
		    new_ring = gaiaAllocRingXYM (ring->Points);
		else if (ring->DimensionModel == GAIA_XY_Z_M)
		    new_ring = gaiaAllocRingXYZM (ring->Points);
		else
		    new_ring = gaiaAllocRing (ring->Points);
		iv2 = 0;
		for (iv = ring->Points - 1; iv >= 0; iv--)
		  {
		      if (ring->DimensionModel == GAIA_XY_Z)
			{
			    gaiaGetPointXYZ (ring->Coords, iv, &x, &y, &z);
			    gaiaSetPointXYZ (new_ring->Coords, iv2, x, y, z);
			}
		      else if (ring->DimensionModel == GAIA_XY_M)
			{
			    gaiaGetPointXYM (ring->Coords, iv, &x, &y, &m);
			    gaiaSetPointXYM (new_ring->Coords, iv2, x, y, m);
			}
		      else if (ring->DimensionModel == GAIA_XY_Z_M)
			{
			    gaiaGetPointXYZM (ring->Coords, iv, &x, &y, &z, &m);
			    gaiaSetPointXYZM (new_ring->Coords, iv2, x, y,
					      z, m);
			}
		      else
			{
			    gaiaGetPoint (ring->Coords, iv, &x, &y);
			    gaiaSetPoint (new_ring->Coords, iv2, x, y);
			}
		      iv2++;
		  }
		for (iv = 0; iv < ring->Points; iv++)
		  {
		      if (ring->DimensionModel == GAIA_XY_Z)
			{
			    gaiaGetPointXYZ (new_ring->Coords, iv, &x, &y, &z);
			    gaiaSetPointXYZ (ring->Coords, iv, x, y, z);
			}
		      else if (ring->DimensionModel == GAIA_XY_M)
			{
			    gaiaGetPointXYM (new_ring->Coords, iv, &x, &y, &m);
			    gaiaSetPointXYM (ring->Coords, iv, x, y, m);
			}
		      else if (ring->DimensionModel == GAIA_XY_Z_M)
			{
			    gaiaGetPointXYZM (new_ring->Coords, iv, &x, &y,
					      &z, &m);
			    gaiaSetPointXYZM (ring->Coords, iv, x, y, z, m);
			}
		      else
			{
			    gaiaGetPoint (new_ring->Coords, iv, &x, &y);
			    gaiaSetPoint (ring->Coords, iv, x, y);
			}
		  }
		gaiaFreeRing (new_ring);
	    }
      }
}

static int
do_export_geometry (gaiaGeomCollPtr geom, unsigned char **bufshp, int *buflen,
		    int xshape, int rowno, int eff_dims)
{
/* attempting to encode a Geometry */
    unsigned char *buf;
    int iv;
    int tot_ln;
    int tot_v;
    int tot_pts;
    int this_size;
    int ix;
    double x;
    double y;
    double z;
    double m;
    int hasM;
    double minZ;
    double maxZ;
    double minM;
    double maxM;
    int endian_arch = gaiaEndianArch ();

    if (geom == NULL)
      {
	  /* exporting a NULL Shape */
	  *buflen = 4;
	  buf = malloc (4);
	  gaiaExport32 (buf + 0, GAIA_SHP_NULL, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = NULL */
	  *bufshp = buf;
	  return 1;
      }

    if (!check_geometry (geom, xshape))
      {
	  /* mismatching Geometry type */
	  fprintf (stderr, "\tinput row #%d: mismatching Geometry type\n",
		   rowno);
	  return 0;
      }
    gaiaMbrGeometry (geom);

    if (xshape == GAIA_SHP_POINT)
      {
	  /* this one is expected to be a POINT */
	  gaiaPointPtr pt = geom->FirstPoint;
	  *buflen = 20;
	  buf = malloc (20);
	  gaiaExport32 (buf + 0, GAIA_SHP_POINT, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = POINT */
	  gaiaExport64 (buf + 4, pt->X, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports X coordinate */
	  gaiaExport64 (buf + 12, pt->Y, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports Y coordinate */
	  *bufshp = buf;
	  return 1;
      }
    if (xshape == GAIA_SHP_POINTZ)
      {
	  /* this one is expected to be a POINT Z */
	  gaiaPointPtr pt = geom->FirstPoint;
	  *buflen = 36;
	  buf = malloc (36);
	  gaiaExport32 (buf + 0, GAIA_SHP_POINTZ, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = POINT Z */
	  gaiaExport64 (buf + 4, pt->X, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports X coordinate */
	  gaiaExport64 (buf + 12, pt->Y, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports Y coordinate */
	  gaiaExport64 (buf + 20, pt->Z, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports Z coordinate */
	  gaiaExport64 (buf + 28, pt->M, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports M coordinate */
	  *bufshp = buf;
	  return 1;
      }
    if (xshape == GAIA_SHP_POINTM)
      {
	  /* this one is expected to be a POINT M */
	  gaiaPointPtr pt = geom->FirstPoint;
	  *buflen = 28;
	  buf = malloc (28);
	  gaiaExport32 (buf + 0, GAIA_SHP_POINTM, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = POINT M */
	  gaiaExport64 (buf + 4, pt->X, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports X coordinate */
	  gaiaExport64 (buf + 12, pt->Y, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports Y coordinate */
	  gaiaExport64 (buf + 20, pt->Y, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports M coordinate */
	  *bufshp = buf;
	  return 1;
      }
    if (xshape == GAIA_SHP_POLYLINE)
      {
	  /* this one is expected to be a LINESTRING / MULTILINESTRING */
	  gaiaLinestringPtr line;
	  tot_ln = 0;
	  tot_v = 0;
	  line = geom->FirstLinestring;
	  while (line)
	    {
		/* computes # lines and total # points */
		tot_v += line->Points;
		tot_ln++;
		line = line->Next;
	    }
	  this_size = 22 + (2 * tot_ln) + (tot_v * 8);	/* size [in 16 bits words !!!] for this SHP entity */
	  *buflen = this_size * 2;
	  buf = malloc (this_size * 2);
	  gaiaExport32 (buf + 0, GAIA_SHP_POLYLINE, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = POLYLINE */
	  gaiaExport64 (buf + 4, geom->MinX, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports the MBR for this geometry */
	  gaiaExport64 (buf + 12, geom->MinY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 20, geom->MaxX, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 28, geom->MaxY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport32 (buf + 36, tot_ln, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports # lines in this polyline */
	  gaiaExport32 (buf + 40, tot_v, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports total # points */
	  tot_v = 0;		/* resets points counter */
	  ix = 44;		/* sets current buffer offset */
	  line = geom->FirstLinestring;
	  while (line)
	    {
		/* exports start point index for each line */
		gaiaExport32 (buf + ix, tot_v, GAIA_LITTLE_ENDIAN, endian_arch);
		tot_v += line->Points;
		ix += 4;
		line = line->Next;
	    }
	  line = geom->FirstLinestring;
	  while (line)
	    {
		/* exports points for each line */
		for (iv = 0; iv < line->Points; iv++)
		  {
		      /* exports a POINT [x,y] */
		      if (line->DimensionModel == GAIA_XY_Z)
			{
			    gaiaGetPointXYZ (line->Coords, iv, &x, &y, &z);
			}
		      else if (line->DimensionModel == GAIA_XY_M)
			{
			    gaiaGetPointXYM (line->Coords, iv, &x, &y, &m);
			}
		      else if (line->DimensionModel == GAIA_XY_Z_M)
			{
			    gaiaGetPointXYZM (line->Coords, iv, &x, &y, &z, &m);
			}
		      else
			{
			    gaiaGetPoint (line->Coords, iv, &x, &y);
			}
		      gaiaExport64 (buf + ix, x,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		      gaiaExport64 (buf + ix, y,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		  }
		line = line->Next;
	    }
	  *bufshp = buf;
	  return 1;
      }
    if (xshape == GAIA_SHP_POLYLINEZ)
      {
	  /* this one is expected to be a LINESTRING / MULTILINESTRING Z */
	  gaiaLinestringPtr line;
	  gaiaZRangeGeometry (geom, &minZ, &maxZ);
	  gaiaMRangeGeometry (geom, &minM, &maxM);
	  tot_ln = 0;
	  tot_v = 0;
	  line = geom->FirstLinestring;
	  while (line)
	    {
		/* computes # lines and total # points */
		tot_v += line->Points;
		tot_ln++;
		line = line->Next;
	    }
	  hasM = 0;
	  if (eff_dims == GAIA_XY_M || eff_dims == GAIA_XY_Z_M)
	      hasM = 1;
	  if (hasM)
	      this_size = 38 + (2 * tot_ln) + (tot_v * 16);	/* size [in 16 bits words !!!] ZM */
	  else
	      this_size = 30 + (2 * tot_ln) + (tot_v * 12);	/* size [in 16 bits words !!!] Z-only */
	  *buflen = this_size * 2;
	  buf = malloc (this_size * 2);
	  gaiaExport32 (buf + 0, GAIA_SHP_POLYLINEZ, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = POLYLINE Z */
	  gaiaExport64 (buf + 4, geom->MinX, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports the MBR for this geometry */
	  gaiaExport64 (buf + 12, geom->MinY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 20, geom->MaxX, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 28, geom->MaxY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport32 (buf + 36, tot_ln, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports # lines in this polyline */
	  gaiaExport32 (buf + 40, tot_v, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports total # points */
	  tot_v = 0;		/* resets points counter */
	  ix = 44;		/* sets current buffer offset */
	  line = geom->FirstLinestring;
	  while (line)
	    {
		/* exports start point index for each line */
		gaiaExport32 (buf + ix, tot_v, GAIA_LITTLE_ENDIAN, endian_arch);
		tot_v += line->Points;
		ix += 4;
		line = line->Next;
	    }
	  line = geom->FirstLinestring;
	  while (line)
	    {
		/* exports points for each line */
		for (iv = 0; iv < line->Points; iv++)
		  {
		      /* exports a POINT [x,y] */
		      if (line->DimensionModel == GAIA_XY_Z)
			{
			    gaiaGetPointXYZ (line->Coords, iv, &x, &y, &z);
			}
		      else if (line->DimensionModel == GAIA_XY_M)
			{
			    gaiaGetPointXYM (line->Coords, iv, &x, &y, &m);
			}
		      else if (line->DimensionModel == GAIA_XY_Z_M)
			{
			    gaiaGetPointXYZM (line->Coords, iv, &x, &y, &z, &m);
			}
		      else
			{
			    gaiaGetPoint (line->Coords, iv, &x, &y);
			}
		      gaiaExport64 (buf + ix, x,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		      gaiaExport64 (buf + ix, y,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		  }
		line = line->Next;
	    }
	  /* exporting the Z-range [min/max] */
	  gaiaExport64 (buf + ix, minZ, GAIA_LITTLE_ENDIAN, endian_arch);
	  ix += 8;
	  gaiaExport64 (buf + ix, maxZ, GAIA_LITTLE_ENDIAN, endian_arch);
	  ix += 8;
	  line = geom->FirstLinestring;
	  while (line)
	    {
		/* exports Z-values for each line */
		for (iv = 0; iv < line->Points; iv++)
		  {
		      /* exports Z-value */
		      z = 0.0;
		      if (line->DimensionModel == GAIA_XY_Z)
			{
			    gaiaGetPointXYZ (line->Coords, iv, &x, &y, &z);
			}
		      else if (line->DimensionModel == GAIA_XY_M)
			{
			    gaiaGetPointXYM (line->Coords, iv, &x, &y, &m);
			}
		      else if (line->DimensionModel == GAIA_XY_Z_M)
			{
			    gaiaGetPointXYZM (line->Coords, iv, &x, &y, &z, &m);
			}
		      else
			{
			    gaiaGetPoint (line->Coords, iv, &x, &y);
			}
		      gaiaExport64 (buf + ix, z,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		  }
		line = line->Next;
	    }
	  if (hasM)
	    {
		/* exporting the M-range [min/max] */
		gaiaExport64 (buf + ix, minM, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		gaiaExport64 (buf + ix, maxM, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		line = geom->FirstLinestring;
		while (line)
		  {
		      /* exports M-values for each line */
		      for (iv = 0; iv < line->Points; iv++)
			{
			    /* exports M-value */
			    m = 0.0;
			    if (line->DimensionModel == GAIA_XY_Z)
			      {
				  gaiaGetPointXYZ (line->Coords, iv,
						   &x, &y, &z);
			      }
			    else if (line->DimensionModel == GAIA_XY_M)
			      {
				  gaiaGetPointXYM (line->Coords, iv,
						   &x, &y, &m);
			      }
			    else if (line->DimensionModel == GAIA_XY_Z_M)
			      {
				  gaiaGetPointXYZM (line->Coords, iv,
						    &x, &y, &z, &m);
			      }
			    else
			      {
				  gaiaGetPoint (line->Coords, iv, &x, &y);
			      }
			    gaiaExport64 (buf + ix, m,
					  GAIA_LITTLE_ENDIAN, endian_arch);
			    ix += 8;
			}
		      line = line->Next;
		  }
	    }
	  *bufshp = buf;
	  return 1;
      }
    if (xshape == GAIA_SHP_POLYLINEM)
      {
	  /* this one is expected to be a LINESTRING / MULTILINESTRING M */
	  gaiaLinestringPtr line;
	  gaiaMRangeGeometry (geom, &minM, &maxM);
	  tot_ln = 0;
	  tot_v = 0;
	  line = geom->FirstLinestring;
	  while (line)
	    {
		/* computes # lines and total # points */
		tot_v += line->Points;
		tot_ln++;
		line = line->Next;
	    }
	  this_size = 30 + (2 * tot_ln) + (tot_v * 12);	/* size [in 16 bits words !!!] for this SHP entity */
	  *buflen = this_size * 2;
	  buf = malloc (this_size * 2);
	  gaiaExport32 (buf + 0, GAIA_SHP_POLYLINEM, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = POLYLINE M */
	  gaiaExport64 (buf + 4, geom->MinX, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports the MBR for this geometry */
	  gaiaExport64 (buf + 12, geom->MinY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 20, geom->MaxX, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 28, geom->MaxY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport32 (buf + 36, tot_ln, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports # lines in this polyline */
	  gaiaExport32 (buf + 40, tot_v, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports total # points */
	  tot_v = 0;		/* resets points counter */
	  ix = 44;		/* sets current buffer offset */
	  line = geom->FirstLinestring;
	  while (line)
	    {
		/* exports start point index for each line */
		gaiaExport32 (buf + ix, tot_v, GAIA_LITTLE_ENDIAN, endian_arch);
		tot_v += line->Points;
		ix += 4;
		line = line->Next;
	    }
	  line = geom->FirstLinestring;
	  while (line)
	    {
		/* exports points for each line */
		for (iv = 0; iv < line->Points; iv++)
		  {
		      /* exports a POINT [x,y] */
		      if (line->DimensionModel == GAIA_XY_Z)
			{
			    gaiaGetPointXYZ (line->Coords, iv, &x, &y, &z);
			}
		      else if (line->DimensionModel == GAIA_XY_M)
			{
			    gaiaGetPointXYM (line->Coords, iv, &x, &y, &m);
			}
		      else if (line->DimensionModel == GAIA_XY_Z_M)
			{
			    gaiaGetPointXYZM (line->Coords, iv, &x, &y, &z, &m);
			}
		      else
			{
			    gaiaGetPoint (line->Coords, iv, &x, &y);
			}
		      gaiaExport64 (buf + ix, x,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		      gaiaExport64 (buf + ix, y,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		  }
		line = line->Next;
	    }
	  /* exporting the M-range [min/max] */
	  gaiaExport64 (buf + ix, minM, GAIA_LITTLE_ENDIAN, endian_arch);
	  ix += 8;
	  gaiaExport64 (buf + ix, maxM, GAIA_LITTLE_ENDIAN, endian_arch);
	  ix += 8;
	  line = geom->FirstLinestring;
	  while (line)
	    {
		/* exports M-values for each line */
		for (iv = 0; iv < line->Points; iv++)
		  {
		      /* exports M-value */
		      m = 0.0;
		      if (line->DimensionModel == GAIA_XY_Z)
			{
			    gaiaGetPointXYZ (line->Coords, iv, &x, &y, &z);
			}
		      else if (line->DimensionModel == GAIA_XY_M)
			{
			    gaiaGetPointXYM (line->Coords, iv, &x, &y, &m);
			}
		      else if (line->DimensionModel == GAIA_XY_Z_M)
			{
			    gaiaGetPointXYZM (line->Coords, iv, &x, &y, &z, &m);
			}
		      else
			{
			    gaiaGetPoint (line->Coords, iv, &x, &y);
			}
		      gaiaExport64 (buf + ix, m,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		  }
		line = line->Next;
	    }
	  *bufshp = buf;
	  return 1;
      }
    if (xshape == GAIA_SHP_POLYGON)
      {
	  /* this one is expected to be a POLYGON or a MULTIPOLYGON */
	  gaiaPolygonPtr polyg;
	  gaiaRingPtr ring;
	  int ib;
	  tot_ln = 0;
	  tot_v = 0;
	  polyg = geom->FirstPolygon;
	  while (polyg)
	    {
		/* computes # rings and total # points */
		gaiaSaneClockwise (polyg);	/* we must assure that exterior ring is clockwise, and interior rings are anti-clockwise */
		ring = polyg->Exterior;	/* this one is the exterior ring */
		tot_v += ring->Points;
		tot_ln++;
		for (ib = 0; ib < polyg->NumInteriors; ib++)
		  {
		      /* that ones are the interior rings */
		      ring = polyg->Interiors + ib;
		      tot_v += ring->Points;
		      tot_ln++;
		  }
		polyg = polyg->Next;
	    }
	  this_size = 22 + (2 * tot_ln) + (tot_v * 8);	/* size [in 16 bits words !!!] for this SHP entity */
	  *buflen = this_size * 2;
	  buf = malloc (this_size * 2);
	  gaiaExport32 (buf + 0, GAIA_SHP_POLYGON, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = POLYGON */
	  gaiaExport64 (buf + 4, geom->MinX, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports the MBR for this geometry */
	  gaiaExport64 (buf + 12, geom->MinY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 20, geom->MaxX, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 28, geom->MaxY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport32 (buf + 36, tot_ln, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports # rings in this polygon */
	  gaiaExport32 (buf + 40, tot_v, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports total # points */
	  tot_v = 0;		/* resets points counter */
	  ix = 44;		/* sets current buffer offset */
	  polyg = geom->FirstPolygon;
	  while (polyg)
	    {
		/* exports start point index for each line */
		ring = polyg->Exterior;	/* this one is the exterior ring */
		gaiaExport32 (buf + ix, tot_v, GAIA_LITTLE_ENDIAN, endian_arch);
		tot_v += ring->Points;
		ix += 4;
		for (ib = 0; ib < polyg->NumInteriors; ib++)
		  {
		      /* that ones are the interior rings */
		      ring = polyg->Interiors + ib;
		      gaiaExport32 (buf + ix, tot_v,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      tot_v += ring->Points;
		      ix += 4;
		  }
		polyg = polyg->Next;
	    }
	  polyg = geom->FirstPolygon;
	  while (polyg)
	    {
		/* exports points for each ring */
		ring = polyg->Exterior;	/* this one is the exterior ring */
		for (iv = 0; iv < ring->Points; iv++)
		  {
		      /* exports a POINT [x,y] - exterior ring */
		      if (ring->DimensionModel == GAIA_XY_Z)
			{
			    gaiaGetPointXYZ (ring->Coords, iv, &x, &y, &z);
			}
		      else if (ring->DimensionModel == GAIA_XY_M)
			{
			    gaiaGetPointXYM (ring->Coords, iv, &x, &y, &m);
			}
		      else if (ring->DimensionModel == GAIA_XY_Z_M)
			{
			    gaiaGetPointXYZM (ring->Coords, iv, &x, &y, &z, &m);
			}
		      else
			{
			    gaiaGetPoint (ring->Coords, iv, &x, &y);
			}
		      gaiaExport64 (buf + ix, x,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		      gaiaExport64 (buf + ix, y,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		  }
		for (ib = 0; ib < polyg->NumInteriors; ib++)
		  {
		      /* that ones are the interior rings */
		      ring = polyg->Interiors + ib;
		      for (iv = 0; iv < ring->Points; iv++)
			{
			    /* exports a POINT [x,y] - interior ring */
			    if (ring->DimensionModel == GAIA_XY_Z)
			      {
				  gaiaGetPointXYZ (ring->Coords, iv,
						   &x, &y, &z);
			      }
			    else if (ring->DimensionModel == GAIA_XY_M)
			      {
				  gaiaGetPointXYM (ring->Coords, iv,
						   &x, &y, &m);
			      }
			    else if (ring->DimensionModel == GAIA_XY_Z_M)
			      {
				  gaiaGetPointXYZM (ring->Coords, iv,
						    &x, &y, &z, &m);
			      }
			    else
			      {
				  gaiaGetPoint (ring->Coords, iv, &x, &y);
			      }
			    gaiaExport64 (buf + ix, x,
					  GAIA_LITTLE_ENDIAN, endian_arch);
			    ix += 8;
			    gaiaExport64 (buf + ix, y,
					  GAIA_LITTLE_ENDIAN, endian_arch);
			    ix += 8;
			}
		  }
		polyg = polyg->Next;
	    }
	  *bufshp = buf;
	  return 1;
      }
    if (xshape == GAIA_SHP_POLYGONZ)
      {
	  /* this one is expected to be a POLYGON or a MULTIPOLYGON Z */
	  gaiaPolygonPtr polyg;
	  gaiaRingPtr ring;
	  int ib;
	  gaiaZRangeGeometry (geom, &minZ, &maxZ);
	  gaiaMRangeGeometry (geom, &minM, &maxM);
	  tot_ln = 0;
	  tot_v = 0;
	  polyg = geom->FirstPolygon;
	  while (polyg)
	    {
		/* computes # rings and total # points */
		gaiaSaneClockwise (polyg);	/* we must assure that exterior ring is clockwise, and interior rings are anti-clockwise */
		ring = polyg->Exterior;	/* this one is the exterior ring */
		tot_v += ring->Points;
		tot_ln++;
		for (ib = 0; ib < polyg->NumInteriors; ib++)
		  {
		      /* that ones are the interior rings */
		      ring = polyg->Interiors + ib;
		      tot_v += ring->Points;
		      tot_ln++;
		  }
		polyg = polyg->Next;
	    }
	  hasM = 0;
	  if (eff_dims == GAIA_XY_M || eff_dims == GAIA_XY_Z_M)
	      hasM = 1;
	  if (hasM)
	      this_size = 38 + (2 * tot_ln) + (tot_v * 16);	/* size [in 16 bits words !!!] ZM */
	  else
	      this_size = 30 + (2 * tot_ln) + (tot_v * 12);	/* size [in 16 bits words !!!] Z-only */
	  *buflen = this_size * 2;
	  buf = malloc (this_size * 2);
	  gaiaExport32 (buf + 0, GAIA_SHP_POLYGONZ, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = POLYGON Z */
	  gaiaExport64 (buf + 4, geom->MinX, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports the MBR for this geometry */
	  gaiaExport64 (buf + 12, geom->MinY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 20, geom->MaxX, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 28, geom->MaxY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport32 (buf + 36, tot_ln, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports # rings in this polygon */
	  gaiaExport32 (buf + 40, tot_v, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports total # points */
	  tot_v = 0;		/* resets points counter */
	  ix = 44;		/* sets current buffer offset */
	  polyg = geom->FirstPolygon;
	  while (polyg)
	    {
		/* exports start point index for each line */
		ring = polyg->Exterior;	/* this one is the exterior ring */
		gaiaExport32 (buf + ix, tot_v, GAIA_LITTLE_ENDIAN, endian_arch);
		tot_v += ring->Points;
		ix += 4;
		for (ib = 0; ib < polyg->NumInteriors; ib++)
		  {
		      /* that ones are the interior rings */
		      ring = polyg->Interiors + ib;
		      gaiaExport32 (buf + ix, tot_v,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      tot_v += ring->Points;
		      ix += 4;
		  }
		polyg = polyg->Next;
	    }
	  polyg = geom->FirstPolygon;
	  while (polyg)
	    {
		/* exports points for each ring */
		ring = polyg->Exterior;	/* this one is the exterior ring */
		for (iv = 0; iv < ring->Points; iv++)
		  {
		      /* exports a POINT [x,y] - exterior ring */
		      if (ring->DimensionModel == GAIA_XY_Z)
			{
			    gaiaGetPointXYZ (ring->Coords, iv, &x, &y, &z);
			}
		      else if (ring->DimensionModel == GAIA_XY_M)
			{
			    gaiaGetPointXYM (ring->Coords, iv, &x, &y, &m);
			}
		      else if (ring->DimensionModel == GAIA_XY_Z_M)
			{
			    gaiaGetPointXYZM (ring->Coords, iv, &x, &y, &z, &m);
			}
		      else
			{
			    gaiaGetPoint (ring->Coords, iv, &x, &y);
			}
		      gaiaExport64 (buf + ix, x,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		      gaiaExport64 (buf + ix, y,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		  }
		for (ib = 0; ib < polyg->NumInteriors; ib++)
		  {
		      /* that ones are the interior rings */
		      ring = polyg->Interiors + ib;
		      for (iv = 0; iv < ring->Points; iv++)
			{
			    /* exports a POINT [x,y] - interior ring */
			    if (ring->DimensionModel == GAIA_XY_Z)
			      {
				  gaiaGetPointXYZ (ring->Coords, iv,
						   &x, &y, &z);
			      }
			    else if (ring->DimensionModel == GAIA_XY_M)
			      {
				  gaiaGetPointXYM (ring->Coords, iv,
						   &x, &y, &m);
			      }
			    else if (ring->DimensionModel == GAIA_XY_Z_M)
			      {
				  gaiaGetPointXYZM (ring->Coords, iv,
						    &x, &y, &z, &m);
			      }
			    else
			      {
				  gaiaGetPoint (ring->Coords, iv, &x, &y);
			      }
			    gaiaExport64 (buf + ix, x,
					  GAIA_LITTLE_ENDIAN, endian_arch);
			    ix += 8;
			    gaiaExport64 (buf + ix, y,
					  GAIA_LITTLE_ENDIAN, endian_arch);
			    ix += 8;
			}
		  }
		polyg = polyg->Next;
	    }
	  /* exporting the Z-range [min/max] */
	  gaiaExport64 (buf + ix, minZ, GAIA_LITTLE_ENDIAN, endian_arch);
	  ix += 8;
	  gaiaExport64 (buf + ix, maxZ, GAIA_LITTLE_ENDIAN, endian_arch);
	  ix += 8;
	  polyg = geom->FirstPolygon;
	  while (polyg)
	    {
		/* exports Z-values for each ring */
		ring = polyg->Exterior;	/* this one is the exterior ring */
		for (iv = 0; iv < ring->Points; iv++)
		  {
		      /* exports Z-values - exterior ring */
		      z = 0.0;
		      if (ring->DimensionModel == GAIA_XY_Z)
			{
			    gaiaGetPointXYZ (ring->Coords, iv, &x, &y, &z);
			}
		      else if (ring->DimensionModel == GAIA_XY_M)
			{
			    gaiaGetPointXYM (ring->Coords, iv, &x, &y, &m);
			}
		      else if (ring->DimensionModel == GAIA_XY_Z_M)
			{
			    gaiaGetPointXYZM (ring->Coords, iv, &x, &y, &z, &m);
			}
		      else
			{
			    gaiaGetPoint (ring->Coords, iv, &x, &y);
			}
		      gaiaExport64 (buf + ix, z,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		  }
		for (ib = 0; ib < polyg->NumInteriors; ib++)
		  {
		      /* that ones are the interior rings */
		      ring = polyg->Interiors + ib;
		      for (iv = 0; iv < ring->Points; iv++)
			{
			    /* exports Z-values - interior ring */
			    z = 0.0;
			    if (ring->DimensionModel == GAIA_XY_Z)
			      {
				  gaiaGetPointXYZ (ring->Coords, iv,
						   &x, &y, &z);
			      }
			    else if (ring->DimensionModel == GAIA_XY_M)
			      {
				  gaiaGetPointXYM (ring->Coords, iv,
						   &x, &y, &m);
			      }
			    else if (ring->DimensionModel == GAIA_XY_Z_M)
			      {
				  gaiaGetPointXYZM (ring->Coords, iv,
						    &x, &y, &z, &m);
			      }
			    else
			      {
				  gaiaGetPoint (ring->Coords, iv, &x, &y);
			      }
			    gaiaExport64 (buf + ix, z,
					  GAIA_LITTLE_ENDIAN, endian_arch);
			    ix += 8;
			}
		  }
		polyg = polyg->Next;
	    }
	  if (hasM)
	    {
		/* exporting the M-range [min/max] */
		gaiaExport64 (buf + ix, minM, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		gaiaExport64 (buf + ix, maxM, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		polyg = geom->FirstPolygon;
		while (polyg)
		  {
		      /* exports M-values for each ring */
		      ring = polyg->Exterior;	/* this one is the exterior ring */
		      for (iv = 0; iv < ring->Points; iv++)
			{
			    /* exports M-values - exterior ring */
			    m = 0.0;
			    if (ring->DimensionModel == GAIA_XY_Z)
			      {
				  gaiaGetPointXYZ (ring->Coords, iv,
						   &x, &y, &z);
			      }
			    else if (ring->DimensionModel == GAIA_XY_M)
			      {
				  gaiaGetPointXYM (ring->Coords, iv,
						   &x, &y, &m);
			      }
			    else if (ring->DimensionModel == GAIA_XY_Z_M)
			      {
				  gaiaGetPointXYZM (ring->Coords, iv,
						    &x, &y, &z, &m);
			      }
			    else
			      {
				  gaiaGetPoint (ring->Coords, iv, &x, &y);
			      }
			    gaiaExport64 (buf + ix, m,
					  GAIA_LITTLE_ENDIAN, endian_arch);
			    ix += 8;
			}
		      for (ib = 0; ib < polyg->NumInteriors; ib++)
			{
			    /* that ones are the interior rings */
			    ring = polyg->Interiors + ib;
			    for (iv = 0; iv < ring->Points; iv++)
			      {
				  /* exports M-values - interior ring */
				  m = 0.0;
				  if (ring->DimensionModel == GAIA_XY_Z)
				    {
					gaiaGetPointXYZ (ring->Coords,
							 iv, &x, &y, &z);
				    }
				  else if (ring->DimensionModel == GAIA_XY_M)
				    {
					gaiaGetPointXYM (ring->Coords,
							 iv, &x, &y, &m);
				    }
				  else if (ring->DimensionModel == GAIA_XY_Z_M)
				    {
					gaiaGetPointXYZM (ring->Coords,
							  iv, &x, &y, &z, &m);
				    }
				  else
				    {
					gaiaGetPoint (ring->Coords, iv, &x, &y);
				    }
				  gaiaExport64 (buf + ix, m,
						GAIA_LITTLE_ENDIAN,
						endian_arch);
				  ix += 8;
			      }
			}
		      polyg = polyg->Next;
		  }
	    }
	  *bufshp = buf;
	  return 1;
      }
    if (xshape == GAIA_SHP_POLYGONM)
      {
	  /* this one is expected to be a POLYGON or a MULTIPOLYGON M */
	  gaiaPolygonPtr polyg;
	  gaiaRingPtr ring;
	  int ib;
	  gaiaMRangeGeometry (geom, &minM, &maxM);
	  tot_ln = 0;
	  tot_v = 0;
	  polyg = geom->FirstPolygon;
	  while (polyg)
	    {
		/* computes # rings and total # points */
		gaiaSaneClockwise (polyg);	/* we must assure that exterior ring is clockwise, and interior rings are anti-clockwise */
		ring = polyg->Exterior;	/* this one is the exterior ring */
		tot_v += ring->Points;
		tot_ln++;
		for (ib = 0; ib < polyg->NumInteriors; ib++)
		  {
		      /* that ones are the interior rings */
		      ring = polyg->Interiors + ib;
		      tot_v += ring->Points;
		      tot_ln++;
		  }
		polyg = polyg->Next;
	    }
	  this_size = 30 + (2 * tot_ln) + (tot_v * 12);	/* size [in 16 bits words !!!] for this SHP entity */
	  *buflen = this_size * 2;
	  buf = malloc (this_size * 2);
	  gaiaExport32 (buf + 0, GAIA_SHP_POLYGONM, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = POLYGON M */
	  gaiaExport64 (buf + 4, geom->MinX, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports the MBR for this geometry */
	  gaiaExport64 (buf + 12, geom->MinY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 20, geom->MaxX, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 28, geom->MaxY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport32 (buf + 36, tot_ln, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports # rings in this polygon */
	  gaiaExport32 (buf + 40, tot_v, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports total # points */
	  tot_v = 0;		/* resets points counter */
	  ix = 44;		/* sets current buffer offset */
	  polyg = geom->FirstPolygon;
	  while (polyg)
	    {
		/* exports start point index for each line */
		ring = polyg->Exterior;	/* this one is the exterior ring */
		gaiaExport32 (buf + ix, tot_v, GAIA_LITTLE_ENDIAN, endian_arch);
		tot_v += ring->Points;
		ix += 4;
		for (ib = 0; ib < polyg->NumInteriors; ib++)
		  {
		      /* that ones are the interior rings */
		      ring = polyg->Interiors + ib;
		      gaiaExport32 (buf + ix, tot_v,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      tot_v += ring->Points;
		      ix += 4;
		  }
		polyg = polyg->Next;
	    }
	  polyg = geom->FirstPolygon;
	  while (polyg)
	    {
		/* exports points for each ring */
		ring = polyg->Exterior;	/* this one is the exterior ring */
		for (iv = 0; iv < ring->Points; iv++)
		  {
		      /* exports a POINT [x,y] - exterior ring */
		      if (ring->DimensionModel == GAIA_XY_Z)
			{
			    gaiaGetPointXYZ (ring->Coords, iv, &x, &y, &z);
			}
		      else if (ring->DimensionModel == GAIA_XY_M)
			{
			    gaiaGetPointXYM (ring->Coords, iv, &x, &y, &m);
			}
		      else if (ring->DimensionModel == GAIA_XY_Z_M)
			{
			    gaiaGetPointXYZM (ring->Coords, iv, &x, &y, &z, &m);
			}
		      else
			{
			    gaiaGetPoint (ring->Coords, iv, &x, &y);
			}
		      gaiaExport64 (buf + ix, x,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		      gaiaExport64 (buf + ix, y,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		  }
		for (ib = 0; ib < polyg->NumInteriors; ib++)
		  {
		      /* that ones are the interior rings */
		      ring = polyg->Interiors + ib;
		      for (iv = 0; iv < ring->Points; iv++)
			{
			    /* exports a POINT [x,y] - interior ring */
			    if (ring->DimensionModel == GAIA_XY_Z)
			      {
				  gaiaGetPointXYZ (ring->Coords, iv,
						   &x, &y, &z);
			      }
			    else if (ring->DimensionModel == GAIA_XY_M)
			      {
				  gaiaGetPointXYM (ring->Coords, iv,
						   &x, &y, &m);
			      }
			    else if (ring->DimensionModel == GAIA_XY_Z_M)
			      {
				  gaiaGetPointXYZM (ring->Coords, iv,
						    &x, &y, &z, &m);
			      }
			    else
			      {
				  gaiaGetPoint (ring->Coords, iv, &x, &y);
			      }
			    gaiaExport64 (buf + ix, x,
					  GAIA_LITTLE_ENDIAN, endian_arch);
			    ix += 8;
			    gaiaExport64 (buf + ix, y,
					  GAIA_LITTLE_ENDIAN, endian_arch);
			    ix += 8;
			}
		  }
		polyg = polyg->Next;
	    }
	  /* exporting the M-range [min/max] */
	  gaiaExport64 (buf + ix, minM, GAIA_LITTLE_ENDIAN, endian_arch);
	  ix += 8;
	  gaiaExport64 (buf + ix, maxM, GAIA_LITTLE_ENDIAN, endian_arch);
	  ix += 8;
	  polyg = geom->FirstPolygon;
	  while (polyg)
	    {
		/* exports M-values for each ring */
		ring = polyg->Exterior;	/* this one is the exterior ring */
		for (iv = 0; iv < ring->Points; iv++)
		  {
		      /* exports M-values - exterior ring */
		      m = 0.0;
		      if (ring->DimensionModel == GAIA_XY_Z)
			{
			    gaiaGetPointXYZ (ring->Coords, iv, &x, &y, &z);
			}
		      else if (ring->DimensionModel == GAIA_XY_M)
			{
			    gaiaGetPointXYM (ring->Coords, iv, &x, &y, &m);
			}
		      else if (ring->DimensionModel == GAIA_XY_Z_M)
			{
			    gaiaGetPointXYZM (ring->Coords, iv, &x, &y, &z, &m);
			}
		      else
			{
			    gaiaGetPoint (ring->Coords, iv, &x, &y);
			}
		      gaiaExport64 (buf + ix, m,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		  }
		for (ib = 0; ib < polyg->NumInteriors; ib++)
		  {
		      /* that ones are the interior rings */
		      ring = polyg->Interiors + ib;
		      for (iv = 0; iv < ring->Points; iv++)
			{
			    /* exports M-values - interior ring */
			    m = 0.0;
			    if (ring->DimensionModel == GAIA_XY_Z)
			      {
				  gaiaGetPointXYZ (ring->Coords, iv,
						   &x, &y, &z);
			      }
			    else if (ring->DimensionModel == GAIA_XY_M)
			      {
				  gaiaGetPointXYM (ring->Coords, iv,
						   &x, &y, &m);
			      }
			    else if (ring->DimensionModel == GAIA_XY_Z_M)
			      {
				  gaiaGetPointXYZM (ring->Coords, iv,
						    &x, &y, &z, &m);
			      }
			    else
			      {
				  gaiaGetPoint (ring->Coords, iv, &x, &y);
			      }
			    gaiaExport64 (buf + ix, m,
					  GAIA_LITTLE_ENDIAN, endian_arch);
			    ix += 8;
			}
		  }
		polyg = polyg->Next;
	    }
	  *bufshp = buf;
	  return 1;
      }
    if (xshape == GAIA_SHP_MULTIPOINT)
      {
	  /* this one is expected to be a MULTIPOINT */
	  gaiaPointPtr pt;
	  tot_pts = 0;
	  pt = geom->FirstPoint;
	  while (pt)
	    {
		/* computes # points */
		tot_pts++;
		pt = pt->Next;
	    }
	  this_size = 20 + (tot_pts * 8);	/* size [in 16 bits words !!!] for this SHP entity */
	  *buflen = this_size * 2;
	  buf = malloc (this_size * 2);
	  gaiaExport32 (buf + 0, GAIA_SHP_MULTIPOINT, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = MULTIPOINT */
	  gaiaExport64 (buf + 4, geom->MinX, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports the MBR for this geometry */
	  gaiaExport64 (buf + 12, geom->MinY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 20, geom->MaxX, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 28, geom->MaxY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport32 (buf + 36, tot_pts, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports total # points */
	  ix = 40;		/* sets current buffer offset */
	  pt = geom->FirstPoint;
	  while (pt)
	    {
		/* exports each point */
		gaiaExport64 (buf + ix, pt->X, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		gaiaExport64 (buf + ix, pt->Y, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		pt = pt->Next;
	    }
	  *bufshp = buf;
	  return 1;
      }
    if (xshape == GAIA_SHP_MULTIPOINTZ)
      {
	  /* this one is expected to be a MULTIPOINT Z */
	  gaiaPointPtr pt;
	  gaiaZRangeGeometry (geom, &minZ, &maxZ);
	  gaiaMRangeGeometry (geom, &minM, &maxM);
	  tot_pts = 0;
	  pt = geom->FirstPoint;
	  while (pt)
	    {
		/* computes # points */
		tot_pts++;
		pt = pt->Next;
	    }
	  hasM = 0;
	  if (eff_dims == GAIA_XY_M || eff_dims == GAIA_XY_Z_M)
	      hasM = 1;
	  if (hasM)
	      this_size = 36 + (tot_pts * 16);	/* size [in 16 bits words !!!] ZM */
	  else
	      this_size = 28 + (tot_pts * 12);	/* size [in 16 bits words !!!] Z-only */
	  *buflen = this_size * 2;
	  buf = malloc (this_size * 2);
	  gaiaExport32 (buf + 0, GAIA_SHP_MULTIPOINTZ, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = MULTIPOINT Z */
	  gaiaExport64 (buf + 4, geom->MinX, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports the MBR for this geometry */
	  gaiaExport64 (buf + 12, geom->MinY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 20, geom->MaxX, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 28, geom->MaxY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport32 (buf + 36, tot_pts, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports total # points */
	  ix = 40;		/* sets current buffer offset */
	  pt = geom->FirstPoint;
	  while (pt)
	    {
		/* exports each point */
		gaiaExport64 (buf + ix, pt->X, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		gaiaExport64 (buf + ix, pt->Y, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		pt = pt->Next;
	    }
	  /* exporting the Z-range [min/max] */
	  gaiaExport64 (buf + ix, minZ, GAIA_LITTLE_ENDIAN, endian_arch);
	  ix += 8;
	  gaiaExport64 (buf + ix, maxZ, GAIA_LITTLE_ENDIAN, endian_arch);
	  ix += 8;
	  pt = geom->FirstPoint;
	  while (pt)
	    {
		/* exports Z-values */
		gaiaExport64 (buf + ix, pt->Z, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		pt = pt->Next;
	    }
	  if (hasM)
	    {
		/* exporting the M-range [min/max] */
		gaiaExport64 (buf + ix, minM, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		gaiaExport64 (buf + ix, maxM, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		pt = geom->FirstPoint;
		while (pt)
		  {
		      /* exports M-values */
		      gaiaExport64 (buf + ix, pt->M,
				    GAIA_LITTLE_ENDIAN, endian_arch);
		      ix += 8;
		      pt = pt->Next;
		  }
	    }
	  *bufshp = buf;
	  return 1;
      }
    if (xshape == GAIA_SHP_MULTIPOINTM)
      {
	  /* this one is expected to be a MULTIPOINT M */
	  gaiaPointPtr pt;
	  gaiaMRangeGeometry (geom, &minM, &maxM);
	  tot_pts = 0;
	  pt = geom->FirstPoint;
	  while (pt)
	    {
		/* computes # points */
		tot_pts++;
		pt = pt->Next;
	    }
	  this_size = 28 + (tot_pts * 12);	/* size [in 16 bits words !!!] for this SHP entity */
	  *buflen = this_size * 2;
	  buf = malloc (this_size * 2);
	  gaiaExport32 (buf + 0, GAIA_SHP_MULTIPOINTM, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports geometry type = MULTIPOINT M */
	  gaiaExport64 (buf + 4, geom->MinX, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports the MBR for this geometry */
	  gaiaExport64 (buf + 12, geom->MinY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 20, geom->MaxX, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport64 (buf + 28, geom->MaxY, GAIA_LITTLE_ENDIAN, endian_arch);
	  gaiaExport32 (buf + 36, tot_pts, GAIA_LITTLE_ENDIAN, endian_arch);	/* exports total # points */
	  ix = 40;		/* sets current buffer offset */
	  pt = geom->FirstPoint;
	  while (pt)
	    {
		/* exports each point */
		gaiaExport64 (buf + ix, pt->X, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		gaiaExport64 (buf + ix, pt->Y, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		pt = pt->Next;
	    }
	  /* exporting the M-range [min/max] */
	  gaiaExport64 (buf + ix, minM, GAIA_LITTLE_ENDIAN, endian_arch);
	  ix += 8;
	  gaiaExport64 (buf + ix, maxM, GAIA_LITTLE_ENDIAN, endian_arch);
	  ix += 8;
	  pt = geom->FirstPoint;
	  while (pt)
	    {
		/* exports M-values */
		gaiaExport64 (buf + ix, pt->M, GAIA_LITTLE_ENDIAN, endian_arch);
		ix += 8;
		pt = pt->Next;
	    }
	  *bufshp = buf;
	  return 1;
      }

    fprintf (stderr,
	     "\tinput row #%d: unable to export a consistent Geometry\n",
	     rowno);
    return 0;
}

static int
do_repair_shapefile (const void *cache, const char *shp_path,
		     const char *out_path, int validate, int esri, int force,
		     int *repair_failed)
{
/* repairing some Shapefile */
    int current_row;
    gaiaShapefilePtr shp_in = NULL;
    gaiaShapefilePtr shp_out = NULL;
    int ret;
    gaiaDbfListPtr dbf_list = NULL;
    gaiaDbfFieldPtr in_fld;
    double minx;
    double miny;
    double maxx;
    double maxy;
    double hMinX;
    double hMinY;
    double hMaxX;
    double hMaxY;
    int mismatching;

    *repair_failed = 0;

/* opening the INPUT SHP */
    shp_in = allocShapefile ();
    openShpRead (shp_in, shp_path, &hMinX, &hMinY, &hMaxX, &hMaxY,
		 &mismatching);
    if (!(shp_in->Valid))
      {
	  char extra[512];
	  *extra = '\0';
	  if (shp_in->LastError)
	      sprintf (extra, "\n\t\tcause: %s\n", shp_in->LastError);
	  fprintf (stderr,
		   "\t\terror: cannot open shapefile '%s'%s", shp_path, extra);
	  freeShapefile (shp_in);
	  return 0;
      }

/* preparing the DBF fields list - OUTPUT */
    dbf_list = gaiaAllocDbfList ();
    in_fld = shp_in->Dbf->First;
    while (in_fld)
      {
	  /* adding a DBF field - OUTPUT */
	  gaiaAddDbfField (dbf_list, in_fld->Name, in_fld->Type, in_fld->Offset,
			   in_fld->Length, in_fld->Decimals);
	  in_fld = in_fld->Next;
      }

/* creating the OUTPUT SHP */
    shp_out = allocShapefile ();
    openShpWrite (shp_out, out_path, shp_in->Shape, dbf_list);
    if (!(shp_out->Valid))
      {
	  char extra[512];
	  *extra = '\0';
	  if (shp_out->LastError)
	      sprintf (extra, "\n\t\tcause: %s\n", shp_out->LastError);
	  fprintf (stderr,
		   "\t\terror: cannot open shapefile '%s'%s", out_path, extra);
	  freeShapefile (shp_in);
	  freeShapefile (shp_out);
	  gaiaFreeDbfList (dbf_list);
	  return 0;
      }

    current_row = 0;
    while (1)
      {
	  /* reading rows from shapefile */
	  int shplen;
	  ret =
	      readShpEntity (shp_in, current_row, &shplen, &minx, &miny, &maxx,
			     &maxy);
	  if (ret < 0)
	    {
		/* found a DBF deleted record */
		current_row++;
		continue;
	    }
	  if (!ret)
	    {
		if (!(shp_in->LastError))	/* normal SHP EOF */
		    break;
		fprintf (stderr, "\t\tERROR: %s\n", shp_in->LastError);
		goto stop;
	    }
	  if (validate || force)
	    {
		/* attempting to rearrange geometries */
		unsigned char *bufshp;
		int buflen;
		int nullshape;
		gaiaGeomCollPtr geom =
		    do_parse_geometry (shp_in->BufShp, shplen,
				       shp_in->EffectiveDims,
				       shp_in->EffectiveType, &nullshape);
		if (nullshape)
		    goto default_null;
		if (geom == NULL)
		  {
		      fprintf (stderr,
			       "\t\tinput row #%d: unexpected NULL geometry\n",
			       current_row);
		      *repair_failed = 1;
		      goto default_null;
		  }

		if (validate)
		  {
		      /* testing for invalid Geometries */
		      int is_invalid = 0;
		      if (esri)
			{
			    /* checking invalid geometries in ESRI mode */
			    gaiaGeomCollPtr detail;
			    detail = gaiaIsValidDetailEx_r (cache, geom, 1);
			    if (detail == NULL)
			      {
				  /* extra checks */
				  int extra = 0;
				  if (gaiaIsToxic_r (cache, geom))
				      extra = 1;
				  if (gaiaIsNotClosedGeomColl_r (cache, geom))
				      extra = 1;
				  if (extra)
				      is_invalid = 1;
			      }
			    else
			      {
				  is_invalid = 1;
				  gaiaFreeGeomColl (detail);
			      }
			}
		      else
			{
			    /* checking invalid geometries in ISO/OGC mode */
			    if (gaiaIsValid_r (cache, geom) != 1)
				is_invalid = 1;
			}

#ifdef ENABLE_RTTOPO		/* only if RTTOPO is enabled */
		      if (is_invalid)
			{
			    /* attempting to repair an invalid Geometry */
			    char *expected;
			    char *actual;
			    gaiaGeomCollPtr discarded;
			    gaiaGeomCollPtr result =
				gaiaMakeValid (cache, geom);
			    if (result == NULL)
			      {
				  fprintf (stderr,
					   "\t\tinput row #%d: unexpected MakeValid failure\n",
					   current_row);
				  gaiaFreeGeomColl (geom);
				  *repair_failed = 1;
				  goto default_null;
			      }
			    discarded = gaiaMakeValidDiscarded (cache, geom);
			    if (discarded != NULL)
			      {
				  fprintf (stderr,
					   "\t\tinput row #%d: MakeValid reports discarded elements\n",
					   current_row);
				  gaiaFreeGeomColl (result);
				  gaiaFreeGeomColl (discarded);
				  gaiaFreeGeomColl (geom);
				  *repair_failed = 1;
				  goto default_null;
			      }
			    if (!check_geometry_verbose
				(result, shp_in->Shape, &expected, &actual))
			      {
				  fprintf (stderr,
					   "\t\tinput row #%d: MakeValid returned an invalid SHAPE (expected %s, got %s)\n",
					   current_row, expected, actual);
				  free (expected);
				  free (actual);
				  gaiaFreeGeomColl (result);
				  gaiaFreeGeomColl (geom);
				  *repair_failed = 1;
				  goto default_null;
			      }
			    gaiaFreeGeomColl (geom);
			    geom = result;
			}
#endif /* end RTTOPO conditional */

		      if (!do_export_geometry
			  (geom, &bufshp, &buflen, shp_in->Shape,
			   current_row, shp_out->EffectiveDims))
			{
			    gaiaFreeGeomColl (geom);
			    goto stop;
			}
		      gaiaFreeGeomColl (geom);
		  }
		else
		  {
		      if (!do_export_geometry
			  (geom, &bufshp, &buflen, shp_in->Shape,
			   current_row, shp_out->EffectiveDims))
			{
			    gaiaFreeGeomColl (geom);
			    goto stop;
			}
		      gaiaFreeGeomColl (geom);
		  }
		goto ok_geom;

	      default_null:
		/* exporting a NULL shape */
		do_export_geometry
		    (NULL, &bufshp, &buflen, shp_in->Shape,
		     current_row, shp_out->EffectiveDims);

	      ok_geom:
		ret = writeShpEntity
		    (shp_out, bufshp, buflen, shp_in->BufDbf,
		     shp_in->DbfReclen);
		free (bufshp);
		if (!ret)
		    goto stop;
	    }
	  else
	    {
		/* passing geometries exactly as they were */
		if (!writeShpEntity
		    (shp_out, shp_in->BufShp, shplen, shp_in->BufDbf,
		     shp_in->DbfReclen))
		    goto stop;
	    }
	  current_row++;
      }
    gaiaFlushShpHeaders (shp_out);
    freeShapefile (shp_in);
    freeShapefile (shp_out);
    return 1;

  stop:
    freeShapefile (shp_in);
    freeShapefile (shp_out);
    fprintf (stderr,
	     "\t\tMalformed shapefile, impossible to repair: quitting\n");
    return 0;
}

static int
do_test_shapefile (const void *cache, const char *shp_path, int validate,
		   int esri, int *invalid)
{
/* testing a Shapefile for validity */
    int n_invalid;

    fprintf (stderr, "\nVerifying %s.shp\n", shp_path);
    *invalid = 0;
    if (!do_read_shp (cache, shp_path, validate, esri, &n_invalid))
	return 0;
    if (n_invalid)
      {
	  fprintf (stderr, "\tfound %d invalidit%s: cleaning required.\n",
		   n_invalid, (n_invalid > 1) ? "ies" : "y");
	  *invalid = 1;
      }
    else
	fprintf (stderr, "\tfound to be already valid.\n");
    return 1;
}

static int
check_extension (const char *file_name)
{
/* checks the file extension */
    const char *mark = NULL;
    int len = strlen (file_name);
    const char *p = file_name + len - 1;

    while (p >= file_name)
      {
	  if (*p == '.')
	      mark = p;
	  p--;
      }
    if (mark == NULL)
	return SUFFIX_DISCARD;
    if (strcasecmp (mark, ".shp") == 0)
	return SUFFIX_SHP;
    if (strcasecmp (mark, ".shx") == 0)
	return SUFFIX_SHX;
    if (strcasecmp (mark, ".dbf") == 0)
	return SUFFIX_DBF;
    return SUFFIX_DISCARD;
}

static int
do_scan_dir (const void *cache, const char *in_dir, const char *out_dir,
	     int *n_shp, int *r_shp, int *x_shp, int validate, int esri,
	     int force)
{
/* scanning a directory and searching for Shapefiles to be checked */
    struct shp_entry *p_shp;
    struct shp_list *list = alloc_shp_list ();

#if defined(_WIN32) && !defined(__MINGW32__)
/* Visual Studio .NET */
    struct _finddata_t c_file;
    intptr_t hFile;
    char *path;
    char *name;
    int len;
    int ret;
    if (_chdir (in_dir) < 0)
	goto error;
    if ((hFile = _findfirst ("*.shp", &c_file)) == -1L)
	;
    else
      {
	  while (1)
	    {
		if ((c_file.attrib & _A_RDONLY) == _A_RDONLY
		    || (c_file.attrib & _A_NORMAL) == _A_NORMAL)
		  {
		      name = sqlite3_mprintf ("%s", c_file.name);
		      len = strlen (name);
		      name[len - 4] = '\0';
		      path = sqlite3_mprintf ("%s/%s", in_dir, name);
		      do_add_shapefile (list, path, name, SUFFIX_SHP);
		  }
		if (_findnext (hFile, &c_file) != 0)
		    break;
	    }
	  _findclose (hFile);
	  if ((hFile = _findfirst ("*.shx", &c_file)) == -1L)
	      ;
	  else
	    {
		while (1)
		  {
		      if ((c_file.attrib & _A_RDONLY) == _A_RDONLY
			  || (c_file.attrib & _A_NORMAL) == _A_NORMAL)
			{
			    name = sqlite3_mprintf ("%s", c_file.name);
			    len = strlen (name);
			    name[len - 4] = '\0';
			    path = sqlite3_mprintf ("%s/%s", in_dir, name);
			    do_add_shapefile (list, path, name, SUFFIX_SHX);
			}
		      if (_findnext (hFile, &c_file) != 0)
			  break;
		  }
		_findclose (hFile);
		if ((hFile = _findfirst ("*.dbf", &c_file)) == -1L)
		    ;
		else
		  {
		      while (1)
			{
			    if ((c_file.attrib & _A_RDONLY) == _A_RDONLY
				|| (c_file.attrib & _A_NORMAL) == _A_NORMAL)
			      {
				  name = sqlite3_mprintf ("%s", c_file.name);
				  len = strlen (name);
				  name[len - 4] = '\0';
				  path =
				      sqlite3_mprintf ("%s/%s", in_dir, name);
				  do_add_shapefile (list, path, name,
						    SUFFIX_DBF);
			      }
			    if (_findnext (hFile, &c_file) != 0)
				break;
			}
		      _findclose (hFile);
		  }
	    }
      }
#else
/* not Visual Studio .NET */
    char *path;
    char *name;
    struct dirent *entry;
    int len;
    int suffix;
    DIR *dir = opendir (in_dir);
    if (!dir)
	goto error;
    while (1)
      {
	  /* extracting the SHP candidates */
	  entry = readdir (dir);
	  if (!entry)
	      break;
	  suffix = check_extension (entry->d_name);
	  if (suffix == SUFFIX_DISCARD)
	      continue;
	  path = sqlite3_mprintf ("%s/%s", in_dir, entry->d_name);
	  len = strlen (path);
	  path[len - 4] = '\0';
	  name = sqlite3_mprintf ("%s", entry->d_name);
	  len = strlen (name);
	  name[len - 4] = '\0';
	  do_add_shapefile (list, path, name, suffix);
      }
    closedir (dir);
#endif

    p_shp = list->first;
    while (p_shp != NULL)
      {
	  if (test_valid_shp (p_shp))
	    {
		int invalid;
		if (!do_test_shapefile
		    (cache, p_shp->base_name, validate, esri, &invalid))
		    goto error;
		*n_shp += 1;
		if (invalid)
		    *x_shp += 1;
		if ((invalid || force) && out_dir != NULL)
		  {
		      /* attempting to repair */
		      int repair_failed;
		      int ret;
		      char *out_path = sqlite3_mprintf ("%s/%s", out_dir,
							p_shp->file_name);
		      fprintf (stderr, "\tAttempting to repair: %s.shp\n",
			       out_path);
		      ret =
			  do_repair_shapefile (cache, p_shp->base_name,
					       out_path, validate, esri, force,
					       &repair_failed);
		      sqlite3_free (out_path);
		      if (!ret)
			  goto error;
		      if (repair_failed)
			{
			    do_clen_files (out_dir, p_shp->base_name);
			    fprintf (stderr,
				     "\tFAILURE: automatic repair is impossible, manual repair required.\n");
			}
		      else
			{
			    *r_shp += 1;
			    fprintf (stderr, "\tOK, successfully repaired.\n");
			}
		  }
	    }
	  p_shp = p_shp->next;
      }

    free_shp_list (list);
    return 1;

  error:
    free_shp_list (list);
    fprintf (stderr, "Unable to access \"%s\"\n", in_dir);
    return 0;
}

static void
do_version ()
{
/* printing version infos */
	fprintf( stderr, "\nVersion infos\n");
	fprintf( stderr, "===========================================\n");
    fprintf (stderr, "shp_sanitize : %s\n", SPATIALITE_VERSION);
	fprintf (stderr, "target CPU ..: %s\n", spatialite_target_cpu ());
    fprintf (stderr, "libspatialite: %s\n", spatialite_version ());
    fprintf (stderr, "libsqlite3 ..: %s\n", sqlite3_libversion ());
    fprintf (stderr, "\n");
}

static void
do_help ()
{
/* printing the argument list */
    fprintf (stderr, "\n\nusage: shp_sanitize ARGLIST\n");
    fprintf (stderr,
	     "=================================================================\n");
    fprintf (stderr,
	     "-h or --help                      print this help message\n");
    fprintf (stderr, "-v or --version                   print version infos\n");
    fprintf (stderr,
	     "-idir or --in-dir   dir-path      directory expected to contain\n"
	     "                                  all SHP to be checked\n");
    fprintf (stderr,
	     "-odir or --out-dir  dir-path      <optional> directory where to\n"
	     "                                  store all repaired SHPs\n\n");
    fprintf (stderr,
	     "======================= optional args ===========================\n"
	     "-geom or --invalid-geoms          checks for invalid Geometries\n"
	     "-esri or --esri-flag              tolerates ESRI-like inner holes\n"
	     "-force or --force-repair          unconditionally repair\n\n");
}

int
main (int argc, char *argv[])
{
/* the MAIN function simply perform arguments checking */
    int i;
    int error = 0;
    int next_arg = ARG_NONE;
    char *in_dir = NULL;
    char *out_dir = NULL;
    int validate = 0;
    int esri = 0;
    int force = 0;
    int n_shp = 0;
    int r_shp = 0;
    int x_shp = 0;
    const void *cache;

    for (i = 1; i < argc; i++)
      {
	  /* parsing the invocation arguments */
	  if (next_arg != ARG_NONE)
	    {
		switch (next_arg)
		  {
		  case ARG_IN_DIR:
		      in_dir = argv[i];
		      break;
		  case ARG_OUT_DIR:
		      out_dir = argv[i];
		      break;
		  };
		next_arg = ARG_NONE;
		continue;
	    }
	  if (strcasecmp (argv[i], "--help") == 0
	      || strcmp (argv[i], "-h") == 0)
	    {
		do_help ();
		return -1;
	    }
	  if (strcasecmp (argv[i], "--version") == 0
	      || strcmp (argv[i], "-v") == 0)
	    {
		do_version ();
		return -1;
	    }
	  if (strcasecmp (argv[i], "-idir") == 0
	      || strcasecmp (argv[i], "--in-dir") == 0)
	    {
		next_arg = ARG_IN_DIR;
		continue;
	    }
	  if (strcasecmp (argv[i], "-odir") == 0
	      || strcasecmp (argv[i], "--out-dir") == 0)
	    {
		next_arg = ARG_OUT_DIR;
		continue;
	    }
	  if (strcasecmp (argv[i], "-geom") == 0
	      || strcasecmp (argv[i], "--invalid-geoms") == 0)
	    {
		validate = 1;
		continue;
	    }
	  if (strcasecmp (argv[i], "-esri") == 0
	      || strcasecmp (argv[i], "--esri-flag") == 0)
	    {
		esri = 1;
		continue;
	    }
	  if (strcasecmp (argv[i], "-force") == 0
	      || strcasecmp (argv[i], "--force-repair") == 0)
	    {
		force = 1;
		continue;
	    }
	  fprintf (stderr, "unknown argument: %s\n", argv[i]);
	  error = 1;
      }
    if (error)
      {
	  do_help ();
	  return -1;
      }
/* checking the arguments */
    if (!in_dir)
      {
	  fprintf (stderr, "did you forget setting the --in-dir argument ?\n");
	  error = 1;
      }
    if (error)
      {
	  do_help ();
	  return -1;
      }

#ifndef ENABLE_RTTOPO		/* only if RTTOPO is disabled */
    if (validate)
      {
	  validate = 0;
	  fprintf (stderr,
		   "the --validate-geoms option will be ignored because\n"
		   "this copy of \"shp_sanitize\" was built by disabling RTTOPO support.\n\n");
      }
#endif /* end RTTOPO conditional */

    if (out_dir != NULL)
      {
#ifdef _WIN32
	  if (_mkdir (out_dir) != 0)
#else
	  if (mkdir (out_dir, 0777) != 0)
#endif
	    {
		fprintf (stderr,
			 "ERROR: unable to create the output directory\n%s\n%s\n\n",
			 out_dir, strerror (errno));
		return -1;
	    }
      }

    cache = spatialite_alloc_connection ();
    spatialite_set_silent_mode (cache);
    fprintf (stderr, "\nInput dir: %s\n", in_dir);
    if (out_dir != NULL)
      {
	  fprintf (stderr, "Output dir: %s\n", out_dir);
	  if (force)
	      fprintf (stderr, "Unconditionally repairing all Shapefiles\n");
      }
    else
	fprintf (stderr, "Only a diagnostic report will be reported\n");
    if (validate)
      {
	  fprintf (stderr, "Checking for invalid geometries (%s mode)\n",
		   esri ? "ESRI" : "ISO/OGC");
      }

    if (!do_scan_dir
	(cache, in_dir, out_dir, &n_shp, &r_shp, &x_shp, validate, esri, force))
      {
	  fprintf (stderr,
		   "\n... quitting ... some unexpected error occurred\n");
	  spatialite_cleanup_ex (cache);
	  return -1;
      }

    fprintf (stderr, "\n===========================================\n");
    fprintf (stderr, "%d Shapefil%s ha%s been inspected.\n", n_shp,
	     (n_shp > 1) ? "es" : "e", (n_shp > 1) ? "ve" : "s");
    fprintf (stderr, "%d malformed Shapefil%s ha%s been identified.\n", x_shp,
	     (x_shp > 1) ? "es" : "e", (x_shp > 1) ? "ve" : "s");
    fprintf (stderr, "%d Shapefil%s ha%s been repaired.\n\n", r_shp,
	     (r_shp > 1) ? "es" : "e", (r_shp > 1) ? "ve" : "s");

    spatialite_cleanup_ex (cache);
    return 0;
}
