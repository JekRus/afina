#include "MapBasedGlobalLockImpl.h"

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value) {
    auto it = _backend.find(key);
    if (it != _backend.end()) {
        _key_list.erase(std::find(_key_list.begin(), _key_list.end(), key));
        _key_list.push_back(key);
        _backend.erase(it);
    } else {
        if (_key_list.size() < _max_size) {
            _key_list.push_back(key);
        } else {
            _backend.erase(_backend.find(_key_list.front()));
            _key_list.pop_front();
        }
    }
    return _backend.emplace(key, value).second;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    auto it = _backend.find(key);
    if (it == _backend.end()) {
        if (_key_list.size() < _max_size) {
            _key_list.push_back(key);
        } else {
            _backend.erase(_backend.find(_key_list.front()));
            _key_list.pop_front();
        }
    }
    return _backend.emplace(key, value).second;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value) {
    auto it = _backend.find(key);
    if (it != _backend.end()) {
        _backend.erase(it);
        _backend.emplace(key, value);
        return true;
    }
    return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key) {
    auto it = _backend.find(key);
    if (it != _backend.end()) {
        _key_list.erase(std::find(_key_list.begin(), _key_list.end(), key));
        _backend.erase(it);
        return true;
    }
    return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) const {
    auto it = _backend.find(key);
    if (it != _backend.end()) {
        if (it->second.length() > value.capacity()) {
            value.reserve(it->second.length());
        }
        value = it->second;
        return true;
    }
    return false;
}

} // namespace Backend
} // namespace Afina
