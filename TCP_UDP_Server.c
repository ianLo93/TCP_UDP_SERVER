/*
The program builds an server for comunication between clients
Support functionalities of LOGIN, LOGOUT, WHO, SEND, BROADCAST, SHARE on UDP or TCP
Support all regular files transmission
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <pthread.h>
#include <math.h>

#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100

struct tcp_client {
  char userid[21];
  int fd;
};

struct udp_client {
  struct sockaddr_in user;
  char userid[21];
};

int tcp_sock;
int udp_sock;

pthread_mutex_t tcp_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t udp_mutex = PTHREAD_MUTEX_INITIALIZER;

struct tcp_client tcp_client_set[MAX_CLIENTS];
int tcp_client_index = 0;
struct udp_client udp_client_set[MAX_CLIENTS];
int udp_client_index = 0;

int idcmp(const void* id1, const void* id2);
void idsort(char* arr[], int n);
int search_tcpfd(int fd);
int search_udpip(char* clientip);
int search_tcpuserid(char* userid);
int search_udpuserid(char* userid);
int logout_tcpuser(int fd);
int logout_udpuser(char* userid, int udp_sock);
void formText(char* text, char* userid, char* msglen, char* message);
void* tcp_thread( void* arg );

int main(int argc, char* argv[])
{
  setbuf(stdout, NULL);
  if (argc != 3) {
    fprintf(stderr, "ERROR Invalid argument(s)\n");
    fprintf(stderr, "USAGE: a.out <tcp-port> <udp-port>\n");
    return EXIT_FAILURE;
  }

  printf("MAIN: Started server\n");
  fflush(stdout);

  unsigned short tcp_port = atoi(argv[1]);
  unsigned short udp_port = atoi(argv[2]);

  fd_set readfds;
  tcp_sock = socket(PF_INET, SOCK_STREAM, 0);
  udp_sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (tcp_sock < 0 || udp_sock < 0)
  {
    perror("socket() failed..");
    return EXIT_FAILURE;
  }

  struct sockaddr_in tcp_server;
  struct sockaddr_in udp_server;
  struct sockaddr_in client;

  tcp_server.sin_family = PF_INET;
  udp_server.sin_family = AF_INET;
  tcp_server.sin_addr.s_addr = htonl(INADDR_ANY);
  udp_server.sin_addr.s_addr = htonl(INADDR_ANY);

  tcp_server.sin_port = htons(tcp_port);
  printf("MAIN: Listening for TCP connections on port: %d\n", tcp_port);
  fflush(stdout);
  udp_server.sin_port = htons(tcp_port);
  printf("MAIN: Listening for UDP datagrams on port: %d\n", udp_port);
  fflush(stdout);

  int tcp_len = sizeof(tcp_server);
  int udp_len = sizeof(udp_server);

  if (bind(tcp_sock, (struct sockaddr*)&tcp_server, tcp_len) < 0 ||
      bind(udp_sock, (struct sockaddr*)&udp_server, udp_len) < 0)
  {
    perror("bind() failed..");
    return EXIT_FAILURE;
  }

  if (listen(tcp_sock, MAX_CLIENTS) == -1) {
    perror("listen() failed..");
    return EXIT_FAILURE;
  }

  char buffer[BUFFER_SIZE];
  int len = sizeof(client);

  while (1)
  {
    // Clear fd set and add tcp, udp, and client sockets to fd set
    FD_ZERO(&readfds);
    FD_SET(tcp_sock, &readfds);
    FD_SET(udp_sock, &readfds);

    int ready = select(FD_SETSIZE, &readfds, NULL, NULL, NULL);
    if (ready == -1) {
      perror("select() failed..");
    }
    int n, found;
    if (FD_ISSET(udp_sock, &readfds)) { // A UDP user try to send a datagram
      memset(buffer, 0, BUFFER_SIZE);
      // Get msg from udp client
      n = recvfrom(udp_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&client, (socklen_t*)&len);
      if (n < 0) {
        perror( "recvfrom() failed" );
        continue;
      }
      char* clientip = inet_ntoa(client.sin_addr);
      printf("MAIN: Rcvd incoming UDP datagram from: %s\n", clientip);
      fflush(stdout);
      found = search_udpip(clientip);

// --------------------------------   LOGIN  --------------------------------
      if (strncmp("LOGIN", buffer, 5) == 0) { // A logged in user try to log in again
        if (found < 0) {
          char username[21];
          memset(username, 0, sizeof(username));
          for (int i=6; i<n-1; i++)  // Copy the username
            username[i-6] = buffer[i];
          username[strlen(username)] = 0;
          printf("MAIN: Rcvd LOGIN request for userid %s\n", username);
          fflush(stdout);
          int used = search_tcpuserid(username);
          if (used >= 0) { // Send error message if any TCP client has connected
            char* error_msg = "ERROR Already connected\n";
            sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
            printf("MAIN: Sent ERROR (Already connected)\n");
            fflush(stdout);
            continue;
          }
          sendto(udp_sock, "OK\n", 3, 0, (struct sockaddr*)&client, len);
          logout_udpuser(username, udp_sock);
          struct udp_client udp_user;
          udp_user.user = client;
          memset(udp_user.userid, 0, sizeof(udp_user.userid));
          memcpy(udp_user.userid, username, strlen(username));
          pthread_mutex_lock(&udp_mutex);
          udp_client_set[udp_client_index++] = udp_user;
          pthread_mutex_unlock(&udp_mutex);
          continue;
        }
        // Get the username
        char username[21];
        memset(username, 0, sizeof(username));
        for (int i=6; i<n-1; i++) // Copy the username
          username[i-6] = buffer[i];
        username[strlen(username)] = '\0';
        printf("MAIN: Rcvd LOGIN request for userid %s\n", username);
        fflush(stdout);
        // Check if the username has been used by a tcp user
        int used = search_tcpuserid(username);
        if (used >= 0) { // Send error message if any TCP client has connected
          char error_msg[50] = "ERROR Already connected\n";
          sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
          printf("MAIN: Sent ERROR (Already connected)\n");
          fflush(stdout);
          continue;
        }
        // Check if the username has been used by a udp user
        used = search_udpuserid(username);
        if (used == found) {
          char error_msg[50] = "ERROR Already connected\n";
          sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
          printf("MAIN: Sent ERROR (Already connected)\n");
          fflush(stdout);
          continue;
        }
        for (int i=found; i<udp_client_index; i++)
          udp_client_set[i] = udp_client_set[i+1];
        udp_client_index--;
        if (used >= 0)
          logout_udpuser(username, udp_sock);
        struct udp_client udp_user;
        udp_user.user = client;
        memset(udp_user.userid, 0, sizeof(udp_user.userid));
        memcpy(udp_user.userid, username, strlen(username));
        udp_client_set[udp_client_index++] = udp_user;
        sendto(udp_sock, "OK\n", 3, 0, (struct sockaddr*)&client, len);
      } // Login
// ----------------------------------   WHO   ----------------------------------
      else if (strncmp("WHO", buffer, 3) == 0) {
        printf("MAIN: Rcvd WHO request\n");
        fflush(stdout);
        if (found < 0) {
          char* error_msg = "ERROR Not logged in\n";
          sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
          printf("MAIN: Sent ERROR (Not logged in)\n");
          fflush(stdout);
          continue;
        }
        int k = 0;
        char* all_users[tcp_client_index+udp_client_index];
        for (int i=0; i<tcp_client_index; i++, k++)
          all_users[k] = tcp_client_set[i].userid;
        for (int i=0; i<udp_client_index; i++, k++)
          all_users[k] = udp_client_set[i].userid;
        idsort(all_users, tcp_client_index+udp_client_index);
        char text[BUFFER_SIZE];
        strcpy(text, "OK\n");
        for (int i=0; i<tcp_client_index+udp_client_index; i++) {
          strcat(text, all_users[i]);
          strcat(text, "\n");
        }
        sendto(udp_sock, text, strlen(text), 0, (struct sockaddr*)&client, len);
        continue;
      } // WHO

// --------------------------------  LOGOUT  --------------------------------
      else if (strncmp("LOGOUT", buffer, 6) == 0) {
        if (found < 0) {
          char* error_msg = "ERROR Not logged in\n";
          sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
          printf("MAIN: Sent ERROR (Not logged in)\n");
          fflush(stdout);
          continue;
        }
        logout_udpuser(udp_client_set[found].userid, udp_sock);
        continue;
      } // LOGOUT

// --------------------------------   SEND   --------------------------------
      else if(strncmp("SEND", buffer, 4) == 0) {
        // Get recipient id
        int i=5;
        char recipient[21];
        memset(recipient, 0, sizeof(recipient));
        for (; i<strlen(buffer); i++) {
          if ( isspace(buffer[i]) || i>=27 )
            break;
          recipient[i-5] = buffer[i];
        }
        recipient[i-5] = 0;
        printf("MAIN: Rcvd SEND request to userid %s\n", recipient);
        fflush(stdout);
        if (found < 0) {
          char* error_msg = "ERROR Not logged in\n";
          sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
          printf("MAIN: Sent ERROR (Not logged in)\n");
          fflush(stdout);
          continue;
        }
        // Check recipient id length
        if (strlen(recipient)<3 || strlen(recipient)>20) {
          char* error_msg = "ERROR Unknown userid\n";
          sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
          printf("MAIN: Sent ERROR (Unknown userid)\n");
          continue;
        }
        // Check send format
        i++;
        // Get msglen
        char msglen_[4];
        int msglen;
        memset(msglen_, 0, sizeof(msglen_));
        int j=0;
          msglen_[j] = buffer[i];
        msglen_[j] = 0;
        msglen = atoi(msglen_);
        if (msglen < 1 || msglen > 994) { // Over lower or upper bounds
          char* error_msg = "ERROR Invalid msglen\n";
          sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
          printf("MAIN: Sent ERROR (Invalid msglen)\n");
          fflush(stdout);
          continue;
        }
        // Check SEND format
        i++;
        // Get the message
        char message[994];
        memset(message, 0, 994);
        j=0;
        for (; i<strlen(buffer); i++, j++)
          message[j] = buffer[i];
        message[j-1] = 0;
        int pos = search_tcpuserid(recipient);
        if (pos < 0) {
          pos = search_udpuserid(recipient);
          if (pos < 0) {
            char* error_msg = "ERROR Unknown userid\n";
            sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
            printf("MAIN: Sent ERROR (Unknown userid)\n");
            fflush(stdout);
            continue;
          } else {
            sendto(udp_sock, "OK\n", 3, 0, (struct sockaddr*)&client, len);
            char text[BUFFER_SIZE];
            memset(text, 0, BUFFER_SIZE);
            formText(text, udp_client_set[found].userid, msglen_, message);
            text[strlen(text)] = 0;
            // strcat(text, "\n");
            sendto(udp_sock, text, strlen(text), 0, (struct sockaddr*)&udp_client_set[pos].user,
                  sizeof(udp_client_set[pos].user));
          }
        } else {
          sendto(udp_sock, "OK\n", 3, 0, (struct sockaddr*)&client, len);
          char text[BUFFER_SIZE];
          memset(text, 0, sizeof(text));
          formText(text, udp_client_set[found].userid, msglen_, message);
          text[strlen(text)] = 0;
          // strcat(text, "\n");
          send(tcp_client_set[pos].fd, text, strlen(text), 0);
        }
      } // SEND

// ------------------------------   BROADCAST   ------------------------------
      else if(strncmp("BROADCAST", buffer, 9) == 0) {
        printf("MAIN: Rcvd BROADCAST request\n");
        fflush(stdout);
        if (found < 0) {
          char* error_msg = "ERROR Not logged in\n";
          sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
          printf("MAIN: Sent ERROR (Not logged in)\n");
          fflush(stdout);
          continue;
        }
        // Get the msglen
        char msglen_[4];
        int msglen;
        memset(msglen_, 0, sizeof(msglen_));
        int i = 10;
        int j = 0;
        for(; buffer[i] != ' '; i++, j++)
          msglen_[j] = buffer[i];
        msglen_[j] = 0;
        msglen = atoi(msglen_);
        if (msglen < 1 || msglen > 994) { // Over lower or upper bounds
          char* error_msg = "ERROR Invalid msglen\n";
          sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
          printf("MAIN: Sent ERROR (Invalid msglen)\n");
          fflush(stdout);
          continue;
        }
        // Get the message
        i++;
        char message[994];
        memset(message, 0, 994);
        j=0;
        for (; i<strlen(buffer); i++, j++)
          message[j] = buffer[i];
        message[j-1] = 0;
        char text[BUFFER_SIZE];
        memset(text, 0, sizeof(text));
        formText(text, udp_client_set[found].userid, msglen_, message);
        text[strlen(text)] = 0;
        // strcat(text, "\n");
        sendto(udp_sock, "OK\n", 3, 0, (struct sockaddr*)&client, len);
        for (int k=0; k<tcp_client_index; k++) {
          send(tcp_client_set[k].fd, text, strlen(text), 0);
        }
        for (int k=0; k<udp_client_index; k++) {
          sendto(udp_sock, text, strlen(text), 0, (struct sockaddr*)&udp_client_set[k].user,
                sizeof(udp_client_set[k].user));
        }
      }
      else if(strncmp("SHARE", buffer, 5) == 0) {
        char* error_msg = "ERROR SHARE not supported over UDP\n";
        sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
        printf("MAIN: Sent ERROR (SHARE not supported over UDP)\n");
        fflush(stdout);
      }
      else {
        char* error_msg = "ERROR Unknown COMMAND\n";
        sendto(udp_sock, error_msg, strlen(error_msg), 0, (struct sockaddr*)&client, len);
        printf("MAIN: Sent ERROR (Unknown COMMAND)\n");
        fflush(stdout);
        continue;
      }
    } // UDP
    if (FD_ISSET(tcp_sock, &readfds)) { // A TCP server trying to connect
      int* newsock = malloc(sizeof(int));
      *newsock = accept( tcp_sock, (struct sockaddr *)&client, (socklen_t *)&len );
      char* clientip = inet_ntoa( client.sin_addr );
      printf("MAIN: Rcvd incoming TCP connection from: %s\n", clientip);
      fflush(stdout);
      pthread_t tid;
      int rc = pthread_create( &tid, NULL, tcp_thread, (void*)newsock );
      if ( rc != 0 ) {
        fprintf(stderr, "MAIN: Could not create thread (%d)\n", rc);
      }
    }
  }

  return EXIT_SUCCESS;
}

int idcmp(const void* id1, const void* id2) {
 return strcmp(*(const char**)id1, *(const char**)id2);
}

void idsort(char* arr[], int n) {
 qsort(arr, n, sizeof(char*), idcmp);
}

int search_tcpfd(int fd) {
  for (int i=0; i<tcp_client_index; i++) {
    if (tcp_client_set[i].fd == fd)
      return i;
  }
  return -1;
}

int search_udpip(char* clientip) {
  pthread_mutex_lock(&udp_mutex);
  for (int i=0; i<udp_client_index; i++) {
    if ( clientip == inet_ntoa(udp_client_set[i].user.sin_addr) ) {
      pthread_mutex_unlock(&udp_mutex);
      return i;
    }
  }
  pthread_mutex_unlock(&udp_mutex);
  return -1;
}

int search_tcpuserid(char* userid) {
  pthread_mutex_lock(&tcp_mutex);
  for (int i=0; i<tcp_client_index; i++) {
    if (strcmp(tcp_client_set[i].userid, userid) == 0) {
      pthread_mutex_unlock(&tcp_mutex);
      return i;
    }
  }
  pthread_mutex_unlock(&tcp_mutex);
  return -1;
}

int search_udpuserid(char* userid) {
  pthread_mutex_lock(&udp_mutex);
  for (int i=0; i<udp_client_index; i++) {
    if (strcmp(udp_client_set[i].userid, userid) == 0) {
      pthread_mutex_unlock(&udp_mutex);
      return i;
    }
  }
  pthread_mutex_unlock(&udp_mutex);
  return -1;
}

int logout_tcpuser(int fd) {
  int pos = search_tcpfd(fd);
  if (pos < 0) return -1;
  send(fd, "OK\n", 3, 0);
  pthread_mutex_lock(&tcp_mutex);
  for (int i=pos; i<tcp_client_index; i++) {
    tcp_client_set[i] = tcp_client_set[i+1];
  }
  tcp_client_index--;
  pthread_mutex_unlock(&tcp_mutex);
  return pos;
}

int logout_udpuser(char* userid, int udp_sock) {
  int pos = search_udpuserid(userid);
  if (pos < 0)
    return -1;
  int length = sizeof(udp_client_set[pos].user);
  printf("MAIN: Rcvd LOGOUT request\n");
  fflush(stdout);
  pthread_mutex_lock(&udp_mutex);
  sendto(udp_sock, "OK\n", 3, 0, (struct sockaddr*)&udp_client_set[pos].user, length);
  for (int i=pos; i<udp_client_index; i++) {
    udp_client_set[i] = udp_client_set[i+1];
  }
  udp_client_index--;
  pthread_mutex_unlock(&udp_mutex);
  return pos;
}

void formText(char* text, char* userid, char* msglen, char* message){
  memset(text, 0, BUFFER_SIZE);
  strcpy(text, "FROM ");
  strcat(text, userid);
  strcat(text, " ");
  strcat(text, msglen);
  strcat(text, " ");
  strcat(text, message);
  return;
}

void* tcp_thread( void* arg ) {
  setbuf(stdout, NULL);
  int newsd = *((int*)arg);
  pthread_detach(pthread_self());
  char buffer[BUFFER_SIZE];
  int n;
  while (1) {
    memset(buffer, 0, BUFFER_SIZE);
    n = recv( newsd, buffer, BUFFER_SIZE, 0 );
    if (n==0) {
      printf("CHILD %u: Client disconnected\n", (unsigned int)pthread_self());
      fflush(stdout);
      logout_tcpuser( newsd );
      close( newsd );
      break;
    }
    int found = search_tcpfd( newsd );
// --------------------------------   LOGIN   --------------------------------
    if (strncmp("LOGIN", buffer, 5) == 0) { // A logged in user try to log in again
      if (found < 0) {
        char username[21];
        memset(username, 0, sizeof(username));
        for (int i=6; i<n-1; i++)  // Copy the username
          username[i-6] = buffer[i];
        username[strlen(username)] = 0;
        printf("CHILD %u: Rcvd LOGIN request for userid %s\n", (unsigned int)pthread_self(), username);
        fflush(stdout);
        int used = search_tcpuserid(username);
        if (used >= 0) { // Send error message if any TCP client has connected
          char* error_msg = "ERROR Already connected\n";
          send(newsd, error_msg, strlen(error_msg), 0);
          printf("CHILD %u: Sent ERROR (Already connected)\n", (unsigned int)pthread_self());
          fflush(stdout);
          continue;
        }
        logout_udpuser(username, udp_sock);
        struct tcp_client tcp_user;
        tcp_user.fd = newsd;
        memset(tcp_user.userid, 0, sizeof(tcp_user.userid));
        memcpy(tcp_user.userid, username, strlen(username));
        pthread_mutex_lock (&tcp_mutex);
        tcp_client_set[tcp_client_index++] = tcp_user;
        pthread_mutex_unlock (&tcp_mutex);
        send(newsd, "OK\n", 3, 0);
        continue;
      }
      // Get the username
      char username[21];
      memset(username, 0, sizeof(username));
      for (int i=6; i<n-1; i++) // Copy the username
        username[i-6] = buffer[i];
      username[strlen(username)] = '\0';
      printf("CHILD %u: Rcvd LOGIN request for userid %s\n", (unsigned int)pthread_self(), username);
      fflush(stdout);
      // Check if the username has been used by a tcp user
      int used = search_tcpuserid(username);
      if (used >= 0) { // Send error message if any TCP client has connected
        char* error_msg = "ERROR Already connected\n";
        send(newsd, error_msg, strlen(error_msg), 0);
        printf("CHILD %u: Sent ERROR (Already connected)\n", (unsigned int)pthread_self());
        fflush(stdout);
        continue;
      }
      // logout the udp user with this name if there is one
      logout_udpuser(username, udp_sock);
      memset(tcp_client_set[found].userid, 0, sizeof(tcp_client_set[found].userid));
      memcpy(tcp_client_set[found].userid, username, sizeof(username));
      send(newsd, "OK\n", 3, 0);
    } // Login
// ----------------------------------   WHO   ----------------------------------
    else if (strncmp("WHO", buffer, 3) == 0) {
      printf("CHILD %u: Rcvd WHO request\n", (unsigned int)pthread_self());
      fflush(stdout);
      if (found<0) {
        char* error_msg = "ERROR Not logged in\n";
        send(newsd, error_msg, strlen(error_msg), 0);
        printf("CHILD %u: Sent ERROR (Not logged in)\n", (unsigned int)pthread_self());
        fflush(stdout);
        continue;
      }
      int k = 0;
      char* all_users[tcp_client_index+udp_client_index];
      for (int i=0; i<tcp_client_index; i++, k++)
        all_users[k] = tcp_client_set[i].userid;
      for (int i=0; i<udp_client_index; i++, k++)
        all_users[k] = udp_client_set[i].userid;
      idsort(all_users, tcp_client_index+udp_client_index);
      char text[BUFFER_SIZE];
      memset(text, 0, BUFFER_SIZE);
      strcpy(text, "OK\n");
      for (int i=0; i<tcp_client_index+udp_client_index; i++) {
        strcat(text, all_users[i]);
        strcat(text, "\n");
      }
      send(newsd, text, strlen(text), 0);
      continue;
    } // WHO

// --------------------------------  LOGOUT  --------------------------------
    else if (strncmp("LOGOUT", buffer, 6) == 0) {
      printf("CHILD %u: Rcvd LOGOUT request\n", (unsigned int)pthread_self());
      fflush(stdout);
      if (found<0) {
        char* error_msg = "ERROR Not logged in\n";
        send(newsd, error_msg, strlen(error_msg), 0);
        printf("CHILD %u: Sent ERROR (Not logged in)\n", (unsigned int)pthread_self());
        fflush(stdout);
        continue;
      }
      logout_tcpuser(newsd);
    } // LOGOUT

// --------------------------------   SEND   --------------------------------
    else if(strncmp("SEND", buffer, 4) == 0) {
      // Get recipient id
      int i=5;
      char recipient[21];
      memset(recipient, 0, sizeof(recipient));
      for (; buffer[i] != ' '; i++)
        recipient[i-5] = buffer[i];
      recipient[i-5] = 0;
      printf("CHILD %u: Rcvd SEND request to userid %s\n", (unsigned int)pthread_self(), recipient);
      fflush(stdout);
      if (found<0) {
        char* error_msg = "ERROR Not logged in\n";
        send(newsd, error_msg, strlen(error_msg), 0);
        printf("CHILD %u: Sent ERROR (Not logged in)\n", (unsigned int)pthread_self());
        fflush(stdout);
        continue;
      }
      // Check send format
      i++;
      // Get msglen
      char msglen_[4];
      memset(msglen_, 0, 4);
      int j=0;
      for(; buffer[i] != ' '; i++, j++)
        msglen_[j] = buffer[i];
      msglen_[j] = 0;
      int msglen = atoi(msglen_);
      if (msglen < 1 || msglen > 994) { // Over lower or upper bounds
        char* error_msg = "ERROR Invalid msglen\n";
        send(newsd, error_msg, strlen(error_msg), 0);
        printf("CHILD %u: Sent ERROR (Invalid msglen)\n", (unsigned int)pthread_self());
        fflush(stdout);
        continue;
      }
      i++;
      // Get the message
      char message[994];
      memset(message, 0, 994);
      j=0;
      for (; i<strlen(buffer); i++, j++)
        message[j] = buffer[i];
      message[j-1] = 0;
      int pos = search_tcpuserid(recipient);
      if (pos < 0) {
        pos = search_udpuserid(recipient);
        if (pos < 0) {
          char* error_msg = "ERROR Unknown userid\n";
          send(newsd, error_msg, strlen(error_msg), 0);
          printf("CHILD %u: Sent ERROR (Unknown userid)\n", (unsigned int)pthread_self());
          fflush(stdout);
          continue;
        } else {
          send(newsd, "OK\n", 3, 0);
          char text[BUFFER_SIZE];
          formText(text, tcp_client_set[found].userid, msglen_, message);
          sendto(udp_sock, text, strlen(text), 0, (struct sockaddr*)&udp_client_set[pos].user,
                sizeof(udp_client_set[pos].user));
        }
      } else {
        send(newsd, "OK\n", 3, 0);
        char text[BUFFER_SIZE];
        formText(text, tcp_client_set[found].userid, msglen_, message);
        send(tcp_client_set[pos].fd, text, strlen(text), 0);
      }
    } // SEND

// ------------------------------   BROADCAST   ------------------------------
    else if(strncmp("BROADCAST", buffer, 9) == 0) {
      printf("CHILD %u: Rcvd BROADCAST request\n", (unsigned int)pthread_self());
      fflush(stdout);
      if (found<0) {
        char* error_msg = "ERROR Not logged in\n";
        send(newsd, error_msg, strlen(error_msg), 0);
        printf("CHILD %u: Sent ERROR (Not logged in)\n", (unsigned int)pthread_self());
        fflush(stdout);
        continue;
      }
      // Get the msglen
      char msglen_[4];
      memset(msglen_, 0, 4);
      int i = 10;
      int j = 0;
      for(; buffer[i] != ' '; i++, j++)
        msglen_[j] = buffer[i];
      msglen_[j] = 0;
      int msglen = atoi(msglen_);
      if (msglen < 1 || msglen > 994) { // Over lower or upper bounds
        char* error_msg = "ERROR Invalid msglen\n";
        send(newsd, error_msg, strlen(error_msg), 0);
        printf("CHILD %u: Sent ERROR (Invalid msglen)\n", (unsigned int)pthread_self());
        fflush(stdout);
        continue;
      }
      // Get the message
      i++;
      char message[994];
      memset(message, 0, 994);
      j=0;
      for (; i<strlen(buffer); i++, j++)
        message[j] = buffer[i];
      message[j-1] = 0;
      send(newsd, "OK\n", 3, 0);
      char text[BUFFER_SIZE];
      formText(text, tcp_client_set[found].userid, msglen_, message);
      for (int k=0; k<tcp_client_index; k++)
        send(tcp_client_set[k].fd, text, strlen(text), 0);
      for (int k=0; k<udp_client_index; k++) {
        sendto(udp_sock, text, strlen(text), 0, (struct sockaddr*)&udp_client_set[k].user,
              sizeof(udp_client_set[k].user));
      }
    }
// -------------------------------  SHARE  ------------------------------------
    else if(strncmp("SHARE", buffer, 5) == 0) {
      printf("CHILD %u: Rcvd SHARE request\n", (unsigned int)pthread_self());
      fflush(stdout);
      if (found<0) {
        char* error_msg = "ERROR Not logged in\n";
        send(newsd, error_msg, strlen(error_msg), 0);
        printf("CHILD %u: Sent ERROR (Not logged in)\n", (unsigned int)pthread_self());
        fflush(stdout);
        continue;
      }
      // Get recipient id
      int i=6;
      char recipient[21];
      memset(recipient, 0, sizeof(recipient));
      for (; buffer[i] != ' '; i++) {
        recipient[i-6] = buffer[i];
      }
      recipient[i-6] = 0;
      // Check send format
      char filelen_[16];
      memset(filelen_, 0, 16);
      i++;
      int j=0;
      for (; buffer[i] != '\n'; i++, j++)
        filelen_[j] = buffer[i];
      filelen_[j] = 0;
      send(newsd, "OK\n", 3, 0);
      char sendmsg[BUFFER_SIZE];
      memset(sendmsg, 0, BUFFER_SIZE);
      strcat(sendmsg, "SHARE");
      strcat(sendmsg, " ");
      strcat(sendmsg, tcp_client_set[found].userid);
      strcat(sendmsg, " ");
      strcat(sendmsg, filelen_);
      int pos = search_tcpuserid(recipient);
      int fd = tcp_client_set[pos].fd;
      send(fd, sendmsg, strlen(sendmsg), 0);
      char filebuf[BUFFER_SIZE];
      memset(filebuf, 0, BUFFER_SIZE);
      int k;
      while (1) {
        memset(filebuf, 0, BUFFER_SIZE);
        k = recv(newsd, filebuf, BUFFER_SIZE, 0);
        if (k<BUFFER_SIZE) {
          send(newsd, "OK\n", 3, 0);
          send(fd, filebuf, k, 0);
          break;
        }
        send(newsd, "OK\n", 3, 0);
        send(fd, filebuf, BUFFER_SIZE, 0);
      }
    }
    else {
      char* error_msg = "ERROR Unknown COMMAND\n";
      send(newsd, error_msg, strlen(error_msg), 0);
      printf("MAIN: Sent ERROR (Unknown COMMAND)\n");
      fflush(stdout);
      continue;
    }
  }

  pthread_exit(NULL);

}




//
