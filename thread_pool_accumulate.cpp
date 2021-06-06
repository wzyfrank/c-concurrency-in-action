#include<thread>
#include<functional>
#include<vector>
#include<future>
#include<mutex>
#include<iostream>
#include<queue>
#include<atomic>
#include<algorithm>
#include<numeric>
#include<type_traits>

using namespace std;

typedef long long ll;

// Thread joiner. 
class join_threads {
 private:
    vector<thread>& threads_;
 public:
    join_threads(vector<thread>& threads) : threads_(threads) {}
    ~join_threads() {
        for(auto& t: threads_) {
            if(t.joinable()) t.join();
        }
    }
};

// Thread-safe queue.
template<typename T>
class threadsafe_queue {
 private:
    mutex mtx;
    queue<T> dq;
    condition_variable cv;

 public:
    threadsafe_queue() {

    }

    void push(T data) {
        unique_lock<mutex> lk(mtx);
        dq.push(std::move(data));
        cv.notify_one();
    }

    void wait_and_pop(T& value) {
        unique_lock<mutex> lk(mtx);
        cv.wait(lk, [this]() { return !dq.empty(); });
        value = move(dq.front());
        dq.pop();
    }

    shared_ptr<T> wait_and_pop() {
        unique_lock<mutex> lk(mtx);
        cv.wait(lk, [this]() { return !dq.empty(); });
        shared_ptr<T> res(make_shared<T>(move(dq.front())));
        dq.pop();       
        return res;
    }

    bool try_pop(T& value) {
        unique_lock<mutex> lk(mtx);
        if(dq.empty()) {
            return false;
        }
        value = move(dq.front());
        dq.pop();
        return true;
    }

    shared_ptr<T> try_pop() {
        unique_lock<mutex> lk(mtx);
        if(dq.empty()) {
            return shared_ptr<T>();
        }
        shared_ptr<T> res(make_shared<T>(move(dq.front())));
        dq.pop();
        return res;
    }

    bool empty() const {
        unique_lock<mutex> lk(mtx);
        return dq.empty();
    }

};

class function_wrapper {
 private:
    struct impl_base {
        virtual void call() = 0;
        virtual ~impl_base() {}
    };

    template<typename F>
    struct impl_type : impl_base {
        F f;
        impl_type(F&& f_) : f(move(f_)) {}
        void call() { f(); };
    };

    unique_ptr<impl_base> impl;

 public:
    template<typename F>
    function_wrapper(F&& f): impl(new impl_type<F>(move(f))) {}

    void operator()() { impl->call(); }

    function_wrapper() = default;

    function_wrapper(function_wrapper&& other) : impl(move(other.impl)) {}

    function_wrapper& operator=(function_wrapper&& other) {
        impl = move(other.impl);
        return *this;
    }

    function_wrapper(const function_wrapper&) = delete;
    function_wrapper(function_wrapper&) = delete;
    function_wrapper& operator=(const function_wrapper&) = delete;
};

class thread_pool {
 private:
    atomic_bool done;
    threadsafe_queue<function_wrapper> work_queue;
    vector<thread> threads;
    join_threads joiner;

    void worker_thread() {
        while(!done) {
            function_wrapper task;
            if(work_queue.try_pop(task)) {
                task();
            } else {
                this_thread::yield();
            }
        }
    }

 public:
    thread_pool(): done(false), joiner(threads) {
        int thread_count= thread::hardware_concurrency();
        try {
            for(int i=0; i<thread_count; ++i) {
                threads.push_back(thread(&thread_pool::worker_thread, this));
            }
        } catch(...) {
            done = true;
            throw;
        }
    }

    ~thread_pool() {
        done = true;
    }

    template<typename FunctionType>
    future<typename result_of<FunctionType()>::type> submit(FunctionType f) {
        typedef typename result_of<FunctionType()>::type result_type;

        packaged_task<result_type()> task(move(f));
        future<result_type> res(task.get_future());
        work_queue.push(move(task));
        return res;
    }
};

template<typename Iterator, typename T>
struct accumulate_block
{
    T operator()(Iterator first, Iterator last) {
        return accumulate(first, last, T());
    }
};

template<typename Iterator, typename T> 
T parallel_accumulate(Iterator first, Iterator last, T init) {
    const int length = std::distance(first, last);
    if(!length) return init;

    const int block_size = 25;
    const int num_blocks = (length + block_size - 1) / block_size;

    vector<future<T> > futures(num_blocks - 1);
    thread_pool pool;

    Iterator block_start = first;
    for(int i=0; i<num_blocks - 1; ++i) {
        Iterator block_end = block_start;
        advance(block_end, block_size);
        auto f = [=](){
            return accumulate_block<Iterator, T>()(block_start, block_end);
        };
        futures[i] = pool.submit(f);
        block_start = block_end;
    }
    T result = accumulate_block<Iterator, T>()(block_start, last);
    
    result += init;
    for(auto& f: futures) {
        result += f.get();
    }
    return result;
}

int main() {
    vector<ll> vec;
    for(ll i=0; i<100000; i++) {
        vec.push_back(i+1);
    }

    auto t_start = std::chrono::high_resolution_clock::now();
    ll init = 0;
    ll result = parallel_accumulate<vector<ll>::iterator, ll>(vec.begin(), vec.end(), init);
    cout << "result is: " << result << endl;
    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end-t_start).count();
    cout << "elapsed time is: " << elapsed_time_ms << endl;
    
    return 0;
}
