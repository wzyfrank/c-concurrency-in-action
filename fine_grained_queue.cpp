#include<mutex>
#include<iostream>
#include<memory>
#include<condition_variable>
#include<thread>
#include<vector>

template<typename T>
class threadsafe_queue {
 private:
    struct node {
        std::shared_ptr<T> data;
        std::unique_ptr<node> next;
    };

    std::mutex head_mutex;
    std::mutex tail_mutex;
    std::condition_variable cv;
    std::unique_ptr<node> head;
    node* tail;

    node* get_tail() {
        std::lock_guard<std::mutex> tail_lock(tail_mutex);
        return tail;
    }

    std::unique_ptr<node> pop_head() {
        std::unique_ptr<node> old_head = std::move(head);
        head = std::move(old_head->next);
        return old_head;
    }

    std::unique_ptr<node> try_pop_head() {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        if(head.get() == get_tail()) {
            return std::unique_ptr<node>();
        }

        return pop_head();
    }

    std::unique_ptr<node> wait_pop_head() {
        std::unique_lock<std::mutex> head_lock(head_mutex);
        cv.wait(head_lock, [&]{ return head.get() != get_tail(); });

        return pop_head();
    }

 public:
    threadsafe_queue(): head(new node), tail(head.get()) {}
    threadsafe_queue(const threadsafe_queue& other) = delete;
    threadsafe_queue& operator=(const threadsafe_queue& other) = delete;

    std::shared_ptr<T> try_pop() {
        std::unique_ptr<node> old_head = pop_head();
        return old_head ? old_head->data : std::shared_ptr<T>();
    } 

    std::shared_ptr<T> wait_and_pop() {
        std::unique_ptr<node> old_head = wait_pop_head();
        return old_head->data;
    }

    void push(T new_value) {
        std::shared_ptr<T> new_data(std::make_shared<T>(std::move(new_value)));
        std::unique_ptr<node> p(new node);
        
        {
            std::lock_guard<std::mutex> tail_lock(tail_mutex);
            node* new_tail = p.get();
            tail->data = new_data;
            tail->next = std::move(p);
            tail = new_tail;
        }

        cv.notify_one();
    }
};

// Test
int main() {
    const int q_size = 1000;
    threadsafe_queue<int> q;
    for(int i=0; i<q_size; ++i)
        q.push(i+1);
    
    int num_threads = 10;
    std::vector<std::thread> threads;
    for(int i=0; i<num_threads; ++i) {
        const int tasks_per_thread = q_size / num_threads;
        threads.push_back(std::thread([&] {
            for(int j=0; j<tasks_per_thread; ++j) {
                auto ptr = q.wait_and_pop();
            }
        }));
    }

    for(auto& t : threads) {
        t.join();
    }

    std::cout << s.size() << std::endl;
    return 0;
}
