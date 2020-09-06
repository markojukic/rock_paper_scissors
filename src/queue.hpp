#pragma once

#include <deque>
#include <mutex>
#include <condition_variable>

// Multiple producer, single consumer queue
// T should be copy-constructible
template <class T>
class Queue {
    std::deque<T> data;                 // Queue elements
    std::mutex data_m;                  // Queue mutex
    std::condition_variable data_cv;    // Notifications for new elements
public:
    typedef typename std::deque<T>::size_type size_type;

    Queue() = default;

    // Number of elements in queue
    size_type size() const {
        std::lock_guard lock(data_m);
        return data.size();
    }

    // Checks whether the queue is empty 
    bool empty() const {
        std::lock_guard lock(data_m);
        return data.empty();
    }


    // Adds an element to the end
    void put(const T& value) {
        {
            std::lock_guard lock(data_m);
            data.push_back(value);
        }
        // Change to notify_all for multiple consumer
        data_cv.notify_one();
    }

    // Removes and returns the first element 
    T get() {
        std::unique_lock lock(data_m);
        data_cv.wait(lock, [this] {
            return !data.empty();
        });
        T value(data.front());
        data.pop_front();
        return value;
    }

    // Clears the contents
    void clear() {
        std::lock_guard lock(data_m);
        data.clear();
    }
};
