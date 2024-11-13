#ifndef REDISDATASTORE_HPP
#define REDISDATASTORE_HPP

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <optional>
#include <map>
#include <queue>
#include <vector>
#include <cstdint>

class RedisDataStore {
public:
    RedisDataStore() {
        std::lock_guard<std::mutex> guard(rds_mutex);
        rds_object_counter++;
        if (rds_object_counter == 1 && !is_continue_monitoring) {
            is_continue_monitoring = true;
            monitor_thread = std::thread(monitor_keys_for_expiry);  // do I need to detach this thread ?
        }
    }

    ~RedisDataStore() {
        std::lock_guard<std::mutex> guard(rds_mutex);
        rds_object_counter--;
        if (rds_object_counter == 0) {
            is_continue_monitoring = false;
            if (monitor_thread.joinable()) {
                monitor_thread.join();
            }
        }
    }

    std::optional<std::string> get_kv(const std::string& key) {
        std::lock_guard<std::mutex> guard(rds_mutex);  // do I need to lock mutex for getting key, value ?
        auto it = key_value_map.find(key);
        if (it == key_value_map.end()) {
            return std::nullopt;
        }
        return key_value_map[key];
    }

    // int set_kv(const std::string& key, const std::string& value) {
    //     uint8_t return_status = 0;
    //     try {
    //         std::lock_guard<std::mutex> guard(rds_mutex);
    //         key_value_map[key] = value;
    //     }
    //     catch(...) {
    //         return_status = -1;
    //     }
    //     return return_status;
    // }

    int set_kv(const std::string& key, const std::string& value, const uint64_t& expiry_time_ms = UINT64_MAX) {
        uint8_t return_status = 0;
        try {
            std::lock_guard<std::mutex> guard(rds_mutex);
            key_value_map[key] = value;
            if (expiry_time_ms != UINT64_MAX)
               key_expiry_pq.push({key, expiry_time_ms});
        }
        catch (std::exception& e) {
            return_status = -1;
        }
        catch(...) {
            return_status = -1;
        }
        return return_status;
    }

    int delete_kv(const std::string& key) {
        do {
            std::lock_guard<std::mutex> guard(rds_mutex);
            auto it = key_value_map.find(key);
            if (it == key_value_map.end()) {
                return -1;
            }
            key_value_map.erase(key);
        } while(false);
        delete_pair_from_pq(key);
        // if (it = key_expiry_pq.find(key) != key_expiry_pq.end())  // no need to check because erase does not throw exception
        return 0;
    }

private:
    void monitor_keys_for_expiry() {

        while(is_continue_monitoring) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            // std::lock_guard<std::mutex> guard(rds_mutex);

            while (!key_expiry_pq.empty()) {
                auto key_value_pair = key_expiry_pq.top();
                std::string key = key_value_pair.first;
                uint64_t key_expiry_time = key_expiry_pq.second;
                if (key_expiry_time > get_current_time_ms())
                    break;
                // delete_kv(key); // but this is slow
                std::lock_guard<std::mutex> guard(rds_mutex);
                key_value_map.erase(key);
                key_expiry_pq.pop();
            }

            // for(auto it = key_expiry_map.begin(); it != key_expiry_map.end(); ) {
            //     if (it->second < get_current_time_ms()) {
            //         // std::lock_guard<std::mutex> guard(key_value_map_mutex);
            //         std::cerr << "\nDeleting key=" << it->first << ", value=" << key_value_map[it->first] << "\n";
            //         key_value_map.erase(it->first);
            //         it = key_expiry_map.erase(it);
            //     }
            //     else {
            //         ++it;
            //     }
            // }
        }
    }

    int delete_pair_from_pq(const std::string& key) {
        std::vector<std::pair<std::string, uint64_t>> temp;
        std::lock_guard<std::mutex> guard(rds_mutex);
        while (!key_expiry_pq.empty()) {
            auto top = key_expiry_pq.top();
            key_expiry_pq.pop();
            if (top->first != key) {
                temp.push_back(top);
            }
            else {
                break;
            }
        }
        for(auto& p : temp) {
            key_expiry_pq.push(p);
        }
    }

    uint64_t get_current_time_ms() {
        auto now = std::chrono::system_clock::now();  // time point object
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            
    }

    struct ExpiryComparator {
        bool operator()(const std::pair<std::string, uint64_t>& left, const std::pair<std::string, uint64_t>& right) {
            return left.second > right.second;  // for min-heap
        }
    };

    static std::map<std::string, std::string> key_value_map;
    // priority queue is meant to store only the keys with expiry so as to get the earliest expiring key
    static std::priority_queue<std::pair<std::string, uint64_t>, std::vector<std::pair<std::string, uint64_t>>, ExpiryComparator> key_expiry_pq;
    static bool is_continue_monitoring; 
    static uint8_t rds_object_counter;
    static std::thread monitor_thread;
    static std::mutex rds_mutex;
    // static std::map<std::string, uint64_t> key_expiry_map;
    // static std::mutex key_expiry_map_mutex;
    // static uint64_t daemon_thread_sleep_duration_ms;
    // static std::vector<std::thread> daemon_thread_pool;
};

std::map<std::string, std::string> RedisDataStore::key_value_map;
std::priority_queue<std::pair<std::string, uint64_t>, std::vector<std::pair<std::string, uint64_t>>, ExpiryComparator> RedisDataStore::key_expiry_pq;
bool RedisDataStore::is_continue_monitoring = false;
uint8_t RedisDataStore::rds_object_counter;
std::thread RedisDataStore::monitor_thread;
std::mutex RedisDataStore::rds_mutex;
// std::map<std::string, uint64_t> RedisDataStore::key_expiry_map;
// uint32_t RedisDataStore::daemon_thread_sleep_duration_ms;
// std::vector<std::thread> RedisDataStore::daemon_thread_pool;

#endif  // REDISDATASTORE_HPP