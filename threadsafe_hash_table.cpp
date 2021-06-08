#include<condition_variable>
#include<mutex>
#include<list>
#include<shared_mutex>
#include<algorithm>
#include<iostream>
#include<utility>
#include<vector>

template<typename Key, typename Value, typename Hash=std::hash<Key> > 
class threadsafe_hash_table {
 private:
    class bucket_type {
     private:
        typedef std::pair<Key, Value> bucket_value;
        typedef std::list<bucket_value> bucket_data;
        typedef typename bucket_data::iterator bucket_iterator;

        bucket_data data;
        mutable std::shared_mutex mtx;

        const bucket_iterator find_entry_for(const Key& key) {
            return std::find_if(data.begin(), data.end(), [&](bucket_value const& item) {
                return item.first == key;
            });
        }

     public:
        Value value_for(const Key& key, const Value& default_value) {
            std::shared_lock<std::shared_mutex> lock(mtx);
            const bucket_iterator it = find_entry_for(key);
            return it == data.end() ? default_value : it->second;
        }

        void add_or_update_mapping(const Key& key, const Value& value) {
            std::unique_lock<std::shared_mutex> lock(mtx);
            const bucket_iterator it = find_entry_for(key);
            if(it == data.end()) {
                data.push_back({key, value});
            } else {
                it->second = value;
            }
        }

        void remove_mapping(const Key& key) {
            std::unique_lock<std::shared_mutex> lock(mtx);
            const bucket_iterator it = find_entry_for(key);
            if(it != data.end()) {
                data.erase(it);
            }
        }

    };

    std::vector<std::unique_ptr<bucket_type> > buckets;
    Hash hasher;

    bucket_type& get_bucket(const Key& key) const {
        const int bucket_index = hasher(key) % buckets.size();
        return *(buckets[bucket_index]);
    }

 public:
    threadsafe_hash_table(int num_buckets=19, const Hash hasher_=Hash()) 
        : buckets(num_buckets), hasher(hasher_)
    {
        for(int i=0; i<num_buckets; ++i) {
            buckets[i].reset(new bucket_type);
        }
    }

    threadsafe_hash_table(const threadsafe_hash_table& other) = delete;
    threadsafe_hash_table& operator=(const threadsafe_hash_table& other) = delete;

    Value value_for(const Key& key, const Value& default_value=Value()) const {
        return get_bucket(key).value_for(key, default_value);
    }

    void add_or_update_mapping(const Key& key, const Value& value) {
        get_bucket(key).add_or_update_mapping(key, value);
    }

    void remove_mapping(const Key& key) {
        get_bucket(key).remove_mapping(key);
    }
};

// Test
int main() {
    threadsafe_hash_table<int, std::string> table;
    table.add_or_update_mapping(1, "hello");
    table.add_or_update_mapping(2, "world");
    table.remove_mapping(1);

    table.add_or_update_mapping(2, "world world");
    auto value = table.value_for(2);
    std::cout << value << std::endl;
    return 0;
}
