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
#include<list>

// Thread joiner. 
class join_threads {
 private:
    std::vector<std::thread>& threads_;
 public:
    join_threads(std::vector<std::thread>& threads) : threads_(threads) {}
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
    std::mutex mtx;
    std::queue<T> dq;
    std::condition_variable cv;

 public:
    threadsafe_queue() {

    }

    void push(T data) {
        std::unique_lock<std::mutex> lk(mtx);
        dq.push(std::move(data));
        cv.notify_one();
    }

    void wait_and_pop(T& value) {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [this]() { return !dq.empty(); });
        value = std::move(dq.front());
        dq.pop();
    }

    std::shared_ptr<T> wait_and_pop() {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [this]() { return !dq.empty(); });
        std::shared_ptr<T> res(std::make_shared<T>(std::move(dq.front())));
        dq.pop();       
        return res;
    }

    bool try_pop(T& value) {
        std::unique_lock<std::mutex> lk(mtx);
        if(dq.empty()) {
            return false;
        }
        value = std::move(dq.front());
        dq.pop();
        return true;
    }

    std::shared_ptr<T> try_pop() {
        std::unique_lock<std::mutex> lk(mtx);
        if(dq.empty()) {
            return std::shared_ptr<T>();
        }
        std::shared_ptr<T> res(std::make_shared<T>(std::move(dq.front())));
        dq.pop();
        return res;
    }

    bool empty() const {
        std::unique_lock<std::mutex> lk(mtx);
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
        impl_type(F&& f_) : f(std::move(f_)) {}
        void call() { f(); };
    };

    std::unique_ptr<impl_base> impl;

 public:
    template<typename F>
    function_wrapper(F&& f): impl(new impl_type<F>(std::move(f))) {}

    void operator()() { impl->call(); }

    function_wrapper() = default;

    function_wrapper(function_wrapper&& other) : impl(std::move(other.impl)) {}

    function_wrapper& operator=(function_wrapper&& other) {
        impl = std::move(other.impl);
        return *this;
    }

    function_wrapper(const function_wrapper&) = delete;
    function_wrapper(function_wrapper&) = delete;
    function_wrapper& operator=(const function_wrapper&) = delete;
};

class thread_pool {
 private:
    std::atomic_bool done;
    threadsafe_queue<function_wrapper> work_queue;
    std::vector<std::thread> threads;
    join_threads joiner;

    void worker_thread() {
        while(!done) {
            function_wrapper task;
            if(work_queue.try_pop(task)) {
                task();
            } else {
                std::this_thread::yield();
            }
        }
    }

 public:
    thread_pool(): done(false), joiner(threads) {
        int thread_count= std::thread::hardware_concurrency();
        try {
            for(int i=0; i<thread_count; ++i) {
                threads.push_back(std::thread(&thread_pool::worker_thread, this));
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
    std::future<typename std::result_of<FunctionType()>::type> submit(FunctionType f) {
        typedef typename std::result_of<FunctionType()>::type result_type;

        std::packaged_task<result_type()> task(std::move(f));
        std::future<result_type> res(task.get_future());
        work_queue.push(move(task));
        return res;
    }

    void run_pending_task() {
        function_wrapper task;
        if(work_queue.try_pop(task)) {
            task();
        } else {
            std::this_thread::yield();
        }
    }
};

template<typename T>
struct sorter {
    thread_pool pool;

    std::list<T> do_sort(std::list<T>& chunk_data) {
        if(chunk_data.empty()) {
            return chunk_data;
        }

        std::list<T> result;
        result.splice(result.begin(), chunk_data, chunk_data.begin());
        const T& partition_val = *result.begin();
        auto divide_point = std::partition(chunk_data.begin(), chunk_data.end(), 
                [&](const T& val){ return val < partition_val; }
            );
        
        std::list<T> new_lower_chunk;
        new_lower_chunk.splice(new_lower_chunk.end(), chunk_data, chunk_data.begin(), divide_point);
        std::future<std::list<T> > new_lower = pool.submit(
            std::bind(&sorter::do_sort, this, std::move(new_lower_chunk))
        );

        std::list<T> new_higher(do_sort(chunk_data));
        result.splice(result.end(), new_higher);

        while(new_lower.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            pool.run_pending_task();
        }
        result.splice(result.begin(), new_lower.get());

        return result;
    }
};

template<typename T>
std::list<T> parallel_quick_sort(std::list<T>& input) {
    if(input.empty()) {
        return input;
    }
    sorter<T> s;
    return s.do_sort(input);
}

int main() {    
    int size = 1000;
    std::vector<int> vec;
    for(int i=0; i<size; ++i) vec.push_back(i+1);
    std::random_shuffle(vec.begin(), vec.end());
    std::list<int> input;

    std::cout << "List before sort: " << std::endl;
    for(const auto& a: vec) {
        std::cout << a << ", ";
        input.push_back(a);
    }
    std::cout << std::endl;

    auto res = parallel_quick_sort(input);

    std::cout << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "List after sort: " << std::endl;
    for(const auto& a : res) {
        std::cout << a << ", "; 
    }
    std::cout << std::endl;

    return 0;
}
