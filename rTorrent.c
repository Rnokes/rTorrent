/**********************************
 * p2p.c
 **/

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <sys/unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <dirent.h>

#define NUM_CONNECTIONS (5)

/* Definition to activate debug messages, set 1 to turn on debugging messages, 0 for off. */
#define DEBUG_MODE (0)

/* Structure to keep track of the connections */
typedef struct {
	char name [100];
	char ip [20];
	int socket;
} ConnectionType;

int port;
int waitingOnConnect;
int numConnections;
int msgsize;

char g_filesCmdMsg[10000];

/* Connection Table */
ConnectionType connections[NUM_CONNECTIONS];

pthread_mutex_t lock;

/* Forward function declarations */
void *userInterface();
void *connectToRemote();
void *connectFromRemote();
void killConnection();
void handleMessages();
int uploadToRemote(int, char[], bool);

/*
 * Listens for incoming connections on user-specified port
 * and spawns user interface thread.
 */
int main(int argc, char **argv) {

  /* Handle incorrect amounts of arguments */
	if (argc < 3) {
		printf("Please provide a port number and message size as arguments.\n");
		return 1;
	} else if (argc > 3) {
		printf("The arguments should only be a single port number, and a single message size specification.\n");
		return 2;
	} else if (atoi(argv[1]) < 1024 || atoi(argv[2]) > 65535) {
    printf("The port arguemnt should be a valid port integer in the range [1024 - 65535]\n");
    return 3;
  } else if (atoi(argv[2]) == 0) {
    printf("The message size needs to be input as valid integer greater than 0.\n");
    return 4;
  }

  /* Assign message size (from command line) */
  msgsize = atoi(argv[2]);

	/* Assign port number (from command line) */
	port = atoi(argv[1]);

	/* Initialize connection table */
	int i;
	for (i = 0; i < NUM_CONNECTIONS; i++) {
	  strcpy(connections[i].name,"");
	  strcpy(connections[i].ip,"");
	  connections[i].socket = -1;
	}
	numConnections = 0;

	/* Start separate thread for UI */
	pthread_t ui_thread;
	pthread_create(&ui_thread, NULL, userInterface, NULL);

	/* Listen for incoming connections (server part) */
	int sock_fd = 0;
	int acc_fd = 0;

  struct sockaddr_in server_addr;

  sock_fd = socket(AF_INET, SOCK_STREAM, 0);

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	bind(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));

	listen(sock_fd, NUM_CONNECTIONS);

	/* Accept and handle connections */
	while (1) {
	  /* Accept the incoming connection */
		acc_fd = accept(sock_fd, NULL, NULL);

		if (numConnections >= NUM_CONNECTIONS) { /* max connections: warn the peer */
        		/* Send a TERMINATE message */
			send(acc_fd, "MAX", 4, 0);

			/* Close the socket */
			close(acc_fd);
		} else { /* connected successfully */
			send(acc_fd, "SUCCESS", 8, 0);

			/* Prepare arg to send to new thread for incoming connection */
			int *arg = malloc(sizeof(int));
			*arg = acc_fd;

			/* Spawn new thread for this connection */
			pthread_t conn_thread;
			pthread_create(&conn_thread, NULL, connectFromRemote, arg);
		}
	}

	close(sock_fd);
	return 0;
} /* main */

/*
 * Provides the user interface
 */
void *userInterface() {
	char command [300] = {0};	// Entire user command
	char arg1 [100] = {0};	// First argument
	char arg2 [100] = {0};  // Second argument
	char arg3 [100] = {0};  // Third argument

	/* Accept and scan first command line */
	printf("p2p> ");
	fgets(command, 299, stdin);
	sscanf(command, "%s %s %s", arg1, arg2, arg3);

	/* Process user commands until "exit" */
	while (strcmp(arg1, "EXIT") && strcmp(arg1, "exit")) {
    #if DEBUG_MODE
    printf("Debug - Command entered: %s\n", command);
    #endif

	  /* Help */
		if (!strcmp(arg1, "HELP") || !strcmp(arg1, "help")) {
			printf("The following commands can be used in this program:\n\n");
			printf("exit\n");
			printf("connect [host_name] [port_number]\n");
			printf("list\n");
			printf("help\n");
			printf("terminate [connection_ID]\n");
      printf("generate [file_name] [file_size(Kb)]\n");
      printf("upload [connection_ID] [upload_file_name]\n");
		  printf("files [connection_ID]\n");
      printf("download [connection_ID] [file_name]\n");
      printf("download_best [file_name]\n");
    }

		/* Connect */
		else if (!strcmp(arg1, "CONNECT") || !strcmp(arg1, "connect")) {
		        /* Check the number of args */
			if (!strcmp(arg2, "") || !strcmp(arg3, "")) {
				printf("CONNECT requires 2 arguments. Connect not successful.\n");
			}

      /* Check that port number is an int, and is a valid port number */
      else if (atoi(arg3) < 1024 || atoi(arg3) > 65535) {
        printf("CONNECT requires a valid port number in the range [1024 - 65535]\n");
      }
			else {
				waitingOnConnect = 1;
				char **args = malloc(sizeof(char *) * 2);
				args[0] = strdup(arg2);
				args[1] = strdup(arg3);

				pthread_t conn_thread;
				pthread_create(&conn_thread, NULL, connectToRemote, args);

				/* Wait for connection to succeed or fail before allowing UI interaction */
				while (waitingOnConnect) {}; /* busy wait until printing success or failure */
			}
		}

		/* List */
		else if (!strcmp(arg1, "LIST") || !strcmp(arg1, "list")) {
		  pthread_mutex_lock (&lock);
		  if (numConnections == 0) printf("No active connections\n");

      int i;
			for (i = 0; i < numConnections; i++) {
				printf("%d : %s : %s\n", i + 1, connections[i].name, connections[i].ip);
			}
			pthread_mutex_unlock (&lock);
		}

		/* Terminate */
		else if (!strcmp(arg1, "TERMINATE") || !strcmp(arg1, "terminate")) {
		  /* Check the number of arguments */
			if (!strcmp(arg2, "") || strcmp(arg3, "")) {
				printf("TERMINATE requires one argument. Terminate not successful.\n");
			}

			/* Check if connection ID is valid */
			else if (atoi(arg2) > numConnections || atoi(arg2) < 1) {
				printf("Invalid connection ID. Terminate not successful.\n");
			}
			else {
        pthread_mutex_lock (&lock);
			  /* Notify peer of termination */
				send(connections[atoi(arg2) - 1].socket, "TERMINATE", 10, 0);

				/* Terminate the connection and remove it from the list */
				killConnection(atoi(arg2) - 1);
				pthread_mutex_unlock (&lock);
			}

		}

    /* 'Generate' */
    else if (!strcmp(arg1, "GENERATE") || !strcmp(arg1, "generate")) {
      /* Check number of arguments */
      if (!strcmp(arg2, "") || !strcmp(arg3, "")) {
				printf("GENERATE requires two arguments. Generation not successful.\n");
      }

      /* Check that the second number is a valid int */
      else if (atoi(arg3) == 0) {
        printf("Please enter a valid integer greater than 0 as the second argument\n");
      }
      else {
        /* Create arguments for system call to dd */
        char createFile[1000] = {0};
        strcat(createFile, "dd if=/dev/urandom of=./Upload/");
        strcat(createFile, arg2);
        strcat(createFile, " count=");

        char fileSize[100] = {0};
        sprintf(fileSize, "%d", atoi(arg3) * 2);
        strcat(createFile, fileSize);

        #if !(DEBUG_MODE)
        strcat(createFile, " 2> /dev/null");
        #endif

        #if DEBUG_MODE
        printf("Debug - Issuing system command: %s\n", createFile);
        #endif

        /* Lock the thread and preform system call */
        pthread_mutex_lock(&lock);
        if (system(createFile) != -1) {
          printf("File succesfully created\n");
        }
        else {
          perror("System Error ");
          printf("Generation not succesful.\n");
        }
        pthread_mutex_unlock(&lock);

        /* Reset buffers */
        memset(createFile, 0, sizeof(createFile));
        memset(fileSize, 0, sizeof(fileSize));
      }
    }

    /* 'Upload' */
    else if (!strcmp(arg1, "UPLOAD") || !strcmp(arg1, "upload")) {
      /* Check number of arguments */
      if (!strcmp(arg2, "") || !strcmp(arg3, "")) {
				printf("Upload requires two arguments. Upload not successful.\n");
      }

			/* Check if connection ID is valid */
			else if (atoi(arg2) > numConnections || atoi(arg2) < 1) {
				printf("Invalid connection ID. Upload not successful.\n");
			}
      else {
        /* Upload helper function call */
        uploadToRemote(atoi(arg2), arg3, true);
      }
    }

    /* 'Files' */
    else if (!strcmp(arg1, "FILES") || !strcmp(arg1, "files")) {
       /* Check number of arguments */
      if (!strcmp(arg2, "") || strcmp(arg3, "")) {
				printf("Files requires one argument. File list not successful.\n");
      }

			/* Check if connection ID is valid */
			else if (atoi(arg2) > numConnections || atoi(arg2) < 1) {
				printf("Invalid connection ID. Files list not successful.\n");
			}
      else {
        pthread_mutex_lock(&lock);
        send(connections[atoi(arg2) - 1].socket, "FILES", 6, 0);
        pthread_mutex_unlock(&lock);

        /* Sleep for a few seconds to ensure that all of the files have been read and transferred */
        sleep(2);

        pthread_mutex_lock(&lock);
        if (!strcmp(g_filesCmdMsg, "")) {
          printf("Files request failed.\n");
        }
        else {
          printf("%s", g_filesCmdMsg);
        }

        memset(g_filesCmdMsg, 0, sizeof(g_filesCmdMsg));
        pthread_mutex_unlock(&lock);
      }
    }

    /* 'Download' */
    else if (!strcmp(arg1, "DOWNLOAD") || !strcmp(arg1, "download")) {
      /* Check number of arguments */
      if (!strcmp(arg2, "") || !strcmp(arg3, "")) {
				printf("Download requires two arguments. Download not successful.\n");
      }

			/* Check if connection ID is valid */
			else if (atoi(arg2) > numConnections || atoi(arg2) < 1) {
				printf("Invalid connection ID. Download not successful.\n");
			}
      else {

        /* Send DOWNLOAD msg */
        send(connections[atoi(arg2) - 1].socket, "DOWNLOAD", 9, 0);
        sleep(1);
        send(connections[atoi(arg2) - 1].socket, arg3, strlen(arg3), 0);

      }
    }

     /* 'Download Best' */
    else if (!strcmp(arg1, "DOWNLOAD_BEST") || !strcmp(arg1, "download_best")) {
      /* Check number of arguments */
      if (!strcmp(arg2, "") || strcmp(arg3, "")) {
				printf("download_best requires one argument. Download not successful.\n");
      }

      /* Check to make sure we have any valid connections */
      else if (numConnections == 0) {
        printf("Please establish at least one connection before using download_best. Download not successful.\n");
      }
      else {
        int fastest_con_id = -1;
        double fastest_time = 100000;

        for (int x = 0; x < numConnections; x++) {
          struct timeval tvS;
          struct timeval tvE;

          /* Start clock */
          gettimeofday(&tvS, NULL);

          pthread_mutex_lock(&lock);
          send(connections[x].socket, "FILES", 6, 0);
          pthread_mutex_unlock(&lock);

          /* Sleep for a few seconds to ensure that all of the files have been read and transferred */
          sleep(2);

          pthread_mutex_lock(&lock);

          /* Check to see if file was found in the client's Upload folder */
          if (strstr(g_filesCmdMsg, arg2) != NULL) {
            #if DEBUG_MODE
            printf("File found on connection ID: %d\n", (x + 1));
            #endif
          }
          else {
            #if DEBUG_MODE
            printf("Files request failed on connection ID: %d, either not found or hit error.\n", (x + 1));
            #endif
            memset(g_filesCmdMsg, 0, sizeof(g_filesCmdMsg));
            pthread_mutex_unlock(&lock);
            continue;
          }

          memset(g_filesCmdMsg, 0, sizeof(g_filesCmdMsg));
          pthread_mutex_unlock(&lock);

          /* End clock */
          gettimeofday(&tvE, NULL);
          double cur_con_time = (double)(tvE.tv_usec - tvS.tv_usec) / 1000000 + (double) (tvE.tv_sec - tvS.tv_sec);

          #if DEBUG_MODE
          printf("\nCurrent connection ID: %d\nCurrent Fastest time: %.0f\nCurrent recorded time: %.3f\n", (x + 1), fastest_time, cur_con_time);
          #endif

          /* Conditional to check if the current connection was faster than another */
          if (cur_con_time < fastest_time) {
            fastest_time = cur_con_time;
            fastest_con_id = (x);
          }
        }

        /* Check to make sure file was actually found */
        if (fastest_con_id == -1) {
          printf("File not found on any of the connections, aborting download attempt.\n");
        }
        else {
          /* Send the DOWNLOAD notice to the socket with the best response time */
          send(connections[fastest_con_id].socket, "DOWNLOAD", 9, 0);
          sleep(1);
          send(connections[fastest_con_id].socket, arg2, sizeof(arg2), 0);
        }
      }
    }

		/* 'Enter' */
		else if (!strcmp(arg1, "")) {
		  /* Do nothing if just 'Enter' key is pressed */
		}

		/* Unknown command */
		else {
			printf("Unknown command.\n");
		}

		/* Reset buffers */
		memset(command, 0, sizeof(command));
		memset(arg1, 0, sizeof(arg1));
		memset(arg2, 0, sizeof(arg2));
		memset(arg3, 0, sizeof(arg3));

		/* Accept and scan next command line */
		printf("\np2p> ");
		fgets(command, 99, stdin);
		sscanf(command, "%s %s %s", arg1, arg2, arg3);
	}

	/* exit: Notify peers of termination */
	int i;
	pthread_mutex_lock (&lock);
	signal(SIGPIPE, SIG_IGN);
  for (i = 0; i < numConnections; i++) {
		send(connections[i].socket, "TERMINATE", 10, 0);
		close(connections[i].socket);
	}
        pthread_mutex_unlock (&lock);
	printf("\n");
	exit(0);
}

/*
 * Check for received messages and send responses
 */
void handleMessages(int socket, ConnectionType conn) {
	while (1) {
		char buffer[10000] = {0};
		recv(socket, buffer, sizeof(buffer)-1, 0);

		/* TERMINATE notice */
		if (!strcmp(buffer, "TERMINATE")) {
		  /* Find index of the connection to kill it */
			int i;
			pthread_mutex_lock (&lock);
			for (i = 0; i < numConnections; i++) {
				if (connections[i].socket == conn.socket)
					break;
			}

			killConnection(i);
			pthread_mutex_unlock(&lock);
			printf(" \n");
			printf("%s (%s) disconnected. Press Enter for prompt.\n", conn.name, conn.ip);
			return;
    }

    /* UPLOAD notice */
    else if (!strcmp(buffer, "UPLOAD")) {

      /* Set up buffers and get size and name of incoming file */
      char filename[1000] = {0};
      char fileSize[100] = {0};


      #if !(DEBUG_MODE)
      recv(socket, fileSize, sizeof(fileSize), 0);
      sleep(1);
      recv(socket, filename, sizeof(filename), 0);
      #endif

      #if DEBUG_MODE
      int recvsize = recv(socket, fileSize, sizeof(fileSize), 0);
      sleep(1);
      int recvname = recv(socket, filename, sizeof(filename), 0);
      #endif

      int size = atoi(fileSize);

      printf("Recieving incoming file Download\nFilename: %s", filename);

      #if DEBUG_MODE
      printf("Incoming File Size: %d\nIncoming File Name: %s\n", size, filename);
      printf("Recv return for size: %d\nRecv return for name: %d\n", recvsize, recvname);

      if (filename[0] == '\0') {
        printf("Failure: %.*s\n", recvname, filename);
      }
      #endif

      /* Check if the file is valid after opening */
      FILE *fp;
      fp = fopen(filename, "wb");
      if (fp == NULL) {
        printf("File could not be created, incoming Upload failed\n");
      }
      else {
        sleep(1);

        /* Init start timestamp */
        struct timeval tvS;
        gettimeofday(&tvS, 0);

        /* Repeatedly recv msgsize bytes at a time until the a download is done on END message */
        char fileContent[msgsize] ;
        int writeCounter = 0;
        int recvCounter = 0;
        int writeErr = 1;
        while (true) {
          recvCounter = recv(socket, fileContent, sizeof(fileContent), 0);
          writeCounter += writeErr;
          if (!strcmp(fileContent, "END") || writeErr == 0) { break; }
          writeErr = fwrite(fileContent, 1, recvCounter, fp);
          memset(fileContent, 0, sizeof(fileContent));
        }

        /* Init end timestamp */
        struct timeval tvE;
        gettimeofday(&tvE, 0);

        #if DEBUG_MODE
        printf("Successful inbound bytes transferred: %d(recv) : %d(written)\n\n", (recvCounter - 1), (writeCounter - 3));
        #endif

        /* Find all requisite information for printing out Rx information */
        int con_id = -1;
        for (int x = 0; x < numConnections; x++) {
          if (connections[x].socket == socket) {
            con_id = (x);
            break;
          }
        }
        char hostname[HOST_NAME_MAX + 1] = {0};
        gethostname(hostname, HOST_NAME_MAX);
        double txTime = (double)(tvE.tv_usec - tvS.tv_usec) / 1000000 + (double) (tvE.tv_sec - tvS.tv_sec);
        double bitSize = (double)(size * 8);

        /* Print out Rx information */
        printf("Download statistics:\n");
        printf("Rx (%s): %s -> %s, File Size: %d Bytes, Time Taken: %.3f useconds, Tx Rate: %.3f bits/second\n", hostname, connections[con_id].name, hostname, size, txTime, (bitSize / txTime));

        /* Close file pointer and reset memory buffers */
        fclose(fp);
        memset(fileContent, 0, sizeof(fileContent));
        memset(hostname, 0, sizeof(hostname));

        printf("Incoming Upload complete\n");
      }

      /* Reset buffers */
      memset(filename, 0, sizeof(filename));
      memset(fileSize, 0, sizeof(fileSize));
      printf("Press Enter for prompt.\n");
    }

    /* FILES notice */
    else if (!strcmp(buffer, "FILES")) {

      /* Get list of files in our current Upload dir */
      FILE *fp;
      char sysout[1000] = {0};
      char msg[10000] = {0};

      strcat(msg, "FILES_RESULT\n");
      fp = popen("ls ./Upload", "r");

      /* Check for failure and send error msg if so */
      if (fp == NULL) {
        pthread_mutex_lock(&lock);
        send(socket, "FILES_ERROR", 12, 0);
        pthread_mutex_unlock(&lock);
      }
      else {
        while (fgets(sysout, sizeof(sysout), fp) != NULL) {
          strcat(msg, sysout);
          strcat(msg, "\n");
        }

        /* Send message result */
        pthread_mutex_lock(&lock);
        send(socket, msg, sizeof(msg), 0);
        pthread_mutex_unlock(&lock);

        /* Reset buffers and close file pointer */
        memset(msg, 0, sizeof(msg));
        memset(sysout, 0, sizeof(sysout));
        fclose(fp);
      }
    }

    /* FILES_ERROR notice */
    else if (!strcmp(buffer, "FILES_ERROR")) {
      pthread_mutex_lock(&lock);
      memset(g_filesCmdMsg, 0, sizeof(g_filesCmdMsg));
      strcpy(g_filesCmdMsg, "Error in opening and listing client's Upload directory. File listing failed.\n");
      pthread_mutex_unlock(&lock);
    }

    /* FILE_RES notice */
    else if (!strncmp(buffer, "FILES_RESULT", 12)) {
      pthread_mutex_lock(&lock);
      memset(g_filesCmdMsg, 0, sizeof(g_filesCmdMsg));
      strcpy(g_filesCmdMsg, buffer);
      pthread_mutex_unlock(&lock);
    }

    /* DOWNLOAD notice */
    else if (!strcmp(buffer, "DOWNLOAD")) {

      /* Find the connection ID based on the socket that current reciever thread is running on */
      int con_id = -1;
      for (int x = 0; x < numConnections; x++) {
        if (connections[x].socket == socket) {
          con_id = (x + 1);
          break;
        }
      }

      /* Conditional to make sure connection ID was found */
      if (con_id == -1) {
        printf("Connection ID could not be found for incoming download request, aborting...\n");
        continue;
      }

      /* Get the filename from the client if con_id was found */
      char filename[1000] = {0};

      #if !(DEBUG_MODE)
      recv(socket, filename, sizeof(filename), 0);
      #endif

      #if DEBUG_MODE
      int recvname = recv(socket, filename, sizeof(filename), 0);
      #endif

      printf("Incoming file upload request from:\nCon_ID: %d\tFilename: %s", con_id, filename);

      #if DEBUG_MODE
      printf("Incoming File Name: %s\n", filename);
      printf("Recv return for name: %d\n", recvname);

      if (filename[0] == '\0') {
        printf("Failure: %.*s\n", recvname, filename);
      }
      #endif

      /* Call uploadToRemote() */
      uploadToRemote(con_id, filename, false);

      /* Reset buffers */
      memset(filename, 0, sizeof(filename));
    }

	}
} /* handleMessages */


/*
 * Uploads specified file to the remote host
 */
int uploadToRemote(int con_id, char name[], bool host) {

  /* Sets correct filename to read from */
  char fileName[1000] = {0};
  strcat(fileName, "./Upload/");
  strcat(fileName, name);

  #if DEBUG_MODE
  printf("Debug - File being opened: %s\n", fileName);
  #endif

  /* Opens given file and checks for errors */
  FILE *fp;
  fp = fopen(fileName, "rb");
  if (fp == NULL) {
    if (!host) { return -1 ;}
    printf("File was inaccessible or does not exist. Upload not successful.\n");
  }
  else {
    printf("Starting Upload now.\n");

    /* Lock and use stat() to get file info and set variables accordingly */
    pthread_mutex_lock(&lock);
    struct stat st;
    stat(fileName, &st);
    int fileSize = st.st_size;
    char cSize[100] = {0};
    sprintf(cSize, "%d", fileSize);

    char fullpath[1000] = {0};
    strcpy(fullpath, "./Download/");
    strcat(fullpath, name);

    #if DEBUG_MODE
    printf("File being sent: %s\n", fullpath);
    printf("File size being sent: c: %s, i: %d\n", cSize, fileSize);
    #endif

    /* Send each info of the message, so that the connection is ready for upload */
    send(connections[con_id - 1].socket, "UPLOAD", 6, 0);
    sleep(1);
    send(connections[con_id - 1].socket, cSize, sizeof(cSize), 0);
    sleep(1);
    send(connections[con_id - 1].socket, fullpath, sizeof(fullpath), 0);

    /* Initialize Start timestamp for displaying Tx data */
    struct timeval tvS;
    gettimeofday(&tvS, 0);

    /* Run through reading the file and send each msg in size of 100 bytes */
    char buf[msgsize];
    int tranCounter = 0;
    int readErr = 1;
    int readCounter = 0;
    while ((readErr = fread(buf, sizeof(buf[0]), sizeof(buf), fp)) > 0) {
      tranCounter += send(connections[con_id - 1].socket, buf, readErr, 0);
      readCounter+=readErr;
      memset(buf, 0, sizeof(buf));
    }

    #if DEBUG_MODE
    printf("Debug - Successful outbound bytes: %d(read) : %d(tran)\n\n", readCounter, tranCounter);
    #endif

    /* Initialize End stamp for displaying Tx data */
    struct timeval tvE;
    gettimeofday(&tvE, 0);

    /* Notify the connection that the upload is complete */
    sleep(3);
    send(connections[con_id - 1].socket, "END", 3, 0);

    /* Print confirmation and stats of Transmission */
    char hostname[HOST_NAME_MAX + 1] = {0};
    gethostname(hostname, HOST_NAME_MAX);
    double txTime = (double)(tvE.tv_usec - tvS.tv_usec) / 1000000 + (double) (tvE.tv_sec - tvS.tv_sec);
    double bitSize = (double)(fileSize * 8);
    printf("Upload statisics:\n");
    printf("Tx (%s): %s -> %s, File Size: %d Bytes, Time Taken: %.3f useconds, Tx Rate: %.3f bits/second\n", hostname, hostname, connections[con_id - 1].name, fileSize, txTime, (bitSize / txTime));
    printf("\nPress Enter for prompt.\n");


    /* Close file pointer and reset buffers */
    fclose(fp);
    memset(cSize, 0, sizeof(cSize));
    memset(fullpath, 0, sizeof(fullpath));
    memset(hostname, 0, sizeof(hostname));

    pthread_mutex_unlock(&lock);
  }

  /* Reset buffer */
  memset(fileName, 0, sizeof(fileName));

  return 0;
} /* uploadToRemote() */


/*
 * Connects the remote host to the local host
 */
void *connectFromRemote(void *arg) {
	int acc_fd = *(int *)arg;

	/* Get the client's IP address */
	socklen_t len;
	struct sockaddr_storage addr;
	char ip[INET6_ADDRSTRLEN];

	len = sizeof(addr);
	getpeername(acc_fd, (struct sockaddr*)&addr, &len);

	struct sockaddr_in *s = (struct sockaddr_in *)&addr;
	inet_ntop(AF_INET, &s->sin_addr, ip, sizeof(ip));

	/* Get the client's host name */
	struct hostent *he;
	struct in_addr ipv4addr;

	/* gethostbyaddr() can be replaced by getnameinfo() */
	inet_pton(AF_INET, ip, &ipv4addr);
	he = gethostbyaddr(&ipv4addr, sizeof(ipv4addr), AF_INET);

	/* Store this connection in the list */
	ConnectionType conn;
	strcpy(conn.name,he->h_name);
	strcpy(conn.ip,ip);
	conn.socket = acc_fd;

	pthread_mutex_lock (&lock);
	++numConnections;
	connections[numConnections - 1] = conn;
  pthread_mutex_unlock (&lock);

  /* Print message showing client connection */
	printf(" \n");
	printf("%s (%s) connected. Press Enter for prompt.\n", he->h_name, ip);

	/* Utilize connection */
	handleMessages(acc_fd, conn);
	return 0;
} /* connectFromRemote */

/*
 * Connects the local host to a remote host
 */
void *connectToRemote(void *args) {
	char **strs = (char **)args;
	char *namestr = strs[0];	// remote hostname
	char *portstr = strs[1];	// remote port
	char *ip;			// remote IP

	/* Make a host entry using hostname to find IP list */
	struct hostent *he;
	struct in_addr **addr_list;

	/* gethostbyname can be replaced by getaddrinfo */
	he = gethostbyname(namestr);

  /* Check to make sure that a valid hostname was found */
  if (he == NULL) {
    printf("Could not find connection host. Connect not succesful.\n");
    waitingOnConnect = 0;
    return 0;
  }

  addr_list = (struct in_addr **)he->h_addr_list;

	/* Iterate through list of addresses, most likely only one */
	int i;
	for (i = 0; addr_list[i] != NULL; i++) {
		ip = strdup(inet_ntoa(*addr_list[i]));
	}

	pthread_mutex_lock (&lock);
        /* Check to ensure we have not exceeded allowed number of connections */
	if (numConnections >= NUM_CONNECTIONS) {
		printf("This process has the maximum number of connections. Connect not successful.\n");
		waitingOnConnect = 0;
		pthread_mutex_unlock (&lock);
		return 0;
	}

	/* Do not allow duplicates or connections to self
	 *	Iterate through connections to check for duplicate IPs */
	int j;
	for (j = 0; j < numConnections; j++) {
		if (!strcmp(connections[j].ip, ip)) {
			printf("Attempted duplicate connection. Connect not successful.\n");
			waitingOnConnect = 0;
			pthread_mutex_unlock (&lock);
			return 0;
		}
	}

	/* 	Check remote host name to make sure it does not match local */
	char hostname[200];
	gethostname(hostname, sizeof(hostname));

	if (!strcmp(hostname, namestr)) { /* hostname matches remote hostname */
		printf("Attempted connection to self. Connect not successful.\n");
		waitingOnConnect = 0;         pthread_mutex_unlock (&lock);
		return 0;
	}

	/* 	Check first three digits of IP for loopback (127...) */
	if (ip[0] == '1' && ip[1] == '2' && ip[2] == '7') {
		printf("Attempted (loopback) connection to self. Connect not successful.\n");
		waitingOnConnect = 0;         pthread_mutex_unlock (&lock);
		return 0;
	}

	/* Attempt connection using found IP */
	int sock_fd = 0;
	struct sockaddr_in addr;

	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(portstr));
	inet_pton(AF_INET, ip, &addr.sin_addr);

	int connect_val = connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr));

	/* Detect failure of connect call (0 = success, -1 = failure) */
	if (connect_val != 0) {
		printf("Connect system call failed. Connect not successful.\n");
		waitingOnConnect = 0;
                pthread_mutex_unlock (&lock);
		return 0;
	}

	/* Check for MAX connections notice */
	char buffer[1024] = {0};
	recv(sock_fd, buffer, sizeof(buffer)-1, 0);

	if (!strcmp(buffer, "MAX")) {
		printf("Remote host has maximum number of connections. Connect not successful.\n");
		waitingOnConnect = 0;
		pthread_mutex_unlock (&lock);
		return 0;
	}

	/* Add the connection to the list */
	printf("Connect succeeded.\n");
	waitingOnConnect = 0;

	ConnectionType conn;
	strcpy(conn.name, namestr);
	strcpy(conn.ip, ip);
	conn.socket = sock_fd;
  free (ip); free(namestr); free (portstr);
	connections[numConnections++] = conn;
	pthread_mutex_unlock (&lock);
	/* Utilize connection */
	handleMessages(sock_fd, conn);
	return 0;

} /* connectToRemote */

/*
 * Remove connection with the given *INTERNAL* ID
 * from the connections list
 * and close its socket just to be safe.
 */
void killConnection(int ID) {
  /* Remove connection from the list and shift the others -1 */
  int fd = connections[ID].socket;

	int i;
	for (i = ID; i < NUM_CONNECTIONS-1 ; i++) {
	  strcpy(connections[i].name,connections[i + 1].name);
	  strcpy(connections[i].ip,connections[i + 1].ip);
	  connections[i].socket = connections[i + 1].socket;
	}

	--numConnections;
	/* Reset empty connection slot values */
	for (i = numConnections; i < NUM_CONNECTIONS; i++) {
	  strcpy(connections[i].name,"");
	  strcpy(connections[i].ip,"");
	  connections[i].socket = -1;
	}
	/* Close the connection to the socket */
	close(fd);
} /* killConnection */

