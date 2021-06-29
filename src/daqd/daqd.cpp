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
#include <vector>

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

static void catchUserStop(int signal) {
	fprintf(stderr, "Caught signal %d\n", signal);
	globalUserStop = true;
}

static int createListeningSocket(const char *clientSocketName);
static void pollSocket(int clientSocket, FrameServer *frameServer);


int main(int argc, char *argv[])
{
	const char *clientSocketName = "/tmp/d.sock";
	const char *shmName = "/daqd_shm";
	int debugLevel = 0;
	
	int daqType = -1;
	unsigned ncards = 1;
	unsigned daqCardPortBits = 5;
	
	static struct option longOptions[] = {
		{ "socket-name", required_argument, 0, 0 },
		{ "debug-level", required_argument, 0, 0 },
		{ "daq-type", required_argument, 0, 0 },
		{ "ncards", required_argument, 0, 0 },
		{ "card-width", required_argument, 0, 0},
		{ NULL, 0, 0, 0 }
	};
	while(1) {
		
		int optionIndex = 0;
		int c = getopt_long(argc, argv, "", longOptions, &optionIndex);

		if (c == -1) {
			break;
		}
		
		else if (c == 0 && optionIndex == 0)
			clientSocketName = (char *)optarg;
		else if (c == 0 && optionIndex == 1)
			debugLevel = boost::lexical_cast<int>((char *)optarg);
		else if (c == 0 && optionIndex == 2) {
			if(strcmp((char *)optarg, "GBE") == 0) {
				daqType = 0;
			}
			else if (strcmp((char *)optarg, "PFP_KX7") == 0) {
				daqType = 1;
			}
			else {
				fprintf(stderr, "ERROR: '%s' is not a valid DAQ type\n", (char *)optarg);
				fprintf(stderr, "Valid DAQ types are 'GBE', 'DTFLY' or 'PFP_KX7'\n");
				return -1;
			}
			
		}
		else if (c == 0 && optionIndex == 3) {
			ncards = boost::lexical_cast<unsigned>((char *)optarg);
		}
		else if (c == 0 && optionIndex == 4) {
			daqCardPortBits = boost::lexical_cast<unsigned>((char *)optarg);
		}
		else {
			fprintf(stderr, "ERROR: Unknown option!\n");
			return -1;
		}
		
	}
	
	if(clientSocketName == NULL) {
		fprintf(stderr, "--socket-name </path/to/socket> required\n");
		return -1;
	}
	
	if (daqType == -1) {
		fprintf(stderr, "--daq-type xxx required\n");
		return -1;
	}


 	globalUserStop = false;					
	signal(SIGTERM, catchUserStop);
	signal(SIGINT, catchUserStop);
	signal(SIGHUP, catchUserStop);
	

	int retval = -1;
	int clientSocket = -1;
	int shmfd = 1;
	RawDataFrame *shmPtr = NULL;
	FrameServer *frameServer = NULL;
	std::vector<AbstractDAQCard *> daqCards;

	clientSocket = createListeningSocket(clientSocketName);
	if(clientSocket < 0) {
		goto cleanup_remove_client_socket;
	}
	
	
	FrameServer::allocateSharedMemory(shmName, shmfd, shmPtr);
	if((shmfd == -1) || (shmPtr == NULL)) {
		goto cleanup_shared_memory;
	}

	
	if (daqType == 0) {
		frameServer = UDPFrameServer::createFrameServer(shmName, shmfd, shmPtr, debugLevel);
	}
	else if (daqType == 1) {
		for(int index = 0; index < ncards; index++) {
			AbstractDAQCard *card = PFP_KX7::openCard(index);
			if(card == NULL)
				goto cleanup_daq_cards;

			daqCards.push_back(card);
		}


		frameServer = DAQFrameServer::createFrameServer(daqCards, daqCardPortBits, shmName, shmfd, shmPtr, debugLevel);
	}
	
	if(frameServer == NULL) {
		goto cleanup_frame_server;
	}

	pollSocket(clientSocket, frameServer);
	retval = 0;
	

cleanup_frame_server:
	if(frameServer != NULL) {
		delete frameServer;
	}

cleanup_daq_cards:
	for(auto iter = daqCards.begin(); iter != daqCards.end(); iter++) {
		delete *iter;
	}

	
cleanup_shared_memory:
	FrameServer::freeSharedMemory(shmName, shmfd, shmPtr);

	
cleanup_remove_client_socket:
	if(clientSocket != -1) {
		close(clientSocket);
		unlink(clientSocketName);
	}

	return retval;
}

int createListeningSocket(const char *clientSocketName)
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
	snprintf(address.sun_path, 108, "%s", clientSocketName);


	if(bind(socket_fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un)) != 0) {
		fprintf(stderr, "ERROR: Could not bind() socket (%d)\n", errno);
		fprintf(stderr, "Check that no other instance is running and remove %s\n", clientSocketName);
		return -1;
	}
	
	if(chmod(clientSocketName, 0660) != 0) {
		perror("ERROR: Could not not set socket permissions\n");
		return -1;
	}
	
	if(listen(socket_fd, 5) != 0) {
		fprintf(stderr, "ERROR: Could not listen() socket (%d)\n", errno);
		return -1;
	}
	
	return socket_fd;
}

void pollSocket(int clientSocket, FrameServer *frameServer)
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
	event.data.fd = clientSocket;
	event.events = EPOLLIN;
	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clientSocket, &event) == -1) {
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
		
		if(event.data.fd == clientSocket) {
			int client = accept(clientSocket, NULL, NULL);
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
