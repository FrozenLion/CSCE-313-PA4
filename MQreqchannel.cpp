#include "common.h"
#include "RequestChannel.h"
#include "MQreqchannel.h"
#include <mqueue.h>
using namespace std;

/*--------------------------------------------------------------------------*/
/* CONSTRUCTOR/DESTRUCTOR FOR CLASS   R e q u e s t C h a n n e l  */
/*--------------------------------------------------------------------------*/

MQRequestChannel::MQRequestChannel(const string _name, const Side _side, int _bufsize) : RequestChannel(_name, _side){
	queue1 = "/mq_" + my_name + "1";
	queue2 = "/mq_" + my_name + "2";

	bufsize = _bufsize;

	if (_side == SERVER_SIDE){
		wfd = open_queue(queue1, O_WRONLY);
		rfd = open_queue(queue2, O_RDONLY);
	}
	else{
		rfd = open_queue(queue1, O_RDONLY);
		wfd = open_queue(queue2, O_WRONLY);
		
	}
	
}

MQRequestChannel::~MQRequestChannel(){
	mq_close(wfd);
    mq_close(rfd);

    mq_unlink(queue1.c_str());
    mq_unlink(queue2.c_str());
}

int MQRequestChannel::open_queue(string _queue_name, int mode){
    struct mq_attr attr{0, 1, bufsize, 0};
    int fd = mq_open(_queue_name.c_str(), O_RDWR | O_CREAT, 0600, &attr);
    if (fd < 0){
		EXITONERROR(_queue_name);
	}
	return fd;
}

int MQRequestChannel::cread(void* msgbuf, int bufcapacity){
	return mq_receive(rfd, (char*) msgbuf, bufsize, 0);
}

int MQRequestChannel::cwrite(void* msgbuf, int len){
	return mq_send(wfd, (char*) msgbuf, len, 0);
}

