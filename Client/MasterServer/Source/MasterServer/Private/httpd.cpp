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

#include "MasterServerPrivatePCH.h"

#include "httpd.h"

#include <string.h> // strcat, strcmp, strstr, strchr, strlen, strcpy
#include <stdarg.h> // va_start, va_end, va_list, strtoul, 
#include <errno.h>  // fprintf, printf, strerror, gethostbyname, memcpy, htons
#include <stdio.h>  // close(socket), send, recv, socket, setsockopt, bind, listen, accept, select, connect
#include <stdlib.h> // calloc, free, qsort, vsprintf, sprintf, bsearch

#ifdef WIN32
#include "AllowWindowsPlatformTypes.h"
#  include <winsock.h>
typedef int socklen_t;
#else
#  include <netdb.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <resolv.h>
#  include <unistd.h>
#  define closesocket close
#endif

static int hexnibble( const char c )
{
  int result = 0;
  if (c>='a' && c<='f') result += c-'a'+10;
  if (c>='A' && c<='F') result += c-'A'+10;
  if (c>='0' && c<='9') result += c-'0';
  return result;
}

// decode '+' and '%xx' back into bytes
static char* uri_decode_inplace( char* _text )
{
  int j=0;
  for (int i=0; _text[i]; ++i)
  {
    char c=_text[i];
    switch( c )
    {
      case '+': c = ' '; break;
      case '%':
      {
        int lo,hi;
        lo = hexnibble(_text[++i]);
        hi = hexnibble(_text[++i]);
        c = lo <<4 | hi;
      }
        break;
      default:
        break;
    }
    _text[j++]=c;
  }
  _text[j]=0;
  return _text;
}

static void quoting_strncpy_append( const char* _text, char* _output, size_t _size, size_t* _index )
{
  if (_output)
  {
    // TODO range check, prevent buffer overflow (_output+_size < _output+_index+strlen(_text))
	  strcat(_output,_text);
  }
  *_index += strlen(_text);
}

static size_t quoting_strncpy( char* _output, const char* _input, size_t _size )
{
  size_t i=0;
  size_t o=0;
  char c[2];
  c[1] = 0;
  
  if (_output) *_output = 0; // clear output buffer
  
  while ( (*c = _input[i++]) )
  {
    switch (*c)
    {
      case '\'': quoting_strncpy_append("&apos;",_output,_size,&o); break;
      case '"': quoting_strncpy_append("&quot;",_output,_size,&o); break;
      case '<': quoting_strncpy_append("&lt;",_output,_size,&o); break;
      case '>': quoting_strncpy_append("&gt;",_output,_size,&o); break;
      case '&': quoting_strncpy_append("&amp;",_output,_size,&o); break;
      case ' ': quoting_strncpy_append("&nbsp;",_output,_size,&o); break;
      case '\t': quoting_strncpy_append("&nbsp;&nbsp;",_output,_size,&o); break;
      default: quoting_strncpy_append(c,_output,_size,&o); break;
    }
  }
  
  return o;
}

struct _HttpResponse
{
  int netsocket;
  char* memory;
  char* method;		// GET, POST, PUT, FINDPROP, ...
  char* location;	// /path/to/page
  int n_args;
  HttpHeader* args;
  int n_headers;
  HttpHeader* headers;
  bool chunked;
};

static const struct
{
	unsigned short code;
	const char* message;
} 
HttpErrorMessages[] =  
{
  { 100, "Continue" }, 
  { 200, "OK" }, 
  { 206, "Partial Content" },
  { 220, "OK" }, 
  { 302, "Found" }, 
  { 303, "See Other" }, 
  { 400, "Bad Request" },
  { 401, "Unauthorized" },
  { 403, "Forbidden" }, 
  { 404, "Not Found" }, 
  { 405, "Method Not Allowed" }, 
  { 408, "Request Timeout" }, 
  { 500, "Internal Server Error" }, 
  { 505, "HTTP Version Not Supported" }, 
};

HTTPD_C_API HttpResponse*	httpresponse_create (unsigned int _socket)
{
	HttpResponse* wr = (HttpResponse*) calloc(1,sizeof(HttpResponse));
  if (wr)
  {
    wr->netsocket = _socket;
    wr->chunked = false;
    wr->memory = 0;
    wr->method = 0;
    wr->location = 0;
    wr->n_args = 0;
    wr->args = 0;
    wr->n_headers = 0;
    wr->headers = 0;
  }
	return wr;
}

HTTPD_C_API void httpresponse_destroy (HttpResponse* _context)
{
	free (_context->memory);
	closesocket(_context->netsocket); 
	free (_context);
}

HTTPD_C_API int httpresponse_write(HttpResponse* _context, const void* _memory, const int _size)
{
  int sendButes = 0;
  while(sendButes < _size)
  {
	  int ret = send(_context->netsocket, &(((const char*)_memory)[sendButes]), (int)_size - sendButes, 0);
	  if (ret != SOCKET_ERROR)
	  {
		  sendButes += ret;
	  }
	  else
		  return -1;
  }
  return sendButes;
}

static int httpresponse_read(HttpResponse* _context, void* _memory, const int _size)
{
  return recv(_context->netsocket, (char*)_memory, (int)_size, 0);
}

static int comparePairs(const void* l, const void* r)
{
  const HttpHeader* pl = (const HttpHeader*)l;
  const HttpHeader* pr = (const HttpHeader*)r;
  return strcmp(pl->name, pr->name);
}

HTTPD_C_API bool httpresponse_parse(HttpResponse* _context)
{
  // TODO: context, make buffer configurable
  const static int RECEIVE_BUFFER_SIZE = 8 * 1024;
  char buffer[RECEIVE_BUFFER_SIZE];
  int bytesRead = httpresponse_read(_context, buffer, RECEIVE_BUFFER_SIZE - 1);
  if (bytesRead <= 0)
  {
    return httpresponse_response(_context, 500, 0, 0, 0);
  }
  buffer[bytesRead] = 0;

  // extract method
  char* method = buffer;
  char* eom = strchr(method, ' ');
  if (0 == eom)
  {
    return httpresponse_response(_context, 500, 0, 0, 0);
  }
  *eom++ = 0; // terminate method

  // extract location and (optional) arguments
  char* location = eom;
  char* eol = strstr(location, " HTTP/1.1\r\n");
  if (0 == eol)
  {
    return httpresponse_response(_context, 500, 0, 0, 0);
  }
  *eol = 0;	// terminate location

  char* args = strchr(location, '?');
  if (args)
  {
    *args++ = 0; // args is now pointing to the argument list
  }
  else
  {
    args = eol;	// empty arguments
  }
  eol += strlen(" HTTP/1.1\r\n");

  char* eoh = 0;			// end of header

  if (0 == strcmp(method, "POST") || 0 == strcmp(method, "GET") || 0 == strcmp(method, "OPTIONS"))
  {
    // mozilla sends the header in two chunks, yeah.
    eoh = strstr(eol, "\r\n\r\n");
    if(0 == eoh)
    {
      bytesRead += httpresponse_read(_context, buffer + bytesRead, RECEIVE_BUFFER_SIZE - bytesRead - 1);
      buffer[bytesRead] = 0;
      eoh = strstr(eol, "\r\n\r\n");
    }
    
    if (0 == eoh)
      return httpresponse_response(_context, 500, 0, 0, 0);
    
    eoh += 4; // end-of-header now points directly to the first byte of content
  }
  else
  {
    return httpresponse_response(_context, 500, "unsupported method", 0, 0);
  }

  // method .. eom : size of method
  // args .. eol : size of GET arguments
  // eol .. eoh-2 : size of headers
  // eoh .. buffer+bytesRead ; size of POST arguments

  _context->n_headers = -1;
  int hchars = 0;
  char* headers = eol;
  for (; headers[hchars]; ++hchars)
  {
    if (headers[hchars] == '\r')
    {
      _context->n_headers++;
    }
  }

  // how many name=value pairs should be allocated:
  _context->n_args = 0;	// count pairs
  int chars = 0;	// count characters
  int bytesLeft = bytesRead - (int)(eoh - buffer);
  for (; args[chars]; ++chars)
  {
    if (args[chars] == '=') _context->n_args++;
  }
  if (0 == strcmp("POST", method))
  {
    for (const char* p = eoh; p < eoh + bytesLeft; ++p)
      if (*p == '=') _context->n_args++;
  }

  // count 

  location = uri_decode_inplace(location);

  int locLen = (int)strlen(location) + 1;
  int metLen = (int)strlen(method) + 1;

  _context->memory = (char*) calloc (1,_context->n_args * sizeof(HttpHeader) + _context->n_headers * sizeof(HttpHeader) + hchars + 1 + chars + bytesLeft + 1 + locLen + metLen);
  _context->location = _context->memory + _context->n_headers * sizeof(HttpHeader) + _context->n_args * sizeof(HttpHeader) + hchars + 1 + chars + bytesLeft + 1;
  _context->method = _context->location + locLen;
  _context->args = (HttpHeader*)(_context->memory);
  _context->headers = (HttpHeader*)(_context->memory + _context->n_args * sizeof(HttpHeader));

  strcpy(_context->location, location);
  strcpy(_context->method, method);

  if (_context->n_headers && hchars)
  {
    char* strings = _context->memory + _context->n_headers * sizeof(HttpHeader) + _context->n_args * sizeof(HttpHeader);
    if (headers)
      strcpy(strings, headers);
    strings[hchars] = 0;
    
    int pair = 0;
    char* last = strings;
    int i = 0;
    bool needeol = false;
    for (; i < hchars; ++i)
    {
      if (strings[i] == ':' && !needeol)
      {
        needeol = true;
        _context->headers[pair].name = last;
        last = strings + i + 1;
        strings[i] = 0;
      }
      if (strings[i] == '\r' && needeol)
      {
        needeol = false;
        _context->headers[pair].value = last+1;
        ++pair;
        last = strings + i + 2; // strlen("\r\n")
        strings[i] = 0;
      }
    }
    if (pair < _context->n_headers)
    {
      _context->headers[pair].value = last;
      strings[i] = 0;
    }
    
    for (int i=0; i<_context->n_headers; ++i)
    {
    	uri_decode_inplace(_context->headers[i].name);
    	uri_decode_inplace(_context->headers[i].value);
    }
    
    if (_context->n_headers)
      qsort(_context->headers, _context->n_headers, sizeof(HttpHeader), &comparePairs);
  }


  if (_context->n_args && (chars || bytesLeft))
  {
    char* strings = _context->memory + _context->n_headers * sizeof(HttpHeader) + _context->n_args * sizeof(HttpHeader) + hchars + 1;
    if (args)
      strcpy(strings, args);
    if (bytesLeft)
      strncpy(strings + chars, eoh, bytesLeft);
    strings[chars + bytesLeft] = 0;
    
    int pair = 0;
    char* last = strings;
    int i = 0;
    for (; i < chars + bytesLeft; ++i)
    {
      if (strings[i] == '=')
      {
        _context->args[pair].name = last;
        last = strings + i + 1;
        strings[i] = 0;
      }
      if (strings[i] == '&')		// GET
      {
        _context->args[pair].value = last;
        ++pair;
        last = strings + i + 1;
        strings[i] = 0;
      }
    }
    if (pair < _context->n_args)
    {
      _context->args[pair].value = last;
      strings[i] = 0;
    }
    
    for (int i = 0; i < _context->n_args; ++i)
    {
      uri_decode_inplace(_context->args[i].name);
      uri_decode_inplace(_context->args[i].value);
    }
    
    if (_context->n_args)
      qsort(_context->args, _context->n_args, sizeof(HttpHeader), &comparePairs);
  }

  return true;
}

HTTPD_C_API int httpresponse_writef(HttpResponse* _context, const char* _fmt, ...)
{
  char buf[4096];
  va_list ap;

  va_start(ap, _fmt);
  int len = vsprintf(buf, _fmt, ap);
  va_end(ap);

  if (0 == len)
  return 0;

  if (_context->chunked && len)
  {
    char num[10];
    sprintf(num, "%x\r\n", len);
    httpresponse_write(_context, num, (int)strlen(num));
    len = httpresponse_write(_context, buf, len);
    httpresponse_write(_context, "\r\n", 2);
    return len;
  }

  return httpresponse_write(_context, buf, len);
}

HTTPD_C_API bool httpresponse_response (HttpResponse* _context, unsigned int _code, const char* _content, const size_t _contentLength, const char* _userHeader)
{
  _context->chunked = false;

  const char* message = "???";
  // for this few messages we don't need binsearch
  for (unsigned int i = 0; i < sizeof(HttpErrorMessages) / sizeof(HttpErrorMessages[0]); ++i)
  {
    if (HttpErrorMessages[i].code == _code)
    {
      message = HttpErrorMessages[i].message;
      break;
    }
  }

  // setup automatic parameters
  size_t contentLength = (0 == _contentLength && _content) ? strlen(_content) : _contentLength;
  const char* userHeader = _userHeader ? _userHeader : "Content-Type: text/html\r\n";

  if (_userHeader == nullptr)
  {
      // send HTTP header for html files, don't cache as most html will be dynamic
      httpresponse_writef(_context, "HTTP/1.1 %03d %s\r\n"
               "Server: UT/httpd\r\n"
               "Cache-Control: no-cache\r\n"
               "Content-Length: %d\r\n"
               "%s"
               "\r\n", _code, message, contentLength, userHeader);
  }
  else
  {
      // send HTTP header for non-html, cache for an hour as images, css, js, etc should not be dynamic
      httpresponse_writef(_context, "HTTP/1.1 %03d %s\r\n"
               "Server: UT/httpd\r\n"
               "Cache-Control: max-age=3600\r\n"
               "Content-Length: %d\r\n"
               "%s"
               "\r\n", _code, message, contentLength, userHeader);
  }
  
  // write the actual content (the "page")
  if (_content)
  {
    httpresponse_write(_context, _content, (int)contentLength);
  }

  return false;
}

HTTPD_C_API const char* httpresponse_location (HttpResponse* _context)
{
	return _context->location;
}

HTTPD_C_API const char*	httpresponse_method(HttpResponse* _context)
{
	return _context->method;
}

HTTPD_C_API int httpresponse_get_n_args(HttpResponse* _context)
{
	return _context->n_args;
}

HTTPD_C_API int httpresponse_get_n_headers(HttpResponse* _context)
{
	return _context->n_headers;
}

HTTPD_C_API void httpresponse_begin(HttpResponse* _context, unsigned int _code, const char* _userHeader)
{
  const char* message = "???";
  // for this few messages we don't need binsearch
  for (unsigned int i = 0; i < sizeof(HttpErrorMessages) / sizeof(HttpErrorMessages[0]); ++i)
  {
    if (HttpErrorMessages[i].code == _code)
    {
      message = HttpErrorMessages[i].message;
      break;
    }
  }

  const char* userHeader = _userHeader ? _userHeader : "Content-Type: text/html\r\n";

  // send HTTP header
  httpresponse_writef(_context, "HTTP/1.1 %03d %s\r\n"
           "Server: dbalster/http\r\n"
           "Cache-Control: no-cache\r\n"
           "Transfer-Encoding: chunked\r\n"
           "%s"
           "\r\n", _code, message, userHeader);

  _context->chunked = true;
}

HTTPD_C_API void httpresponse_end(HttpResponse* _context )
{
    httpresponse_write(_context, "0\r\n\r\n", 5);
    _context->chunked = false;
}

HTTPD_C_API const char* httpresponse_get_arg(HttpResponse* _context, const char* _key) 
{
	HttpHeader p;
	p.name = ((char*)_key);
  const HttpHeader* result = (const HttpHeader*)bsearch(&p, _context->args, _context->n_args, sizeof(HttpHeader), &comparePairs);
  if (result) return result->value;
  return 0;
}

HTTPD_C_API const HttpHeader* httpresponse_get_arg_by_index(HttpResponse* _context, int _index) 
{
  if (_index < 0 || _index >= _context->n_args) return 0;
  return &(_context->args[_index]);
}

HTTPD_C_API const char* httpresponse_get_header(HttpResponse* _context, const char* _key) 
{
	HttpHeader p;
	p.name = ((char*)_key);
	const HttpHeader* result = (const HttpHeader*)bsearch(&p, _context->headers, _context->n_headers, sizeof(HttpHeader), &comparePairs);
  if (result) return result->value;
  return 0;
}

HTTPD_C_API const HttpHeader* httpresponse_get_header_by_index(HttpResponse* _context, int _index) 
{
  if (_index < 0 || _index >= _context->n_headers) return 0;
  return &(_context->headers[_index]);
}

// httpd

struct _Httpd
{
  int                 socket;
  void*               userdata;
	HttpRequestHandler  handler;
};

Httpd* httpd_create ( unsigned short _port, HttpRequestHandler _handler, void* _userdata )
{
  bool result = false;
	Httpd* server = (Httpd*) calloc(1,sizeof(Httpd));
		
	server->handler = _handler;
  server->userdata = _userdata;

  server->socket = (int) socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server->socket == -1)
  {
    printf ("socket");
  }

  int opt = 1;
  if (setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)))
  {
    printf ("setsocketopt");
  }
  
  // non-blocking
  u_long nonblock = 1;
  ioctlsocket(server->socket, FIONBIO, &nonblock);

  struct sockaddr_in sa;
  sa.sin_port = htons(_port);
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = INADDR_ANY;
  if (bind(server->socket, (struct sockaddr*)&sa, sizeof(sa)))
  {
    printf ("bind");
  }

  if (-1 == listen(server->socket, 5))
  {
    printf ("listen");
  }
  else
  {
    result = true;
  }
  
  if (result==false)
  {
    httpd_destroy(server);
    return 0;
  }

  return server;
}

void httpd_destroy (Httpd* _server)
{
  if (_server)
  {
    closesocket(_server->socket);
    free (_server);
  }
}

void httpd_process (Httpd* _server, bool _blocking)
{
  if (-1 == _server->socket) return;

  struct sockaddr_in sa;
  int sin_size = sizeof(sa);

  if (false == _blocking)
  {
    fd_set fds;
    struct timeval tv;
    int rc;

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    FD_ZERO(&fds);
    FD_SET(_server->socket, &fds);
    rc = select(_server->socket + 1, &fds, NULL, NULL, &tv);

    if (rc <= 0) return;
  }

  int client = (int)accept(_server->socket, (struct sockaddr*)&sa, (socklen_t*)&sin_size);
  if (client < 0) return;

  HttpResponse* req = httpresponse_create (client); 
  if (req)
  {
    if (httpresponse_parse (req))
    {
      _server->handler(req,_server->userdata);
    }
    httpresponse_destroy(req);
  }
  else
  {
    closesocket(client);
  }
}


struct _HttpRequest {
  unsigned short  result;
  unsigned short  port;
  size_t          bytesUsed;
  size_t          maxBytes;
  // internal scratchpad
  char            bytes[1];  
};

void* httprequest_alloc( HttpRequest* _req, size_t _size )
{
  char* p = _req->bytes + _req->bytesUsed;
  _req->bytesUsed += _size;
  return p;
}

HTTPD_C_API void httprequest_sprintf( HttpRequest* _req, const char* _fmt, ... )
{
  va_list ap;
  va_start(ap,_fmt);
  // TODO: vsnprintf
  int len = vsprintf(_req->bytes + _req->bytesUsed,_fmt,ap);
  _req->bytesUsed += len;
  va_end(ap);
}

HTTPD_C_API void httprequest_strcat( HttpRequest* _req, const char* _orig )
{
  char* p = _req->bytes + _req->bytesUsed;
  int i=0;
  while (*_orig && _req->bytesUsed<_req->maxBytes)
  {
    p[i++] = *_orig++;
    _req->bytesUsed++;
  }
}

static char* httprequest_strdup( HttpRequest* _req, const char* _orig )
{
  char* p = _req->bytes + _req->bytesUsed;
  int i=0;
  while (*_orig && _req->bytesUsed<_req->maxBytes)
  {
    p[i++] = *_orig++;
    _req->bytesUsed++;
  }
  p[i] = 0;
  _req->bytesUsed++;
  return p;
}

HTTPD_C_API HttpRequest* httprequest_create( const char* _hostname, unsigned short _port, const char* _location, const char* _method, size_t _maxBytes )
{
  HttpRequest* req = (HttpRequest*) calloc(1,sizeof(HttpRequest)+_maxBytes);
  if (req)
  {
    req->maxBytes = _maxBytes;
    req->bytesUsed = 0;    
    httprequest_strdup(req,_hostname);
    req->port = _port;    
    httprequest_sprintf(req,"%s %s HTTP/1.1\r\n",_method,_location);
    httprequest_strcat(req,"Host: dbalster\r\n");
    httprequest_strcat(req,"Connection: close\r\n");
    httprequest_strcat(req,"Content-Length: 00000000\r\n");
    
    httprequest_reset(req);
  }
  return req;
}

HTTPD_C_API void httprequest_reset( HttpRequest* _req )
{
  char* p = _req->bytes+1+strlen(_req->bytes);
  p = strstr(p,"Content-Length:");
  if (p)
  {
    _req->bytesUsed = p-_req->bytes;
    httprequest_strcat(_req,"Content-Length: 00000000\r\n");
  }
}

static bool httprequest_error(const char* _msg, ... )
{
  char msg[1000];
  va_list ap;
  va_start(ap,_msg);
  vsprintf(msg,_msg,ap);
  va_end(ap);
  fprintf(stderr,"ERROR: %s: %s\n",strerror(errno),msg);
  return false;
}

HTTPD_C_API bool httprequest_execute( HttpRequest* _req )
{
  bool result = false;
  char* hostname = _req->bytes;
  char* start = hostname+strlen(hostname)+1;
  char* p = strstr(start,"\r\n\r\n");
  if (p==0)
  {
    httprequest_strcat(_req,"\r\n");
    p = strstr(start,"\r\n\r\n");
  }
  if (p)
  {
    p += 4; // skip CRLFCRLF
    char* end = _req->bytes + _req->bytesUsed;
    int contentSize = (int)(end - p);
    char* contentLength = strstr(start,"Content-Length: ") + strlen("Content-Length: ");
    sprintf(contentLength,"%8d",contentSize);
    contentLength[8] = '\r';
  
    struct hostent* server = gethostbyname(hostname);
    if (0==server)
    {
      return httprequest_error(hostname);
    }
    int sock = (int) socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (sock==-1)
    {
      return httprequest_error("socket()");
    }
    struct sockaddr_in serv_addr;
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
    serv_addr.sin_port = htons(_req->port);
    if (connect(sock,(struct sockaddr*)&serv_addr,sizeof(serv_addr)) < 0)
    {
      httprequest_error("connect()");
    }
    else
    {
      int len = strlen(start);
      int written = send(sock,start,len,0);
      if (written == len)
      {
        int read = recv(sock,_req->bytes,_req->maxBytes,0);
        if (read>=0)
        {
          _req->bytes[read]=0;

          if ( recv(sock,_req->bytes+read,_req->maxBytes-read,MSG_PEEK) || errno==EAGAIN)
          {
            int read2 = recv(sock,_req->bytes+read,_req->maxBytes-read,0);
            if (read2>0) _req->bytes[read+read2]=0; 
          }
          result = true;
        }
      }
    }
    closesocket(sock);
  }
  else
  {
    httprequest_error("invalid header and content");
  }
  
  return result;
}

HTTPD_C_API size_t httprequest_get_content_length( HttpRequest* _req )
{
  return strtoul(httprequest_get_header(_req,"Content-Length:"),0,0);
}

HTTPD_C_API const char* httprequest_get_header( HttpRequest* _req, const char* _header )
{
  char* p = strstr(_req->bytes,_header);
  if (p)
  {
    return p+strlen(_header);
  }
  return 0;
}

HTTPD_C_API const char* httprequest_get_content( HttpRequest* _req )
{
  return strstr(_req->bytes,"\r\n\r\n") + 4;
}

HTTPD_C_API int httprequest_get_result( HttpRequest* _req )
{
  int result = 0;
  result = strtoul( _req->bytes + strlen("HTTP/1.1"),0,0);
  return result;
}

HTTPD_C_API void httprequest_destroy( HttpRequest* _req )
{
  if (_req)
  {
    free(_req);
  }
}

