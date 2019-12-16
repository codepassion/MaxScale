/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "storage_memcached"
#include "memcachedstorage.hh"
#include <libmemcached/memcached.h>

using std::string;
using std::vector;
using std::unique_ptr;

namespace
{

vector<char> get_memcached_key(const CACHE_KEY& key)
{
    vector<char> mkey;
    mkey.reserve(key.user.size() + key.host.size() + sizeof(uint64_t) + sizeof(uint64_t));

    auto it = std::back_inserter(mkey);

    const char* p;

    p = key.user.c_str();
    std::copy(p, p + key.user.size(), it);
    p = key.host.c_str();
    std::copy(p, p + key.host.size(), it);
    p = reinterpret_cast<const char*>(&key.data_hash);
    std::copy(p, p + sizeof(key.data_hash), it);
    p = reinterpret_cast<const char*>(&key.full_hash);
    std::copy(p, p + sizeof(key.full_hash), it);

    return mkey;
}

class MemcachedToken : public Storage::Token
{
public:
    ~MemcachedToken()
    {
        memcached_free(m_pMemc);
    }

    static bool create(const string& memcached_config, unique_ptr<Storage::Token>* psToken)
    {
        bool rv = false;
        memcached_st* pMemc = memcached(memcached_config.c_str(), memcached_config.size());

        if (pMemc)
        {
            psToken->reset(new (std::nothrow) MemcachedToken(pMemc));

            if (*psToken)
            {
                rv = true;
            }
            else
            {
                memcached_free(pMemc);
            }
        }
        else
        {
            MXS_ERROR("Could not create memcached handle.");
        }

        return rv;
    }

    cache_result_t get_value(const CACHE_KEY& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF** ppValue,
                             std::function<void (cache_result_t, GWBUF*)> cb)
    {
        vector<char> mkey = get_memcached_key(key);

        size_t nValue;
        uint32_t memcached_flags;
        memcached_return_t result;
        char* pValue = memcached_get(m_pMemc, mkey.data(), mkey.size(), &nValue, &memcached_flags, &result);

        cache_result_t rv;

        if (pValue)
        {
            *ppValue = gwbuf_alloc_and_load(nValue, pValue);

            MXS_FREE(pValue);
            rv = CACHE_RESULT_OK;
        }
        else
        {
            MXS_NOTICE("memcached_get failed: %s", memcached_last_error_message(m_pMemc));
            rv = CACHE_RESULT_NOT_FOUND;
        }

        return rv;
    }

    cache_result_t put_value(const CACHE_KEY& key,
                             const std::vector<std::string>& invalidation_words,
                             const GWBUF* pValue,
                             std::function<void (cache_result_t)> cb)
    {
        vector<char> mkey = get_memcached_key(key);

        memcached_return_t result = memcached_set(m_pMemc, mkey.data(), mkey.size(),
                                                  reinterpret_cast<const char*>(GWBUF_DATA(pValue)),
                                                  GWBUF_LENGTH(pValue), 0, 0);

        cache_result_t rv;

        if (result == MEMCACHED_SUCCESS)
        {
            rv = CACHE_RESULT_OK;
        }
        else
        {
            MXS_NOTICE("memcached_get failed: %s", memcached_last_error_message(m_pMemc));
            rv = CACHE_RESULT_ERROR;
        }

        return rv;
    }

    cache_result_t del_value(const CACHE_KEY& key,
                             std::function<void (cache_result_t)> cb)
    {
        vector<char> mkey = get_memcached_key(key);

        memcached_return_t result = memcached_delete(m_pMemc, mkey.data(), mkey.size(), 0);

        cache_result_t rv;

        if (result == MEMCACHED_SUCCESS)
        {
            rv = CACHE_RESULT_OK;
        }
        else
        {
            MXS_NOTICE("memcached_get failed: %s", memcached_last_error_message(m_pMemc));
            rv = CACHE_RESULT_ERROR;
        }

        return rv;
    }

private:
    MemcachedToken(memcached_st* pMemc)
        : m_pMemc(pMemc)
    {
    }

private:
    memcached_st* m_pMemc;
};

}

MemcachedStorage::MemcachedStorage(const string& name,
                                   const Config& config,
                                   const string& memcached_config)
    : m_name(name)
    , m_config(config)
    , m_memcached_config(memcached_config)
{
}

MemcachedStorage::~MemcachedStorage()
{
}

//static
bool MemcachedStorage::initialize(cache_storage_kind_t* pKind, uint32_t* pCapabilities)
{
    *pKind = CACHE_STORAGE_SHARED;
    *pCapabilities = (CACHE_STORAGE_CAP_ST | CACHE_STORAGE_CAP_MT);
    return true;
}

//static
void MemcachedStorage::finalize()
{
}

//static
MemcachedStorage* MemcachedStorage::create(const std::string& name,
                                           const Config& config,
                                           int argc,
                                           char* argv[])
{
    MemcachedStorage* pStorage = nullptr;

    if (config.thread_model != CACHE_THREAD_MODEL_ST)
    {
        MXS_ERROR("The storage storage_memcached only supports single-threaded use.");
    }
    else if (config.invalidate != CACHE_INVALIDATE_NEVER)
    {
        MXS_ERROR("The storage storage_memcached does not support invalidation.");
    }
    else
    {
        if (config.max_size != 0)
        {
            MXS_WARNING("The storage storage_memcached does not support specifying "
                        "a maximum size of the cache storage.");
        }

        if (config.max_count != 0)
        {
            MXS_WARNING("The storage storage_memcached does not support specifying "
                        "a maximum number of items in the cache storage.");
        }

        string memcached_config;

        for (int i = 0; i < argc; ++i)
        {
            memcached_config += argv[i];

            if (i < argc - 1)
            {
                memcached_config += " ";
            }
        }

        // Only for checking that the configuration is acceptable.
        memcached_st* pMemc = memcached(memcached_config.c_str(), memcached_config.size());

        if (pMemc)
        {
            pStorage = new (std::nothrow) MemcachedStorage(name, config, memcached_config);

            memcached_free(pMemc);
        }
        else
        {
            MXS_ERROR("Could not create memcached handle.");
        }
    }

    return pStorage;
}

bool MemcachedStorage::create_token(std::unique_ptr<Storage::Token>* psToken)
{
    return MemcachedToken::create(m_memcached_config, psToken);
}

void MemcachedStorage::get_config(Config* pConfig)
{
    *pConfig = m_config;
}

cache_result_t MemcachedStorage::get_info(uint32_t what, json_t** ppInfo) const
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_value(Storage::Token* pToken,
                                           const CACHE_KEY& key,
                                           uint32_t flags,
                                           uint32_t soft_ttl,
                                           uint32_t hard_ttl,
                                           GWBUF** ppValue,
                                           std::function<void (cache_result_t, GWBUF*)> cb)
{
    mxb_assert(pToken);

    return static_cast<MemcachedToken*>(pToken)->get_value(key, flags, soft_ttl, hard_ttl, ppValue, cb);
}

cache_result_t MemcachedStorage::put_value(Token* pToken,
                                           const CACHE_KEY& key,
                                           const std::vector<std::string>& invalidation_words,
                                           const GWBUF* pValue,
                                           std::function<void (cache_result_t)> cb)
{
    mxb_assert(pToken);

    return static_cast<MemcachedToken*>(pToken)->put_value(key, invalidation_words, pValue, cb);
}

cache_result_t MemcachedStorage::del_value(Token* pToken,
                                           const CACHE_KEY& key,
                                           std::function<void (cache_result_t)> cb)
{
    mxb_assert(pToken);

    return static_cast<MemcachedToken*>(pToken)->del_value(key, cb);
}

cache_result_t MemcachedStorage::invalidate(Token* pToken,
                                            const std::vector<std::string>& words)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::clear(Token* pToken)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_head(CACHE_KEY* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_tail(CACHE_KEY* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_size(uint64_t* pSize) const
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_items(uint64_t* pItems) const
{
    return CACHE_RESULT_ERROR;
}