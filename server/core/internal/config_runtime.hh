/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file config_runtime.hh  - Functions for runtime configuration modifications
 */

#include <maxscale/ccdefs.hh>

#include <maxscale/adminusers.hh>
#include <maxscale/monitor.hh>
#include <maxscale/service.hh>

#include "service.hh"
#include "filter.hh"

class Server;

namespace maxscale
{
class RoutingWorker;
}

/**
 * @brief Log error to be returned to client
 *
 * This function logs an error message that later will be returned to
 * the client. Note that each call to this function will overwrite
 * an already logged error message.
 *
 * @param fmt  Printf format string.
 */
void config_runtime_error(const char* fmt, ...) mxs_attribute((format (printf, 1, 2)));

/**
 * @brief Create a new server
 *
 * This function creates a new, persistent server by first allocating a new
 * server and then storing the resulting configuration file on disk. This
 * function should be used only from administrative interface modules and internal
 * modules should use server_alloc() instead.
 *
 * @param name          Server name
 * @param address       Network address
 * @param port          Network port
 * @param external      If true, the name will be validated and the created server
 *                      serialized.
 * @return True on success, false if an error occurred
 */
bool runtime_create_server(const char* name, const char* address, const char* port, bool external = true);

/**
 * @brief Destroy a server
 *
 * This removes any created server configuration files and marks the server removed
 * If the server is not in use.
 *
 * @param server Server to destroy
 * @return True if server was destroyed
 */
bool runtime_destroy_server(Server* server);

/**
 * @brief Destroy a listener
 *
 * This disables the listener by removing it from the polling system. It also
 * removes any generated configurations for this listener.
 *
 * @param service Service where the listener exists
 * @param name Name of the listener
 *
 * @return True if the listener was successfully destroyed
 */
bool runtime_destroy_listener(Service* service, const char* name);

/**
 * Destroy a filter
 *
 * The filter can only be destroyed if no service uses it
 *
 * @param service Filter to destroy
 *
 * @return True if filter was destroyed
 */
bool runtime_destroy_filter(const SFilterDef& filter);

/**
 * @brief Destroy a monitor
 *
 * Monitors are not removed from the runtime configuration but they are stopped.
 * Destroyed monitor are removed after a restart.
 *
 * @param monitor Monitor to destroy
 * @return True if monitor was destroyed
 */
bool runtime_destroy_monitor(mxs::Monitor* monitor);

/**
 * Destroy a service
 *
 * The service can only be destroyed if it uses no servers and has no active listeners.
 *
 * @param service Service to destroy
 *
 * @return True if service was destroyed
 */
bool runtime_destroy_service(Service* service);

/**
 * @brief Create a new server from JSON
 *
 * @param json JSON defining the server
 *
 * @return True if server was created, false on error
 */
bool runtime_create_server_from_json(json_t* json);

/**
 * @brief Alter a server using JSON
 *
 * @param server Server to alter
 * @param new_json JSON definition of the updated server
 *
 * @return True if the server was successfully modified to represent @c new_json
 */
bool runtime_alter_server_from_json(Server* server, json_t* new_json);

/**
 * @brief Alter server relationships
 *
 * @param server Server to alter
 * @param type Type of the relation, either @c services or @c monitors
 * @param json JSON that defines the relationship data
 *
 * @return True if the relationships were successfully modified
 */
bool runtime_alter_server_relationships_from_json(Server* server, const char* type, json_t* json);

/**
 * @brief Create a new monitor from JSON
 *
 * @param json JSON defining the monitor
 *
 * @return True if the monitor was created, false on error
 */
bool runtime_create_monitor_from_json(json_t* json);

/**
 * @brief Create a new filter from JSON
 *
 * @param json JSON defining the filter
 *
 * @return True if the filter was created, false on error
 */
bool runtime_create_filter_from_json(json_t* json);

/**
 * @brief Create a new service from JSON
 *
 * @param json JSON defining the service
 *
 * @return True if the service was created, false on error
 */
bool runtime_create_service_from_json(json_t* json);

/**
 * @brief Alter a monitor using JSON
 *
 * @param monitor Monitor to alter
 * @param new_json JSON definition of the updated monitor
 *
 * @return True if the monitor was successfully modified to represent @c new_json
 */
bool runtime_alter_monitor_from_json(mxs::Monitor* monitor, json_t* new_json);

/**
 * @brief Alter monitor relationships
 *
 * @param monitor Monitor to alter
 * @param type    Relationship type
 * @param json    JSON that defines the new relationships
 *
 * @return True if the relationships were successfully modified
 */
bool runtime_alter_monitor_relationships_from_json(mxs::Monitor* monitor, const char* type, json_t* json);

/**
 * @brief Alter a service using JSON
 *
 * @param service Service to alter
 * @param new_json JSON definition of the updated service
 *
 * @return True if the service was successfully modified to represent @c new_json
 */
bool runtime_alter_service_from_json(Service* service, json_t* new_json);

/**
 * @brief Alter service relationships
 *
 * @param service Service to alter
 * @param type    Type of relationship to alter
 * @param json    JSON that defines the new relationships
 *
 * @return True if the relationships were successfully modified
 */
bool runtime_alter_service_relationships_from_json(Service* service, const char* type, json_t* json);

/**
 * @brief Create a listener from JSON
 *
 * @param service Service where the listener is created
 * @param json JSON definition of the new listener
 *
 * @return True if the listener was successfully created and started
 */
bool runtime_create_listener_from_json(Service* service, json_t* json);

/**
 * @brief Alter logging options using JSON
 *
 * @param json JSON definition of the updated logging options
 *
 * @return True if the modifications were successful
 */
bool runtime_alter_logs_from_json(json_t* json);

/**
 * @brief Get current runtime error in JSON format
 *
 * @return The latest runtime error in JSON format or NULL if no error has occurred
 */
json_t* runtime_get_json_error();

/**
 * @brief Create a new user account
 *
 * @param json JSON defining the user
 *
 * @return True if the user was successfully created
 */
bool runtime_create_user_from_json(json_t* json);

/**
 * @brief Remove admin user
 *
 * @param id   Username of the network user
 * @param type USER_TYPE_INET for network user and USER_TYPE_UNIX for enabled accounts
 *
 * @return True if user was successfully removed
 */
bool runtime_remove_user(const char* id, enum user_type type);

/**
 * @brief Alter admin user password
 *
 * @param user Username
 * @param type Type of the user
 * @param json JSON defining the new user
 *
 * @return True if the user was altered successfully
 */
bool runtime_alter_user(const std::string& user, const std::string& type, json_t* json);

/**
 * @brief Alter core MaxScale parameters from JSON
 *
 * @param new_json JSON defining the new core parameters
 *
 * @return True if the core parameters are valid and were successfully applied
 */
bool runtime_alter_maxscale_from_json(json_t* new_json);

/**
 * Rebalance work of particular thread.
 *
 * @param worker      The worker to be rebalanced.
 * @param sessions    The number of sessions to move.
 * @param recipient   The thread to move them to.
 *
 * @return True, if the rebalancing could be initiated.
 */
bool runtime_thread_rebalance(maxscale::RoutingWorker& worker,
                              const std::string& sessions,
                              const std::string& recipient);

/**
 * Rebalance work of threads.
 *
 * @param threashold  The rebalancing threshold.
 *
 * @return True, if the rebalancing could be initiated.
 */
bool runtime_threads_rebalance(const std::string& threshold);
