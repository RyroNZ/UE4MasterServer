/*
 * Copyright (c) Daniel Balster
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Daniel Balster nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY DANIEL BALSTER ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL DANIEL BALSTER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef DBALSTER_WEBRESPONSE_H
#define DBALSTER_WEBRESPONSE_H

#include <sys/types.h>

#ifdef __cplusplus
# ifdef WIN32
#	 define HTTPD_C_API extern "C"
# else
#  define HTTPD_C_API extern "C"
# endif
#else
#	define HTTPD_C_API
# include <stdbool.h>
#endif

typedef struct _HttpResponse HttpResponse;
typedef struct _HttpHeader HttpHeader;
typedef struct _Httpd Httpd;
typedef struct _HttpRequest HttpRequest;

typedef void  (*HttpRequestHandler)( HttpResponse* _response, void* _userdata );

struct _HttpHeader
{
  char* name;
  char* value;
};

HTTPD_C_API Httpd* httpd_create (unsigned short _port, HttpRequestHandler _handler, void* _userdata);
HTTPD_C_API void httpd_destroy (Httpd* _server);
HTTPD_C_API void httpd_process (Httpd* _server, bool _blocking);

HTTPD_C_API HttpRequest* httprequest_create( const char* _hostname, unsigned short _port, const char* _location, const char* _method, size_t _maxBytes );
HTTPD_C_API void httprequest_sprintf( HttpRequest* _req, const char* _fmt, ... );
HTTPD_C_API void httprequest_strcat( HttpRequest* _req, const char* _orig );
HTTPD_C_API bool httprequest_execute( HttpRequest* _req );
HTTPD_C_API int httprequest_get_result( HttpRequest* _req );
HTTPD_C_API const char* httprequest_get_header( HttpRequest* _req, const char* _header );
HTTPD_C_API const char* httprequest_get_content( HttpRequest* _req );
HTTPD_C_API size_t httprequest_get_content_length( HttpRequest* _req );
HTTPD_C_API void httprequest_destroy( HttpRequest* _req );
HTTPD_C_API void httprequest_reset( HttpRequest* _req );

HTTPD_C_API HttpResponse* httpresponse_create (unsigned int _socket);
HTTPD_C_API void httpresponse_destroy (HttpResponse* _context);
HTTPD_C_API bool httpresponse_parse(HttpResponse* _context);
HTTPD_C_API bool httpresponse_response(HttpResponse* _context, unsigned int _code, const char* _content, size_t _contentLength, const char* _userHeader);
HTTPD_C_API int httpresponse_write(HttpResponse* _context, const void* _memory, const int _size);
HTTPD_C_API void	httpresponse_begin(HttpResponse* _context, unsigned int _code, const char* _userHeader);
HTTPD_C_API int httpresponse_writef(HttpResponse* _context, const char* _fmt, ...);   
HTTPD_C_API void	httpresponse_end(HttpResponse* _context);
HTTPD_C_API const char* httpresponse_location (HttpResponse* _context);
HTTPD_C_API const char* httpresponse_method(HttpResponse* _context);
HTTPD_C_API int httpresponse_get_n_args(HttpResponse* _context);
HTTPD_C_API int httpresponse_get_n_headers(HttpResponse* _context);
HTTPD_C_API const char* httpresponse_get_arg(HttpResponse* _context, const char* _key);
HTTPD_C_API const HttpHeader* httpresponse_get_arg_by_index(HttpResponse* _context, int _index);
HTTPD_C_API const char* httpresponse_get_header(HttpResponse* _context, const char* _key);
HTTPD_C_API const HttpHeader*	httpresponse_get_header_by_index(HttpResponse* _context, int _index);

#endif

