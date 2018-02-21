#include "MapBasedGlobalLockImpl.h"

#include <mutex>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);

    if (_backend.find(key) != _backend.end())
        _list.del(_backend[key]);
    else if (_backend.size() == _max_size)
        _backend.erase(_list.pop_back());

    _backend[key] = _list.add_front(&(_backend.find(key)->first), value);
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);

    if (_backend.find(key) != _backend.end())
        return false;

    if (_backend.size() == _max_size)
            _backend.erase(_list.pop_back());

    _backend[key] = _list.add_front(&(_backend.find(key)->first), value);

    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);

    if (_backend.find(key) == _backend.end())
        return false;

    _backend[key]->_value = value;
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key) {
    std::unique_lock<std::mutex> guard(_lock);

    if (_backend.find(key) == _backend.end())
        return false;

    _list.del(_backend[key]);
    _backend.erase(key);
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) const {
    std::unique_lock<std::mutex> guard(*const_cast<std::mutex *>(&_lock));

    if (_backend.find(key) == _backend.end())
        return false;
    value = _backend[key]->_value;
    _list.del(_backend[key]);
    _backend[key] = _list.add_front(&(_backend.find(key)->first), value);

    return true;
}

} // namespace Backend
} // namespace Afina
