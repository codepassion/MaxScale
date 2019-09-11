/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/cdc/module_names.hh>
#define MXS_MODULE_NAME MXS_CDCPLAINAUTH_AUTHENTICATOR_NAME

#include <maxscale/authenticator2.hh>

#include <fcntl.h>
#include <maxbase/alloc.h>
#include <maxscale/protocol/cdc/cdc.hh>
#include <maxscale/event.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/paths.h>
#include <maxscale/secrets.h>
#include <maxscale/users.hh>
#include <maxscale/utils.h>

/* Allowed time interval (in seconds) after last update*/
#define CDC_USERS_REFRESH_TIME 30
/* Max number of load calls within the time interval */
#define CDC_USERS_REFRESH_MAX_PER_TIME 4

const char CDC_USERS_FILENAME[] = "cdcusers";

static int cdc_set_service_user(Listener* listener);
static int cdc_replace_users(Listener* listener);

static int cdc_auth_check(DCB* dcb, char* username, uint8_t* auth_data);

using mxs::USER_ACCOUNT_ADMIN;

class CDCAuthenticatorModule : public mxs::AuthenticatorModule
{
public:
    static CDCAuthenticatorModule* create(char** options)
    {
        return new(std::nothrow) CDCAuthenticatorModule();
    }

    ~CDCAuthenticatorModule() override = default;

    std::unique_ptr<mxs::ClientAuthenticator> create_client_authenticator() override;

    int load_users(Listener* listener) override
    {
        return cdc_replace_users(listener);
    }

    void diagnostics(DCB* output, Listener* listener) override
    {
        users_default_diagnostic(output, listener);
    }

    json_t* diagnostics_json(const Listener* listener) override
    {
        return users_default_diagnostic_json(listener);
    }

    std::string supported_protocol() const override
    {
        return MXS_CDC_PROTOCOL_NAME;
    }
};

class CDCClientAuthenticator : public mxs::ClientAuthenticatorT<CDCAuthenticatorModule>
{
public:
    CDCClientAuthenticator(CDCAuthenticatorModule* module)
        : ClientAuthenticatorT(module)
    {
    }

    ~CDCClientAuthenticator() override = default;
    bool extract(DCB* client, GWBUF* buffer) override;

    bool ssl_capable(DCB* client) override
    {
        return false;
    }

    int authenticate(DCB* client) override;

    void free_data(DCB* client) override
    {
    }

private:
    bool set_client_data(uint8_t* client_auth_packet, int client_auth_packet_size);

    char    m_user[CDC_USER_MAXLEN + 1] {'\0'}; /*< username for authentication */
    uint8_t m_auth_data[SHA_DIGEST_LENGTH] {0}; /*< Password Hash               */
};

std::unique_ptr<mxs::ClientAuthenticator> CDCAuthenticatorModule::create_client_authenticator()
{
    return std::unique_ptr<mxs::ClientAuthenticator>(new(std::nothrow) CDCClientAuthenticator(this));
}

/**
 * @brief Add a new CDC user
 *
 * This function should not be called directly. The module command system will
 * call it when necessary.
 *
 * @param args Arguments for this command
 * @return True if user was successfully added
 */
static bool cdc_add_new_user(const MODULECMD_ARG* args, json_t** output)
{
    const char* user = args->argv[1].value.string;
    size_t userlen = strlen(user);
    const char* password = args->argv[2].value.string;
    uint8_t phase1[SHA_DIGEST_LENGTH];
    uint8_t phase2[SHA_DIGEST_LENGTH];
    SHA1((uint8_t*)password, strlen(password), phase1);
    SHA1(phase1, sizeof(phase1), phase2);

    size_t data_size = userlen + 2 + SHA_DIGEST_LENGTH * 2;     // Extra for the : and newline
    char final_data[data_size];
    strcpy(final_data, user);
    strcat(final_data, ":");
    gw_bin2hex(final_data + userlen + 1, phase2, sizeof(phase2));
    final_data[data_size - 1] = '\n';

    SERVICE* service = args->argv[0].value.service;
    char path[PATH_MAX + 1];
    snprintf(path, PATH_MAX, "%s/%s/", get_datadir(), service->name());
    bool rval = false;

    if (mxs_mkdir_all(path, 0777))
    {
        strcat(path, CDC_USERS_FILENAME);
        int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0660);

        if (fd != -1)
        {
            if (write(fd, final_data, sizeof(final_data)) == static_cast<int>(sizeof(final_data)))
            {
                MXS_NOTICE("Added user '%s' to service '%s'", user, service->name());
                rval = true;
            }
            else
            {
                const char* real_err = mxs_strerror(errno);
                MXS_NOTICE("Failed to write to file '%s': %s", path, real_err);
                modulecmd_set_error("Failed to write to file '%s': %s", path, real_err);
            }

            close(fd);
        }
        else
        {
            const char* real_err = mxs_strerror(errno);
            MXS_NOTICE("Failed to open file '%s': %s", path, real_err);
            modulecmd_set_error("Failed to open file '%s': %s", path, real_err);
        }
    }
    else
    {
        modulecmd_set_error("Failed to create directory '%s'. Read the MaxScale "
                            "log for more details.",
                            path);
    }

    return rval;
}

extern "C"
{
/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static modulecmd_arg_type_t args[] =
    {
        {MODULECMD_ARG_SERVICE, "Service where the user is added"},
        {MODULECMD_ARG_STRING,  "User to add"                    },
        {MODULECMD_ARG_STRING,  "Password of the user"           }
    };

    modulecmd_register_command("cdc",
                               "add_user",
                               MODULECMD_TYPE_ACTIVE,
                               cdc_add_new_user,
                               3,
                               args,
                               "Add a new CDC user");

    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_GA,
        MXS_AUTHENTICATOR_VERSION,
        "The CDC client to MaxScale authenticator implementation",
        "V1.1.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::AuthenticatorApi<CDCAuthenticatorModule>::s_api,
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}

/**
 * @brief Function to easily call authentication check.
 *
 * @param dcb Request handler DCB connected to the client
 * @param username  String containing username
 * @param auth_data  The encrypted password for authentication
 * @return Authentication status
 * @note Authentication status codes are defined in cdc.h
 */
static int cdc_auth_check(DCB* dcb, char* username, uint8_t* auth_data)
{
    int rval = CDC_STATE_AUTH_FAILED;

    if (dcb->session()->listener->users())
    {
        /* compute SHA1 of auth_data */
        uint8_t sha1_step1[SHA_DIGEST_LENGTH] = "";
        char hex_step1[2 * SHA_DIGEST_LENGTH + 1] = "";

        gw_sha1_str(auth_data, SHA_DIGEST_LENGTH, sha1_step1);
        gw_bin2hex(hex_step1, sha1_step1, SHA_DIGEST_LENGTH);

        if (users_auth(dcb->session()->listener->users(), username, hex_step1))
        {
            rval = CDC_STATE_AUTH_OK;
        }
    }

    return rval;
}

/**
 * @brief Authenticates a CDC user who is a client to MaxScale.
 *
 * @param generic_dcb Request handler DCB connected to the client
 * @return Authentication status
 * @note Authentication status codes are defined in cdc.h
 */
int CDCClientAuthenticator::authenticate(DCB* generic_dcb)
{
    mxb_assert(generic_dcb->role() == DCB::Role::CLIENT);
    auto dcb = static_cast<ClientDCB*>(generic_dcb);

    int auth_ret;

    if (0 == strlen(m_user))
    {
        auth_ret = CDC_STATE_AUTH_ERR;
    }
    else
    {
        MXS_DEBUG("Receiving connection from '%s'", m_user);

        auth_ret = cdc_auth_check(dcb, m_user, m_auth_data);

        /* On failed authentication try to reload users and authenticate again */
        if (CDC_STATE_AUTH_OK != auth_ret
            && cdc_replace_users(dcb->session()->listener.get()) == MXS_AUTH_LOADUSERS_OK)
        {
            auth_ret = cdc_auth_check(dcb, m_user, m_auth_data);
        }

        /* on successful authentication, set user into dcb field */
        if (CDC_STATE_AUTH_OK == auth_ret)
        {
            dcb->m_user = MXS_STRDUP_A(m_user);
            MXS_INFO("%s: Client [%s] authenticated with user [%s]",
                     dcb->service()->name(),
                     dcb->m_remote != NULL ? dcb->m_remote : "",
                     m_user);
        }
        else if (dcb->service()->config().log_auth_warnings)
        {
            MXS_LOG_EVENT(maxscale::event::AUTHENTICATION_FAILURE,
                          "%s: login attempt for user '%s' from [%s], authentication failed.",
                          dcb->service()->name(),
                          m_user,
                          dcb->m_remote != NULL ? dcb->m_remote : "");
        }
    }

    return auth_ret;
}

/**
 * @brief Transfer data from the authentication request to the DCB.
 *
 * The request handler DCB has a field called data that contains protocol
 * specific information. This function examines a buffer containing CDC
 * authentication data and puts it into a structure that is referred to
 * by the DCB. If the information in the buffer is invalid, then a failure
 * code is returned. A call to cdc_auth_set_client_data does the
 * detailed work.
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Pointer to pointer to buffer containing data from client
 * @return True on success, false on error
 */
bool CDCClientAuthenticator::extract(DCB* generic_dcb, GWBUF* buf)
{
    mxb_assert(generic_dcb->role() == DCB::Role::CLIENT);
    uint8_t* client_auth_packet = GWBUF_DATA(buf);
    int client_auth_packet_size = gwbuf_length(buf);
    return set_client_data(client_auth_packet, client_auth_packet_size);
}

/**
 * @brief Transfer detailed data from the authentication request to the DCB.
 *
 * The caller has created the data structure pointed to by the DCB, and this
 * function fills in the details. If problems are found with the data, the
 * return code indicates failure.
 *
 * @param client_auth_packet The data from the buffer received from client
 * @param client_auth_packet size An integer giving the size of the data
 * @return True on success, false on error
 */
bool CDCClientAuthenticator::set_client_data(uint8_t* client_auth_packet, int client_auth_packet_size)
{
    if (client_auth_packet_size % 2 != 0)
    {
        /** gw_hex2bin expects an even number of bytes */
        client_auth_packet_size--;
    }

    bool rval = false;
    int decoded_size = client_auth_packet_size / 2;
    char decoded_buffer[decoded_size + 1];      // Extra for terminating null

    /* decode input data */
    if (client_auth_packet_size <= CDC_USER_MAXLEN)
    {
        gw_hex2bin((uint8_t*)decoded_buffer, (const char*)client_auth_packet, client_auth_packet_size);
        decoded_buffer[decoded_size] = '\0';
        char* tmp_ptr = strchr(decoded_buffer, ':');

        if (tmp_ptr)
        {
            size_t user_len = tmp_ptr - decoded_buffer;
            *tmp_ptr++ = '\0';
            size_t auth_len = decoded_size - (tmp_ptr - decoded_buffer);

            if (user_len <= CDC_USER_MAXLEN && auth_len == SHA_DIGEST_LENGTH)
            {
                strcpy(m_user, decoded_buffer);
                memcpy(m_auth_data, tmp_ptr, auth_len);
                rval = true;
            }
        }
        else
        {
            MXS_ERROR("Authentication failed, the decoded client authentication "
                      "packet is malformed. Expected <username>:SHA1(<password>)");
        }
    }
    else
    {
        MXS_ERROR("Authentication failed, client authentication packet length "
                  "exceeds the maximum allowed length of %d bytes.",
                  CDC_USER_MAXLEN);
    }

    return rval;
}

/*
 * Add the service user to CDC dbusers (service->users)
 * via cdc_users_alloc
 *
 * @param service   The current service
 * @return      0 on success, 1 on failure
 */
static int cdc_set_service_user(Listener* listener)
{
    SERVICE* service = listener->service();
    char* dpwd = NULL;
    char* newpasswd = NULL;
    const char* service_user = NULL;
    const char* service_passwd = NULL;

    serviceGetUser(service, &service_user, &service_passwd);
    dpwd = decrypt_password(service_passwd);

    if (!dpwd)
    {
        MXS_ERROR("decrypt password failed for service user %s, service %s",
                  service_user,
                  service->name());

        return 1;
    }

    newpasswd = create_hex_sha1_sha1_passwd(dpwd);

    if (!newpasswd)
    {
        MXS_ERROR("create hex_sha1_sha1_password failed for service user %s",
                  service_user);

        MXS_FREE(dpwd);
        return 1;
    }

    /* add service user */
    const char* user;
    const char* password;
    serviceGetUser(service, &user, &password);
    users_add(listener->users(), user, newpasswd, USER_ACCOUNT_ADMIN);

    MXS_FREE(newpasswd);
    MXS_FREE(dpwd);

    return 0;
}

/**
 * Load the AVRO users
 *
 * @param service    Current service
 * @param usersfile  File with users
 * @return -1 on error or users loaded (including 0)
 */

static int cdc_read_users(USERS* users, char* usersfile)
{
    FILE* fp;
    int loaded = 0;
    char* avro_user;
    char* user_passwd;
    /* user maxlen ':' password hash  '\n' '\0' */
    char read_buffer[CDC_USER_MAXLEN + 1 + SHA_DIGEST_LENGTH + 1 + 1];

    int max_line_size = sizeof(read_buffer) - 1;

    if ((fp = fopen(usersfile, "r")) == NULL)
    {
        return -1;
    }

    while (!feof(fp))
    {
        if (fgets(read_buffer, max_line_size, fp) != NULL)
        {
            char* tmp_ptr = read_buffer;

            if ((tmp_ptr = strchr(read_buffer, ':')) != NULL)
            {
                *tmp_ptr++ = '\0';
                avro_user = read_buffer;
                user_passwd = tmp_ptr;
                if ((tmp_ptr = strchr(user_passwd, '\n')) != NULL)
                {
                    *tmp_ptr = '\0';
                }

                /* add user */
                users_add(users, avro_user, user_passwd, USER_ACCOUNT_ADMIN);

                loaded++;
            }
        }
    }

    fclose(fp);

    return loaded;
}

/**
 * @brief Replace the user/passwd in the servicei users tbale from a db file
 *
 * @param service The current service
 */
int cdc_replace_users(Listener* listener)
{
    int rc = MXS_AUTH_LOADUSERS_ERROR;
    USERS* newusers = users_alloc();

    if (newusers)
    {
        char path[PATH_MAX + 1];
        snprintf(path,
                 PATH_MAX,
                 "%s/%s/%s",
                 get_datadir(),
                 listener->service()->name(),
                 CDC_USERS_FILENAME);

        int i = cdc_read_users(newusers, path);
        USERS* oldusers = NULL;

        if (i > 0)
        {
            /** Successfully loaded at least one user */
            oldusers = listener->users();
            listener->set_users(newusers);
            rc = MXS_AUTH_LOADUSERS_OK;
        }
        else if (listener->users())
        {
            /** Failed to load users, use the old users table */
            users_free(newusers);
        }
        else
        {
            /** No existing users, use the new empty users table */
            listener->set_users(newusers);
        }

        cdc_set_service_user(listener);

        if (oldusers)
        {
            users_free(oldusers);
        }
    }
    return rc;
}
