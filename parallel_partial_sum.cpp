#include<numeric>
#include<vector>
#include<iostream>
#include<thread>
#include<functional>
#include<algorithm>
#include<ctime>
#include<future>
#include<exception>

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

template<typename Iterator>
void parallel_partial_sum(Iterator first, Iterator last) {
    typedef typename Iterator::value_type value_type;

    struct process_chunk {
      void operator()(Iterator begin, Iterator last,
      std::future<value_type>* previous_end_value,
      std::promise<value_type>* end_value) {
        try {
          Iterator end = last;
          ++end;
          partial_sum(begin, end, begin);
          if(previous_end_value) {
            const value_type addend = previous_end_value->get();
            *last += addend;
            if(end_value) {
              end_value->set_value(*last);
            }
            std::for_each(begin, last, [addend](value_type& item){
              item += addend;
            });
          } else if(end_value) {
            end_value->set_value(*last);
          }
        } catch(...) {
          if(end_value) {
            end_value->set_exception(std::current_exception());
          }
        }
      } 
    };

    const int length = std::distance(first, last);
    if(!length) return;

    const int min_per_thread = 25;
    const int max_thread = (length + min_per_thread - 1) / min_per_thread;
    const int hardware_thread_limit = std::thread::hardware_concurrency();
    const int num_threads = std::min(hardware_thread_limit, max_thread);
    const int block_size = length / num_threads;

    typedef typename Iterator::value_type value_type;

    std::vector<std::thread> threads(num_threads - 1);
    std::vector<std::promise<value_type> > end_values(num_threads - 1);
    std::vector<std::future<value_type> > previous_end_values;
    previous_end_values.reserve(num_threads - 1);
    join_threads joiner(threads);

    Iterator block_start = first;
    for(int i=0; i<num_threads - 1; ++i) {
      Iterator block_end = block_start;
      std::advance(block_end, block_size);
      threads[i] = std::thread(process_chunk(), block_start, block_end, (i == 0) ? nullptr : &previous_end_values[i-1], &end_values[i]);

      block_start = block_end;
      ++block_start;
      previous_end_values.push_back(end_values[i].get_future());
    }

    Iterator final_element = block_start;
    std::advance(final_element, std::distance(block_start, last) - 1);
    process_chunk()(block_start, final_element, num_threads > 1 ? &previous_end_values.back() : 0, 0);
}

// Testing.
int main(int argc, char** argv) {
    std::vector<int> vec;
    for(int i=0; i<200; i++) {
        vec.push_back(i+1);
    }

    parallel_partial_sum(vec.begin(), vec.end());
    for(const auto& a : vec) {
        std::cout << a << ", ";
    }
    std::cout << std::endl;
    return 0;
}
