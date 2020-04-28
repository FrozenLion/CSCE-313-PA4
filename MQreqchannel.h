
#ifndef _MQreqchannel_H_
#define _MQreqchannel_H_

#include "common.h"
#include "RequestChannel.h"

class MQRequestChannel: public RequestChannel
{
public:
	enum Mode {READ_MODE, WRITE_MODE};
	
private:
	/*  The current implementation uses named queues. */
	int wfd;
	int rfd;
	
	string queue1, queue2;
	int open_queue(string _queue_name, int mode);

	int bufsize;

public:
	MQRequestChannel(const string _name, const Side _side, int _bufsize);

	~MQRequestChannel();


	int cread(void* msgbuf, int bufcapacity);
	
	int cwrite(void *msgbuf , int msglen);

};

#endif
