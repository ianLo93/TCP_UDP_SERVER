# TCP_UDP_SERVER
## Description
The program constructs a chat server for multiple clients to share informations, it supports TCP or UDP clients to send messages to other same channel users, but not cross-channel, the program will use select() function to poll for TCP/UDP client messages and operations. The program supports multiple online users by producing multi-threads for different users.

The server supports the following functionalities:

- LOGIN

Add user to TCP or UDP client lists, users have to login to accomplish other actions. Program will identify users by username and IP address, same usernames cannont be used at the same time. UDP users will stay online even if they disconnect unless "LOGOUT" command is specified or other users use the same username to LOGIN. TCP users will automatically logout once they diconnected.

- LOGOUT

Remove user from its own list, users explicitly disconnect from the server.

- WHO

Pull out the whole list of users in the same channel.

- SEND

Send text messages to a specified user.

- BROADCAST

Send text messages to all other users in the same channel.

- SHARE

Send files to a specified user, supports all regular files

## Usage

Compile the server program:
```
gcc TCP_UDP_Server.c -o chat_server.exe -pthread -Wall -Werror
```

Run the server program: 
```
./chat_server.exe
```

Clients can get access to the server by specifying the IP address and port number of the server, can either use netcat command on command window or connect through a socket program.

## Client commands

Login: 
```
LOGIN <userid>
```

Replace <userid> with your username.

Send:
```
SEND <recipient-userid> <msglen> <message>
```

<recipient-userid> is the target user you want to send message to, <msglen> to specify message length, replace <message> with your sending text message.
 
Broadcast:
```
BROADCAST <msglen> <message>
```

Specify message length in <msglen>, add space and the sending messages.
 
Share:
```
SHARE <recipient-userid> <filelen>
```

Add target user to <recipient-userid> and specify the file length. Note: SHARE command does not support UDP users
