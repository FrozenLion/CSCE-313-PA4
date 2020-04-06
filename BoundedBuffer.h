#ifndef BoundedBuffer_h
#define BoundedBuffer_h

#include <stdio.h>
#include <queue>
#include <string>
#include <mutex>
#include <assert.h>
#include <condition_variable>

using namespace std;

class BoundedBuffer
{
private:
	int cap; // max number of items in the buffer
	queue<vector<char>> q;	/* the queue of items in the buffer. Note
	that each item a sequence of characters that is best represented by a vector<char> for 2 reasons:
	1. An STL std::string cannot keep binary/non-printables
	2. The other alternative is keeping a char* for the sequence and an integer length (i.e., the items can be of variable length).
	While this would work, it is clearly more tedious */

	// add necessary synchronization variables and data structures
	mutex m;
	condition_variable data_ready; //wait for popping
	condition_variable slot_ready; //wait for pushing


public:
	BoundedBuffer(int _cap){
        cap = _cap;
	}
	~BoundedBuffer(){
        cap = 0;
        for(int i = 0; i < q.size(); i++){
            vector<char> v = q.front();
            v.clear();
            q.pop();
        }
	}

	void push(char* data, int len){
        //1. Convert the incoming byte sequence given by data and len into a vector<char>
        vector<char> v(data, data+len);

		//2. Wait until there is room in the queue (i.e., queue lengh is less than cap)
        unique_lock<mutex> l(m);
        slot_ready.wait(l, [this]{return q.size() < cap;});

		//3. Then push the vector at the end of the queue
		q.push(v);
        l.unlock();

		//4. Wake up pop threads
		data_ready.notify_one();
	}

	int pop(char* buf, int bufcap){
		//1. Wait until the queue has at least 1 item
        unique_lock<mutex> l(m);
        data_ready.wait(l, [this]{return q.size() > 0;});

		//2. pop the front item of the queue. The popped item is a vector<char>
        int ret = bufcap;
        vector<char> v = q.front();
        q.pop();
        l.unlock();

		//3. Convert the popped vector<char> into a char*, copy that into buf, make sure that vector<char>'s length is <= bufcap
		assert(v.size() < bufcap);
		memcpy(buf, v.data(), v.size());

		//4. Wake up push threads
		slot_ready.notify_one();

		//5. Return the vector's length to the caller so that he knows many bytes were popped
        return v.size();
	}
};

#endif /* BoundedBuffer_ */
