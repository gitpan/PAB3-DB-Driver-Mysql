#include <errmsg.h>

#ifdef USE_THREADS
#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#endif

#include "my_mysql.h"


struct st_refbuf {
	struct st_refbuf *prev, *next;
};

#define refbuf_add(rbs,rbd)     _refbuf_add( (struct st_refbuf *) (rbs), (struct st_refbuf *) (rbd) )
#define refbuf_rem(rb)          _refbuf_rem( (struct st_refbuf *) (rb) )

void _refbuf_add( struct st_refbuf *rbs, struct st_refbuf *rbd );
void _refbuf_rem( struct st_refbuf *rb );


void my_set_error( const char *tpl, ... ) {
	dMY_CXT;
	va_list ap;
	MY_CON *con = my_mysql_con_find_by_tid( get_current_thread_id() );
	va_start( ap, tpl );
	if( con != NULL )
		vsprintf( con->my_error, tpl, ap );
	else
		vsprintf( MY_CXT.lasterror, tpl, ap );
	va_end( ap );
}

UV _my_verify_linkid( UV linkid, int error ) {
	dMY_CXT;
	if( linkid ) {
		return my_mysql_con_exists( (MY_CON *) linkid ) ? linkid : 0;
	}
#ifdef USE_THREADS
	else {
		linkid = (UV) my_mysql_con_find_by_tid( get_current_thread_id() );
		if( linkid )
			return linkid;
		if( error )
			sprintf( MY_CXT.lasterror, "No connection exists" );
		return 0;
	}
#endif
	if( ! MY_CXT.lastcon ) {
		if( error )
			sprintf( MY_CXT.lasterror, "No connection exists" );
		return 0;
	}
	return (UV) MY_CXT.lastcon;
}

int my_mysql_get_type( UV *ptr ) {
	dMY_CXT;
	MY_STMT *s1;
	MY_CON *c1;
	MY_RES *r1;
	if( ! *ptr ) {
		*ptr = my_verify_linkid( *ptr );
		return *ptr != 0 ? 3 : 0;
	}
	for( c1 = MY_CXT.con; c1 != NULL; c1 = c1->next ) {
		if( (UV) c1 == *ptr ) return 3;
		for( r1 = c1->res; r1 != NULL; r1 = r1->next )
			if( (UV) r1 == *ptr ) return 1;
		for( s1 = c1->first_stmt; s1 != NULL; s1 = s1->next )
			if( (UV) s1 == *ptr ) return 2;
	}
	my_set_error( "Link ID 0x%06X is unknown", *ptr );
	return 0;
}

void my_mysql_cleanup() {
	MY_CON *c1, *c2;
	dMY_CXT;
	c1 = MY_CXT.con;
	while( c1 ) {
		c2 = c1->next;
		my_mysql_con_free( c1 );
		c1 = c2;
	}
	MY_CXT.lastcon = MY_CXT.con = 0;
	//Safefree( MY_CXT.lasterror );
}

void my_mysql_cleanup_connections() {
	MY_CON *c1;
	dMY_CXT;
	c1 = MY_CXT.con;
	while( c1 ) {
		my_mysql_con_cleanup( c1 );
		c1 = c1->next;
	}
}

MY_CON *my_mysql_con_add( MYSQL *mysql, DWORD tid, DWORD client_flag ) {
	dMY_CXT;
	MY_CON *con;
	Newz( 1, con, 1, MY_CON );
	con->conid = mysql;
	con->tid = tid;
	con->client_flag = client_flag;
	STR_CREATEANDCOPY( mysql->host, con->host );
	STR_CREATEANDCOPY( mysql->user, con->user );
	STR_CREATEANDCOPY( mysql->passwd, con->passwd );
	STR_CREATEANDCOPY( mysql->unix_socket, con->unix_socket );
	STR_CREATEANDCOPY( mysql->db, con->db );
	con->port = mysql->port;
	con->my_flags = MYCF_AUTOCOMMIT;
	if( MY_CXT.con == NULL )
		MY_CXT.con = con;
	else
		refbuf_add( MY_CXT.lastcon, con );
	MY_CXT.lastcon = con;
	return con;
}

void my_mysql_con_free( MY_CON *con ) {
	my_mysql_con_cleanup( con );
	mysql_close( con->conid );
	Safefree( con->charset );
	Safefree( con->host );
	Safefree( con->user );
	Safefree( con->passwd );
	Safefree( con->unix_socket );
	Safefree( con->db );
	Safefree( con->res );
	Safefree( con->conid );
	Safefree( con );
}

void my_mysql_con_rem( MY_CON *con ) {
	dMY_CXT;
	if( con == MY_CXT.lastcon )
		MY_CXT.lastcon = con->prev;
	if( con == MY_CXT.con )
		MY_CXT.con = con->next;
	refbuf_rem( con );
	my_mysql_con_free( con );
}

void my_mysql_con_cleanup( MY_CON *con ) {
	MY_RES *r1, *r2;
	MY_STMT *s1, *s2;
	s1 = con->first_stmt;
	while( s1 ) {
		s2 = s1->next;
		my_mysql_stmt_free( s1 );
		s1 = s2;
	}
	con->first_stmt = con->last_stmt = NULL;
	r1 = con->res;
	while( r1 ) {
		r2 = r1->next;
		mysql_free_result( r1->res );
		Safefree( r1 );
		r1 = r2;
	}
	con->res = con->lastres = NULL;
}

int my_mysql_con_exists( MY_CON *con ) {
	dMY_CXT;
	MY_CON *c1;
	for( c1 = MY_CXT.con; c1 != NULL; c1 = c1->next )
		if( con == c1 ) return 1;
	my_set_error( "Connection ID 0x%06X does not exist", con );
	return 0;
}

MY_CON *my_mysql_con_find_by_tid( DWORD tid ) {
	dMY_CXT;
	MY_CON *c1;
	for( c1 = MY_CXT.con; c1 != NULL; c1 = c1->next )
		if( c1->tid == tid ) return c1;
	return NULL;
}

MY_RES *my_mysql_res_add( MY_CON *con, MYSQL_RES *res ) {
	MY_RES *ret;
	Newz( 1, ret, 1, MY_RES );
	ret->res = res;
	ret->con = con;
	ret->numrows = mysql_num_rows( res );
	if( con->res == NULL )
		con->res = ret;
	else
		refbuf_add( con->lastres, ret );
	con->lastres = ret;
	return ret;
}

void my_mysql_res_rem( MY_RES *res ) {
	MY_CON *con;
	if( res == NULL ) return;
//	printf( "mysql free result 0x%06X\n", res );
	mysql_free_result( res->res );
	con = res->con;
	if( con->lastres == res )
		con->lastres = res->prev;
	if( con->res == res )
		con->res = res->next;
	refbuf_rem( res );
	Safefree( res );
}

int my_mysql_res_exists( MY_RES *res ) {
	dMY_CXT;
	MY_RES *r1;
	MY_CON *c1;
	if( res != NULL ) {
		for( c1 = MY_CXT.con; c1 != NULL; c1 = c1->next ) {
			for( r1 = c1->res; r1 != NULL; r1 = r1->next )
				if( r1 == res ) return 1;
		}
	}
	my_set_error( "Result ID 0x%06X is unknown", res );
	return 0;
}

MYSQL *my_mysql_reconnect( MY_CON *con ) {
	MYSQL *res;
	my_mysql_con_cleanup( con );
	mysql_close( con->conid );
	res = mysql_real_connect(
		con->conid,
		con->host, con->user, con->passwd, con->db, con->port,
		con->unix_socket, ( con->client_flag ^ CLIENT_RECONNECT )
	);
	if( ! res ) return 0;
	if( con->charset != 0 ) {
		char *sql, *p1;
		New( 1, sql, 13 + con->charset_length, char );
		p1 = my_strcpy( sql, "SET NAMES '" );
		p1 = my_strcpy( p1, con->charset );
		p1 = my_strcpy( p1, "'" );
		mysql_real_query( con->conid, sql, 12 + con->charset_length );
		Safefree( sql );
	}
	return res;
}

MY_STMT *my_mysql_stmt_init( MY_CON *con, const char *query, DWORD length ) {
	MY_STMT *stmt;
	int hr;
	Newz( 1, stmt, 1, MY_STMT );
	if( stmt == NULL ) return NULL;
	stmt->stmt = mysql_stmt_init( con->conid );
	hr = mysql_stmt_prepare( stmt->stmt, query, length ); 
	if( hr != 0 ) {
		Safefree( stmt );
		return NULL;
	}
	if( con->first_stmt == NULL )
		con->first_stmt = stmt;
	else
		refbuf_add( con->last_stmt, stmt );
	con->last_stmt = stmt;
	stmt->con = con;
	stmt->param_count = mysql_stmt_param_count( stmt->stmt );
	if( stmt->param_count > 0 ) {
		Newz( 1, stmt->params, stmt->param_count, MYSQL_BIND );
		Newz( 1, stmt->param_types, stmt->param_count, char );
	}
	return stmt;
}

void my_mysql_stmt_free( MY_STMT *stmt ) {
	DWORD i;
	if( stmt == NULL ) return;
	if( stmt->meta != NULL ) {
		mysql_free_result( stmt->meta );
		stmt->meta = NULL;
	}
	if( stmt->stmt != NULL ) {
		mysql_stmt_close( stmt->stmt );
		stmt->stmt = NULL;
	}
	for( i = 0; i < stmt->param_count; i ++ ) {
		my_mysql_bind_free( &stmt->params[i] );
	}
	for( i = 0; i < stmt->field_count; i ++ ) {
		my_mysql_bind_free( &stmt->result[i] );
	}
	Safefree( stmt->params );
	Safefree( stmt->param_types );
	Safefree( stmt->result );
	Safefree( stmt );
}

void my_mysql_stmt_rem( MY_STMT *stmt ) {
	MY_CON *con;
	if( stmt == NULL ) return;
	con = stmt->con;
	if( con->first_stmt == stmt )
		con->first_stmt = stmt->next;
	if( con->last_stmt == stmt )
		con->last_stmt = stmt->prev;
	refbuf_rem( stmt );
	my_mysql_stmt_free( stmt );
}

int my_mysql_stmt_exists( MY_STMT *stmt ) {
	dMY_CXT;
	MY_STMT *s1;
	MY_CON *c1;
	if( stmt != NULL ) {
		for( c1 = MY_CXT.con; c1 != NULL; c1 = c1->next ) {
			for( s1 = c1->first_stmt; s1 != NULL; s1 = s1->next )
				if( s1 == stmt ) return 2;
		}
	}
	my_set_error( "Statement ID 0x%06X is unknown", stmt );
	return 0;
}

int my_mysql_stmt_or_res( UV ptr ) {
	dMY_CXT;
	MY_RES *r1;
	MY_STMT *s1;
	MY_CON *c1;
	if( ptr != 0 ) {
		for( c1 = MY_CXT.con; c1 != NULL; c1 = c1->next ) {
			for( r1 = c1->res; r1 != NULL; r1 = r1->next )
				if( (UV) r1 == ptr ) return 1;
			for( s1 = c1->first_stmt; s1 != NULL; s1 = s1->next )
				if( (UV) s1 == ptr ) return 2;
		}
	}
	my_set_error( "ID 0x%06X is unknown", ptr );
	return 0;
}

int my_mysql_stmt_or_con( UV *ptr ) {
	dMY_CXT;
	MY_STMT *s1;
	MY_CON *c1;
	if( ! *ptr ) {
		*ptr = my_verify_linkid( *ptr );
		return *ptr != 0 ? 3 : 0;
	}
	for( c1 = MY_CXT.con; c1 != NULL; c1 = c1->next ) {
		if( (UV) c1 == *ptr ) return 3;
		for( s1 = c1->first_stmt; s1 != NULL; s1 = s1->next )
			if( (UV) s1 == *ptr ) return 2;
	}
	my_set_error( "ID 0x%06X is unknown", *ptr );
	return 0;
}

int my_mysql_bind_param( MY_STMT *stmt, DWORD p_num, SV *val, char type ) {
	MYSQL_BIND *bind;
	STRLEN svlen;
	char *p1;
	if( stmt->stmt == NULL ) return 0;
	if( p_num == 0 || stmt->param_count < p_num ) {
		sprintf( stmt->con->my_error,
			"Parameter %lu is not in range (%lu)",
			p_num, stmt->param_count
		);
		return 0;
	}
	if( type != 0 )
		stmt->param_types[p_num - 1] = type;
	bind = &stmt->params[p_num - 1];
	if( ! SvOK( val ) ) {
		bind->buffer_type = MYSQL_TYPE_NULL;
		return 1;
	}
	switch( stmt->param_types[p_num - 1] ) {
	case 'i':
		bind->buffer_type = MYSQL_TYPE_LONG;
		Renew( bind->buffer, 1, int );
		if( SvIOK_UV( val ) ) {
			bind->is_unsigned = 1;
			*((int *) bind->buffer) = SvUV( val );
		}
		else {
			bind->is_unsigned = 0;
			*((int *) bind->buffer) = SvIV( val );
		}
		return 1;
	case 'd':
		bind->buffer_type = MYSQL_TYPE_DOUBLE;
		Renew( bind->buffer, 1, double );
		*((double *) bind->buffer) = SvNV( val );
		return 1;
	case 's':
		bind->buffer_type = MYSQL_TYPE_STRING;
		svlen = SvLEN( val ) + 1;
		Renew( bind->buffer, svlen, char );
		p1 = SvPV( val, svlen );
		Copy( p1, bind->buffer, svlen, char );
		Renew( bind->length, 1, unsigned long );
		*bind->length = svlen;
		return 1;
	case 'b':
		bind->buffer_type = MYSQL_TYPE_BLOB;
		svlen = SvLEN( val );
		Renew( bind->buffer, svlen, char );
		p1 = SvPVbyte( val, svlen );
		Copy( p1, bind->buffer, svlen, char );
		Renew( bind->length, 1, unsigned long );
		*bind->length = svlen;
		return 1;
	}
	// autodetect type
	if( SvIOK( val ) ) {
		bind->buffer_type = MYSQL_TYPE_LONG;
		Renew( bind->buffer, 1, int );
		if( SvIOK_UV( val ) ) {
			bind->is_unsigned = 1;
			*((int *) bind->buffer) = SvUV( val );
		}
		else {
			bind->is_unsigned = 0;
			*((int *) bind->buffer) = SvIV( val );
		}
	}
	else if( SvNOK( val ) ) {
		bind->buffer_type = MYSQL_TYPE_DOUBLE;
		Renew( bind->buffer, 1, double );
		*((double *) bind->buffer) = SvNV( val );
	}
	else {
		svlen = SvLEN( val ) + 1;
		bind->buffer_type = MYSQL_TYPE_STRING;
		Renew( bind->buffer, svlen, char );
		p1 = SvPV( val, svlen );
		Copy( p1, bind->buffer, svlen, char );
		Renew( bind->length, 1, unsigned long );
		*bind->length = svlen;
	}
	return 1;
}

void my_mysql_bind_free( MYSQL_BIND *bind ) {
	if( bind == NULL ) return;
	if( bind->buffer != NULL ) {
		Safefree( bind->buffer );
	}
	if( bind->length != NULL ) {
		Safefree( bind->length );
	}
	if( bind->is_null != NULL ) {
		Safefree( bind->is_null );
	}
	Zero( bind, 1, MYSQL_BIND );
}

int my_mysql_handle_return( MY_CON *con, long ret ) {
	switch( ret ) {
	case 0:
		// all fine
		return 0;
	case 1:
	case CR_SERVER_GONE_ERROR:
	case CR_SERVER_LOST:
		if( ( con->client_flag & CLIENT_RECONNECT ) != 0 ) {
			ret = (long) my_mysql_reconnect( con );
			if( ret == 0 ) return mysql_errno( con->conid );
		}
		return 0;
	}
	return ret;
}

DWORD get_current_thread_id() {
#ifdef USE_THREADS
#ifdef _WIN32
	return GetCurrentThreadId();
#else
	return (DWORD) pthread_self();
#endif
#else
	return 0;
#endif
}

void _refbuf_add( struct st_refbuf *rbs, struct st_refbuf *rbd ) {
	while( rbs ) {
		if( rbs->next == NULL ) {
			rbs->next = rbd;
			rbd->prev = rbs;
			return;
		}
		rbs = rbs->next;
	}
}

void _refbuf_rem( struct st_refbuf *rb ) {
	if( rb ) {
		struct st_refbuf *rbp = rb->prev;
		struct st_refbuf *rbn = rb->next;
		if( rbp ) {
			rbp->next = rbn;
		}
		if( rbn ) {
			rbn->prev = rbp;
		}
	}
}

/*
#define CRC32_POLYNOMIAL 0xEDB88320

DWORD my_crc32( const char *str, DWORD len ) {
	DWORD idx, bit, data, crc = 0xffffffff;
	for( idx = 0; idx < len; idx ++ ) {
		data = *str ++;
	    for( bit = 0; bit < 8; bit ++, data >>= 1 ) {
			crc = ( crc >> 1 ) ^ ( ( ( crc ^ data ) & 1 ) ? CRC32_POLYNOMIAL : 0 );
		}
	}
	return crc;
}
*/

char *my_strcpyn( char *dst, const char *src, unsigned long len ) {
	char ch;
	for( ; len > 0; len -- ) {
		if( ( ch = *src ++ ) == '\0' ) {
			*dst = '\0';
			return dst;
		}
		*dst ++ = ch;
	}
	*dst = '\0';
	return dst;
}

char *my_strcpy( char *dst, const char *src ) {
	char ch;
	while( 1 ) {
		if( ( ch = *src ++ ) == '\0' ) {
			*dst = '\0';
			return dst;
		}
		*dst ++ = ch;
	}
	*dst = '\0';
	return dst;
}

/*
char *str_replace( const char *str, const char *search, const char *replace ) {
	unsigned long istr, isch, idst, irep, lstr, lrep, lsch, lmax;
	char *sz = 0;
	lstr = strlen( str );
	if( ! str ) return 0;
	lsch = strlen( search );
	if( lsch == 0 ) {
		New( 1, sz, lstr, char );
		Copy( str, sz, lstr + 1, char );
		return sz;
	}
	lrep = strlen( replace );
	if( lrep > lsch ) {
		lmax = (int)( (float) lstr / (float) lsch * (float) lrep ) + lrep + 1;
	}
	else {
		lmax = lstr + 1;
	}
	New( 1, sz, lmax, char );
	isch = 0;
	idst = 0;
	for( istr = 0; istr < lstr; istr ++ ) {
		if( str[istr] == search[isch] ) {
			isch ++;
			if( isch == lsch ) {
				for( irep = 0; irep < lrep; irep ++ ) {
					sz[idst++] = replace[irep];
				}
				isch = 0;
				continue;
			}
		}
		else {
			while( isch > 0 ) {
				sz[idst++] = str[istr-isch];
				isch --;
			}
			sz[idst++] = str[istr];
		}
	}
	sz[idst++] = 0;
	return sz;
}

#ifndef strrev
#define strrev _strrev
char *_strrev( char *str ) {
	char *p1, *p2;
	if( ! str || ! *str ) return str;
	for( p1 = str, p2 = str + strlen( str ) - 1; p2 > p1; ++ p1, -- p2 ) {
		*p1 ^= *p2;
		*p2 ^= *p1;
		*p1 ^= *p2;
	}
	return str;
}
#endif

char *my_itoa( char *str, int value, int radix ) {
	int  rem = 0;
	int  pos = 0;
	char ch  = '!' ;
	do {
		rem    = value % radix ;
		value /= radix;
		if( 16 == radix ) {
			if( rem >= 10 && rem <= 15 ) {
				switch( rem ) {
				case 10:
					ch = 'a' ;
					break;
				case 11:
					ch ='b' ;
					break;
				case 12:
					ch = 'c' ;
					break;
				case 13:
					ch ='d' ;
					break;
				case 14:
					ch = 'e' ;
					break;
				case 15:
					ch ='f' ;
					break;
				}
			}
		}
		if( '!' == ch ) {
			str[pos ++] = (char) ( rem + 0x30 );
		}
		else {
			str[pos ++] = ch ;
		}
	} while( value != 0 );
	str[pos] = '\0';
	strrev( str );
	return &str[pos];
}
*/
