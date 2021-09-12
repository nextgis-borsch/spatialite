/*
/ spatialite_dem
/
/ a tool for setting Z coordinates from XYZ files
/
/ version 1.0, 2017-09-14
/
/ Author: Mark Johnson, Berlin Germany mj10777@googlemail.com
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
/    along with this program.  If not, see <http://www.gnu.source_geom/licenses/>.
/
*/
// -- -- ---------------------------------- --
// astyle spatialite_dem.c
// valgrind --track-origins=yes --leak-check=full --show-leak-kinds=all --log-file=valgrind.spatialite_dem.utm_fetchz.txt ./spatialite_dem  -fetchz_xy  24747.115253 20725.147344
// -- -- ---------------------------------- --
#if defined(_WIN32) && !defined(__MINGW32__)
/* MSVC strictly requires this include [off_t] */
#include <sys/types.h>
#endif

#if defined(_WIN32) && !defined(__MINGW32__)
#include <Winsock2.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32) && !defined(__MINGW32__)
#include "config-msvc.h"
#else
#include "config.h"
#endif

#if defined(_WIN32)
#include <io.h>
#include <direct.h>
#else
#include "config.h"
#include <dirent.h>
#endif

#ifdef SPATIALITE_AMALGAMATION
#include <spatialite/sqlite3.h>
#else
#include <sqlite3.h>
#endif

#include <spatialite/gaiaaux.h>
#include <spatialite/gaiageo.h>
#include <spatialite.h>

#ifdef _WIN32
#define strcasecmp	_stricmp
#endif /* not WIN32 */
// -- -- ---------------------------------- --
#define ARG_NONE		0
#define ARG_DB_PATH		1
#define ARG_TABLE		2
#define ARG_COL		3
#define ARG_DEM_PATH		4
#define ARG_TABLE_DEM		5
#define ARG_COL_DEM		6
#define ARG_RESOLUTION_DEM		7
#define ARG_COPY_M		8
#define ARG_COMMAND_TYPE		9
#define ARG_FETCHZ_X		10
#define ARG_FETCHZ_Y		11
#define ARG_FETCHZ_XY		12
#define ARG_DEFAULT_SRID		13
// -- -- ---------------------------------- --
#define CMD_DEM_SNIFF		100
#define CMD_DEM_FETCHZ		101
#define CMD_DEM_CREATE		102
#define CMD_DEM_IMPORT_XYZ		103
#define CMD_DEM_UPDATEZ		104
// -- -- ---------------------------------- --
#define CONF_TYPE_DEM		1
#define CONF_TYPE_SOURCE	2
// -- -- ---------------------------------- --
// Definitions used for dem-conf
// -- -- ---------------------------------- --
#define MAXBUF 1024
#define DELIM "="
#ifdef _WIN32
#define LENGTHNL 2
#else
#define LENGTHNL 1
#endif /* not WIN32 */

#if defined(_WIN32) && !defined(__MINGW32__)

#define strtok_r strtok_s
#define sleep Sleep

// https://git.postgresql.org/gitweb/?p=postgresql.git;a=blob;f=src/port/gettimeofday.c;h=75a91993b74414c0a1c13a2a09ce739cb8aa8a08;hb=HEAD

/* FILETIME of Jan 1 1970 00:00:00. */
static const unsigned __int64 epoch = 116444736000000000;

/*
* timezone information is stored outside the kernel so tzp isn't used anymore.
*
* Note: this function is not for Win32 high precision timing purpose. See
* elapsed_time().
*/

int gettimeofday(struct timeval * tp, struct timezone * tzp)
{
    FILETIME    file_time;
    SYSTEMTIME  system_time;
    ULARGE_INTEGER ularge;

    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);
    ularge.LowPart = file_time.dwLowDateTime;
    ularge.HighPart = file_time.dwHighDateTime;

    tp->tv_sec = (long) ((ularge.QuadPart - epoch) / 10000000L);
    tp->tv_usec = (long) (system_time.wMilliseconds * 1000);

    return 0;
}

#endif
// -- -- ---------------------------------- --
// Output of time elapse
// min      = (int)(time_diff.tv_sec/60);
// secs    = (int)(time_diff.tv_sec-(min*60));
// msecs = (int)(time_diff.tv_usec/1000);
// printf("%d mins %02d.%03d secs",min,sec,msecs);
// -- -- ---------------------------------- --
int timeval_subtract (struct timeval *time_diff, struct timeval *time_end, struct timeval *time_start, char **time_message)
{
 int diff=0;
 int days=0;
 int hours=0;
 int mins=0;
 int secs=0;
 int msecs=0;
// Perform the carry for the later subtraction by updating time_start.
 if (time_end->tv_usec < time_start->tv_usec)
 {
  int nsec = (time_start->tv_usec - time_end->tv_usec) / 1000000 + 1;
  time_start->tv_usec -= 1000000 * nsec;
  time_start->tv_sec += nsec;
 }
 if (time_end->tv_usec - time_start->tv_usec > 1000000)
 {
  int nsec = (time_end->tv_usec - time_start->tv_usec) / 1000000;
  time_start->tv_usec += 1000000 * nsec;
  time_start->tv_sec -= nsec;
 }
// Compute the time remaining to wait. tv_usec is certainly positive.
 time_diff->tv_sec = time_end->tv_sec - time_start->tv_sec;
 time_diff->tv_usec = time_end->tv_usec - time_start->tv_usec;
// -- -- ---------------------------------- --
 diff = (int)time_diff->tv_sec;
 if (diff > 86400 )
 {// sec per day
  days = diff / 86400;
  diff = diff-(days*86400);
 }
 if (diff > 3660 )
 {// sec per hour
  hours = diff / 3660;
  diff = diff -(hours*3660);
 }
 if (diff > 60 )
 {
  mins = diff / 60;
 }
 secs = diff - (mins * 60);
 msecs = (int)(time_diff->tv_usec/1000);
// -- -- ---------------------------------- --
 if (*time_message)
 {
  sqlite3_free(*time_message);
  *time_message = NULL;
 }
 if ( days > 0)
 {
  *time_message = sqlite3_mprintf(">> time needed: %2 days %02d hours %02d mins %02d secs %02d milli-secs", days, hours, mins, secs,msecs);
 } else if ( hours > 0)
 {
  *time_message = sqlite3_mprintf(">> time needed: %02d hours %02d mins %02d secs %02d milli-secs", hours, mins, secs,msecs);
 } else if ( mins > 0)
 {
  *time_message = sqlite3_mprintf(">> time needed: %02d mins %02d secs %02d milli-secs", mins, secs,msecs);
 }
 else if (secs > 0 )
 {
  *time_message = sqlite3_mprintf(">> time needed: %02d secs %02d milli-secs", secs,msecs);
 }
 else
 {
  *time_message = sqlite3_mprintf(">> time needed: %02d milli-secs",msecs);
 }
// -- -- ---------------------------------- --
// Return 1 if time_diff is negative.
 return time_end->tv_sec < time_start->tv_sec;
// -- -- ---------------------------------- --
}
// -- -- ---------------------------------- --
// dem-conf structure
// -- -- ---------------------------------- --
struct config_dem
{
 char dem_path[MAXBUF];
 char dem_table[MAXBUF];
 char dem_geometry[MAXBUF];
 double dem_extent_minx;
 double dem_extent_miny;
 double dem_extent_maxx;
 double dem_extent_maxy;
 double dem_resolution;
 int dem_srid;
 unsigned int dem_rows_count;
 int default_srid;
// Not to be used for conf_file [internal use only]
 char *schema;
 double fetchz_x;
 double fetchz_y;
 double dem_z;
 double dem_m;
 int has_z;
 int has_m;
 int has_spatial_index;
 int is_spatial_table;
 int config_type; // dem=0 ; source=1;
 unsigned int id_rowid; // For debugging
 unsigned int count_points; // For debugging
 unsigned int count_points_nr; // For debugging
};
// -- -- ---------------------------------- --
// Reading dem-conf
// Environment var 'SPATIALITE_DEM'
// - with path to dem-conf
// ->  must be created by
//  (or using same syntax as)  write_demconfig
// -- -- ---------------------------------- --
// if not found, the tool will look for a
// - 'spatialite_dem.conf' file in the active directory
// if not found, default values will be used
// -- -- ---------------------------------- --
// - any line not starting
// -> with a '#' [comment]
// -> not containing a '=' will be ignored
// - any other line will look for specific
//   keywords before the '='
// -> unknown keywords will be ignored
// -- -- ---------------------------------- --
struct config_dem get_demconfig(char *conf_filename, int verbose)
{
 struct config_dem config_struct;
// -- -- ---------------------------------- --
// Setting default values
// -- -- ---------------------------------- --
 strcpy(config_struct.dem_path,"");
 strcpy(config_struct.dem_table,"");
 strcpy(config_struct.dem_geometry,"");
 config_struct.dem_extent_minx=0.0;
 config_struct.dem_extent_miny=0.0;
 config_struct.dem_extent_maxx=0.0;
 config_struct.dem_extent_maxy=0.0;
 config_struct.dem_resolution=0.0;
 config_struct.dem_srid=-2; // invalid
 config_struct.default_srid=-2; // invalid
 config_struct.dem_rows_count=0;
// -- -- ---------------------------------- --
// Not to be used for conf_file [internal use only]
// -- -- ---------------------------------- --
 config_struct.fetchz_x=0.0;
 config_struct.fetchz_y=0.0;
 config_struct.dem_z=0.0;
 config_struct.dem_m=0.0;
 config_struct.has_z=0;
 config_struct.has_m=0;
 config_struct.has_spatial_index=0;
 config_struct.is_spatial_table=0;
 config_struct.schema=NULL;
 config_struct.config_type=-1;
 config_struct.id_rowid=0; // For debugging
 config_struct.count_points=0; // For debugging
 config_struct.count_points_nr=0; // For debugging
// -- -- ---------------------------------- --
 if ((conf_filename) && (strlen(conf_filename) > 0) )
 {
  FILE *conf_file = fopen(conf_filename, "r");
  if (conf_file != NULL)
  {
   char line[MAXBUF];
   while(fgets(line, sizeof(line), conf_file) != NULL)
   {
    char *conf_parm=NULL;
    char *conf_value=NULL;
    conf_parm=(char *)line;
    conf_value = strstr((char *)line,DELIM);
    // Skip any comments (#) lines that may also contain a '='
    if ( (conf_parm[0] != '#') && (conf_value) )
    {
     conf_parm[(int)(conf_value-conf_parm)]=0;
     conf_value = conf_value + strlen(DELIM);
     conf_value[strcspn(conf_value, "\r\n")] = 0;
     // printf("parm[%s] value[%s]\n",conf_parm,conf_value);
     if (strcmp(conf_parm,"dem_path") == 0)
     {
      strcpy(config_struct.dem_path,conf_value);
      // printf("%s[%s]\n",conf_parm,config_struct.dem_path);
     } else if (strcmp(conf_parm,"dem_table") == 0)
     {
      strcpy(config_struct.dem_table,conf_value);
      // printf("%s[%s]\n",conf_parm,config_struct.dem_table);
     } else if (strcmp(conf_parm,"dem_geometry") == 0)
     {
      strcpy(config_struct.dem_geometry,conf_value);
      // printf("%s[%s]\n",conf_parm,config_struct.dem_geometry);
     } else if (strcmp(conf_parm,"dem_extent_minx") == 0)
     {
      config_struct.dem_extent_minx=atof(conf_value);
      //printf("%s[%2.7f]\n",conf_parm,config_struct.dem_extent_minx);
     } else if (strcmp(conf_parm,"dem_extent_miny") == 0)
     {
      config_struct.dem_extent_miny=atof(conf_value);
      //printf("%s[%2.7f]\n",conf_parm,config_struct.dem_extent_miny);
     } else if (strcmp(conf_parm,"dem_extent_maxx") == 0)
     {
      config_struct.dem_extent_maxx=atof(conf_value);
      //printf("%s[%2.7f]\n",conf_parm,config_struct.dem_extent_maxx);
     } else if (strcmp(conf_parm,"dem_extent_maxy") == 0)
     {
      config_struct.dem_extent_maxy=atof(conf_value);
      //printf("%s[%2.7f]\n",conf_parm,config_struct.dem_extent_maxy);
     } else if (strcmp(conf_parm,"dem_resolution") == 0)
     {
      config_struct.dem_resolution=atof(conf_value);
      //printf("%s[%2.7f]\n",conf_parm,config_struct.dem_resolution);
     } else if (strcmp(conf_parm,"dem_srid") == 0)
     {
      config_struct.dem_rows_count=atol(conf_value);
      // printf("%s[%d]\n",conf_parm,config_struct.dem_rows_count);
     } else if (strcmp(conf_parm,"dem_srid") == 0)
     {
      config_struct.dem_srid=atoi(conf_value);
      // printf("%s[%d]\n",conf_parm,config_struct.dem_srid);
     } else if (strcmp(conf_parm,"default_srid") == 0)
     {
      config_struct.default_srid=atoi(conf_value);
      // printf("%s[%d]\n",conf_parm,config_struct.default_srid);
     }
    }
   } // End while
   fclose(conf_file);
  } // End if file
  else
  {
   if (strcmp(conf_filename,"spatialite_dem.conf") != 0)
   {
    if (verbose)
    {
     fprintf(stderr, "-E-> spatialite_dem: not found: conf_filename[%s]\n",conf_filename);
    }
   }
  }
 }
 return config_struct;
}
// -- -- ---------------------------------- --
// writing dem-conf
// - any line not starting
// -> with a '#' [comment]
// -> not containing a '=' will be ignored
// - any other line will look for specific
//   keywords before the '='
// -> unknown keywords will be ignored
// -- -- ---------------------------------- --
// Saving dem-conf '-save_conf'
//  - will save to files set in 'SPATIALITE_DEM'
// - if empty to the default 'spatialite_dem.conf'
// -- -- ---------------------------------- --
int write_demconfig(char *conf_filename, struct config_dem config_struct)
{
 int rc=0;
 FILE *conf_file = fopen(conf_filename, "w");
 if (conf_file != NULL)
 {
  fprintf(conf_file, "# -- -- ---------------------------------- --\n");
  fprintf(conf_file, "# For use with spatialite_dem\n");
  fprintf(conf_file, "# -- -- ---------------------------------- --\n");
  fprintf(conf_file, "# export SPATIALITE_DEM=%s\n",conf_filename);
  fprintf(conf_file, "# -- -- ---------------------------------- --\n");
  fprintf(conf_file, "# Full path to Spatialite-Database containing a Dem-POINTZ (or POINTZM) Geometry\n");
  fprintf(conf_file, "dem_path=%s\n", config_struct.dem_path);
  fprintf(conf_file, "# Table-Name containing a Dem-POINTZ (or POINTZM) Geometry\n");
  fprintf(conf_file, "dem_table=%s\n", config_struct.dem_table);
  fprintf(conf_file, "# Geometry-Column containing a Dem-POINTZ (or POINTZM) Geometry\n");
  fprintf(conf_file, "dem_geometry=%s\n", config_struct.dem_geometry);
  fprintf(conf_file, "# Srid of the Dem-Geometry\n");
  fprintf(conf_file, "dem_srid=%d\n", config_struct.dem_srid);
  fprintf(conf_file, "# -- -- ---------------------------------- --\n");
  fprintf(conf_file, "# Area around given point to Query Dem-Geometry in units of Dem-Srid\n");
  fprintf(conf_file, "# -> Rule: a Dem with 1m resolution: min=0.50\n");
  fprintf(conf_file, "dem_resolution=%2.7f\n", config_struct.dem_resolution);
  fprintf(conf_file, "# -- -- ---------------------------------- --\n");
  fprintf(conf_file, "# Default Srid to use for queries against Dem-Geometry [-fetchz_xy, -updatez]\n");
  fprintf(conf_file, "default_srid=%d\n", config_struct.default_srid);
  fprintf(conf_file, "# -- -- ---------------------------------- --\n");
  fprintf(conf_file, "# Count of rows in Dem-Geometry\n");
  fprintf(conf_file, "dem_rows_count=%u\n", config_struct.dem_rows_count);
  fprintf(conf_file, "# Min X of Dem-Geometry\n");
  fprintf(conf_file, "dem_extent_minx=%2.7f\n", config_struct.dem_extent_minx);
  fprintf(conf_file, "# Max X of Dem-Geometry\n");
  fprintf(conf_file, "dem_extent_maxx=%2.7f\n", config_struct.dem_extent_maxx);
  fprintf(conf_file, "# Min Y of Dem-Geometry\n");
  fprintf(conf_file, "dem_extent_miny=%2.7f\n", config_struct.dem_extent_miny);
  fprintf(conf_file, "# Max Y of Dem-Geometry\n");
  fprintf(conf_file, "dem_extent_maxy=%2.7f\n", config_struct.dem_extent_maxy);
  fprintf(conf_file, "# Width of Dem-Area in Srid-Units\n");
  fprintf(conf_file, "dem_extent_width=%2.7f\n", (config_struct.dem_extent_maxx-config_struct.dem_extent_minx));
  fprintf(conf_file, "# Height of Dem-Area in Srid-Units\n");
  fprintf(conf_file, "dem_extent_height=%2.7f\n", (config_struct.dem_extent_maxy-config_struct.dem_extent_miny));
  fprintf(conf_file, "# -- -- ---------------------------------- --\n");
  fclose(conf_file);
  rc=1;
 }
 return rc;
}
// -- -- ---------------------------------- --
// From a given point, build area around it by 'resolution_dem'
// - utm 0.999 meters, Solder Berlin 1.0375644 meters
// (resolution_dem/2) could also be done
// - but to insure that at least 1 point is returned, left as is
// The nearest point will always be retrieved, or none at all.
// -- -- ---------------------------------- --
static int
insert_dem_points(sqlite3 *db_handle, struct config_dem *dem_config, double *xx_source, double *yy_source, double *zz_source, int verbose)
{
 /* checking for 3D geometries - version 4 */
 int ret=0;
 int ret_insert=SQLITE_OK;
 int i=0;
 char *sql_statement = NULL;
 sqlite3_stmt *stmt = NULL;
 char *sql_err = NULL;
 if (zz_source)
 {
  sql_statement = sqlite3_mprintf("INSERT INTO \"%s\" (point_x,point_y, point_z,\"%s\") "
                                  "VALUES(?,?,?,MakePointZ(?,?,?,%d)) ",dem_config->dem_table,dem_config->dem_geometry,dem_config->dem_srid);
  ret = sqlite3_prepare_v2( db_handle, sql_statement, -1, &stmt, NULL );
  if ( ret == SQLITE_OK )
  {
   sqlite3_free(sql_statement);
   if (sqlite3_exec(db_handle, "BEGIN", NULL, NULL, &sql_err) == SQLITE_OK)
   {
    for (i=0; i<(int)(dem_config->count_points); i++)
    {
     if (ret_insert != SQLITE_ABORT )
     {
      // Note: sqlite3_bind_* index is 1-based, os apposed to sqlite3_column_* that is 0-based.
      sqlite3_bind_double(stmt, 1, xx_source[i]);
      sqlite3_bind_double(stmt, 2, yy_source[i]);
      sqlite3_bind_double(stmt, 3, zz_source[i]);
      sqlite3_bind_double(stmt, 4, xx_source[i]);
      sqlite3_bind_double(stmt, 5, yy_source[i]);
      sqlite3_bind_double(stmt, 6, zz_source[i]);
      dem_config->count_points_nr=i;
      ret_insert = sqlite3_step( stmt );
      if ( ret_insert == SQLITE_DONE || ret_insert == SQLITE_ROW )
      {
       ret_insert=SQLITE_OK;
      }
      else
      {
       ret_insert=SQLITE_ABORT;
      }
     }
     sqlite3_reset(stmt);
     sqlite3_clear_bindings(stmt);
     xx_source[i]=0.0;
     yy_source[i]=0.0;
     zz_source[i]=0.0;
    }
    ret=0;
    if (ret_insert == SQLITE_ABORT )
    {
     if (sqlite3_exec(db_handle, "ROLLBACK", NULL, NULL, &sql_err) == SQLITE_OK)
     {
      ret=0;
     }
    }
    else
    {
     if (sqlite3_exec(db_handle, "COMMIT", NULL, NULL, &sql_err) == SQLITE_OK)
     {
      ret = 1;
      dem_config->dem_rows_count+=dem_config->count_points;
      dem_config->count_points=0;
      dem_config->count_points_nr=0;
     }
    }
   }
   sqlite3_finalize( stmt );
   if (sql_err)
   {
    sqlite3_free(sql_err);
   }
  }
  else
  {
   if (verbose)
   {
    fprintf(stderr, "-W-> insert_dem_points: rc=%d sql[%s]\n",ret,sql_statement);
   }
   sqlite3_free(sql_statement);
  }
 }
 return ret;
}
// -- -- ---------------------------------- --
// From a given point, build area around it by 'resolution_dem'
// - utm 0.999 meters, Solder Berlin 1.0375644 meters
// (resolution_dem/2) could also be done
// - but to insure that at least 1 point is returned, left as is
// The nearest point will always be retrieved, or none at all.
// -- -- ---------------------------------- --
static int
retrieve_dem_points(sqlite3 *db_handle, struct config_dem *dem_config, int count_points, double *xx_source, double *yy_source, double *zz, double *mm, int *count_z, int *count_m, int verbose)
{
 /* checking for 3D geometries - version 4 */
 int ret=0;
 int i=0;
 char *sql_statement = NULL;
 sqlite3_stmt *stmt = NULL;
 int has_m = 0;
 double x_source=0.0;
 double y_source=0.0;
 double z_source=0.0;
 double m_source=0.0;
 *count_z=0;
 *count_m=0;
 if (mm)
 {
  has_m = 1;
 }
 if (zz)
 {
  for (i=0; i<count_points; i++)
  {
   dem_config->count_points_nr=i;
   x_source=xx_source[i];
   y_source=yy_source[i];
   /* checking the GEOMETRY_COLUMNS table */
   if (has_m)
   {
    sql_statement = sqlite3_mprintf("SELECT ST_Z(\"%s\"), ST_M(\"%s\") "
                                    "FROM '%s'.'%s' WHERE (ROWID IN ( "
                                    "SELECT ROWID FROM SpatialIndex WHERE ( "
                                    "(f_table_name = 'DB=%s.%s') AND "
                                    "(f_geometry_column = '%s') AND "
                                    "(search_frame = ST_Buffer(MakePoint(%2.7f,%2.7f,%d),%2.7f)))) AND "
                                    "(ST_ClosestPoint(%s, MakePoint(%2.7f,%2.7f,%d)) IS NOT Null) ) "
                                    "ORDER BY ST_Distance(%s,MakePoint(%2.7f,%2.7f,%d)) ASC LIMIT 1"
                                    ,dem_config->dem_geometry,dem_config->dem_geometry,dem_config->schema,dem_config->dem_table,dem_config->schema,dem_config->dem_table
                                    ,dem_config->dem_geometry,x_source,y_source,dem_config->dem_srid,dem_config->dem_resolution
                                    ,dem_config->dem_geometry,x_source,y_source,dem_config->dem_srid,dem_config->dem_geometry,x_source,y_source,dem_config->dem_srid);
   }
   else
   {
    sql_statement = sqlite3_mprintf("SELECT ST_Z(\"%s\") "
                                    "FROM '%s'.'%s' WHERE (ROWID IN ( "
                                    "SELECT ROWID FROM SpatialIndex WHERE ("
                                    "(f_table_name = 'DB=%s.%s') AND "
                                    "(f_geometry_column = '%s') AND "
                                    "(search_frame = ST_Buffer(MakePoint(%2.7f,%2.7f,%d),%2.7f)))) AND "
                                    "(ST_ClosestPoint(%s, MakePoint(%2.7f,%2.7f,%d)) IS NOT Null) ) "
                                    "ORDER BY ST_Distance(%s,MakePoint(%2.7f,%2.7f,%d)) ASC LIMIT 1"
                                    ,dem_config->dem_geometry,dem_config->schema,dem_config->dem_table,dem_config->schema,dem_config->dem_table
                                    ,dem_config->dem_geometry,x_source,y_source,dem_config->dem_srid,dem_config->dem_resolution
                                    ,dem_config->dem_geometry,x_source,y_source,dem_config->dem_srid,dem_config->dem_geometry,x_source,y_source,dem_config->dem_srid);
   }
   ret = sqlite3_prepare_v2( db_handle, sql_statement, -1, &stmt, NULL );
#if 0
   if ((dem_config->id_rowid == 354) && (dem_config->count_points == 207))
   {
    fprintf(stderr, "-III-> [EXTERIOR RING] -1a- cnt[%d,%d,%d] sql[%s] id_rowid[%d]\n",dem_config->count_points,dem_config->count_points_nr,ret,sql_statement,dem_config->id_rowid);
   }
#endif
   if ( ret == SQLITE_OK )
   {
    sqlite3_free(sql_statement);
    while ( sqlite3_step( stmt ) == SQLITE_ROW )
    {
     if ( sqlite3_column_type( stmt, 0 ) != SQLITE_NULL )
     {
      z_source = sqlite3_column_double (stmt, 0);
      if ( (z_source != 0.0 ) && (zz[i] != z_source ) )
      {// Do not force an update if everything is 0 or has not otherwise changed
       zz[i] = z_source;
       *count_z += 1;
      }
     }
     if ( sqlite3_column_type( stmt, 1 ) != SQLITE_NULL )
     {
      m_source = sqlite3_column_double (stmt, 1);
      if ( (m_source != 0.0 ) && (mm[i] != m_source ) )
      {// Do not force an update if everything is 0 or has not otherwise changed
       mm[i] = m_source;
       *count_m += 1;
      }
      mm[i] = sqlite3_column_double (stmt, 1);
     }
    }
    sqlite3_finalize( stmt );
   }
   else
   {
    if (verbose)
    {
     fprintf(stderr, "-W-> retrieve_dem_points: rc=%d sql[%s]\n",ret,sql_statement);
    }
    sqlite3_free(sql_statement);
   }
  }
 }
// printf("-I-> retrieve_dem_points: total[%d] not 0.0: z[%d] m[%d]\n",count_points,*count_z,*count_m);
 if (*count_z > 0)
  return 1;
 return 0;
}
// -- -- ---------------------------------- --
//
// -- -- ---------------------------------- --
static int
callFetchZ(sqlite3 *db_handle, struct config_dem *dem_config, int verbose)
{
 int ret=0;
 char *sql_statement = NULL;
 sqlite3_stmt *stmt = NULL;
 int i_count_z=0;
 int i_count_m=0;
 double *xx_use = NULL;
 double *yy_use = NULL;
 double *mm_use = NULL;
 double *zz = NULL;
// -- -- ---------------------------------- --
 dem_config->dem_z=0;;
 dem_config->dem_m=0;
// -- -- ---------------------------------- --
 if (dem_config->dem_srid != dem_config->default_srid )
 {
  sql_statement = sqlite3_mprintf("SELECT ST_X(ST_Transform(MakePoint(%2.7f,%2.7f,%d),%d)),  "
                                  "ST_Y(ST_Transform(MakePoint(%2.7f,%2.7f,%d),%d))"
                                  ,dem_config->fetchz_x, dem_config->fetchz_y, dem_config->default_srid,dem_config->dem_srid
                                  ,dem_config->fetchz_x, dem_config->fetchz_y, dem_config->default_srid,dem_config->dem_srid);
  ret = sqlite3_prepare_v2( db_handle, sql_statement, -1, &stmt, NULL );
  if ( ret == SQLITE_OK )
  {
   sqlite3_free(sql_statement);
   while ( sqlite3_step( stmt ) == SQLITE_ROW )
   {
    if (( sqlite3_column_type( stmt, 0 ) != SQLITE_NULL ) &&
        ( sqlite3_column_type( stmt, 1 ) != SQLITE_NULL ) )
    {
     dem_config->fetchz_x = sqlite3_column_double(stmt, 0);
     dem_config->fetchz_y = sqlite3_column_double(stmt, 1);
    }
   }
   sqlite3_finalize( stmt );
  }
  else
  {
   if (verbose)
   {
    fprintf(stderr, "-W-> callFetchZ : rc=%d sql[%s]\n",ret,sql_statement);
   }
   sqlite3_free(sql_statement);
  }
 }
// -- -- ---------------------------------- --
 ret=0;
 if ((dem_config->fetchz_x >= dem_config->dem_extent_minx) && (dem_config->fetchz_x <= dem_config->dem_extent_maxx) &&
     (dem_config->fetchz_y >= dem_config->dem_extent_miny) && (dem_config->fetchz_y <= dem_config->dem_extent_maxy ) )
 {
  xx_use = malloc(sizeof (double) * 1);
  yy_use = malloc(sizeof (double) * 1);
  zz = malloc(sizeof (double) * 1);
  mm_use = malloc(sizeof (double) * 1);
  xx_use[0] = dem_config->fetchz_x;
  yy_use[0] = dem_config->fetchz_y;
  zz[0] = dem_config->dem_z;
  mm_use[0] = dem_config->dem_m;
  dem_config->count_points=1;
  if (retrieve_dem_points(db_handle, dem_config, 1, xx_use, yy_use,zz,mm_use,&i_count_z, &i_count_m,verbose))
  {
   ret=1;
   dem_config->dem_z=zz[0];
   dem_config->dem_m=mm_use[0];
  }
  free(xx_use);
  free(yy_use);
  free(zz);
  free(mm_use);
  xx_use = NULL;
  yy_use = NULL;
  mm_use = NULL;
  zz = NULL;
 }
// -- -- ---------------------------------- --
 return ret;
}
// -- -- ---------------------------------- --
// GNU libc (Linux, and FreeBSD)
// - sys/param.h
// -- -- ---------------------------------- --
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
// -- -- ---------------------------------- --
// Based on gg_transform.c gaiaTransformCommon
// if the source geometry is out of range of the dem area, NULL is returned
// if the if the z or m values have not changed, NULL is returned
// - in both cases no update should be done and is not an error
// if the Dem-Database Geometry-Column uses a different Srid
// - a second Geometry will be sent with the transfored values
// --> those x,y values will be sent to retrieve_dem_points
//       to retrieve the nearst point
// The retrieved z,m values will be copied WITHOUT any changes
// -- -- ---------------------------------- --
static gaiaGeomCollPtr
getDemCollect(sqlite3 *db_handle, gaiaGeomCollPtr source_geom, gaiaGeomCollPtr dem_geom, struct config_dem *dem_config,
              int *count_points_total, int *count_z_total, int *count_m_total, int verbose)
{
// creates a new GEOMETRY replacing found z-points from the original one
 int ib=0;
 int cnt=0;
 int i=0;
 double *xx = NULL;
 double *yy = NULL;
 double *xx_dem = NULL;
 double *yy_dem = NULL;
 double *zz = NULL;
 double *mm = NULL;
 double *xx_use = NULL;
 double *yy_use = NULL;
 double *mm_use = NULL;
 int count_z=0;
 int count_m=0;
 int i_count_z=0;
 int i_count_m=0;
 double x = 0.0;
 double y = 0.0;
 double z = 0.0;
 double m = 0.0;
 double extent_minx=0.0;
 double extent_miny=0.0;
 double extent_maxx=0.0;
 double extent_maxy=0.0;
 int error = 0;
 int isExtentWithin=0;
 gaiaPointPtr pt = NULL;
 gaiaPointPtr pt_dem = NULL;
 gaiaLinestringPtr ln = NULL;
 gaiaLinestringPtr ln_dem = NULL;
 gaiaLinestringPtr dst_ln = NULL;
 gaiaPolygonPtr pg = NULL;
 gaiaPolygonPtr pg_dem = NULL;
 gaiaPolygonPtr dst_pg = NULL;
 gaiaRingPtr rng = NULL;
 gaiaRingPtr rng_dem = NULL;
 gaiaRingPtr dst_rng = NULL;
 gaiaGeomCollPtr dst = NULL;
 if (source_geom->DimensionModel == GAIA_XY_Z)
  dst = gaiaAllocGeomCollXYZ();
 else if (source_geom->DimensionModel == GAIA_XY_M)
  dst = gaiaAllocGeomCollXYM();
 else if (source_geom->DimensionModel == GAIA_XY_Z_M)
  dst = gaiaAllocGeomCollXYZM();
 else
  dst = gaiaAllocGeomColl ();
 cnt = 0;
 dst->Srid = source_geom->Srid;
 pt = source_geom->FirstPoint;
 extent_minx=source_geom->MinX;
 extent_miny=source_geom->MinY;
 extent_maxx=source_geom->MaxX;
 extent_maxy=source_geom->MaxY;
 if (dem_geom)
 {
  extent_minx=dem_geom->MinX;
  extent_miny=dem_geom->MinY;
  extent_maxx=dem_geom->MaxX;
  extent_maxy=dem_geom->MaxY;
 }
// -- -- ---------------------------------- --
// Touches (partially within)
// -- -- ---------------------------------- --
 int left_x = MAX(extent_minx, dem_config->dem_extent_minx);
 int right_x = MIN(extent_maxx, dem_config->dem_extent_maxx);
 int bottom_y = MAX(extent_miny, dem_config->dem_extent_miny);
 int top_y = MIN(extent_maxy, dem_config->dem_extent_maxy);
 if ((right_x > left_x) && (top_y > bottom_y))
 {
  isExtentWithin=1;
 }
 else if ((right_x == left_x) && (top_y == bottom_y))
 {
  // This is a point
  isExtentWithin=1;
 }
// -- -- ---------------------------------- --
#if 0
 if (verbose)
 {
  fprintf(stderr, "-I-> getDemCollect: isExtentWithin[%d]  left_x[%d]  right_x[%d] bottom_y[%d]  top_y[%d]\n",isExtentWithin,left_x,right_x,bottom_y,top_y);
 }
#endif
 if (!isExtentWithin)
 {
  error=1;
  goto stop;
 }
// -- -- ---------------------------------- --
// Call only if geometry is partially within the Dem extent
// -- -- ---------------------------------- --
// Points
// -- -- ---------------------------------- --
 while (pt)
 {
  // counting POINTs
  cnt++;
  pt = pt->Next;
 }
 if (cnt)
 {
  // reprojecting POINTs
  xx = malloc(sizeof (double) * cnt);
  yy = malloc(sizeof (double) * cnt);
  if (dem_geom)
  {
   xx_dem = malloc(sizeof (double) * cnt);
   yy_dem = malloc(sizeof (double) * cnt);
   xx_use = xx_dem;
   yy_use = yy_dem;
  }
  else
  {
   xx_use = xx;
   yy_use = yy;
  }
  zz = malloc(sizeof (double) * cnt);
  if (source_geom->DimensionModel == GAIA_XY_M || source_geom->DimensionModel == GAIA_XY_Z_M)
   mm = malloc(sizeof (double) * cnt);
  i = 0;
  pt = source_geom->FirstPoint;
  if (dem_geom)
  {
   pt_dem = dem_geom->FirstPoint;
  }
  while (pt)
  {
   // inserting points to be converted in temporary arrays
   xx[i] = pt->X;
   yy[i] = pt->Y;
   if (source_geom->DimensionModel == GAIA_XY_Z || source_geom->DimensionModel == GAIA_XY_Z_M)
    zz[i] = pt->Z;
   else
    zz[i] = 0.0;
   if (source_geom->DimensionModel == GAIA_XY_M || source_geom->DimensionModel == GAIA_XY_Z_M)
    mm[i] = pt->M;
   if (pt_dem)
   {
    xx_dem[i] = pt_dem->X;
    yy_dem[i] = pt_dem->Y;
   }
   i++;
   // -- -- ---------------------------------- --
   // MultiPoints, next
   // -- -- ---------------------------------- --
   pt = pt->Next;
   if (dem_geom)
   {
    pt_dem =pt_dem->Next;
   }
  }
  if ((dem_config->has_m) && (mm) )
  {
   mm_use = mm;
  }
  // searching for nearest point
  *count_points_total+=cnt;
  i_count_z=0;
  i_count_m=0;
  dem_config->count_points=cnt;
  if (retrieve_dem_points(db_handle, dem_config, cnt, xx_use, yy_use,zz,mm_use,&i_count_z, &i_count_m,verbose))
  {
   *count_z_total+=i_count_z;
   *count_m_total+=i_count_m;
   count_z+=i_count_z;
   count_m+=i_count_m;
  }
  xx_use = NULL;
  yy_use = NULL;
  mm_use = NULL;
  // inserting the reprojected POINTs in the new GEOMETRY
  for (i = 0; i < cnt; i++)
  {
   x = xx[i];
   y = yy[i];
   if (source_geom->DimensionModel == GAIA_XY_Z || source_geom->DimensionModel == GAIA_XY_Z_M)
    z = zz[i];
   else
    z = 0.0;
   if (source_geom->DimensionModel == GAIA_XY_M || source_geom->DimensionModel == GAIA_XY_Z_M)
    m = mm[i];
   else
    m = 0.0;
   if (dst->DimensionModel == GAIA_XY_Z)
    gaiaAddPointToGeomCollXYZ(dst, x, y, z);
   else if (dst->DimensionModel == GAIA_XY_M)
    gaiaAddPointToGeomCollXYM(dst, x, y, m);
   else if (dst->DimensionModel == GAIA_XY_Z_M)
    gaiaAddPointToGeomCollXYZM(dst, x, y, z, m);
   else
    gaiaAddPointToGeomColl(dst, x, y);
  }
  free(xx);
  free(yy);
  free(zz);
  xx = NULL;
  yy = NULL;
  zz = NULL;
  if (xx_dem)
  {
   free(xx_dem);
   xx_dem = NULL;
   free(yy_dem);
   yy_dem = NULL;
  }
  if (source_geom->DimensionModel == GAIA_XY_M || source_geom->DimensionModel == GAIA_XY_Z_M)
  {
   free(mm);
   mm = NULL;
  }
 }
 if (error)
  goto stop;
// -- -- ---------------------------------- --
// Linestrings
// -- -- ---------------------------------- --
 ln = source_geom->FirstLinestring;
 if (dem_geom)
 {
  ln_dem = dem_geom->FirstLinestring;
 }
// Call only if geometry is inside the Dem extent
 while (ln)
 {
  // reprojecting LINESTRINGs
  cnt = ln->Points;
  xx = malloc(sizeof (double) * cnt);
  yy = malloc(sizeof (double) * cnt);
  if (dem_geom)
  {
   xx_dem = malloc(sizeof (double) * cnt);
   yy_dem = malloc(sizeof (double) * cnt);
   xx_use = xx_dem;
   yy_use = yy_dem;
  }
  else
  {
   xx_use = xx;
   yy_use = yy;
  }
  zz = malloc(sizeof (double) * cnt);
  if (ln->DimensionModel == GAIA_XY_M || ln->DimensionModel == GAIA_XY_Z_M)
   mm = malloc(sizeof (double) * cnt);
  for (i = 0; i < cnt; i++)
  {
   // inserting points to be converted in temporary arrays
   if (ln->DimensionModel == GAIA_XY_Z)
   {
    gaiaGetPointXYZ(ln->Coords, i, &x, &y, &z);
   }
   else if (ln->DimensionModel == GAIA_XY_M)
   {
    gaiaGetPointXYM(ln->Coords, i, &x, &y, &m);
   }
   else if (ln->DimensionModel == GAIA_XY_Z_M)
   {
    gaiaGetPointXYZM(ln->Coords, i, &x, &y, &z, &m);
   }
   else
   {
    gaiaGetPoint(ln->Coords, i, &x, &y);
   }
   xx[i] = x;
   yy[i] = y;
   if (ln_dem)
   {
    if (ln_dem->DimensionModel == GAIA_XY_Z)
    {
     gaiaGetPointXYZ(ln_dem->Coords, i, &x, &y, &z);
    }
    else if (ln_dem->DimensionModel == GAIA_XY_M)
    {
     gaiaGetPointXYM(ln->Coords, i, &x, &y, &m);
    }
    else if (ln_dem->DimensionModel == GAIA_XY_Z_M)
    {
     gaiaGetPointXYZM(ln_dem->Coords, i, &x, &y, &z, &m);
    }
    else
    {
     gaiaGetPoint(ln_dem->Coords, i, &x, &y);
    }
    xx_dem[i] = x;
    yy_dem[i] = y;
   }
   if (ln->DimensionModel == GAIA_XY_Z || ln->DimensionModel == GAIA_XY_Z_M)
    zz[i] = z;
   else
    zz[i] = 0.0;
   if (ln->DimensionModel == GAIA_XY_M || ln->DimensionModel == GAIA_XY_Z_M)
    mm[i] = m;
  }
  if ((dem_config->has_m) && (mm) )
  {
   mm_use = mm;
  }
  // searching for nearest point
  *count_points_total+=cnt;
  i_count_z=0;
  i_count_m=0;
  dem_config->count_points=cnt;
  if (retrieve_dem_points(db_handle, dem_config, cnt, xx_use, yy_use,zz,mm_use,&i_count_z, &i_count_m,verbose))
  {
   *count_z_total+=i_count_z;
   *count_m_total+=i_count_m;
   count_z+=i_count_z;
   count_m+=i_count_m;
  }
  xx_use = NULL;
  yy_use = NULL;
  mm_use = NULL;
  // inserting the reprojected LINESTRING in the new GEOMETRY
  dst_ln = gaiaAddLinestringToGeomColl (dst, cnt);
  for (i = 0; i < cnt; i++)
  {
   // setting LINESTRING points
   x = xx[i];
   y = yy[i];
   if (ln->DimensionModel == GAIA_XY_Z || ln->DimensionModel == GAIA_XY_Z_M)
    z = zz[i];
   else
    z = 0.0;
   if (ln->DimensionModel == GAIA_XY_M || ln->DimensionModel == GAIA_XY_Z_M)
    m = mm[i];
   else
    m = 0.0;
   if (dst_ln->DimensionModel == GAIA_XY_Z)
   {
    gaiaSetPointXYZ (dst_ln->Coords, i, x, y, z);
   }
   else if (dst_ln->DimensionModel == GAIA_XY_M)
   {
    gaiaSetPointXYM(dst_ln->Coords, i, x, y, m);
   }
   else if (dst_ln->DimensionModel == GAIA_XY_Z_M)
   {
    gaiaSetPointXYZM(dst_ln->Coords, i, x, y, z, m);
   }
   else
   {
    gaiaSetPoint(dst_ln->Coords, i, x, y);
   }
  }
  free(xx);
  free(yy);
  xx = NULL;
  yy = NULL;
  if (xx_dem)
  {
   free(xx_dem);
   xx_dem = NULL;
   free(yy_dem);
   yy_dem = NULL;
  }
  free(zz);
  zz = NULL;
  if (ln->DimensionModel == GAIA_XY_M || ln->DimensionModel == GAIA_XY_Z_M)
  {
   free(mm);
   mm = NULL;
  }
  if (error)
   goto stop;
// -- -- ---------------------------------- --
// MultiLinestrings, next
// -- -- ---------------------------------- --
  ln = ln->Next;
  if (dem_geom)
  {
   ln_dem = ln_dem->Next;
  }
 }
// -- -- ---------------------------------- --
// Polygons
// -- -- ---------------------------------- --
 pg = source_geom->FirstPolygon;
 if (dem_geom)
 {
  pg_dem = dem_geom->FirstPolygon;
 }
 while (pg)
 {
  // -- -- ---------------------------------- --
  // Polygons-ExteriorRing
  // -- -- ---------------------------------- --
  rng = pg->Exterior;
  cnt = rng->Points;
  dst_pg = gaiaAddPolygonToGeomColl(dst, cnt, pg->NumInteriors);
  xx = malloc(sizeof (double) * cnt);
  yy = malloc(sizeof (double) * cnt);
  if (dem_geom)
  {
   xx_dem = malloc(sizeof (double) * cnt);
   yy_dem = malloc(sizeof (double) * cnt);
   rng_dem = pg_dem->Exterior;
   xx_use = xx_dem;
   yy_use = yy_dem;
  }
  else
  {
   xx_use = xx;
   yy_use = yy;
  }
  zz = malloc(sizeof (double) * cnt);
  if (rng->DimensionModel == GAIA_XY_M || rng->DimensionModel == GAIA_XY_Z_M)
   mm = malloc(sizeof (double) * cnt);
  for (i = 0; i < cnt; i++)
  {
   // inserting points to be converted in temporary arrays [EXTERIOR RING]
   if (rng->DimensionModel == GAIA_XY_Z)
   {
    gaiaGetPointXYZ(rng->Coords, i, &x, &y, &z);
   }
   else if (rng->DimensionModel == GAIA_XY_M)
   {
    gaiaGetPointXYM(rng->Coords, i, &x, &y, &m);
   }
   else if (rng->DimensionModel == GAIA_XY_Z_M)
   {
    gaiaGetPointXYZM(rng->Coords, i, &x, &y, &z, &m);
   }
   else
   {
    gaiaGetPoint(rng->Coords, i, &x, &y);
   }
   xx[i] = x;
   yy[i] = y;
   if (rng_dem)
   {
    if (rng_dem->DimensionModel == GAIA_XY_Z)
    {
     gaiaGetPointXYZ(rng_dem->Coords, i, &x, &y, &z);
    }
    else if (rng_dem->DimensionModel == GAIA_XY_M)
    {
     gaiaGetPointXYM(rng_dem->Coords, i, &x, &y, &m);
    }
    else if (rng_dem->DimensionModel == GAIA_XY_Z_M)
    {
     gaiaGetPointXYZM(rng_dem->Coords, i, &x, &y, &z, &m);
    }
    else
    {
     gaiaGetPoint(rng_dem->Coords, i, &x, &y);
    }
    xx_dem[i] = x;
    yy_dem[i] = y;
   }
   if (rng->DimensionModel == GAIA_XY_Z  || rng->DimensionModel == GAIA_XY_Z_M)
    zz[i] = z;
   else
    zz[i] = 0.0;
   if (rng->DimensionModel == GAIA_XY_M || rng->DimensionModel == GAIA_XY_Z_M)
    mm[i] = m;
  }
  if ((dem_config->has_m) && (mm) )
  {
   mm_use = mm;
  }
  // searching for nearest point
  *count_points_total+=cnt;
  i_count_z=0;
  i_count_m=0;
  dem_config->count_points=cnt;
  if (retrieve_dem_points(db_handle, dem_config, cnt, xx_use, yy_use,zz,mm_use,&i_count_z, &i_count_m,verbose))
  {
   *count_z_total+=i_count_z;
   *count_m_total+=i_count_m;
   count_z+=i_count_z;
   count_m+=i_count_m;
  }
  xx_use = NULL;
  yy_use = NULL;
  mm_use = NULL;
  // inserting the reprojected POLYGON in the new GEOMETRY
  dst_rng = dst_pg->Exterior;
  for (i = 0; i < cnt; i++)
  {
   // setting EXTERIOR RING points
   x = xx[i];
   y = yy[i];
   if (rng->DimensionModel == GAIA_XY_Z || rng->DimensionModel == GAIA_XY_Z_M)
    z = zz[i];
   else
    z = 0.0;
   if (rng->DimensionModel == GAIA_XY_M  || rng->DimensionModel == GAIA_XY_Z_M)
    m = mm[i];
   else
    m = 0.0;
   if (dst_rng->DimensionModel == GAIA_XY_Z)
   {
    gaiaSetPointXYZ (dst_rng->Coords, i, x, y, z);
   }
   else if (dst_rng->DimensionModel == GAIA_XY_M)
   {
    gaiaSetPointXYM(dst_rng->Coords, i, x, y, m);
   }
   else if (dst_rng->DimensionModel == GAIA_XY_Z_M)
   {
    gaiaSetPointXYZM(dst_rng->Coords, i, x, y, z, m);
   }
   else
   {
    gaiaSetPoint(dst_rng->Coords, i, x, y);
   }
  }
  free(xx);
  free(yy);
  xx = NULL;
  yy = NULL;
  if (xx_dem)
  {
   free(xx_dem);
   xx_dem = NULL;
   free(yy_dem);
   yy_dem = NULL;
  }
  free(zz);
  zz = NULL;
  if (rng->DimensionModel == GAIA_XY_M || rng->DimensionModel == GAIA_XY_Z_M)
  {
   free(mm);
   mm = NULL;
  }
  if (error)
   goto stop;
  // -- -- ---------------------------------- --
  // Polygons-InteriorRings
  // -- -- ---------------------------------- --
  for (ib = 0; ib < pg->NumInteriors; ib++)
  {
   // processing INTERIOR RINGS
   rng = pg->Interiors + ib;
   cnt = rng->Points;
   xx = malloc(sizeof (double) * cnt);
   yy = malloc(sizeof (double) * cnt);
   if (dem_geom)
   {
    xx_dem = malloc(sizeof (double) * cnt);
    yy_dem = malloc(sizeof (double) * cnt);
    rng_dem = pg_dem->Interiors + ib;
    xx_use = xx_dem;
    yy_use = yy_dem;
   }
   else
   {
    xx_use = xx;
    yy_use = yy;
   }
   zz = malloc(sizeof (double) * cnt);
   if (rng->DimensionModel == GAIA_XY_M || rng->DimensionModel == GAIA_XY_Z_M)
    mm = malloc(sizeof (double) * cnt);
   for (i = 0; i < cnt; i++)
   {
    // inserting points to be converted in temporary arrays [INTERIOR RING]
    if (rng->DimensionModel == GAIA_XY_Z)
    {
     gaiaGetPointXYZ(rng->Coords, i, &x, &y, &z);
    }
    else if (rng->DimensionModel == GAIA_XY_M)
    {
     gaiaGetPointXYM(rng->Coords, i, &x, &y, &m);
    }
    else if (rng->DimensionModel == GAIA_XY_Z_M)
    {
     gaiaGetPointXYZM(rng->Coords, i, &x, &y, &z, &m);
    }
    else
    {
     gaiaGetPoint(rng->Coords, i, &x, &y);
    }
    xx[i] = x;
    yy[i] = y;
    if (rng_dem)
    {
     if (rng_dem->DimensionModel == GAIA_XY_Z)
     {
      gaiaGetPointXYZ(rng_dem->Coords, i, &x, &y, &z);
     }
     else if (rng_dem->DimensionModel == GAIA_XY_M)
     {
      gaiaGetPointXYM(rng_dem->Coords, i, &x, &y, &m);
     }
     else if (rng_dem->DimensionModel == GAIA_XY_Z_M)
     {
      gaiaGetPointXYZM(rng_dem->Coords, i, &x, &y, &z, &m);
     }
     else
     {
      gaiaGetPoint(rng_dem->Coords, i, &x, &y);
     }
     xx_dem[i] = x;
     yy_dem[i] = y;
    }
    if (rng->DimensionModel == GAIA_XY_Z  || rng->DimensionModel == GAIA_XY_Z_M)
     zz[i] = z;
    else
     zz[i] = 0.0;
    if (rng->DimensionModel == GAIA_XY_M  || rng->DimensionModel == GAIA_XY_Z_M)
     mm[i] = m;
   }
   if ((dem_config->has_m) && (mm) )
   {
    mm_use = mm;
   }
   // searching for nearest point
   *count_points_total+=cnt;
   i_count_z=0;
   i_count_m=0;
   dem_config->count_points=cnt;
   if (retrieve_dem_points(db_handle, dem_config, cnt, xx_use, yy_use,zz,mm_use,&i_count_z, &i_count_m,verbose))
   {
    *count_z_total+=i_count_z;
    *count_m_total+=i_count_m;
    count_z+=i_count_z;
    count_m+=i_count_m;
   }
   xx_use = NULL;
   yy_use = NULL;
   mm_use = NULL;
   // inserting the reprojected POLYGON in the new GEOMETRY
   dst_rng = gaiaAddInteriorRing(dst_pg, ib, cnt);
   for (i = 0; i < cnt; i++)
   {
    // setting INTERIOR RING points
    x = xx[i];
    y = yy[i];
    if (rng->DimensionModel == GAIA_XY_Z || rng->DimensionModel == GAIA_XY_Z_M)
     z = zz[i];
    else
     z = 0.0;
    if (rng->DimensionModel == GAIA_XY_M || rng->DimensionModel == GAIA_XY_Z_M)
     m = mm[i];
    else
     m = 0.0;
    if (dst_rng->DimensionModel == GAIA_XY_Z)
    {
     gaiaSetPointXYZ(dst_rng->Coords, i, x, y, z);
    }
    else if (dst_rng->DimensionModel == GAIA_XY_M)
    {
     gaiaSetPointXYM(dst_rng->Coords, i, x, y, m);
    }
    else if (dst_rng->DimensionModel == GAIA_XY_Z_M)
    {
     gaiaSetPointXYZM(dst_rng->Coords, i, x, y, z, m);
    }
    else
    {
     gaiaSetPoint(dst_rng->Coords, i, x, y);
    }
   }
   free(xx);
   free(yy);
   xx = NULL;
   yy = NULL;
   if (xx_dem)
   {
    free(xx_dem);
    xx_dem = NULL;
    free(yy_dem);
    yy_dem = NULL;
   }
   free(zz);
   zz = NULL;
   if (rng->DimensionModel == GAIA_XY_M || rng->DimensionModel == GAIA_XY_Z_M)
   {
    free(mm);
    mm = NULL;
   }
   if (error)
    goto stop;
  }
// -- -- ---------------------------------- --
// MultiPolygons, next
// -- -- ---------------------------------- --
  pg = pg->Next;
  if (dem_geom)
  {
   pg_dem = pg_dem->Next;
  }
 }
// -- -- ---------------------------------- --
// -end- Geometry types
// -- -- ---------------------------------- --
stop:
 if ((count_z+count_m) == 0)
 {// Do not force an update if everything is 0 or has not otherwise changed
  error=1;
 }
 if (error)
 {
  // some error occurred, or no changes needed
  gaiaPointPtr pP;
  gaiaPointPtr pPn;
  gaiaLinestringPtr pL;
  gaiaLinestringPtr pLn;
  gaiaPolygonPtr pA;
  gaiaPolygonPtr pAn;
  pP = dst->FirstPoint;
  while (pP != NULL)
  {
   pPn = pP->Next;
   gaiaFreePoint(pP);
   pP = pPn;
  }
  pL = dst->FirstLinestring;
  while (pL != NULL)
  {
   pLn = pL->Next;
   gaiaFreeLinestring(pL);
   pL = pLn;
  }
  pA = dst->FirstPolygon;
  while (pA != NULL)
  {
   pAn = pA->Next;
   gaiaFreePolygon(pA);
   pA = pAn;
  }
  dst->FirstPoint = NULL;
  dst->LastPoint = NULL;
  dst->FirstLinestring = NULL;
  dst->LastLinestring = NULL;
  dst->FirstPolygon = NULL;
  dst->LastPolygon = NULL;
  gaiaFreeGeomColl(dst);
  dst = NULL;
  // -- -- ---------------------------------- --
  // if the source geometry is out of range of the dem area, NULL is returned
  // -- -- ---------------------------------- --
  return NULL;
 }
 if (dst)
 {
  gaiaMbrGeometry(dst);
  dst->DeclaredType = source_geom->DeclaredType;
 }
 return dst;
}
// -- -- ---------------------------------- --
// if the source geometry is out of range of the dem area, NULL is returned
// - no update should be done and is not an error
// if the source geometry cannot be updated, when changed
// - this is an error and the loop should stop
// The source must be a SpatialTable,
// - since ROWID is used for a (possibly) needed update
// -- -- ---------------------------------- --
static int
retrieve_geometries(sqlite3 *db_handle, struct config_dem *source_config, struct config_dem *dem_config, int *count_total_geometries, int *count_changed_geometries,
                    int *count_points_total, int *count_z_total, int *count_m_total, int verbose)
{
 char *sql_statement = NULL;
 sqlite3_stmt *stmt = NULL;
 sqlite3_stmt *stmt_update = NULL;
 char *sql_err = NULL;
 unsigned char *blob_value = NULL;
 int blob_bytes=0;
 unsigned char *blob_update = NULL;
 int blob_bytes_update=0;
 int id_rowid=0;
 int ret=0;
 int ret_update=SQLITE_ABORT;
 unsigned int i_sleep=1; // 1 second
 int count_geometries_remainder=100;
 int transaction_update_changed_last=0;
 int transaction_count_loops=0;
 double remainder_calc=0.10;
 gaiaGeomCollPtr source_geom = NULL;
 gaiaGeomCollPtr geom_dem = NULL;
 gaiaGeomCollPtr geom_result = NULL;
 *count_total_geometries=0;
 *count_changed_geometries=0;
 *count_points_total=0;
 *count_z_total=0;
 *count_m_total=0;
// -- -- ---------------------------------- --
 count_geometries_remainder=source_config->dem_rows_count/100;
 if (count_geometries_remainder > 1000)
 {// Display results every 1.25% of total geometries, when verbose
  remainder_calc=remainder_calc/8;
 } else if (source_config->dem_rows_count > 500)
 {// Display results every 2.5% of total geometries, when verbose
  remainder_calc=remainder_calc/4;
 } else if (source_config->dem_rows_count > 100)
 {// Display results every 5% of total geometries, when verbose
  remainder_calc=remainder_calc/2;
 } // else: Display results every 10% of total geometries, when verbose
 count_geometries_remainder=(int)(source_config->dem_rows_count*remainder_calc);
// -- -- ---------------------------------- --
 if (verbose)
 {
  fprintf(stderr, "-I-> retrieve_geometries: results will be shown after each group of %d geometries, total[%u] \n",count_geometries_remainder,source_config->dem_rows_count);
 }
 if (dem_config->default_srid == dem_config->dem_srid)
 {
  sql_statement = sqlite3_mprintf("SELECT ROWID, \"%s\" FROM '%s'.'%s' WHERE \"%s\" IS NOT NULL",
                                  source_config->dem_geometry, source_config->schema,source_config->dem_table, source_config->dem_geometry);
 }
 else
 {
  sql_statement = sqlite3_mprintf("SELECT ROWID, \"%s\", ST_Transform(\"%s\",%d) FROM '%s'.'%s' WHERE \"%s\"  IS NOT NULL",
                                  source_config->dem_geometry,source_config->dem_geometry, dem_config->dem_srid, source_config->schema,source_config->dem_table,source_config->dem_geometry);
 }
#if 0
 if (verbose)
 {
  fprintf(stderr, "-I-> retrieve_geometries: sql[%s] \n",sql_statement);
 }
#endif
 ret = sqlite3_prepare_v2(db_handle, sql_statement, -1, &stmt, NULL );
 if ( ret == SQLITE_OK )
 {
  sqlite3_free(sql_statement);
  while ( sqlite3_step( stmt ) == SQLITE_ROW )
  {
   if (( sqlite3_column_type( stmt, 0 ) != SQLITE_NULL ) &&
       ( sqlite3_column_type( stmt, 1 ) != SQLITE_NULL ) )
   {
    id_rowid = sqlite3_column_int (stmt, 0);
    dem_config->id_rowid=id_rowid; // for debugging
    blob_value = (unsigned char *)sqlite3_column_blob(stmt, 1);
    blob_bytes = sqlite3_column_bytes(stmt,1);
    source_geom = gaiaFromSpatiaLiteBlobWkb(blob_value, blob_bytes);
    *count_total_geometries+=1;
   }
   if ( sqlite3_column_type( stmt, 2 ) != SQLITE_NULL )
   {
    blob_value = (unsigned char *)sqlite3_column_blob(stmt, 2);
    blob_bytes = sqlite3_column_bytes(stmt,2);
    geom_dem = gaiaFromSpatiaLiteBlobWkb(blob_value, blob_bytes);
   }
   if (source_geom)
   {
    // if the source geometry is out of range of the dem area, NULL is returned: this is not an error, but no update
    geom_result=getDemCollect(db_handle, source_geom, geom_dem, dem_config, count_points_total,count_z_total,count_m_total,verbose);
    gaiaFreeGeomColl(source_geom);
    source_geom = NULL;
    if (geom_dem)
    {
     gaiaFreeGeomColl(geom_dem);
     geom_dem = NULL;
    }
    ret_update=SQLITE_OK;
    if (geom_result)
    {
     sql_statement = sqlite3_mprintf("UPDATE '%s'.'%s' SET '%s'=? WHERE ROWID=%d",
                                     source_config->schema,source_config->dem_table,source_config->dem_geometry,id_rowid);
     ret=sqlite3_prepare_v2(db_handle, sql_statement, -1, &stmt_update, NULL);
     if ( ret == SQLITE_OK)
     {
      sqlite3_free(sql_statement);
      gaiaToSpatiaLiteBlobWkb(geom_result, &blob_update, &blob_bytes_update);
      // Note: sqlite3_bind_* index is 1-based, os apposed to sqlite3_column_* that is 0-based.
      sqlite3_bind_blob(stmt_update, 1, blob_update, blob_bytes_update, free);
      ret_update = sqlite3_step( stmt_update );
      if ( ret_update == SQLITE_DONE || ret_update == SQLITE_ROW )
      {
       ret_update=SQLITE_OK;
       *count_changed_geometries += 1;
      }
      else
      {
       ret_update=SQLITE_ABORT;
      }
      sqlite3_finalize( stmt_update );
     }
     else
     {
      if (verbose)
      {
       fprintf(stderr, "-W-> retrieve_geometries [UPDATE]: rc=%d sql[%s]\n",ret,sql_statement);
      }
      sqlite3_free(sql_statement);
     }
    }
    gaiaFreeGeomColl(geom_result);
    geom_result = NULL;
    if (((*count_total_geometries % count_geometries_remainder) == 0))
    {
     if (*count_changed_geometries > transaction_update_changed_last)
     {// Store results only if something has changed
      // Note: while testing, this process stopped, with no further updates being reported.
      // The assumption was that there was a logical error else where, which was not the case.
      // This saving was build in to get to this point and analyse. [cause: missing Next for Linestrings/Polygon for dem_geom]
      // Since the logic exists, that UPDATEs are only done after changes have been made
      // This sporadic COMMIT/BEGIN has been retained. What is done is done.
      if (sqlite3_exec(db_handle, "COMMIT", NULL, NULL, &sql_err) == SQLITE_OK)
      {
       sleep(i_sleep);
       if (sqlite3_exec(db_handle, "BEGIN", NULL, NULL, &sql_err) == SQLITE_OK)
       {
       }
      }
      if (sql_err)
      {
       sqlite3_free(sql_err);
      }
     }
     if (verbose)
     {
      double procent_diff=(double)(*count_total_geometries)/source_config->dem_rows_count;
      remainder_calc=procent_diff*100;
      if (transaction_count_loops == 0)
      {// Show only once
       fprintf(stderr,"-I-> converted geometries committed to Database: \n");
      }
      transaction_count_loops++;
      if (dem_config->has_m)
      {// overwrite the previous message [\r]
       fprintf(stderr, "\r %02.2f%% total read[%d] changed[%d] ; points total[%d] changed z[%d] changed m[%d] ",remainder_calc,*count_total_geometries,*count_changed_geometries,*count_points_total,*count_z_total,*count_m_total);
      }
      else
      {// overwrite the previous message [\r]
       fprintf(stderr, "\r %02.2f%% total read[%d] changed[%d] ; points total[%d] changed z[%d] ",remainder_calc,*count_total_geometries,*count_changed_geometries,*count_points_total,*count_z_total);
      }
     }
     transaction_update_changed_last=*count_changed_geometries;
    }
   }
   if (ret_update == SQLITE_ABORT )
   {
    break;
   }
  }
  sqlite3_finalize( stmt );
  if (verbose)
  {// new line after last message [\n]
   fprintf(stderr, "\n");
  }
 }
 else
 {
  if (verbose)
  {
   fprintf(stderr, "-W-> retrieve_geometries [SELECT]: rc=%d sql[%s]\n",ret,sql_statement);
  }
  sqlite3_free(sql_statement);
 }
 if (ret_update == SQLITE_ABORT )
 {
  return 0;
 }
 return 1;
}
// -- -- ---------------------------------- --
// Retrieve information about given
// - table and geometry-column
// -> for Source and Dem geometries
// Dem
// - must be a POINTZ or POINTZM
// - SpatialIndex must exist
// -  Extent and row_count
// -> to calculate resolution
// Source
// - must have a Z or ZM Dimension
// Both
// -  Srid of geometries
// -- -- ---------------------------------- --
static int
check_geometry_dimension(sqlite3 *db_handle, struct config_dem *use_config, int *geometry_type, int verbose)
{
 /* checking the table, geometry exists and if the geometry supports z-values */
 int ret=0;
 char *sql_statement = NULL;
 sqlite3_stmt *stmt = NULL;
 int *srid_default = NULL;
 use_config->has_z = 0;
 use_config->has_m = 0;
 use_config->dem_extent_minx=0.0;
 use_config->dem_extent_miny=0.0;
 use_config->dem_extent_maxx=0.0;
 use_config->dem_extent_maxy=0.0;
 use_config->has_spatial_index=0;
 use_config->is_spatial_table=0;
 if (use_config->config_type == CONF_TYPE_DEM )
 {
  srid_default = &use_config->dem_srid;
 }
 else
 {
  srid_default = &use_config->default_srid;
 }
 *geometry_type = 0;
 *srid_default = 0;
// 390718.000000	5818887.000000	392757.000000	5820847.000000
// 392757-390718=2039
// 5820847-5818887=1960
// 1960*2039=3996440/4000440=0.99900011 resolution
 sql_statement = sqlite3_mprintf("SELECT a.geometry_type, a.srid, a.spatial_index_enabled, a.layer_type, "
                                 "b.extent_min_x, b.extent_min_y, b.extent_max_x, b.extent_max_y, b.row_count "
                                 "FROM '%s'.vector_layers AS a LEFT JOIN '%s'.vector_layers_statistics AS b "
                                 "ON a.table_name=b.table_name  AND a.geometry_column=b.geometry_column "
                                 "WHERE a.table_name='%s' AND a.geometry_column='%s'", use_config->schema, use_config->schema,
                                 use_config->dem_table, use_config->dem_geometry);
 ret = sqlite3_prepare_v2(db_handle, sql_statement, -1, &stmt, NULL );
 if ( ret == SQLITE_OK )
 {
  sqlite3_free(sql_statement);
  while ( sqlite3_step( stmt ) == SQLITE_ROW )
  {
   if (( sqlite3_column_type( stmt, 0 ) != SQLITE_NULL ) &&
       ( sqlite3_column_type( stmt, 1 ) != SQLITE_NULL ) &&
       ( sqlite3_column_type( stmt, 2 ) != SQLITE_NULL ) &&
       ( sqlite3_column_type( stmt, 3 ) != SQLITE_NULL ))
   {
    *geometry_type = sqlite3_column_int( stmt, 0 );
    *srid_default = sqlite3_column_int( stmt, 1 );
    use_config->has_spatial_index = sqlite3_column_int( stmt, 2 );
    if (strcmp((const char *)sqlite3_column_text( stmt, 3 ),"SpatialTable") == 0)
    {
     // The source Database must be in a SpatialTable to be updated [no checking for writable SpatialView's]
     // will be using ROWID to UPDATE
     use_config->is_spatial_table = 1;
    }
    // printf("-I-> check_geometry_dimension: coord_dimension=%d GAIA_XY_*l[%d,%d,%d]\n",coord_dimension,GAIA_XY_Z,GAIA_XY_Z_M,GAIA_XY_M);
    switch (*geometry_type)
    {
     case GAIA_POINTZ:
     case GAIA_LINESTRINGZ:
     case GAIA_POLYGONZ:
     case GAIA_MULTIPOINTZ:
     case GAIA_MULTILINESTRINGZ:
     case GAIA_MULTIPOLYGONZ:
     case GAIA_GEOMETRYCOLLECTIONZ:
      use_config->has_z = 1;
      break;
    }
    switch (*geometry_type)
    {
     case GAIA_POINTZM:
     case GAIA_LINESTRINGZM:
     case GAIA_POLYGONZM:
     case GAIA_MULTIPOINTZM:
     case GAIA_MULTILINESTRINGZM:
     case GAIA_MULTIPOLYGONZM:
     case GAIA_GEOMETRYCOLLECTIONZM:
      use_config->has_z = 1;
      use_config->has_m = 1;
      break;
    }
    switch (*geometry_type)
    {
     case GAIA_POINTM:
     case GAIA_LINESTRINGM:
     case GAIA_POLYGONM:
     case GAIA_MULTIPOINTM:
     case GAIA_MULTILINESTRINGM:
     case GAIA_MULTIPOLYGONM:
     case GAIA_GEOMETRYCOLLECTIONM:
      use_config->has_m = 1;
      break;
    }
   }
   if (( sqlite3_column_type( stmt, 4 ) != SQLITE_NULL ) &&
       ( sqlite3_column_type( stmt, 5 ) != SQLITE_NULL ) &&
       ( sqlite3_column_type( stmt, 6 ) != SQLITE_NULL ) &&
       ( sqlite3_column_type( stmt, 7 ) != SQLITE_NULL ) &&
       ( sqlite3_column_type( stmt, 8 ) != SQLITE_NULL ))
   {
    use_config->dem_extent_minx = sqlite3_column_double( stmt, 4 );
    use_config->dem_extent_miny = sqlite3_column_double( stmt, 5 );
    use_config->dem_extent_maxx = sqlite3_column_double( stmt, 6 );
    use_config->dem_extent_maxy = sqlite3_column_double( stmt, 7 );
    use_config->dem_rows_count = sqlite3_column_int64( stmt, 8 );
   }
  }
  sqlite3_finalize( stmt );
  if ((use_config->is_spatial_table == 1) && (use_config->has_z) && (use_config->dem_rows_count > 0))
  {
   ret=1; // Valid for usage
  }
 }
 else
 {
  if (verbose)
  {
   fprintf(stderr, "-W-> check_geometry_dimension: rc=%d sql[%s]\n",ret,sql_statement);
  }
  sqlite3_free(sql_statement);
 }
 return ret;
}
static void
spatialite_autocreate(sqlite3 *db_handle)
{
 /* attempting to perform self-initialization for a newly created DB */
 int ret;
 char sql[1024];
 char *err_msg = NULL;
 int count;
 int i;
 char **results;
 int rows;
 int columns;

 /* checking if this DB is really empty */
 strcpy(sql, "SELECT Count(*) from sqlite_master");
 ret = sqlite3_get_table(db_handle, sql, &results, &rows, &columns, NULL);
 if (ret != SQLITE_OK)
  return;
 if (rows < 1)
  ;
 else
 {
  for (i = 1; i <= rows; i++)
   count = atoi (results[(i * columns) + 0]);
 }
 sqlite3_free_table(results);

 if (count > 0)
  return;

 /* all right, it's empty: proceding to initialize */
 strcpy(sql, "SELECT InitSpatialMetadataFull(1)");
 ret = sqlite3_exec(db_handle, sql, NULL, NULL, &err_msg);
 if (ret != SQLITE_OK)
 {
  fprintf(stderr, "InitSpatialMetadataFull() error: %s\n", err_msg);
  sqlite3_free(err_msg);
  return;
 }
}
// -- -- ---------------------------------- --
// Collecting a list of Dem-xyz file
// - with checks on the first record
// Case 1: a single xyz.file is given
// -> the file will be checked and added to the list
// Case 2: a directory is given
// -> each file will be checked and added to the list
// Case 3: a single list.file is given
// - contains a list of single xyz.files to be read
// -> expected to be inside the given directory
// -> each file will be checked and added to the list
// -- -- ---------------------------------- --
// The list is the TABLE db_memory.xyz_files
// Goal is to read the files in a specific order:
// --> y='South to North' and x='West to East'
// -- -- ---------------------------------- --
static int
collect_xyz_files(sqlite3 *db_handle,const char *xyz_filename, int *count_xyz_files, int verbose)
{
 int ret=0;
 int i_count_fields=0;
 int i_count_fields_check=3;
 int ret_insert=SQLITE_OK;
 if (*count_xyz_files < 0)
 {
  *count_xyz_files=0;
 }
 double point_x=0.0;
 double point_y=0.0;
 double point_z=0.0;
 char *sql_statement = NULL;
 int result_file_type=0;
 char *directory_from_filename = NULL;
// -- -- ---------------------------------- --
 FILE *xyz_file = fopen(xyz_filename, "rt");
 if (xyz_file != NULL)
 {
  if (verbose)
  {
   fprintf(stderr,"-I-> collect_xyz_files:reading xyz_filename[%s] \n", xyz_filename);
  }
  char line[MAXBUF];
  while(fgets(line, sizeof(line), xyz_file) != NULL)
  {
   if (strcmp(line, "SQLite format 3") != 0)
   {
    line[strcspn(line, "\r\n")] = 0;
    char *token;
    char *ptr_strtod;
    char *saveptr;
    i_count_fields=0;
    token = strtok_r(line, " ",&saveptr);
    point_x=strtod(token, &ptr_strtod);
    // atof will cause a signal 11 (SIGSEGV), if token does not contain a double
    if ((int)strlen(ptr_strtod) == 0)
    {
     i_count_fields++;
     while(token != NULL)
     {
      token = strtok_r(NULL," ",&saveptr);
      switch (i_count_fields)
      {
       case 1:
        point_y=strtod(token, &ptr_strtod);
        if ((int)strlen(ptr_strtod) == 0)
        {
         i_count_fields++;
        }
        break;
       case 2:
        point_z=strtod(token, &ptr_strtod);
        if ((int)strlen(ptr_strtod) == 0)
        {
         if (point_z < 0.0 || point_z > 0.0)
         {
          // suppressing stupid compiler warnings
          i_count_fields++;
         }
        }
        break;
      }
     }
    }
    if (i_count_fields == 0)
    {
     // This may be a list of xyz.file-names,
     // - contained in the same directory
     // - that should be read [possibly not all of the xyz.files should be read]
     // - order is not important, will be sorted by point_y ASC, point_x ASC of the first record
     if (!directory_from_filename)
     {
      const char *slash = strrchr(xyz_filename,'/');
      directory_from_filename=malloc(sizeof(char)*((slash-xyz_filename)+1));
      strncpy(directory_from_filename,xyz_filename,slash-xyz_filename);
      directory_from_filename[(slash-xyz_filename)]=0;
     }
     sql_statement = sqlite3_mprintf("%s/%s", directory_from_filename,token);
     // the first record will be read. If 3 doubles, seperated by a space, can be created
     // 1 : will be returned, after adding the path/file-name, point_x and point_y to db_memory.xyz_files
     result_file_type=collect_xyz_files(db_handle,sql_statement, count_xyz_files, 0);
     if (result_file_type == 1)
     {//file_name has been added to db_memory.xyz_files
      ret=1; // xyz-format
     }
     sqlite3_free(sql_statement);
    }
    if (i_count_fields == i_count_fields_check)
    {
     sql_statement = sqlite3_mprintf("INSERT INTO db_memory.xyz_files (point_x,point_y,file_name) "
                                     "VALUES(%2.7f,%2.7f,'%s') ",point_x,point_y,xyz_filename);
     if (db_handle)
     {
      if (sqlite3_exec(db_handle, sql_statement, NULL, NULL, NULL) == SQLITE_OK)
      {
       *count_xyz_files+=1; // xyz-format
       result_file_type=1;
      }
     }
     sqlite3_free(sql_statement);
     ret_insert = SQLITE_ABORT;
    }
    if (verbose)
    {
     fprintf(stderr,"-I->  collect_xyz_files: i_count_fields[%d] check[%d] count_files[%d]\n", i_count_fields,i_count_fields_check,*count_xyz_files);
    }
   }
   else
   {
    result_file_type=2; // Sqlite3-format
   }
   if (ret_insert == SQLITE_ABORT )
   {
    break;
   }
   if (ret_insert != SQLITE_ABORT )
   {
    // if this is a list of .xyz files that are being added,
    // result_file_type will be 1, but not SQLITE_ABORT
    // - since all entries must be read
    if (result_file_type != 1)
    {
     break; // unknown format
    }
   }
  } // End while
  fclose(xyz_file);
  if (directory_from_filename)
  {
   free(directory_from_filename);
   directory_from_filename=NULL;
  }
 } // Checking for a file
 else
 {
#if defined(_WIN32)
  /* Visual Studio .NET */
  struct _finddata_t c_file;
  intptr_t hFile;
  char *name;
  int len;
  int ret;
  if (_chdir (xyz_filename) < 0)
   return ret;
  if ((hFile = _findfirst ("*.xyz", &c_file)) == -1L)
   ;
  else
  {
   while (1)
   {// A directory with .xyz files
    if ((c_file.attrib & _A_RDONLY) == _A_RDONLY  || (c_file.attrib & _A_NORMAL) == _A_NORMAL)
    {
     sql_statement = sqlite3_mprintf("%s/%s", xyz_filename,c_file.name);
     if (collect_xyz_files(db_handle,sql_statement, count_xyz_files, 0) == 1)
     {//file_name has been added to db_memory.xyz_files
     }
     sqlite3_free(sql_statement);
    }
    if (_findnext (hFile, &c_file) != 0)
     break;
   }
   _findclose (hFile);
  }
#else
  DIR *xyz_dir = opendir(xyz_filename);
  struct dirent *dir;
  if (xyz_dir)
  {
   while ((dir = readdir(xyz_dir)) != NULL)
   {
    if (dir->d_type == DT_REG)
    {
     const char *ext = strrchr(dir->d_name,'.');
     if ((ext) || (ext != dir->d_name))
     {
      if (strcmp(ext, ".xyz") == 0)
      {// A directory with .xyz files
       // - order is not important, will be sorted by point_y ASC, point_x ASC of the first record
       sql_statement = sqlite3_mprintf("%s/%s", xyz_filename,dir->d_name);
       // the first record will be read. If 3 doubles, seperated by a space, can be created
       // 1 : will be returned, after adding the path/file-name, point_x and point_y to db_memory.xyz_files
       result_file_type=collect_xyz_files(db_handle,sql_statement, count_xyz_files, 0);
       if (result_file_type == 1)
       {//file_name has been added to db_memory.xyz_files
       }
       sqlite3_free(sql_statement);
      }
     }
    }
   }
   closedir(xyz_dir);
  } // Checking for a directory
#endif
 }
// -- -- ---------------------------------- --
 if (*count_xyz_files > 0)
 {
  ret=1; // xyz-format
 }
 else
 {
  ret=0; // not supported
  if (verbose)
  {
   fprintf(stderr,"-E-> collect_xyz_files: import.xyz file format not found [%s]\n", xyz_filename);
  }
 }
// -- -- ---------------------------------- -
 return ret;
}
// -- -- ---------------------------------- --
// Read list of Dem-xyz files
// - from db_memory.xyz_files
// Goal is to INSERT the points in a specific order:
// --> y='South to North' and x='West to East'
// -- -- ---------------------------------- --
static int
import_xyz(sqlite3 *db_handle, struct config_dem *dem_config, int count_xyz_files, int verbose)
{
 int ret=0;
 int ret_select=0;
 sqlite3_stmt *stmt = NULL;
 char *sql_statement = NULL;
 int i_count_loop=100000;
 int i_count_fields=0;
 int i_count_fields_check=3;
 int i_count_in_loop=0;
 int i_file_count=0;
 const char *xyz_path_filename;
// 18.446.744.073.709.551.615
 unsigned int i_sleep=1; // 1 second
 double point_x=0.0;
 double point_y=0.0;
 double point_z=0.0;
 double *xx = NULL;
 double *yy = NULL;
 double *zz = NULL;
 int ret_insert=SQLITE_OK;
 if (count_xyz_files > 0)
 {// input-files should be sorted from y='South to North' and x='West to East':  sort -n -k2 -k1 input_file.xyz -o output_file.sort.xyz
  // Select files sorted by y='South to North' and x='West to East'
  sql_statement = sqlite3_mprintf("SELECT file_name FROM db_memory.xyz_files ORDER BY point_y ASC, point_x ASC");
  ret_select = sqlite3_prepare_v2(db_handle, sql_statement, -1, &stmt, NULL );
  sqlite3_free(sql_statement);
  if ( ret_select == SQLITE_OK )
  {
   while ( sqlite3_step( stmt ) == SQLITE_ROW )
   {
    if ( sqlite3_column_type( stmt, 0 ) != SQLITE_NULL )
    {
     xyz_path_filename=(const char *) sqlite3_column_text (stmt, 0);
     // -- -- ---------------------------------- --
     FILE *xyz_file = fopen(xyz_path_filename, "rt");
     if (xyz_file != NULL)
     {
      char line[MAXBUF];
      i_file_count++;
      i_count_in_loop=0;
      if (verbose)
      {
       // CPU: 4-11% ; memory 9.3 Ḿib ; normal working with mouse and applications
       fprintf(stderr,"import_xyz: reading  xyz_filename[%s]\n (file %d of %d) in steps of [%u].\n",xyz_path_filename,i_file_count,count_xyz_files, i_count_loop);
      }
      xx = malloc(sizeof (double) * i_count_loop);
      yy = malloc(sizeof (double) * i_count_loop);
      zz = malloc(sizeof (double) * i_count_loop);
      while(fgets(line, sizeof(line), xyz_file) != NULL)
      {
       line[strcspn(line, "\r\n")] = 0;
       char *token;
       char *ptr_strtod;
       char *saveptr;
       i_count_fields=0;
       token = strtok_r(line, " ",&saveptr);
       point_x=strtod(token, &ptr_strtod);
       // atof will cause a signal 11 (SIGSEGV), if token does not contain a double
       if ((int)strlen(ptr_strtod) == 0)
       {
        i_count_fields++;
        while(token != NULL)
        {
         token = strtok_r(NULL," ",&saveptr);
         switch (i_count_fields)
         {
          case 1:
           point_y=strtod(token, &ptr_strtod);
           if ((int)strlen(ptr_strtod) == 0)
           {
            i_count_fields++;
           }
           break;
          case 2:
           point_z=strtod(token, &ptr_strtod);
           if ((int)strlen(ptr_strtod) == 0)
           {
            i_count_fields++;
           }
           break;
         }
        }
       }
       if (i_count_fields == i_count_fields_check)
       {
        if (i_count_in_loop < i_count_loop)
        {
         xx[i_count_in_loop]=point_x;
         yy[i_count_in_loop]=point_y;
         zz[i_count_in_loop]=point_z;
         i_count_in_loop++;
        }
        if (i_count_in_loop == i_count_loop)
        {
         dem_config->count_points=i_count_in_loop;
         if (!insert_dem_points(db_handle, dem_config, xx, yy, zz, verbose))
         {
          // Inserting failed, abort
          ret_insert = SQLITE_ABORT;
         }
         else
         {
          if (verbose)
          {
           fprintf(stderr,"\r inserted [%u] ... ", dem_config->dem_rows_count);
          }
         }
         // xx,yy,zz values are reset to 0.0 in insert_dem_points
         i_count_in_loop=0;
         sleep(i_sleep);
        }
       }
       else
       {
        ret_insert = SQLITE_ABORT;
       }
       if (ret_insert == SQLITE_ABORT )
       {
        break;
       }
      } // End while
      fclose(xyz_file);
      // -- -- ---------------------------------- --
      // Complete what is left over, when no abort
      // -- -- ---------------------------------- --
      if (ret_insert != SQLITE_ABORT )
      {
       ret=1;
       if ( i_count_in_loop < i_count_loop)
       {
        ret=0;
        dem_config->count_points=i_count_in_loop;
        fprintf(stderr,"import_xyz: calling insert_dem_points: i_count_in_loop[%d].\n",dem_config->count_points);
        if (insert_dem_points(db_handle, dem_config, xx, yy, zz, verbose))
        {
         ret=1;
         if (verbose)
         {
          fprintf(stderr,"\r file%d: inserting completed [%u]\n",i_file_count, dem_config->dem_rows_count);
         }
        }
       }
      }
      // -- -- ---------------------------------- --
      // clean up
      // -- -- ---------------------------------- --
      if (xx)
      {
       free(xx);
       xx=NULL;
      }
      if (yy)
      {
       free(yy);
       yy=NULL;
      }
      if (zz)
      {
       free(zz);
       zz=NULL;
      }
     }
     else
     {
      if (verbose)
      {
       fprintf(stderr,"-E-> import_xyz: import.xyz file not found [%s]\n", xyz_path_filename);
      }
     }
    }
   }
   sqlite3_finalize( stmt );
  }
 }
// -- -- ---------------------------------- --
 return ret;
}
// -- -- ---------------------------------- --
// Recover Dem-Geometry with SpatialIndex
// - recovering a full Geometry Column
// -- -- ---------------------------------- --
static int
recover_geometry_dem(sqlite3 *db_handle, struct config_dem *dem_config, int verbose)
{
 /* recovering a full Geometry Column */
 int ret;
 char *err_msg;
 char *sql_statement = NULL;
 if (verbose)
 {
  fprintf(stderr,"Recovering Geometry:    %s(%s) as POINTZ with srid=%d\n", dem_config->dem_table,dem_config->dem_geometry, dem_config->dem_srid);
 }
 sql_statement = sqlite3_mprintf("SELECT RecoverGeometryColumn(%Q, %Q, %d, %Q, %Q)",
                                 dem_config->dem_table,dem_config->dem_geometry, dem_config->dem_srid, "POINT", "XYZ");
 ret = sqlite3_exec(db_handle, sql_statement, NULL, NULL, &err_msg);
 sqlite3_free(sql_statement);
 if (ret != SQLITE_OK)
 {
  if (verbose)
  {
   fprintf(stderr, "RecoverGeometryColumn error: %s\n", err_msg);
  }
  sqlite3_free(err_msg);
  return 0;
 }
 if (verbose)
 {
  fprintf(stderr, "Creating Spatial Index: %s(%s)\n", dem_config->dem_table,dem_config->dem_geometry);
 }
 sql_statement = sqlite3_mprintf("SELECT CreateSpatialIndex(%Q, %Q)", dem_config->dem_table,dem_config->dem_geometry);
 ret = sqlite3_exec(db_handle, sql_statement, NULL, NULL, &err_msg);
 sqlite3_free(sql_statement);
 if (ret != SQLITE_OK)
 {
  fprintf(stderr, "CreateSpatialIndex error: %s\n", err_msg);
  sqlite3_free(err_msg);
  return 0;
 }
 if (verbose)
 {
  fprintf(stderr,"UpdateLayerStatistics:  %s(%s)\n", dem_config->dem_table,dem_config->dem_geometry);
 }
 sql_statement = sqlite3_mprintf("SELECT UpdateLayerStatistics(%Q, %Q)", dem_config->dem_table,dem_config->dem_geometry);
 ret = sqlite3_exec(db_handle, sql_statement, NULL, NULL, &err_msg);
 sqlite3_free(sql_statement);
 if (ret != SQLITE_OK)
 {
  fprintf(stderr, "UpdateLayerStatistics error: %s\n", err_msg);
  sqlite3_free(err_msg);
  return 0;
 }
 return 1;
}
// -- -- ---------------------------------- --
// Create the Database for Dem
// - CREATE TABLE for minimal Dem-Data
// -> db_memory.xyz_files
// -- -- ---------------------------------- --
static int
create_dem_db(const char *path_dem, sqlite3 ** handle, void *cache, const char *table_dem, const char *column_dem, int verbose)
{
 /* opening the DB */
 sqlite3 *db_handle = NULL;
 int ret=0;
 char *sql_statement = NULL;
 *handle = NULL;
 if ( verbose )
 {
  fprintf(stderr,"SQLite version: %s\n", sqlite3_libversion());
  fprintf(stderr,"SpatiaLite version: %s\n\n", spatialite_version());
 }
 FILE *db_file = fopen(path_dem, "r");
 if (db_file != NULL)
 {
  fclose(db_file);
  return ret;
 }
 ret = sqlite3_open_v2(path_dem, &db_handle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
 if (ret != SQLITE_OK)
 {
  fprintf(stderr, "cannot open '%s': %s\n", path_dem, sqlite3_errmsg (db_handle));
  sqlite3_close(db_handle);
  return ret;
 }
 spatialite_init_ex(db_handle, cache, 0);
 spatialite_autocreate(db_handle);
 if ( (table_dem) && ( column_dem ) )
 {
  sql_statement = sqlite3_mprintf("CREATE TABLE \"%s\" ("
                                  "id_dem INTEGER PRIMARY KEY AUTOINCREMENT, "
                                  "point_x DOUBLE DEFAULT 0, "
                                  "point_y DOUBLE DEFAULT 0, "
                                  "point_z DOUBLE DEFAULT 0, "
                                  "%s BLOB DEFAULT NULL)"
                                  ,table_dem, column_dem);
  ret = sqlite3_exec(db_handle, sql_statement, NULL, NULL, NULL);
  if (ret != SQLITE_OK)
  {
   fprintf(stderr, "cannot CREATE Table: '%s' sql[%s]\n\t: %s\n", table_dem, sql_statement, sqlite3_errmsg (db_handle));
   sqlite3_free(sql_statement);
   sqlite3_close(db_handle);
   return ret;
  }
  sqlite3_free(sql_statement);
  if (verbose)
  {
   fprintf(stderr,"Created table(geometry):  %s(%s)\n", table_dem, column_dem);
  }
  sql_statement = sqlite3_mprintf("ATTACH DATABASE ':memory:' AS db_memory");
  ret = sqlite3_exec(db_handle, sql_statement, NULL, NULL, NULL);
  sqlite3_free(sql_statement);
  if (ret == SQLITE_OK)
  {
   sql_statement = sqlite3_mprintf("CREATE TABLE db_memory.xyz_files ("
                                   "file_name TEXT DEFAULT '', "
                                   "point_x DOUBLE DEFAULT 0, "
                                   "point_y DOUBLE DEFAULT 0)");
   ret = sqlite3_exec(db_handle, sql_statement, NULL, NULL, NULL);
   sqlite3_free(sql_statement);
   if (ret != SQLITE_OK)
   {
   }
  }
 }
 *handle = db_handle;
 ret=1;
 return ret;
}
// -- -- ---------------------------------- --
// Close the Database
// - DETACH a connected Database if needed
// -> db_memory
// -- -- ---------------------------------- --
static void
close_db(sqlite3 *db_handle, void *cache, const char *schema_dem)
{
 char *sql_statement = NULL;
 int ret=0;
 if (schema_dem)
 {
  if (strcmp(schema_dem, "main") != 0)
  {
   sql_statement = sqlite3_mprintf("DETACH  DATABASE  %s", schema_dem);
   ret = sqlite3_exec(db_handle, sql_statement, NULL, NULL, NULL);
   sqlite3_free(sql_statement);
  }
 }
 sql_statement = sqlite3_mprintf("DETACH  DATABASE  db_memory");
 ret = sqlite3_exec(db_handle, sql_statement, NULL, NULL, NULL);
 if (ret == SQLITE_OK)
 {
  // suppressing stupid compiler warnings
 }
 sqlite3_free(sql_statement);
 sqlite3_close(db_handle);
 if (cache)
 {
  spatialite_cleanup_ex(cache);
 }
 spatialite_shutdown();
 return;
}
// -- -- ---------------------------------- --
// Open the Database
// - ATTACH a connected Database if needed
// While 'sniffing' a second source may not be needed
// - CREATE TABLE for minimal Dem-Data
// -> db_memory.xyz_files
// -- -- ---------------------------------- --
static int
open_db(sqlite3 **handle, void *cache, struct config_dem *source_config, struct config_dem *dem_config, int verbose)
{
 /* opening the DB */
 sqlite3 *db_handle = NULL;
 int ret=0;
 char *sql_statement = NULL;
 const char *path_db=NULL;
 const char *path_attach=NULL;
 const char *schema_attach=NULL;
 *handle = NULL;
 if ( verbose )
 {
  fprintf(stderr,"SQLite version: %s\n", sqlite3_libversion());
  fprintf(stderr,"SpatiaLite version: %s\n\n", spatialite_version());
 }
 if ((strlen(dem_config->dem_path) > 0) && ( (source_config) && (strlen(source_config->dem_path) == 0)))
 {
  // Open the Dem-Database as source [sniff without source or fetchz]
  path_db=dem_config->dem_path;
  dem_config->schema="main";
  schema_attach=dem_config->schema;
 }
 else
 {
  path_db=source_config->dem_path;
  path_attach=dem_config->dem_path;
  schema_attach=dem_config->schema;
 }
 ret = sqlite3_open_v2(path_db, &db_handle, SQLITE_OPEN_READWRITE, NULL);
 if (ret != SQLITE_OK)
 {
  fprintf(stderr, "cannot open '%s': %s\n", path_db, sqlite3_errmsg (db_handle));
  close_db(db_handle, cache, NULL);
  return 0;
 }
 if (path_attach)
 {
  sql_statement = sqlite3_mprintf("ATTACH DATABASE \"%s\" AS %s",path_attach, schema_attach);
  ret = sqlite3_exec(db_handle, sql_statement, NULL, NULL, NULL);
  if (ret != SQLITE_OK)
  {
   fprintf(stderr, "cannot ATTACH Database: '%s' sql[%s]\n\t: %s\n", path_attach, sql_statement, sqlite3_errmsg (db_handle));
   sqlite3_free(sql_statement);
   close_db(db_handle, cache, NULL);
   return 0;
  }
  sqlite3_free(sql_statement);
 }
 sql_statement = sqlite3_mprintf("ATTACH DATABASE ':memory:' AS db_memory");
 ret = sqlite3_exec(db_handle, sql_statement, NULL, NULL, NULL);
 sqlite3_free(sql_statement);
 if (ret == SQLITE_OK)
 {
  sql_statement = sqlite3_mprintf("CREATE TABLE db_memory.xyz_files ("
                                  "file_name TEXT DEFAULT '', "
                                  "point_x DOUBLE DEFAULT 0, "
                                  "point_y DOUBLE DEFAULT 0)");
  ret = sqlite3_exec(db_handle, sql_statement, NULL, NULL, NULL);
  sqlite3_free(sql_statement);
  if (ret != SQLITE_OK)
  {
  }
 }
 spatialite_init_ex(db_handle, cache, 0);
 *handle = db_handle;
 return 1;
}
// -- -- ---------------------------------- --
// Help Messages
// -- -- ---------------------------------- --
static void
do_help()
{
 /* printing the argument list */
 fprintf(stderr, "\n\nusage: spatialite_dem ARGLIST\n");
 fprintf(stderr,  "==============================================================\n");
 fprintf(stderr,  "-h or --help                    print this help message\n");
 fprintf(stderr, "========================== Parameters ========================\n");
 fprintf(stderr, "  -- -- ---------------- Dem-Data Database ---------------- --\n");
 fprintf(stderr, "-ddem or --dem-path  pathname to the SpatiaLite Dem DB \n");
 fprintf(stderr, "-tdem or --table-dem table_name [SpatialTable or SpatialView]\n");
 fprintf(stderr, "-gdem or --geometry-dem-column col_name the Geometry column\n");
 fprintf(stderr, "\t must be a POINT Z or a POINT ZM type\n");
 fprintf(stderr, "-rdem or --dem-resolution of the dem points while searching\n");
 fprintf(stderr, "\t the automatic resolution calculation is based on the row_count\n");
 fprintf(stderr, "\t within the extent, which may not be correct!\n");
 fprintf(stderr, "\t Use '-rdem' to set a realistic value\n");
 fprintf(stderr, "\n  -- -- -------------- Source-Update-Database ----------------- --\n");
 fprintf(stderr, "-d or --db-path pathname to the SpatiaLite DB\n");
 fprintf(stderr, "-t or --table table_name,  must be a SpatialTable\n");
 fprintf(stderr, "-g or --geometry-column the Geometry column to update\n");
 fprintf(stderr, "\t must  be a Z or a ZM Dimension type\n\t use CastToXYZ(geom) or CastToXYZM(geom) to convert \n");
 fprintf(stderr, "  -- -- --------------- General Parameters ---------------- --\n");
 fprintf(stderr, "-mdem or --copy-m [0=no, 1= yes [default] if exists]\n");
 fprintf(stderr, "-default_srid or --srid for use with -fetchz\n");
 fprintf(stderr, "-fetchz_xy x- and y-value for use with -fetchz\n");
 fprintf(stderr, "-v or  --verbose messages during -updatez and -fetchz\n");
 fprintf(stderr, "-save_conf based on active -ddem , -tdem, -gdem and -srid when valid\n");
 fprintf(stderr, "\n  -- -- -------------------- Notes:  ---------------------- --\n");
 fprintf(stderr, "-I-> the Z value will be copied from the nearest point found\n");
 fprintf(stderr, "-I-> the Srid of the source Geometry and the Dem-POINT can be different\n");
 fprintf(stderr, "-I-> when -fetchz_xy is used in a bash script, -v should not be used\n");
 fprintf(stderr, "\t the z-value will then be returned as the result\n");
 fprintf(stderr, "\n  -- -- -------------------- Conf file:  ------------------- --\n");
 fprintf(stderr, "-I-> if 'SPATIALITE_DEM' is set with the path to a file\n");
 fprintf(stderr, "-I--> 'export SPATIALITE_DEM=/long/path/to/file/berlin_dhh92.conf'\n");
 fprintf(stderr, "-I-> then '-save_conf' save the config to that file\n");
 fprintf(stderr, "-I-> this file will be read on each application start, setting those values\n");
 fprintf(stderr, "-I--> the parameters for :\n");
 fprintf(stderr, "\t  which Dem-Database and Geometry and the default_srid to use for queries\n");
 fprintf(stderr, "\t  -> would then not be needed\n");
 fprintf(stderr, "\n  -- -- ---------------- Importing .xyz files:  ------------------- --\n");
 fprintf(stderr, "-I-> a single xyz.file or a directory containing .xyz files can be given\n");
 fprintf(stderr, "\t for directories: only files with the extension .xyz will be searched for\n");
 fprintf(stderr, "-I-> a single list.file inside a directory containing .xyz files can be given\n");
 fprintf(stderr, "\t each line containing the file-name that must exist in that directory\n");
 fprintf(stderr, "-I-> validty checks are done before importing xyz-files\n");
 fprintf(stderr, "\t the first line may contain only 3 double values (point_x/y/z)\n");
 fprintf(stderr, "\t if valid, the file-name and the point_x/y points are stored\n");
 fprintf(stderr, "\t when importing, the list will be read based of the y/x points\n");
 fprintf(stderr, "\n  -- -- ---------------- Sorting .xyz files:  ---------------------- --\n");
 fprintf(stderr, "-I->  xyz.files should be sorted:\n");
 fprintf(stderr, "\t y='South to North' and x='West to East': \n");
 fprintf(stderr, "\t sort -n -k2 -k1 input_file.xyz -o output_file.sort.xyz");
 fprintf(stderr, "\n=========================== Commands ===========================\n");
 fprintf(stderr, "-sniff   [default] analyse settings without UPDATE of z-values \n");
 fprintf(stderr, "-updatez Perform UPDATE of z-values \n");
 fprintf(stderr, "-fetchz Perform Query of z-values using  -fetchz_x_y and default_srid\n");
 fprintf(stderr, "\t will be assumed when using  -fetchz_x_y\n");
 fprintf(stderr, "-create_dem create Dem-Database using -ddem,-tdem, -gdem and -srid for the Database \n");
 fprintf(stderr, "\t -d as a dem.xyz file \n");
 fprintf(stderr, "-import_xyz import another .xyz file into a Dem-Database created with -create_dem \n");
 fprintf(stderr, "\t these points will not be sorted, but added to the end ");
 fprintf(stderr, "\n=========================== Sample ===========================\n");
 fprintf(stderr, "--> with 'SPATIALITE_DEM' set: \n");
 fprintf(stderr, "spatialite_dem -fetchz_xy  24700.55278283251 20674.74537357586\n");
 fprintf(stderr, "33.5600000 \n");
 fprintf(stderr,  "==============================================================\n");
}
// -- -- ---------------------------------- --
// Checking the status of the Dem-Database
// - used by differenct command types
// -- -- ---------------------------------- --
static int
command_check_source_db(sqlite3 *db_handle, struct config_dem*source_config, int verbose)
{
 int ret=0;
 int geometry_type=0;
// -- -- ---------------------------------- --
 if ((strlen(source_config->dem_path) > 0) && (strlen(source_config->dem_table) > 0) && (strlen(source_config->dem_geometry) > 0))
 {
  if (check_geometry_dimension(db_handle,source_config, &geometry_type,verbose))
  {
   if (verbose)
   {
    fprintf(stderr,"Source: srid %d\n", source_config->default_srid);
    fprintf(stderr,"Source: extent min x/y(%2.7f,%2.7f)\n\t       max x/y(%2.7f,%2.7f)\n",
            source_config->dem_extent_minx,source_config->dem_extent_miny,
            source_config->dem_extent_maxx,source_config->dem_extent_maxy);
    fprintf(stderr,"Source: rows_count(%s) %d\n",source_config->dem_geometry, source_config->dem_rows_count);
    fprintf(stderr,"Source: geometry_type(%d) has_z[%d]\n",geometry_type,source_config->has_z);
    fprintf(stderr,"Source: spatial_index_enabled[%d]\n",source_config->has_spatial_index);
   }
   if (source_config->is_spatial_table == 1)
   {
    if (source_config->has_z)
    {// The source Database Table and geometry-columns exists and contains a z-value dimension.
     ret = 0;
     if (verbose)
     {
      fprintf(stderr,"Source '%s'\n", source_config->dem_path);
      fprintf(stderr," will set %s(%s) Z-Values\n\tfrom nearest POINT found in\n",source_config->dem_table, source_config->dem_geometry);
     }
    }
    else
    {
     ret = -1;
     if (verbose)
     {
      fprintf(stderr, "DB '%s'\n", source_config->dem_path);
      fprintf(stderr, "TABLE[%s] or GEOMETRY-Column[%s] does not contained in a SpatialTable [will not update]\n",source_config->dem_table, source_config->dem_geometry);
      fprintf(stderr, "\t command_check_source_db failed: sorry, cowardly quitting\n\n");
     }
    }
   }
   else
   {
    ret = -1;
    if (verbose)
    {
     fprintf(stderr, "DB '%s'\n", source_config->dem_path);
     fprintf(stderr, "TABLE[%s] or GEOMETRY-Column[%s] does not contain a Z-Dimension\n",source_config->dem_table, source_config->dem_geometry);
     fprintf(stderr, "\t command_check_source_db failed: sorry, cowardly quitting\n\n");
    }
   }
  }
  else
  {// The source Database Table or geometry-columns does not exist.
   ret = -1;
   if (verbose)
   {
    fprintf(stderr, "DB '%s'\n", source_config->dem_path);
    fprintf(stderr, "TABLE[%s] or GEOMETRY-Column[%s] not found\n",source_config->dem_table, source_config->dem_geometry);
    fprintf(stderr, "\t check_geometry_dimension failed: sorry, cowardly quitting\n\n");
   }
  }
  if (ret == 0)
  {
   if (verbose)
   {
    fprintf(stderr, "Source Database: has passed all checks.\n\n");
   }
  }
 }
 else
 {
  if ((strlen(source_config->dem_path) > 0) && (strcmp(source_config->dem_path,".xyz") != 1))
  {
   if (verbose)
   {
    fprintf(stderr,"-E-> command_check_source_db: preconditions failed for check_source_db [%s(%s)]\n\t source[%s] \n",source_config->dem_table, source_config->dem_geometry,source_config->dem_path);
   }
  }
 }
// -- -- ---------------------------------- --
 return ret;
}
// -- -- ---------------------------------- --
// Checking the status of the Dem-Database
// - used by differenct command types
// -- -- ---------------------------------- --
static int
command_check_dem_db(sqlite3 *db_handle, struct config_dem*dem_config, struct config_dem*source_config, int verbose)
{
 int ret=0;
 double resolution_calc=0.0;
 double resolution_dem=dem_config->dem_resolution;
 int geometry_type=0;
// -- -- ---------------------------------- --
 if ((strlen(dem_config->dem_path) > 0) && (strlen(dem_config->dem_table) > 0) && (strlen(dem_config->dem_geometry) > 0))
 {
  if (check_geometry_dimension(db_handle,dem_config, &geometry_type, verbose))
  {
   if (dem_config->dem_rows_count)
   {
    resolution_calc=(dem_config->dem_extent_maxx-dem_config->dem_extent_minx)*(dem_config->dem_extent_maxy-dem_config->dem_extent_miny)/(double)dem_config->dem_rows_count;
   }
   if (verbose)
   {
    fprintf(stderr,"Dem: srid %d\n", dem_config->dem_srid);
    if ( (dem_config->default_srid > 0) &&  (dem_config->default_srid != dem_config->dem_srid) )
    {
     fprintf(stderr, "Dem: default_srid[%d]\n",dem_config->default_srid);
    }
    fprintf(stderr,"Dem: extent min x/y(%2.7f,%2.7f)\n\t    max x/y(%2.7f,%2.7f)\n",
            dem_config->dem_extent_minx,dem_config->dem_extent_miny,
            dem_config->dem_extent_maxx,dem_config->dem_extent_maxy);
    fprintf(stderr,"Dem: extent width(%2.7f)\n\t   height(%2.7f)\n",
            (dem_config->dem_extent_maxx-dem_config->dem_extent_minx),
            (dem_config->dem_extent_maxy-dem_config->dem_extent_miny));
    fprintf(stderr,"Dem: rows_count(%s) %u\n",dem_config->dem_geometry, dem_config->dem_rows_count);
    fprintf(stderr,"Dem: resolution(%s) %2.7f\n",dem_config->dem_geometry, resolution_calc);
    fprintf(stderr,"Dem: geometry_type(%d) has_z[%d] has_m[%d]\n",geometry_type,dem_config->has_z, dem_config->has_m);
    fprintf(stderr,"Dem: spatial_index_enabled[%d]\n",dem_config->has_spatial_index);
   }
   if (dem_config->has_z)
   {// The dem Database Table and geometry-columns exist and contains a z-value dimension.
    switch (geometry_type)
    {
     case GAIA_POINTZ:
     case GAIA_POINTZM:
      {
       if (dem_config->has_spatial_index == 1)
       {
        if ( (source_config) && (strlen(source_config->dem_path) > 0) && (strlen(source_config->dem_table) > 0))
        {// No printing when .xyz file
         if (verbose)
         {
          fprintf(stderr,"Source '%s'\n", source_config->dem_path);
          fprintf(stderr," will set %s(%s) Z-Values\n\tfrom nearest POINT found in\n",source_config->dem_table, source_config->dem_geometry);
         }
        }
        if (verbose)
        {
         fprintf(stderr,"Dem '%s'\n", dem_config->dem_path);
         fprintf(stderr," TABLE[%s] with GEOMETRY-Column[%s]\n",dem_config->dem_table, dem_config->dem_geometry);
        }
        ret = 0;
       }
       else
       {
        if (verbose)
        {
         fprintf(stderr, "Dem '%s'\n", dem_config->dem_path);
         fprintf(stderr, "TABLE[%s] or GEOMETRY-Column[%s] must be a POINT with a Z-Dimension with a SpatialIndex\n",dem_config->dem_table, dem_config->dem_geometry);
         fprintf(stderr, "\t command_check_dem_db failed: sorry, cowardly quitting\n\n");
        }
        ret = -1;
       }
      }
      break;
     default:
      if (verbose)
      {
       fprintf(stderr, "Dem '%s'\n", dem_config->dem_path);
       fprintf(stderr, "TABLE[%s] or GEOMETRY-Column[%s] must be a POINT with a Z-Dimension\n",dem_config->dem_table, dem_config->dem_geometry);
       fprintf(stderr, "\t command_check_dem_db failed: sorry, cowardly quitting\n\n");
      }
      ret = -1;
      break;
    }
   }
   else
   {
    if (verbose)
    {
     fprintf(stderr, "Dem '%s'\n", dem_config->dem_path);
     fprintf(stderr,  "TABLE[%s] or GEOMETRY-Column[%s] does not contain a Z-Dimension\n",dem_config->dem_table, dem_config->dem_geometry);
     fprintf(stderr, "\t command_check_dem_db failed: sorry, cowardly quitting\n\n");
    }
    ret = -1;
   }
  }
  else
  {// The dem Database Table or geometry-columns does not exist.
   if (verbose)
   {
    fprintf(stderr, "Dem '%s'\n", dem_config->dem_path);
    fprintf(stderr, "TABLE[%s] or GEOMETRY-Column[%s] not found\n",dem_config->dem_table, dem_config->dem_geometry);
    fprintf(stderr, "\t check_geometry_dimension failed: sorry, cowardly quitting\n\n");
   }
   ret = -1;
  }
  if (ret == 0)
  {
   if (resolution_dem <= 0.0)
   {
    if (verbose)
    {
     fprintf(stderr, "-W-> -rdem was not set. Using: resolution(%s) %2.7f\n",dem_config->dem_geometry, resolution_calc);
    }
    resolution_dem=resolution_calc;
   }
   else
   {
    if (verbose)
    {
     fprintf(stderr, "-W-> -rdem was set. Using: resolution(%2.7f), overriding the calculated value: %2.7f\n",resolution_dem, resolution_calc);
    }
   }
   dem_config->dem_resolution=resolution_dem;
   if ((source_config) && (source_config->has_z))
   {
    dem_config->default_srid=source_config->default_srid;
    if (verbose)
    {
     if (dem_config->dem_srid == source_config->default_srid)
     {
      fprintf(stderr, "Dem srid[%d]: is the same as the Source srid[%d].\n", dem_config->dem_srid,dem_config->default_srid);
      if ( ( source_config->dem_extent_minx >= dem_config->dem_extent_minx ) && (source_config->dem_extent_maxx <= dem_config->dem_extent_maxx ) &&
           ( source_config->dem_extent_miny >= dem_config->dem_extent_miny ) && (source_config->dem_extent_maxy <= dem_config->dem_extent_maxy ) )
      {
       fprintf(stderr, "The Source[%s]: is totally within the Dem[%s] area.\n", source_config->dem_geometry,dem_config->dem_geometry);
      }
      else if ( ( source_config->dem_extent_minx < dem_config->dem_extent_minx ) && (source_config->dem_extent_maxx > dem_config->dem_extent_maxx ) &&
                ( source_config->dem_extent_miny < dem_config->dem_extent_miny ) && (source_config->dem_extent_maxy > dem_config->dem_extent_maxy ) )
      {
       fprintf(stderr, "The Source[%s]: is totally covers the Dem[%s] area.\n", source_config->dem_geometry,dem_config->dem_geometry);
       fprintf(stderr, "\t only geometries totally within the Dem area will be updated.\n");
      }
      else if ( ( ( source_config->dem_extent_minx < dem_config->dem_extent_minx ) || ( source_config->dem_extent_minx > dem_config->dem_extent_maxx ) ) &&
                ( ( source_config->dem_extent_maxx > dem_config->dem_extent_maxx ) || ( source_config->dem_extent_maxx < dem_config->dem_extent_minx ) ) &&
                ( ( source_config->dem_extent_miny < dem_config->dem_extent_miny ) || (source_config->dem_extent_miny > dem_config->dem_extent_maxy) ) &&
                ( ( source_config->dem_extent_maxy > dem_config->dem_extent_maxy ) || ( source_config->dem_extent_maxy < dem_config->dem_extent_miny ) ) )
      {
       // ?? correct ??
       fprintf(stderr, "The Dem[%s]: is totally outside of the Source[%s] area.\n", dem_config->dem_geometry,source_config->dem_geometry);
      }
      else
      {
       fprintf(stderr, "The Source[%s]: is partially inside of the Dem[%s] area.\n", source_config->dem_geometry,dem_config->dem_geometry);
      }
     }
     else
     {
      fprintf(stderr, "Dem default_srid[%d]: is different from the Source default_srid[%d].\n", dem_config->dem_srid,dem_config->default_srid);
      fprintf(stderr, "\t When searching for the nearest point, the Source points will be transformed to srid[%d].\n", dem_config->dem_srid);
     }
     fprintf(stderr, "Dem Database: has passed all checks.\n");
    }
   }
   ret=1;
  }
 }
 else
 {
  if (strlen(dem_config->dem_path) > 0)
  {
   if (verbose)
   {
    fprintf(stderr,"-E-> command_check_dem_db: preconditions failed [%s(%s)] \n",dem_config->dem_table, dem_config->dem_geometry);
   }
  }
 }
// -- -- ---------------------------------- --
 return ret;
}
// -- -- ---------------------------------- --
// Implementation of command: updatez
// - from a Table, geometry of Dem
// --> update each z value with z value of
// --> nearest Point of Dem
// -- -- ---------------------------------- --
static int
command_updatez_db(sqlite3 *db_handle, struct config_dem*source_config, struct config_dem*dem_config,  int verbose)
{
 int ret=0;
 char *time_message = NULL;
 struct timeval time_start;
 struct timeval time_end;
 struct timeval time_diff;
 char *sql_err = NULL;
 int count_total_geometries=0;
 int count_changed_geometries=0;
 int count_points_total=0;
 int count_z_total=0;
 int count_m_total=0;

 if ((strlen(dem_config->dem_path) > 0) && (strlen(dem_config->dem_table) > 0) && (strlen(dem_config->dem_geometry) > 0) &&
     (dem_config->dem_srid > 0) && (dem_config->has_z) &&
     (strlen(source_config->dem_path) > 0) && (strlen(source_config->dem_table) > 0) && (strlen(source_config->dem_geometry) > 0) &&
     (source_config->default_srid > 0) && (source_config->has_z))
 {// The dem Database Table and geometry-columns exist and contains a z-value dimension.
  // -- -- ---------------------------------- --
  if (verbose)
  {
   fprintf(stderr,"-I-> starting update of [%s(%s)] where Z-Values are different.\n",source_config->dem_table, source_config->dem_geometry);
  }
  /* ok, going to convert */
  /* the complete operation is handled as an unique SQL Transaction */
  gettimeofday(&time_start, 0);
  if (sqlite3_exec(db_handle, "BEGIN", NULL, NULL, &sql_err) == SQLITE_OK)
  {
   if (retrieve_geometries(db_handle, source_config, dem_config, &count_total_geometries,&count_changed_geometries,&count_points_total,&count_z_total,&count_m_total, verbose) )
   {
    /* committing the pending SQL Transaction */
    if (sqlite3_exec(db_handle, "COMMIT", NULL, NULL, &sql_err) == SQLITE_OK)
    {
     ret = 0;
    }
    else
    {
     if (verbose)
     {
      fprintf(stderr, "COMMIT TRANSACTION error: %s\n", sql_err);
     }
     sqlite3_free(sql_err);
     if (sql_err)
     {
      sqlite3_free(sql_err);
     }
     ret = -1;
    }
   }
   else
   {
    ret = -1;
    if (sqlite3_exec(db_handle, "ROLLBACK", NULL, NULL, &sql_err) == SQLITE_OK)
    {
    }
    if (sql_err)
    {
     sqlite3_free(sql_err);
    }
    if (verbose)
    {
     fprintf(stderr, "DB '%s'\n", source_config->dem_path);
     fprintf(stderr, "TABLE[%s] or GEOMETRY-Column[%s] error during UPDATE\n",source_config->dem_table, source_config->dem_geometry);
     fprintf(stderr, "*** ERROR: conversion failed\n\n");
    }
   }
   gettimeofday(&time_end, 0);
   timeval_subtract(&time_diff,&time_end,&time_start,&time_message);
   if (ret == 0)
   {
    if (verbose)
    {
     fprintf(stderr,"-I-> geometries total[%d] changed[%d] ; points total[%d] changed z[%d] changed m[%d]\n",count_total_geometries,count_changed_geometries,count_points_total,count_z_total,count_m_total);
     fprintf(stderr,"\tDatabase-file successfully updated found, changed, Z-Values !!!\n");
     fprintf(stderr,"%s\n\n", time_message);
    }
   }
  }
  else
  {
   if (verbose)
   {
    fprintf(stderr, "BEGIN TRANSACTION error: %s\n", sql_err);
   }
   sqlite3_free(sql_err);
   if (sql_err)
   {
    sqlite3_free(sql_err);
   }
   ret = -1;
  }
 }
 else
 {// preconditions not fulfilled
  if (verbose)
  {
   fprintf(stderr,"-E-> command_updatez_db: preconditions failed [%s(%s)] \n",source_config->dem_table, source_config->dem_geometry);
  }
 }
// -- -- ---------------------------------- --
 if (time_message)
 {
  sqlite3_free(time_message);
  time_message = NULL;
 }
// -- -- ---------------------------------- --
 return ret;
}
// -- -- ---------------------------------- --
// Implementation of command: fetchz
// - from a given srid, point_x,point_y
// --> return point_z value
// -- -- ---------------------------------- --
static int
command_fetchz(sqlite3 *db_handle, struct config_dem *dem_config, int verbose)
{
 int ret=0;
 char *time_message = NULL;
 struct timeval time_start;
 struct timeval time_end;
 struct timeval time_diff;
// -- -- ---------------------------------- --
 if ( (dem_config->fetchz_x != 0.0) && (dem_config->fetchz_x != dem_config->fetchz_y) && (dem_config->default_srid > 0)  && (dem_config->dem_srid > 0))
 {
  if (verbose)
  {
   fprintf(stderr, "FetchZ modus: with default_srid[%d]  x[%2.7f] y[%2.7f] has_m[%d]\n",dem_config->default_srid,dem_config->fetchz_x,dem_config->fetchz_y,dem_config->has_m);
  }
  gettimeofday(&time_start, 0);
  if (callFetchZ(db_handle,dem_config,verbose) )
  {
   ret=1;
   gettimeofday(&time_end, 0);
   timeval_subtract(&time_diff,&time_end,&time_start,&time_message);
   if (verbose)
   {
    if (dem_config->has_m)
    {
     fprintf(stderr, "FetchZ modus: with     dem_srid[%d] x[%2.7f] y[%2.7f] z[%2.7f] m[%2.7f]\n",dem_config->dem_srid,dem_config->fetchz_x,dem_config->fetchz_y,dem_config->dem_z,dem_config->dem_m);
     fprintf(stderr,"%s\n", time_message);
    }
    else
    {
     fprintf(stderr, "FetchZ modus: with     dem_srid[%d] x[%2.7f] y[%2.7f] z[%2.7f]\n",dem_config->dem_srid,dem_config->fetchz_x,dem_config->fetchz_y,dem_config->dem_z);
     fprintf(stderr,"%s\n", time_message);
    }
   }
   else
   {// Output for bash
    if (dem_config->has_m)
    {
     printf("%2.7f %2.7f\n", dem_config->dem_z,dem_config->dem_m);
    }
    else
    {
     printf("%2.7f\n", dem_config->dem_z);
    }
   }
  }
  else
  {
   // callFetchZ failed
  }
 }
 else
 {
  // preconditions failed
  if (verbose)
  {
   if ( dem_config->fetchz_x == 0.0)
   {
    fprintf(stderr, "did you forget setting the -fetchz_x argument ?\n");
   }
   if ( dem_config->fetchz_y == 0.0)
   {
    fprintf(stderr, "did you forget setting the -fetchz_y argument ?\n");
   }
   if ( dem_config->default_srid <= 0)
   {
    fprintf(stderr, "did you forget setting the -default_srid argument ?\n");
   }
   if ( dem_config->dem_srid <= 0)
   {
    fprintf(stderr, "The dem-srid is invalid\n");
   }
   fprintf(stderr, "-E command_fetchz: sorry, cowardly quitting\n\n");
  }
 }
// -- -- ---------------------------------- --
 if (time_message)
 {
  sqlite3_free(time_message);
  time_message = NULL;
 }
// -- -- ---------------------------------- --
 return ret;
}
// -- -- ---------------------------------- --
// Implementation of command: fetchz
// - from a given srid, point_x,point_y
// --> return point_z value
// -- -- ---------------------------------- --
static int
command_dem_create(sqlite3 **db_handle, void *cache, struct config_dem *source_config, struct config_dem *dem_config, int verbose)
{
 int ret=0;
 char *time_message = NULL;
 struct timeval time_start;
 struct timeval time_end;
 struct timeval time_diff;
 int count_xyz_files=0;
// -- -- ---------------------------------- --
 if (cache)
 {
  if ( dem_config->dem_srid <= 0)
  {
   dem_config->dem_srid=dem_config->default_srid;
  }
  if ((strlen(dem_config->dem_path) > 0) && (strlen(dem_config->dem_table) > 0) && (strlen(dem_config->dem_geometry) > 0) && (dem_config->dem_srid > 0))
  {
   gettimeofday(&time_start, 0);
   if (create_dem_db(dem_config->dem_path, db_handle, cache, dem_config->dem_table, dem_config->dem_geometry,verbose))
   {
    if (verbose)
    {
     fprintf(stderr,"-I-> command_dem_createt: created [%s] \n", dem_config->dem_path);
    }
    if (collect_xyz_files(*db_handle,source_config->dem_path, &count_xyz_files, 0) == 1)
    {
     dem_config->dem_rows_count=0;
     if (import_xyz(*db_handle, dem_config,count_xyz_files,verbose))
     {// Import completed correctly
      gettimeofday(&time_end, 0);
      timeval_subtract(&time_diff,&time_end,&time_start,&time_message);
      if (verbose)
      {
       fprintf(stderr,"%s\n", time_message);
      }
      gettimeofday(&time_start, 0);
      if (recover_geometry_dem(*db_handle, dem_config,verbose))
      {// Task completed correctly
      }
      else
      {// Task failed
       if (verbose)
       {
        fprintf(stderr,"-W-> command_dem_created: recover_geometry_dem failed [%s(%s)]  srid[%d]  \n", dem_config->dem_table, dem_config->dem_geometry, dem_config->dem_srid);
       }
      }
      // Sniff the results, set schema_dem to 'main'
      dem_config->schema=source_config->schema;
      source_config->schema=NULL;
      gettimeofday(&time_end, 0);
      timeval_subtract(&time_diff,&time_end,&time_start,&time_message);
      if (verbose)
      {
       fprintf(stderr,"%s\n", time_message);
      }
     }
     else
     {// Import failed
      if (verbose)
      {
       fprintf(stderr,"-W-> command_dem_created: import_xyz failed [%d] [%s] \n", count_xyz_files,source_config->dem_path);
      }
     }
    }
   }
   else
   {
    // Database exits or cannot be created
    if (verbose)
    {
     fprintf(stderr, "Dem '%s'\n", dem_config->dem_path);
     fprintf(stderr, "Database exists and will not be overwritten, use -import_xyz to add new data\n");
     fprintf(stderr, "-E-> command_dem_create: sorry, cowardly quitting\n\n");
    }
   }
  }
  else
  {
   // preconditions failed
   if (verbose)
   {
    if (strlen(dem_config->dem_path) <= 0)
    {
     fprintf(stderr, "did you forget setting the -ddem  argument ?\n");
    }
    if (strlen(dem_config->dem_table) <= 0)
    {
     fprintf(stderr, "did you forget setting the -tdem  argument ?\n");
    }
    if (strlen(dem_config->dem_geometry) <= 0)
    {
     fprintf(stderr, "did you forget setting the -gdem  argument ?\n");
    }
    if ( dem_config->default_srid <= 0)
    {
     fprintf(stderr, "did you forget setting the -default_srid argument ?\n");
    }
    if ( dem_config->dem_srid <= 0)
    {
     fprintf(stderr, "The dem-srid is invalid\n");
    }
    fprintf(stderr, "-E command_fetchz: sorry, cowardly quitting\n\n");
   }
  }
 }
// -- -- ---------------------------------- --
 if (time_message)
 {
  sqlite3_free(time_message);
  time_message = NULL;
 }
// -- -- ---------------------------------- --
 return ret;
}
// -- -- ---------------------------------- --
// Implementation of command: -import_xyz
// - from a given srid, point_x,point_y
// --> return point_z value
// -- -- ---------------------------------- --
static int
command_import_xyz(sqlite3 *db_handle, struct config_dem *source_config, struct config_dem *dem_config, int verbose)
{
 int ret=0;
 char *time_message = NULL;
 char *sql_statement = NULL;
 char *err_msg = NULL;;
 struct timeval time_start;
 struct timeval time_end;
 struct timeval time_diff;
 int count_xyz_files=0;
// -- -- ---------------------------------- --
 if ((strlen(source_config->dem_path) > 0) && (strlen(dem_config->dem_path) > 0) && (strlen(dem_config->dem_table) > 0) && (strlen(dem_config->dem_geometry) > 0))
 {
  if ((dem_config->has_z) && (dem_config->dem_srid > 0))
  {
   if (db_handle)
   {
    gettimeofday(&time_start, 0);
    if (verbose)
    {
     fprintf(stderr, "-import_xyz: with srid[%d] .xyz[%s] \n",source_config->default_srid,source_config->dem_path);
    }
    if (collect_xyz_files(db_handle,source_config->dem_path, &count_xyz_files, 0) == 1)
    {
     dem_config->dem_rows_count=0; // Set to 0, just in case
     if (import_xyz(db_handle, dem_config,count_xyz_files,verbose))
     {// Import completed correctly
      gettimeofday(&time_end, 0);
      timeval_subtract(&time_diff,&time_end,&time_start,&time_message);
      if (verbose)
      {
       fprintf(stderr,"%s\n", time_message);
      }
      gettimeofday(&time_start, 0);
      if (verbose)
      {
       fprintf(stderr,"UpdateLayerStatistics:  %s(%s)\n", dem_config->dem_table,dem_config->dem_geometry);
      }
      sql_statement = sqlite3_mprintf("SELECT UpdateLayerStatistics(%Q, %Q)", dem_config->dem_table,dem_config->dem_geometry);
      int ret_update = sqlite3_exec(db_handle, sql_statement, NULL, NULL, &err_msg);
      sqlite3_free(sql_statement);
      if (ret_update != SQLITE_OK)
      {
       fprintf(stderr, "UpdateLayerStatistics error: %s\n", err_msg);
       sqlite3_free(err_msg);
      }
      else
      {
       ret=1;
      }
      gettimeofday(&time_end, 0);
      timeval_subtract(&time_diff,&time_end,&time_start,&time_message);
      if (verbose)
      {
       fprintf(stderr,"%s\n", time_message);
      }
     }
    }
   }
  }
 }
// -- -- ---------------------------------- --
 if (time_message)
 {
  sqlite3_free(time_message);
  time_message = NULL;
 }
// -- -- ---------------------------------- --
 return ret;
}
// -- -- ---------------------------------- --
// Main
// Commands
// - sniff
// -> allows the user to prepair the 'update' command
// -> Source and Dem can be done separately or together
// - update
// -- -- ---------------------------------- --
int
main(int argc, char *argv[])
{
 /* the MAIN function simply perform arguments checking */
 sqlite3 *db_handle = NULL;
 char *schema_db = "main";
 char *schema_dem = "db_dem";
 char *dem_geometry_default = "dem_point";
 void *cache = NULL;
 int verbose=0;
 int copy_m = 1;
 int next_arg = ARG_NONE;
 int i_command_type=CMD_DEM_SNIFF;
 int i_sniff_on=0;
 struct config_dem dem_config;
 struct config_dem source_config;
 int save_conf=0;
 int exit_code=1; // unix_exit_code: 0=correct, 1=error
 int i=0;
 int error = 0;
// -- -- ---------------------------------- --
// Will look for conf [not an error if nothing found]
// - if not found, all arguments must be set
// -- -- ---------------------------------- --
 char *dem_configfile =  "spatialite_dem.conf";
 char *spatialite_dem = getenv("SPATIALITE_DEM");
// -- -- ---------------------------------- --
// Reading the configuration, if found
// - setting default values
// -- -- ---------------------------------- --
 if (spatialite_dem)
 {
  dem_configfile=spatialite_dem;
 }
// -- -- ---------------------------------- --
// Warning, if non default, conf is given but not found
// -- -- ---------------------------------- --
 dem_config = get_demconfig(dem_configfile,1);
 dem_config.config_type = CONF_TYPE_DEM; // dem
 dem_config.schema = schema_dem;  // dem
// -- -- ---------------------------------- --
// No external source config
// - returns default values only
// -- -- ---------------------------------- --
 source_config = get_demconfig(NULL,0);
 source_config.config_type = CONF_TYPE_SOURCE; // source
 source_config.schema = schema_db; // source
// -- -- ---------------------------------- --
 if (strlen(dem_config.dem_path) > 0)
 {
  if (dem_config.dem_srid > 0)
  {
   source_config.dem_srid=dem_config.dem_srid;
  }
  if (dem_config.default_srid > 0)
  {
   source_config.default_srid=dem_config.default_srid;
  }
 }
// -- -- ---------------------------------- --
// Reading the arguments
// -- -- ---------------------------------- --
 for (i = 1; i < argc; i++)
 {
  // parsing the invocation arguments
  if (next_arg != ARG_NONE)
  {
   switch (next_arg)
   {
    case ARG_DB_PATH:
     strcpy(source_config.dem_path,argv[i]);
     break;
    case ARG_TABLE:
     strcpy(source_config.dem_table,argv[i]);
     break;
    case ARG_COL:
     strcpy(source_config.dem_geometry,argv[i]);
     break;
    case ARG_DEM_PATH:
     strcpy(dem_config.dem_path,argv[i]);
     break;
    case ARG_TABLE_DEM:
     strcpy(dem_config.dem_table,argv[i]);
     break;
    case ARG_COL_DEM:
     strcpy(dem_config.dem_geometry,argv[i]);
     break;
    case ARG_RESOLUTION_DEM:
     // this will override the calculated value (which may not be correct)
     // - it also gives the user the choice to change the area around a point to search for.
     dem_config.dem_resolution = atof(argv[i]);
     break;
    case ARG_COPY_M:
     copy_m = atoi(argv[i]);
     if (copy_m != 1 )
      copy_m=0;
     break;
    case ARG_FETCHZ_X:
     dem_config.fetchz_x = atof(argv[i]);
     break;
    case ARG_FETCHZ_Y:
     dem_config.fetchz_y = atof(argv[i]);
     break;
    case ARG_FETCHZ_XY:
     dem_config.fetchz_x = atof(argv[i++]);
     dem_config.fetchz_y = atof(argv[i]);
     break;
    case ARG_DEFAULT_SRID:
     source_config.default_srid = atoi(argv[i]);
     dem_config.default_srid = atoi(argv[i]);
     break;
   };
   next_arg = ARG_NONE;
   continue;
  }
  if (strcasecmp (argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
  {
   do_help ();
   return exit_code;
  }
  if (strcmp(argv[i], "-d") == 0)
  {
   next_arg = ARG_DB_PATH;
   continue;
  }
  if (strcasecmp (argv[i], "--db-path") == 0)
  {
   next_arg = ARG_DB_PATH;
   continue;
  }
  if (strcasecmp (argv[i], "--table") == 0)
  {
   next_arg = ARG_TABLE;
   continue;
  }
  if (strcmp(argv[i], "-t") == 0)
  {
   next_arg = ARG_TABLE;
   continue;
  }
  if (strcasecmp (argv[i], "--geometry-column") == 0)
  {
   next_arg = ARG_COL;
   continue;
  }
  if (strcmp(argv[i], "-g") == 0)
  {
   next_arg = ARG_COL;
   continue;
  }
  if (strcasecmp (argv[i], "--dem-path") == 0)
  {
   next_arg = ARG_DEM_PATH;
   continue;
  }
  if (strcmp(argv[i], "-ddem") == 0)
  {
   next_arg = ARG_DEM_PATH;
   continue;
  }
  if (strcasecmp (argv[i], "--table-dem") == 0)
  {
   next_arg = ARG_TABLE_DEM;
   continue;
  }
  if (strcmp(argv[i], "-tdem") == 0)
  {
   next_arg = ARG_TABLE_DEM;
   continue;
  }
  if (strcasecmp (argv[i], "--geometry-dem-column") == 0)
  {
   next_arg = ARG_COL_DEM;
   continue;
  }
  if (strcmp(argv[i], "-gdem") == 0)
  {
   next_arg = ARG_COL_DEM;
   continue;
  }
  if (strcasecmp (argv[i], "--dem-resolution") == 0)
  {
   next_arg = ARG_RESOLUTION_DEM;
   continue;
  }
  if (strcmp(argv[i], "-rdem") == 0)
  {
   next_arg = ARG_RESOLUTION_DEM;
   continue;
  }
  if (strcasecmp (argv[i], "--m-copy") == 0)
  {
   next_arg = ARG_COPY_M;
   continue;
  }
  if (strcmp(argv[i], "-mdem") == 0)
  {
   next_arg = ARG_COPY_M;
   continue;
  }
  if (strcmp(argv[i], "-sniff") == 0)
  {
   i_command_type=CMD_DEM_SNIFF;
   i_sniff_on=1;
   continue;
  }
  if (strcmp(argv[i], "-updatez") == 0)
  {
   i_command_type=CMD_DEM_UPDATEZ;
   continue;
  }
  if (strcmp(argv[i], "-fetchz") == 0)
  {
   i_command_type=CMD_DEM_FETCHZ;
   continue;
  }
  if (strcmp(argv[i], "-create_dem") == 0)
  {
   i_command_type=CMD_DEM_CREATE;
   continue;
  }
  if (strcmp(argv[i], "-import_xyz") == 0)
  {
   i_command_type=CMD_DEM_IMPORT_XYZ;
   continue;
  }
  if (strcmp(argv[i], "-fetchz_x") == 0)
  {
   next_arg = ARG_FETCHZ_X;
   continue;
  }
  if (strcmp(argv[i], "-fetchz_y") == 0)
  {
   next_arg = ARG_FETCHZ_Y;
   continue;
  }
  if (strcmp(argv[i], "-fetchz_xy") == 0)
  {
   next_arg = ARG_FETCHZ_XY;
   continue;
  }
  if ( (strcmp(argv[i], "-default_srid") == 0) ||  (strcmp(argv[i], "--srid") == 0) )
  {
   next_arg = ARG_DEFAULT_SRID;
   continue;
  }
  if ( (strcmp(argv[i], "-v") == 0) ||  (strcmp(argv[i], "--verbose") == 0) )
  {
   verbose = 1;
   continue;
  }
  if ( (strcmp(argv[i], "-save_conf") == 0) ||  (strcmp(argv[i], "--dem_conf") == 0) )
  {
   save_conf=1;
   continue;
  }
  fprintf(stderr, "unknown argument: %s\n", argv[i]);
  error = 1;
 }
// -- -- ---------------------------------- --
// Setting the default argument of dem_geometry
// - dem_point
// -- -- ---------------------------------- --
 if (strlen(dem_config.dem_geometry) == 0)
 {
  strcpy(dem_config.dem_geometry,dem_geometry_default);
 }
// -- -- ---------------------------------- --
// checking, resetting the arguments
// -- -- ---------------------------------- --
 if ( (i_command_type == CMD_DEM_SNIFF) || (i_sniff_on == 1) )
 {
  if ((strlen(dem_config.dem_path) > 0) && (strlen(dem_config.dem_table) > 0) && (strlen(dem_config.dem_geometry) > 0) &&
      (dem_config.fetchz_x != 0.0) && (dem_config.fetchz_x != dem_config.fetchz_y) )
  {// -fetchz was intended but forgotten, be tolerant to the lazy user
   i_command_type = CMD_DEM_FETCHZ;
  }
  // for -sniff -v is always active
  verbose=1;
 }
 if (verbose)
 {
  if (strlen(dem_config.dem_path) == 0)
  {
   if (i_command_type == CMD_DEM_UPDATEZ)
   {
    fprintf(stderr, "did you forget setting the --dem-path argument ?\n");
    error = 1;
   }
   else
   {
    fprintf(stderr, "Warning: --dem-path argument has not been set [assuming -sniff only]\n");
   }
  }
  if (strlen(source_config.dem_path) == 0)
  {
   if (i_command_type == CMD_DEM_UPDATEZ)
   {
    fprintf(stderr, "did you forget setting the --db-path argument ?\n");
    error = 1;
   }
  }
 }
// -- -- ---------------------------------- --
// Bale out on errors
// -- -- ---------------------------------- --
 if (error)
 {
  do_help();
  return exit_code;
 }
// -- -- ---------------------------------- --
// opening the DB
// - method 1: create a new Database
// - method 2: input is not a Database, only Dem
// - method 3: both input and dem are a Database, when given
// -- -- ---------------------------------- --
 cache = spatialite_alloc_connection();
 if (i_command_type == CMD_DEM_CREATE)
 {
  if (command_dem_create(&db_handle, cache, &source_config, &dem_config, verbose))
  {
   // Sniff the results, set schema_dem to 'main'
   i_command_type = CMD_DEM_SNIFF;
   dem_config.schema=schema_db;
  }
 }
 else
 {
  if (i_command_type == CMD_DEM_IMPORT_XYZ)
  {// Open the Dem-Database as the main source [not attached ; since db_path=import.xyz]
   open_db(&db_handle, cache, NULL, &dem_config,verbose);
  }
  else
  {// Open the Dem-Database as the main source if there is no source [otherwise attached, with source as main ]
   open_db(&db_handle, cache, &source_config, &dem_config,verbose);
  }
 }
// -- -- ---------------------------------- --
// Bale out if no connection
// -- -- ---------------------------------- --
 if (!db_handle)
 {
  spatialite_cleanup_ex(cache);
  cache=NULL;
  return exit_code;
 }
// -- -- ---------------------------------- --
// checking the Source-Database
// -- -- ---------------------------------- --
 command_check_source_db(db_handle,&source_config, verbose);
// -- -- ---------------------------------- --
// checking the Dem-Database
// -- -- ---------------------------------- --
 if (command_check_dem_db(db_handle,&dem_config, &source_config, verbose) )
 {
  if ( save_conf == 1)
  {
   if (write_demconfig(dem_configfile, dem_config))
   {
    fprintf(stderr, "Dem-conf: with default_srid[%d] was saved to\n\t[%s].\n",dem_config.default_srid,dem_configfile);
   }
  }
 }
// -- -- ---------------------------------- --
// After checking, the called functions
//  will check the result before running
// -- -- ---------------------------------- --
 if ( (i_sniff_on == 1) && (dem_config.has_z) && (source_config.has_z) )
 {
  if (verbose)
  {
   fprintf(stderr, "Sniffing modus: All pre-conditions have been fulfilled.\n");
   fprintf(stderr, "\t to start update, use the '-updatez' parameter without '-sniff'.\n");
   fprintf(stderr, "\t to save dem-conf,  use the '-save_conf' parameter.\n");
  }
  exit_code = 0; // correct
 }
 if (i_sniff_on == 0)
 {
  // -- -- ---------------------------------- --
  // Run only if 'sniff' is off
  // -- -- ---------------------------------- --
  // Start --update
  // -- -- ---------------------------------- --
  if (i_command_type == CMD_DEM_UPDATEZ)
  {
   if (!copy_m)
   {// The User desires that m values be ignored
    dem_config.has_m=0;
   }
   if (command_updatez_db(db_handle, &source_config,&dem_config, verbose) )
   {
    exit_code = 0; // correct
   }
  }
  // -- -- ---------------------------------- --
  // Start -import_xyz
  // -- -- ---------------------------------- --
  if (i_command_type == CMD_DEM_IMPORT_XYZ)
  {
   if (command_import_xyz(db_handle, &source_config, &dem_config, verbose))
   {
    exit_code = 0; // correct
   }
  }
  // -- -- ---------------------------------- --
  // Start -fetchz
  // -- -- ---------------------------------- --
  if (i_command_type == CMD_DEM_FETCHZ)
  {
   if (command_fetchz(db_handle, &dem_config, verbose) )
   {
    exit_code = 0; // correct
   }
  }
 }
 else
 {
  if (i_command_type == CMD_DEM_UPDATEZ)
  {
   if (verbose)
   {
    fprintf(stderr, "Sniffing modus:  '-updatez' will not be called.\n");
   }
  }
  else if (i_command_type == CMD_DEM_IMPORT_XYZ)
  {
   if (verbose)
   {
    fprintf(stderr, "Sniffing modus:  '-import_xyz' will not be called.\n");
   }
  }
  else if (i_command_type == CMD_DEM_FETCHZ)
  {
   if (verbose)
   {
    fprintf(stderr, "Sniffing modus:  '-fetchz' will not be called.\n");
   }
  }
 }
// -- -- ---------------------------------- --
// Close Application
// - DETACH when needed
// -- -- ---------------------------------- --
 if (db_handle)
 {
  close_db(db_handle,cache, schema_dem);
  cache=NULL;
 }
 return exit_code;
// -- -- ---------------------------------- --
}

