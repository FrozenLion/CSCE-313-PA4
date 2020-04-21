#ifndef _SHMreqchannel_H_
#define _SHMreqchannel_H_

#include "common.h"
#include "RequestChannel.h"
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

class SMBB{
private:
    string name;
    sem_t* proddone;
    sem_t* consdone;

    int shmfd;
    char* data;
    int bufsize;

public:
    SMBB(string _name, int _bufsize): bufsize(_bufsize), name(_name){
        shmfd = shm_open(name.c_str(), O_RDWR | O_CREAT, 0644);
        ftruncate(shmfd, bufsize);
        data = (char*) mmap(NULL, bufsize, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);

        proddone = sem_open((name + "P").c_str(), O_CREAT, 0644, 0);
        consdone = sem_open((name + "C").c_str(), O_CREAT, 0644, 1);
    }

    ~SMBB(){
        munmap(data, bufsize);
        close(shmfd);
        shm_unlink(name.c_str());

        sem_close(proddone);
        sem_close(consdone);

        sem_unlink((name + "P").c_str());
        sem_unlink((name + "C").c_str());
    }

    int push(char* msg, int msglen){
        sem_wait(consdone);
        memcpy(data, msg, msglen);
        sem_post(proddone);

        return msglen;
    }

    int pop(char* msg, int msglen){
        sem_wait(proddone);
        memcpy(msg, data, msglen);
        sem_post(consdone);

        return msglen;
    }
};

class SHMRequestChannel: public RequestChannel{
private:
    SMBB* b1;
    SMBB* b2;
    string s1, s2;
    int bufsize;

public:
    SHMRequestChannel(const string _name, const Side _side, int _bufsize): RequestChannel(_name, _side), bufsize(_bufsize){
        s1 = "/bb_" + _name + "1";
        s2 = "/bb_" + _name + "2";

        if(_side == SERVER_SIDE){
            b1 = new SMBB(s1, bufsize);
            b2 = new SMBB(s2, bufsize);
        }
        else{
            b1 = new SMBB(s2, bufsize);
            b2 = new SMBB(s1, bufsize);
        }
    }

    ~SHMRequestChannel(){
        delete b1;
        delete b2;
    }

    int cread(void* buf, int len){
        return b1->pop((char*)buf, len);
    }

    int cwrite(void* buf, int len){
        return b2->push((char*)buf, len);
    }
};

#endif
