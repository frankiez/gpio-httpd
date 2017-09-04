#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <math.h>
#include <thread>
#include <iostream>
#include <fstream>

#include "http-status.h"
#include "http-parser.h"
#include "http-response.h"
#include "http-server.h"
#include "string_ops.h"

#define TCP_BUFFER_SIZE 1024
#define IO_STREAM_BUFFER_SIZE 262144

//2MB.
#define HTTP_MAX_REQUEST_SIZE 2097152

using std::string;
using std::ifstream;

const string SERVER_NAME = "gpiohttpd";
const string SERVER_VERSION = "0.1.0";
const string SERVER_SIGNATURE = SERVER_NAME + "/" + SERVER_VERSION;

http_srv server_create(const unsigned int port, const unsigned int max_queue, string document_root_fp)
{
  http_srv server = {};

  //Create the socket.
  server.sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server.sock_fd < 0)
  {
    printf("Fatal: Could not create socket.\n");
    exit(1);
  }

  //Set socket options
  int optval = 1;
  if ( setsockopt(server.sock_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval,
                  sizeof(int)) == -1 )
  {
    printf("Fatal: Could not setsockopt.\n");
    exit(1);
  }

  bzero((char *) &server.addr, sizeof(server.addr));
  server.addr.sin_family = AF_INET;

  //Automatically determine IP address
  server.addr.sin_addr.s_addr = htonl(INADDR_ANY);

  //Set listen port
  server.addr.sin_port = htons(port);

  //Bind to socket.
  if (::bind(server.sock_fd, (struct sockaddr *) &server.addr,
	   sizeof(server.addr)) < 0)
  {
    printf("Fatal: could not bind to socket on port %i.\n", port);
    close(server.sock_fd);
    exit(1);
  }

  //Mark the socket as ready to listen.
  if (listen(server.sock_fd, max_queue) < 0)
  {
    printf("Fatal: could not open socket for listening on port %i.\n", port);
    close(server.sock_fd);
    exit(1);
  }

  //Set the root directory
  server.document_root_dir = document_root_fp;

  return server;

}

void server_loop(http_srv &server)
{
  struct sockaddr_in client_addr;
  int client_fd;
  socklen_t client_len;

  while (1) {

    //Wait for a connection.
    client_fd = accept(server.sock_fd, (struct sockaddr *) &client_addr, &client_len);
    if (client_fd < 0)
      printf("Error when accepting socket\n");

    std::thread req(server_handle_request, server, client_fd);
    req.detach();

  }

}

void server_handle_request(const http_srv &server, const int client_fd)
{
  http_client client;
  char buf[TCP_BUFFER_SIZE];
  unsigned int r_bytes_read = 0;
  string buffer, response;
  size_t eor_search;
  http_request http_req;
  http_response http_res;

  client.client_fd = client_fd;

  //Clear the buffer data.
  bzero(buf, TCP_BUFFER_SIZE);

  //Read the buffer data.
  while (1)
  {
    client.n = read(client.client_fd, buf, TCP_BUFFER_SIZE);
    if (client.n <= 0)
    {
      //printf("ERROR reading from socket\n");
      client.error = CLIENT_UNKNOWN_ERROR;
      break;
    }

    //Increment the number of bytes read.
    r_bytes_read += strlen(buf);

    //Add the read-in buffer to the string buffer.
    buffer += buf;

    //If we have been waiting too long...

    //Request size too big. Drop.
    if ( r_bytes_read >= HTTP_MAX_REQUEST_SIZE )
    {
      client.error = CLIENT_REQUEST_TOO_LARGE;
      break;
    }

    //If \r\n exists, end of request.
    eor_search = buffer.rfind("\r\n\r\n");
    if ( eor_search!=string::npos )
    {
      break;
    }

    //Reclear the buffer.
    bzero(buf, TCP_BUFFER_SIZE);

  }

  http_req = http_parse_request(buffer);

  if ( (int)http_req.error > 0 )
  {
    //Handle error respone.
    http_res = http_create_error_response(http_req);
  }
  else {

    //Route the request.
    switch(http_req.type)
    {
      case request_type::HTTP_GET:
        http_res = http_create_get_response(http_req, server.document_root_dir);
        break;
      case request_type::HTTP_POST:
        http_res = http_create_post_response(http_req, server.document_root_dir);
        break;
      default:
        http_res = http_create_error_response(http_req);
        break;
    }

  }

  if ( http_res.serve_file == "" )
  {
    server_reply(client, http_res);
  }
  else
  {
    server_stream_file(client, http_res);
  }

  //Should I close, i.e. keep open?
  //If keep open, re-call this function?
  close(client.client_fd);

}

string server_generate_head_string(const http_response &response)
{
  string str_res;

  //Head.
  str_res = "HTTP/1.1 " + to_string(response.status) + " " + \
             response.status_msg + "\r\n";

  //Date.
  str_res += "Date: " + response.date_time + "\r\n";

  //Server.
  str_res += "Server: " + SERVER_SIGNATURE + "\r\n";

  //Various other headers.

  //Content type.
  str_res += "Content-Type: " + response.content_type + "\r\n";

  //Data.
  if ( response.data_length > 0 )
  {
    str_res += "Content-Length: " + to_string(response.data_length) + "\r\n";

    //Only write data on non-file streams
    if ( response.serve_file == "" )
    {
      str_res += "\r\n" + response.data;
    }

  }

  //End of head.
  str_res += "\r\n";

  return str_res;

}

void server_reply(const http_client &client, const http_response &response)
{
  string str_res;

  str_res = server_generate_head_string(response);

  //Send it.
  write(client.client_fd, str_res.c_str(), strlen(str_res.c_str()));
  if (client.n <= 0)
    printf("ERROR writing to socket\n");

}

void server_stream_file(const http_client &client, http_response &response)
{
  long bytes_read = 0;
  int loop_buffer_byte_size, last_loop_index = 0, i = 0;
  string str_res;

  //Open file stream.
  ifstream is (response.serve_file, ifstream::binary);

  //If there is a problem opening the stream, fallback to error.
  if (! is )
  {
    response.status = http_status::FORBIDDEN;
    response.content_type = "text/html;";
    response.data = "<h1>Unauthorized</h1><p>You are not authorized to access" \
                    " this resource.</p>";
    response.serve_file = "";
    http_response_set_data_length(response);
    server_reply(client, response);
    return;
  }

  //Generate reply-head
  str_res = server_generate_head_string(response);

  //Write the head.
  write(client.client_fd, str_res.c_str(), strlen(str_res.c_str()));
  if (client.n <= 0)
    printf("ERROR writing to socket\n");

  //Create a buffer.
  char buffer[IO_STREAM_BUFFER_SIZE];

  //Set the default loop buffer byte size.
  loop_buffer_byte_size = IO_STREAM_BUFFER_SIZE;

  //Determine the last loop index.
  //This is done pre-loop, as comparing a loop iteration counter against a
  //pre-computed value is faster than computing and comparing large numbers
  //continously.
  if ( response.data_length > 0 && loop_buffer_byte_size > 0 )
    last_loop_index = ceil(response.data_length / loop_buffer_byte_size);

  while ( bytes_read < response.data_length )
  {

    //For the last iteration of the loop, only read the remaining number of bytes
    //into the buffer, and only send the remaining number of bytes to the client.
    if ( i == last_loop_index )
      loop_buffer_byte_size = response.data_length - bytes_read;

    //Read into buffer.
    is.read(buffer, loop_buffer_byte_size);

    //Write to client.
    write(client.client_fd, buffer, loop_buffer_byte_size);
    if ( client.n <= 0 )
      printf("ERROR writing to socket\n");

    bytes_read += loop_buffer_byte_size;
    i++;

  }

}
