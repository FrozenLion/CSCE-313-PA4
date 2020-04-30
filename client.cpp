#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "common.h"
#include "HistogramCollection.h"
#include "FIFOreqchannel.h"
#include "MQreqchannel.h"
#include "SHMreqchannel.h"
#include "TCPreqchannel.h"
#include <thread>
#include <mutex>
#include <unistd.h>
#include <cstdio>
#include <signal.h>
#include <time.h>

using namespace std;

#define REALTIME 0         //determines whether to display realtime progress updates of program or not

//GLOBAL VARIABLES (must be global for signal handler)
HistogramCollection hc;
__int64_t iters;
__int64_t progress;

void sig_hdlr(int signo){
    if(signo == SIGALRM){
#if REALTIME
        system("clear");
        if(!hc.is_empty()){
            hc.print();
            cout << "\n\n\n\n" << endl;
        }
        if(iters > 0){
            int pct = ((double)progress / (double)iters) * 100;

            cout << "+----------------------------------------------------------------------------------------------------+" << endl;
            cout << "|";
            for(int i = 0; i < 100; i++){
                if(i <= pct){
                    cout << "#";
                }
                else{
                    cout << " ";
                }
            }
            cout << "|" << endl;
            cout << "+----------------------------------------------------------------------------------------------------+" << endl;
        }
#endif
    }
}

void file_thread_function(string filename, int m, BoundedBuffer* reqbuf, RequestChannel* chan){
    string rpsfname = "recv/" + filename;
    char buf[m];
    filemsg msg(0, 0);

    memcpy(buf, &msg, sizeof(msg));
    strcpy(buf + sizeof(msg), filename.c_str());
    chan->cwrite(buf, sizeof(msg) + filename.size() + 1);

    __int64_t flength;
    chan->cread(&flength, sizeof(flength));

    FILE* fptr = fopen(rpsfname.c_str(), "wb");
    if(fptr == NULL){
        cout << "error: " << strerror(errno) << endl;

        return;
    }

    fseek(fptr, flength, SEEK_SET);
    fclose(fptr);

    filemsg* fmsg = (filemsg*)buf;
    __int64_t remlength = flength;
    iters = ceil((double)flength / (double)m);

    while(remlength > 0){
        fmsg->length = min(remlength, (__int64_t)m);
        reqbuf->push(buf, sizeof(filemsg) + filename.size() + 1);
        fmsg->offset += fmsg->length;
        remlength -= fmsg->length;
    }
}

void patient_thread_function(int points, int patient, BoundedBuffer* reqbuf){
    datamsg msg(patient, 0.0, 1);
    for(int i = 0; i < points; i++){
        reqbuf->push((char*)&msg, sizeof(msg));

        msg.seconds += 0.004;
    }
}

void worker_thread_function(RequestChannel* chan, BoundedBuffer* reqbuf, HistogramCollection* hc, int mem, mutex* m){
    char buf[1024];
    double rsp;
    bool running = true;
    char rspbuf[mem];

    while(running){
        int mSize = reqbuf->pop(buf, 1024);
        MESSAGE_TYPE* msg = (MESSAGE_TYPE *)buf;

        switch (*msg){
            case DATA_MSG:
                chan->cwrite(buf, sizeof(datamsg));
                chan->cread(&rsp, sizeof(rsp));
                hc->update(((datamsg*)msg)->person, rsp);
                break;

            case FILE_MSG: {
                filemsg *fmsg = (filemsg *) buf;
                string filename = (char *) (fmsg + 1);
                chan->cwrite(buf, sizeof(filemsg) + filename.size() + 1);
                chan->cread(rspbuf, mem);

                string rspfname = "recv/" + filename;
                FILE *fptr = fopen(rspfname.c_str(), "rb+");
                if(fptr == NULL){
                    cout << "error: " << strerror(errno) << endl;

                    MESSAGE_TYPE qm = QUIT_MSG;
                    chan->cwrite(&qm, sizeof(qm));
                    running = false;
                    break;
                }

                fseek(fptr, fmsg->offset, SEEK_SET);
                fwrite(rspbuf, 1, fmsg->length, fptr);
                fclose(fptr);

                m->lock();
                progress++;
                m->unlock();
                break;
            }

            case QUIT_MSG:
                chan->cwrite(msg, sizeof(MESSAGE_TYPE));
                running = false;
                delete chan;
                break;
        }
    }
}

//create new fifo channels for the threads to use
RequestChannel* create_channel(RequestChannel* main, string imsg, int m, string host, string port){
    RequestChannel* newchan = NULL;
    if(imsg.compare("t") == 0){
        newchan = new TCPRequestChannel(host, port, RequestChannel::CLIENT_SIDE);
        return newchan;
    }

    char name [1024];
    MESSAGE_TYPE msg = NEWCHANNEL_MSG;
    main->cwrite((char*)&msg, sizeof(msg));
    main->cread(name, 1024);

    if(imsg.compare("f") == 0) {
        newchan = new FIFORequestChannel(name, RequestChannel::CLIENT_SIDE);
    }
    else if(imsg.compare("q") == 0){
        newchan = new MQRequestChannel(name, RequestChannel::CLIENT_SIDE, m);
    }
    else if(imsg.compare("s") == 0){
        newchan = new SHMRequestChannel(name, RequestChannel::CLIENT_SIDE, m);
    }
    return newchan;
}

//main function
int main(int argc, char *argv[])
{
    bool safety = false;

    int n = 0;    //default number of requests per "patient"
    int p = 0;     // number of patients [1,15]
    int w = 0;    //default number of worker threads
    int b = 20; 	// default capacity of the request buffer, you should change this default
	int m = MAX_MESSAGE; 	// default capacity of the message buffer
	string filename = "";
	string imsg = "f";
	string host = "";
	string port = "";

	//set globals
	iters = 0;
	progress = 0;

    srand(time_t(NULL));

    int c;
    while((c = getopt(argc, argv, "n:p:w:b:m:f:i:h:r:")) != -1) {
        switch (c) {
            case 'n':
                n = atoi(optarg);
                break;
            case 'p':
                p = atoi(optarg);
                break;
            case 'w':
                w = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 'm':
                m = atoi(optarg);
                break;
            case 'f':
                filename = optarg;
                break;
            case 'i':
                imsg = optarg;
                break;
            case 'h':
                host = optarg;
                break;
            case 'r':
                port = optarg;
                break;
        }
    }

    if(imsg.compare("t") != 0) {    //don't run the server for TCP connection
        int pid = fork();
        if (pid == 0) {
            string memtoa = to_string(m);
            execl("server", "server", "-m", (char *) memtoa.c_str(), "-i", imsg.c_str(), (char *) NULL);
        }
    }

    RequestChannel* chan;
    if(imsg.compare("f") == 0) {
        chan = new FIFORequestChannel("control", RequestChannel::CLIENT_SIDE);
    }
    else if(imsg.compare("q") == 0){
        chan = new MQRequestChannel("control", RequestChannel::CLIENT_SIDE, m);
    }
    else if(imsg.compare("s") == 0){
        chan = new SHMRequestChannel("control", RequestChannel::CLIENT_SIDE, m);
    }
    else if(imsg.compare("t") == 0) {
        chan = new TCPRequestChannel(host, port, RequestChannel::CLIENT_SIDE);
    }

        BoundedBuffer request_buffer(b);

	//setup the histograms
	for(int i = 0; i < p; i++){
	                        //bins, start_ecg_val, end_ecg_val
	    Histogram* h = new Histogram(10, -2.0, 2.0);
	    hc.add(h);
	}


    //set up signal handler
    struct sigaction sa;
    sa.sa_handler = sig_hdlr;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;

    sigaction(SIGALRM, &sa, NULL);

    //set up timer
    timer_t timer;
    struct sigevent sigev;
    sigev.sigev_notify = SIGEV_SIGNAL;
    sigev.sigev_signo = SIGALRM;
    sigev.sigev_value.sival_ptr = &hc; //test this
    timer_create(CLOCK_REALTIME, &sigev, &timer);
    struct itimerspec its;
    its.it_value.tv_sec = 2;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;
    timer_settime(timer, 0, &its, NULL);

	//setup worker channels
	RequestChannel* wc [w];
	for(int i = 0; i < w; i++){
        wc[i] = create_channel(chan, imsg, m, host, port);
	}

    struct timeval start, end;
    gettimeofday (&start, 0);

    /* Start all threads here */
    thread patient[p];
    if(n > 0 && (p > 0 || p <= 15) && w > 0) {
        safety = true;
        for (int i = 0; i < p; i++) {
            patient[i] = thread(&patient_thread_function, n, i + 1, &request_buffer);
        }
    }

    thread* filethread;
    if(filename.size() > 0 && w > 0) {
        safety = true;
        filethread = new thread(&file_thread_function, filename, m, &request_buffer, chan);
    }

    mutex mtx;
    thread worker[w];
    for(int i = 0; safety && i < w; i++){
        worker[i] = thread(&worker_thread_function, wc[i], &request_buffer, &hc, m, &mtx);
    }



	/* Join all threads here */
	if(n > 0 && (p > 0 || p <= 15) && w > 0) {
        for (int i = 0; i < p; i++) {
            patient[i].join();
        }
        cout << "Patient threads done" << endl;
    }

    if(filename.size() > 0) {
        filethread->join();
        cout << "File thread done" << endl;
    }
    delete filethread;

    MESSAGE_TYPE q = QUIT_MSG;
    for(int i = 0; i < w; i++){
        request_buffer.push((char*)&q, sizeof(q));
    }

    for(int i = 0; safety && i < w; i++){
        worker[i].join();
    }
    cout << "Worker threads done" << endl;

    gettimeofday (&end, 0);
    // print the results
	hc.print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;

    //clean up main channel
    chan->cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    cout << "All Done!!!" << endl;
    delete chan;

    
}
