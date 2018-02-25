#include "MapBasedGlobalLockImpl.h"

#include <mutex>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value) {
    std::unique_lock<std::recursive_mutex> guard(_lock);

    if (_backend.find(key) != _backend.end())
        _list.del(_backend[key]);
    else
        _backend[key] = nullptr; // in order to create element with key

    SizeControl(key.size() + value.size());
    _backend[key] = _list.add_front(_backend.find(key)->first, value); // by standard references to keys in unordered_map doesn't invalidate even if rehashing occurs
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    std::unique_lock<std::recursive_mutex> guard(_lock);

    if (_backend.find(key) != _backend.end())
        return false;

    SizeControl(key.size() + value.size());
    _backend[key] = nullptr;
    _backend[key] = _list.add_front(_backend.find(key)->first, value);

    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value) {
    std::unique_lock<std::recursive_mutex> guard(_lock);

    if (_backend.find(key) == _backend.end())
        return false;

    SizeControl(value.size() - (_backend[key]->_value).size());
    _backend[key]->_value = value;
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key) {
    std::unique_lock<std::recursive_mutex> guard(_lock);

    if (_backend.find(key) == _backend.end())
        return false;

    _current_size -= key.size() + (_backend[key]->_value).size();
    _list.del(_backend[key]);
    _backend.erase(key);
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) const {
    std::unique_lock<std::recursive_mutex> guard(_lock);

    if (_backend.find(key) == _backend.end())
        return false;
    value = _backend[key]->_value;
    _list.del(_backend[key]);
    _backend[key] = _list.add_front(_backend.find(key)->first, value);

    return true;
}

void MapBasedGlobalLockImpl::SizeControl(size_t add_memory){
    _current_size += add_memory;
    while (_current_size > _max_size){
        Delete(_list.get_back());
    }
}

} // namespace Backend
} // namespace Afina
