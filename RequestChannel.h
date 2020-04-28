#ifndef REQUEST_CHANNEL_H
#define REQUEST_CHANNEL_H

#include "common.h"

class RequestChannel{
public:
    typedef enum {SERVER_SIDE, CLIENT_SIDE} Side;

protected:
    string my_name;
    Side my_side;

public:
    RequestChannel(const string _name, const Side _side){
        my_name = _name;
        my_side = _side;
    }
    virtual ~RequestChannel(){}

    virtual int cread(void* msgbuf, int bufcap)=0;
    virtual int cwrite(void* msgbuf, int msglen)=0;

    string name(){ return my_name; }
};

#endif