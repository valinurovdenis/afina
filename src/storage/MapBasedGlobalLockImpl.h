#ifndef AFINA_STORAGE_MAP_BASED_GLOBAL_LOCK_IMPL_H
#define AFINA_STORAGE_MAP_BASED_GLOBAL_LOCK_IMPL_H

#include <unordered_map>
#include <mutex>
#include <string>
#include <functional>

#include <afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation with global lock
 *
 *
 */

struct Entry {
    Entry(const std::string &key, const std::string &value): _prev(nullptr), _next(nullptr), _key(key), _value(value){}

    const std::string &_key;
    std::string _value;
    Entry *_prev, *_next;
};

class DoubleLinked {
public:
    DoubleLinked(): _tail(nullptr), _head(nullptr){}

    Entry* add_front(const std::string &key, const std::string &value){
        if (_head == nullptr){
            _head = _tail = new Entry(key, value);
        } else {
            Entry* temp = new Entry(key, value);
            _head->_next = temp;
            temp->_prev = _head;
            _head = temp;
        }
        return _head;
    }

    std::string del(Entry* ptr){
        if (ptr == _head)
            _head = ptr->_prev;
        if (ptr == _tail)
            _tail = ptr->_next;

        if (ptr->_next != nullptr)
            ptr->_next->_prev = ptr->_prev;
        if (ptr->_prev != nullptr)
            ptr->_prev->_next = ptr->_next;
        std::string ret = ptr->_key;
        delete ptr;

        return ret;
    }

    std::string pop_back(){
        return del(_tail);
    }

    std::string get_back(){
        return _tail->_key;
    }

private:
    Entry *_tail, *_head;
};

class MapBasedGlobalLockImpl : public Afina::Storage {
public:
    MapBasedGlobalLockImpl(size_t max_size = 1024) : _max_size(max_size), _current_size(0) {}
    ~MapBasedGlobalLockImpl() {}

    // Implements Afina::Storage interface
    bool Put(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool PutIfAbsent(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Set(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Delete(const std::string &key) override;

    // Implements Afina::Storage interface
    bool Get(const std::string &key, std::string &value) const override;

private:
    void SizeControl(size_t add_memory);

    mutable std::recursive_mutex _lock;
    size_t _max_size;
    size_t _current_size;
    mutable std::unordered_map<std::string, Entry*> _backend;
    mutable DoubleLinked _list;
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_MAP_BASED_GLOBAL_LOCK_IMPL_H
