#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "common.h"
#include "HistogramCollection.h"
#include "FIFOreqchannel.h"
#include <thread>
#include <mutex>
#include <unistd.h>
#include <cstdio>
using namespace std;

void file_thread_function(string filename, int m, BoundedBuffer* reqbuf, FIFORequestChannel* chan){
    string rpsfname = "recv/" + filename;
    char buf[1024];
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

void worker_thread_function(FIFORequestChannel* chan, BoundedBuffer* reqbuf, HistogramCollection* hc, int mem){
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
                fseek(fptr, fmsg->offset, SEEK_SET);
                fwrite(rspbuf, 1, fmsg->length, fptr);
                fclose(fptr);
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
FIFORequestChannel* create_channel(FIFORequestChannel* main){
    char name [1024];
    MESSAGE_TYPE msg = NEWCHANNEL_MSG;
    main->cwrite((char*)&msg, sizeof(msg));
    main->cread(name, 1024);
    FIFORequestChannel* newch = new FIFORequestChannel(name, FIFORequestChannel::CLIENT_SIDE);
    return newch;
}

int main(int argc, char *argv[])
{
    int n = 0;    //default number of requests per "patient"
    int p = 0;     // number of patients [1,15]
    int w = 0;    //default number of worker threads
    int b = 20; 	// default capacity of the request buffer, you should change this default
	int m = MAX_MESSAGE; 	// default capacity of the message buffer
	string filename = "";

    srand(time_t(NULL));

    int c;
    while((c = getopt(argc, argv, ":c:n:p:w:b:m:f:")) != -1){
        switch(c){
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
            case 'm':
                m = atoi(optarg);
                break;
            case 'f':
                filename = optarg;
                break;
            //is this needed??
            case ':':
                switch(optopt){
                    case 'c':
                        //channel = true;
                        break;
                }
                break;
        }
    }

    int pid = fork();
    if (pid == 0){
		string memtoa = to_string(m);
        execl ("server", "server", "-m", (char*)memtoa.c_str(), (char *)NULL);
    }

	FIFORequestChannel* chan = new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE);
    BoundedBuffer request_buffer(b);
	HistogramCollection hc;

	//setup the histograms
	for(int i = 0; i < p; i++){
	                        //bins, start_ecg_val, end_ecg_val
	    Histogram* h = new Histogram(10, -2.0, 2.0);
	    hc.add(h);
	}

	//setup worker channels
	FIFORequestChannel* wc [w];
	for(int i = 0; i < w; i++){
        wc[i] = create_channel(chan);
	}

    struct timeval start, end;
    gettimeofday (&start, 0);

    /* Start all threads here */
    thread patient[p];
    if(n > 0 && (p > 0 || p <= 15) && w > 0) {
        for (int i = 0; i < p; i++) {
            patient[i] = thread(&patient_thread_function, n, i + 1, &request_buffer);
        }
    }

    thread* filethread;
    if(filename.size() > 0 && w > 0) {
        filethread = new thread(&file_thread_function, filename, m, &request_buffer, chan);
    }

    thread worker[w];
    for(int i = 0; i < w; i++){
        worker[i] = thread(&worker_thread_function, wc[i], &request_buffer, &hc, m);
    }



	/* Join all threads here */
    for(int i = 0; i < p; i++){
        patient[i].join();
    }
    cout << "Patient threads done" << endl;

    if(filename.size() > 0) {
        filethread->join();
        cout << "File thread done" << endl;
    }

    MESSAGE_TYPE q = QUIT_MSG;
    for(int i = 0; i < w; i++){
        request_buffer.push((char*)&q, sizeof(q));
    }

    for(int i = 0; i < w; i++){
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
