#ifndef HTTP_STATUS_
#define HTTP_STATUS_
#include <string>

using std::string;

enum http_status
{
  OK = 200,
  CREATED = 201,
  ACCEPTED = 202,
  NON_AUTHORITATIVE_INFO = 203,
  NO_CONTENT = 204,
  MULTIPLE_CHOICES = 300,
  MOVED_PERMANENTLY = 301,
  FOUND = 302,
  NOT_MODIFIED = 304,
  TEMPORARY_REDIRECT = 307,
  PERMANENT_REDIRECT = 308,
  BAD_REQUEST = 400,
  UNAUTHORIZED = 401,
  FORBIDDEN = 403,
  NOT_FOUND = 404,
  METHOD_NOT_ALLOWED = 405,
  REQUEST_URI_TOO_LONG = 414,
  INTERNAL_SERVER_ERROR = 500,
  NOT_IMPLEMENTED = 501,
  BAD_GATEWAY = 502,
  SERVICE_UNAVAILABLE = 503,
  HTTP_VERSION_NOT_SUPPORTED = 505
};

/**
 * Returns the related HTTP status string from a http_status
 * @param  status The http_status code
 * @return        The status string
 */
string http_get_status_string(const http_status &status);

#endif
