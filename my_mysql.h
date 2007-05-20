#ifndef __INC__MY_MYSQL_H__
#define __INC__MY_MYSQL_H__ 1

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

//#include "ppport.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <mysql.h>

static const my_bool MYBOOL_TRUE	= 1;
static const my_bool MYBOOL_FALSE	= 0;

#define	CLIENT_RECONNECT	16384

#define MYCF_TRANSACTION	1
#define MYCF_AUTOCOMMIT		2

#ifndef DWORD
#define DWORD unsigned long
#endif

#ifndef my_longlong
#if defined __unix__
#define my_longlong long long
#elif defined _WIN32
#define my_longlong __int64
#else
#define my_longlong long
#endif
#endif

typedef struct st_my_con {
	struct st_my_con	*prev, *next;
	DWORD				tid;
	MYSQL				*conid;
	struct st_my_res	*res;
	struct st_my_res	*lastres;
	struct st_my_stmt	*first_stmt;
	struct st_my_stmt	*last_stmt;
	unsigned int		port;
	char				*charset;
	char				*host;
	char				*user;
	char				*passwd;
	char				*unix_socket;
	char				*db;
	DWORD				client_flag;
	DWORD				my_flags;
	DWORD				charset_length;
	char				my_error[256];
} MY_CON;

typedef struct st_my_stmt {
	struct st_my_stmt	*prev, *next;
	MYSQL_STMT			*stmt;
	MYSQL_BIND			*params;
	char				*param_types;
	DWORD				param_count;
	MYSQL_BIND			*result;
	DWORD				field_count;
	MYSQL_RES			*meta;
	MY_CON				*con;
	DWORD				exec_count;
	my_longlong			rowpos;
	my_longlong			numrows;
} MY_STMT;

typedef struct st_my_res {
	struct st_my_res	*prev, *next;
	MYSQL_RES			*res;
	MY_CON				*con;
	my_longlong			rowpos;
	my_longlong			numrows;
} MY_RES;

#define MY_CXT_KEY "PAB::DB::Driver::Mysql::_guts" XS_VERSION

typedef struct st_my_cxt {
	MY_CON				*con;
	MY_CON				*lastcon;
	char				lasterror[256];
	unsigned int		lasterrno;
#ifdef USE_THREADS
	perl_mutex			thread_lock;
#endif
} my_cxt_t;

START_MY_CXT

#define STR_CREATEANDCOPYN( src, dst, len ) \
	if( (src) && (len) ) { \
		New( 1, (dst), (len) + 1, char ); \
		Copy( (src), (dst), (len) + 1, char ); \
	} \
	else { \
		(dst) = NULL; \
	}

#define STR_CREATEANDCOPY( src, dst ) \
	STR_CREATEANDCOPYN( (src), (dst), (src) ? strlen( (src) ) : 0 )

char *my_strcpyn( char *dst, const char *src, unsigned long len );
char *my_strcpy( char *dst, const char *src );
//char *my_itoa( int value, char* str, int radix );
//char *str_replace( const char *str, const char *search, const char *replace );

#define MY_ERROR_MEMEROY	"Out of memory!",

//DWORD my_crc32( const char *str, DWORD len );
DWORD get_current_thread_id();
void my_set_error( const char *tpl, ... );
UV _my_verify_linkid( UV linkid, int error );
#define my_verify_linkid(linkid)	_my_verify_linkid( (linkid), 1 )
#define my_verify_linkid_noerror(linkid)	_my_verify_linkid( (linkid), 0 )
int my_mysql_get_type( UV *ptr );

void my_mysql_cleanup();
void my_mysql_cleanup_connections();
MYSQL *my_mysql_reconnect( MY_CON *con );

MY_CON *my_mysql_con_add( MYSQL *mysql, DWORD tid, DWORD client_flag );
void my_mysql_con_rem( MY_CON *con );
MY_CON *my_mysql_con_find_by_tid( DWORD tid );
void my_mysql_con_free( MY_CON *con );
void my_mysql_con_cleanup( MY_CON *con );
int my_mysql_con_exists( MY_CON *con );

MY_RES *my_mysql_res_add( MY_CON *con, MYSQL_RES *res );
void my_mysql_res_rem( MY_RES *res );
int my_mysql_res_exists( MY_RES *res );

MY_STMT *my_mysql_stmt_init( MY_CON *con, const char *query, DWORD length );
void my_mysql_stmt_free( MY_STMT *stmt );
int my_mysql_stmt_exists( MY_STMT *stmt );
void my_mysql_stmt_rem( MY_STMT *stmt );
int my_mysql_stmt_or_res( UV ptr );
int my_mysql_stmt_or_con( UV *ptr );

int my_mysql_bind_param( MY_STMT *stmt, DWORD p_num, SV *val, char type );
void my_mysql_bind_free( MYSQL_BIND *bind );

int my_mysql_handle_return( MY_CON *con, long ret );

#endif
