#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <map>
#include <string.h>
#include <getopt.h>
#include <boost/lexical_cast.hpp>

#include "UDPFrameServer.hpp"
#include "FrameServer.hpp"
#include "Protocol.hpp"
#include "Client.hpp"

#include "PFP_KX7.hpp"

using namespace PETSYS;

/*
 * Used to stop on CTRL-C or kill
 */
static bool globalUserStop = false;
static FrameServer *globalFrameServer = NULL;

static void catchUserStop(int signal) {
	fprintf(stderr, "Caught signal %d\n", signal);
	globalUserStop = true;
}

static int createListeningSocket(char *socketName);
static void pollSocket(int listeningSocket, FrameServer *frameServer);


int main(int argc, char *argv[])
{
	bool feTypeHasBeenSet = false;
	int feType[5] = { -1, -1, -1, -1, -1 };
	char *socketName = NULL;	
	int debugLevel = 0;
	
	int daqType = -1;
	
	static struct option longOptions[] = {
		{ "fe-type", required_argument, 0, 0 },
		{ "socket-name", required_argument, 0, 0 },
		{ "debug-level", required_argument, 0, 0 },
		{ "daq-type", required_argument, 0, 0 },
		{ NULL, 0, 0, 0 }
	};
	while(1) {
		
		int optionIndex = 0;
		int c = getopt_long(argc, argv, "", longOptions, &optionIndex);
		
		if (c == -1) {
			break;
		}
		
		if (c == 0 && optionIndex == 0) {
			char *s = (char *)optarg;
			if(strlen(s) != 5) break;
			for (int i = 0; i < 5; i++) {
				switch(s[i]) {
				case 't':
				case 'T': 
					feType[i] = 0; 
					break;
				case 's':
				case 'S': 
					feType[i] = 1; 
					break;
				case 'd':
				case 'D': 
					feType[i] = 2; break;
				case '0':
					feType[i] = -1; break;
				default : 
					fprintf(stderr, "Valid values for x are 0 (not present) t (TOFPET), s (STiCv3) or d (dSiPM)\n");
					return -1;
				}
			}
			feTypeHasBeenSet = true;
		}
		else if (c == 0 && optionIndex == 1) 
			socketName = (char *)optarg;
		else if (c == 0 && optionIndex == 2)
			debugLevel = boost::lexical_cast<int>((char *)optarg);
		else if (c == 0 && optionIndex == 3) {
			if(strcmp((char *)optarg, "GBE") == 0) {
				daqType = 0;
				feTypeHasBeenSet = true;
			}
			else if (strcmp((char *)optarg, "PFP_KX7") == 0) {
				daqType = 2;
				feTypeHasBeenSet = 1;
			}
			else {
				fprintf(stderr, "ERROR: '%s' is not a valid DAQ type\n", (char *)optarg);
				fprintf(stderr, "Valid DAQ types are 'GBE', 'DTFLY' or 'PFP_KX7'\n");
				return -1;
			}
			
		}
		else {
			fprintf(stderr, "ERROR: Unknown option!\n");
		}
		
	}
	
	if(socketName == NULL) {
		fprintf(stderr, "--socket-name </path/to/socket> required\n");
		return -1;
	}
	
	if (daqType == -1) {
		fprintf(stderr, "--daq-type xxx required\n");
		return -1;
	}

	if(!feTypeHasBeenSet) {
		fprintf(stderr, "--fe-type xxxxx required\n");
		return -1;
	}
	

 	globalUserStop = false;					
	signal(SIGTERM, catchUserStop);
	signal(SIGINT, catchUserStop);
	

	int listeningSocket = createListeningSocket(socketName);
	if(listeningSocket < 0)
		return -1;


	
	if (daqType == 0) {
		globalFrameServer = new UDPFrameServer(debugLevel);
	}
	else if (daqType == 2) {		
		globalFrameServer = new DAQFrameServer(new PFP_KX7(), 0, NULL, debugLevel);
	}

	pollSocket(listeningSocket, globalFrameServer);	
	close(listeningSocket);	
	unlink(socketName);

	delete globalFrameServer;

	return 0;
}

int createListeningSocket(char *socketName)
{
	struct sockaddr_un address;
	int socket_fd = -1;
	socklen_t address_length;
	
	if((socket_fd = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
		fprintf(stderr, "ERROR: Could not allocate socket (%d)\n", errno);
		return -1;
	}

	
	memset(&address, 0, sizeof(struct sockaddr_un));
	address.sun_family = AF_UNIX;
	snprintf(address.sun_path, PATH_MAX, socketName);


	if(bind(socket_fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un)) != 0) {
		fprintf(stderr, "ERROR: Could not bind() socket (%d)\n", errno);
		fprintf(stderr, "Check that no other instance is running and remove %s\n", socketName);
		return -1;
	}
	
	if(chmod(socketName, 0660) != 0) {
		perror("ERROR: Could not not set socket permissions\n");
		return -1;
	}
	
	if(listen(socket_fd, 5) != 0) {
		fprintf(stderr, "ERROR: Could not listen() socket (%d)\n", errno);
		return -1;
	}
	
	return socket_fd;
}

void pollSocket(int listeningSocket, FrameServer *frameServer)
{
	sigset_t mask, omask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	
	struct epoll_event event;	
	int epoll_fd = epoll_create(10);
	if(epoll_fd == -1) {
	  fprintf(stderr, "ERROR: %d on epoll_create()\n", errno);
	  return;
	}
	
	// Add the listening socket
	memset(&event, 0, sizeof(event));
	event.data.fd = listeningSocket;
	event.events = EPOLLIN;
	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listeningSocket, &event) == -1) {
	  fprintf(stderr, "ERROR: %d on epoll_ctl()\n", errno);
	  return;
	  
	}
	
	std::map<int, Client *> clientList;
	
	while (true) {
		if (sigprocmask(SIG_BLOCK, &mask, &omask)) {
			fprintf(stderr, "ERROR: Could not set sigprockmask() (%d)\n", errno);
			break;
		}
		  
		if(globalUserStop == 1) {
			sigprocmask(SIG_SETMASK, &omask, NULL);
			break;
		}
	  
		// Poll for one event
		memset(&event, 0, sizeof(event));
		int nReady = epoll_pwait(epoll_fd, &event, 1, 100, &omask);		
		sigprocmask(SIG_SETMASK, &omask, NULL);
	  
		if (nReady == -1) {
		  fprintf(stderr, "ERROR: %d on epoll_pwait()\n", errno);
		  break;
		  
		}
		else if (nReady < 1)
		  continue;
		
		if(event.data.fd == listeningSocket) {
			int client = accept(listeningSocket, NULL, NULL);
			fprintf(stderr, "Got a new client: %d\n", client);
			
			// Add the event to the list
			memset(&event, 0, sizeof(event));
			event.data.fd = client;
			event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
			epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client, &event);
			
			clientList.insert(std::pair<int, Client *>(client, new Client(client, frameServer)));
		}		
		else {
//		  fprintf(stderr, "Got a client (%d) event %08llX\n", event.data.fd, event.events);
		  if ((event.events & EPOLLHUP) || (event.events & EPOLLERR)) {
			fprintf(stderr, "INFO: Client hung up or error\n");
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event.data.fd, NULL);
			delete clientList[event.data.fd]; clientList.erase(event.data.fd);
		  }
		  else if (event.events & EPOLLIN) {			
			Client * client = clientList[event.data.fd];
			int actionStatus = client->handleRequest();
			
			if(actionStatus == -1) {
				fprintf(stderr, "ERROR: Handling client %d\n", event.data.fd);
				epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event.data.fd, NULL);
				delete clientList[event.data.fd]; clientList.erase(event.data.fd);
				continue;
			}
			
		  }
		  else {
			fprintf(stderr, "WARING: epoll() event was WTF\n");
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event.data.fd, NULL);
			delete clientList[event.data.fd]; clientList.erase(event.data.fd);
		  }
		  
		  
		  
		}
		  
		
		
	}
	
	
  
}
