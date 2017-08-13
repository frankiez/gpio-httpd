#define HTTPD_MTHD_UNKNOWN                    0
#define HTTPD_MTHD_GET                        1
#define HTTPD_MTHD_POST                       2
#define HTTPD_MTHD_GET_GPIO                   3

typedef struct request {
  unsigned char method;
  char *uri;
  unsigned int error;
} request;

array* httpd_str_split(char *delim, char *buffer);
request httpd_parse_request(char *buffer);
void httpd_parse_request_head(request *req, char *line);