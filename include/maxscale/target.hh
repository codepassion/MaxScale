/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>

#include <string>

#include <maxscale/modinfo.hh>
#include <maxscale/buffer.hh>

struct MXS_SESSION;

constexpr int RANK_PRIMARY = 1;
constexpr int RANK_SECONDARY = 2;

// The enum values for `rank`
extern const MXS_ENUM_VALUE rank_values[];

// Default value for `rank`
extern const char* DEFAULT_RANK;

/**
 * Status bits in the status() method, which describes the general state of a target. Although the
 * individual bits are independent, not all combinations make sense or are used. The bitfield is 64bits wide.
 */
// TODO: Rename with a different prefix or something
// Bits used by most monitors
#define SERVER_RUNNING              (1 << 0)    /**<< The server is up and running */
#define SERVER_MAINT                (1 << 1)    /**<< Server is in maintenance mode */
#define SERVER_AUTH_ERROR           (1 << 2)    /**<< Authentication error from monitor */
#define SERVER_MASTER               (1 << 3)    /**<< The server is a master, i.e. can handle writes */
#define SERVER_SLAVE                (1 << 4)    /**<< The server is a slave, i.e. can handle reads */
#define SERVER_DRAINING             (1 << 5)    /**<< The server is being drained, i.e. no new connection
                                                 * should be created. */
#define SERVER_DISK_SPACE_EXHAUSTED (1 << 6)    /**<< The disk space of the server is exhausted */
// Bits used by MariaDB Monitor (mostly)
#define SERVER_SLAVE_OF_EXT_MASTER (1 << 16)    /**<< Server is slave of a non-monitored master */
#define SERVER_RELAY               (1 << 17)    /**<< Server is a relay */
#define SERVER_WAS_MASTER          (1 << 18)    /**<< Server was a master but lost all slaves. */
// Bits used by other monitors
#define SERVER_JOINED            (1 << 19)      /**<< The server is joined in a Galera cluster */
#define SERVER_MASTER_STICKINESS (1 << 20)      /**<< Server Master stickiness */

inline bool status_is_connectable(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_MAINT | SERVER_DRAINING)) == SERVER_RUNNING;
}

inline bool status_is_usable(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_MAINT)) == SERVER_RUNNING;
}

inline bool status_is_running(uint64_t status)
{
    return status & SERVER_RUNNING;
}

inline bool status_is_down(uint64_t status)
{
    return (status & SERVER_RUNNING) == 0;
}

inline bool status_is_in_maint(uint64_t status)
{
    return status & SERVER_MAINT;
}

inline bool status_is_draining(uint64_t status)
{
    return status & SERVER_DRAINING;
}

inline bool status_is_master(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_MASTER | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_MASTER);
}

inline bool status_is_slave(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_SLAVE | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_SLAVE);
}

inline bool status_is_relay(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_RELAY | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_RELAY);
}

inline bool status_is_joined(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_JOINED | SERVER_MAINT)) == (SERVER_RUNNING | SERVER_JOINED);
}

inline bool status_is_slave_of_ext_master(uint64_t status)
{
    return (status & (SERVER_RUNNING | SERVER_SLAVE_OF_EXT_MASTER))
           == (SERVER_RUNNING | SERVER_SLAVE_OF_EXT_MASTER);
}

inline bool status_is_disk_space_exhausted(uint64_t status)
{
    return status & SERVER_DISK_SPACE_EXHAUSTED;
}

namespace maxscale
{

class Target;

// A routing component
class Component
{
public:
    virtual ~Component() = default;

    virtual mxs::Target* target() const = 0;

    virtual int32_t routeQuery(GWBUF* buffer) = 0;

    virtual int32_t clientReply(GWBUF* buffer, Component* down) = 0;

    virtual bool handleError(GWBUF* error, Component* down) = 0;
};

// A connectable routing endpoint (a service or a server)
class Endpoint : public Component
{
public:
    virtual ~Endpoint() = default;

    virtual bool connect() = 0;

    virtual void close() = 0;

    virtual bool is_open() const = 0;
};

// A routing target
class Target
{
public:

    virtual ~Target() = default;

    /**
     * Get the target name
     *
     * The value is returned as a c-string for printing convenience.
     *
     * @return Target name
     */
    virtual const char* name() const = 0;

    /**
     * Get target status
     *
     * @return The status bitmask of the target
     */
    virtual uint64_t status() const = 0;

    /**
     * Is the target still active
     *
     * @return True if target is still active
     */
    virtual bool active() const = 0;

    /**
     * Get target rank
     */
    virtual int64_t rank() const = 0;

    /**
     * Get a connection handle to this target
     */
    // TODO: Implement this
    // virtual std::unique_ptr<Endpoint> get_connection(Component* up, MXS_SESSION* session) = 0;

    /* Target connection and usage statistics */
    struct Stats
    {
        // NOTE: Currently mutable as various parts of the system modify these when they should only be
        //       modified by the inherited objects.
        mutable int      n_connections = 0; /**< Number of connections */
        mutable int      n_current = 0;     /**< Current connections */
        mutable int      n_current_ops = 0; /**< Current active operations */
        mutable uint64_t packets = 0;       /**< Number of packets routed to this server */
    };

    /**
     * Current server status as a string
     *
     * @return A string representation of the status
     */
    std::string status_string() const
    {
        return status_to_string(status(), stats().n_connections);
    }

    // Converts status bits to strings
    static std::string status_to_string(uint64_t flags, int n_connections);

    /**
     * Get target statistics
     */
    const Stats& stats() const
    {
        return m_stats;
    }

    /**
     * Is the target running and can be connected to?
     *
     * @return True if the target can be connected to.
     */
    bool is_connectable() const
    {
        return status_is_connectable(status());
    }

    /**
     * Is the target running and not in maintenance?
     *
     * @return True if target can be used.
     */
    bool is_usable() const
    {
        return status_is_usable(status());
    }

    /**
     * Is the target running?
     *
     * @return True if the target is running
     */
    bool is_running() const
    {
        return status_is_running(status());
    }

    /**
     * Is the target down?
     *
     * @return True if monitor cannot connect to the target.
     */
    bool is_down() const
    {
        return status_is_down(status());
    }

    /**
     * Is the target in maintenance mode?
     *
     * @return True if target is in maintenance.
     */
    bool is_in_maint() const
    {
        return status_is_in_maint(status());
    }

    /**
     * Is the target being drained?
     *
     * @return True if target is being drained.
     */
    bool is_draining() const
    {
        return status_is_draining(status());
    }

    /**
     * Is the target a master?
     *
     * @return True if target is running and marked as master.
     */
    bool is_master() const
    {
        return status_is_master(status());
    }

    /**
     * Is the target a slave.
     *
     * @return True if target is running and marked as slave.
     */
    bool is_slave() const
    {
        return status_is_slave(status());
    }

    /**
     * Is the target a relay slave?
     *
     * @return True, if target is a running relay.
     */
    bool is_relay() const
    {
        return status_is_relay(status());
    }

    /**
     * Is the target joined Galera node?
     *
     * @return True, if target is running and joined.
     */
    bool is_joined() const
    {
        return status_is_joined(status());
    }

    bool is_in_cluster() const
    {
        return (status() & (SERVER_MASTER | SERVER_SLAVE | SERVER_RELAY | SERVER_JOINED)) != 0;
    }

    bool is_slave_of_ext_master() const
    {
        return status_is_slave_of_ext_master(status());
    }

    bool is_low_on_disk_space() const
    {
        return status_is_disk_space_exhausted(status());
    }

protected:
    Stats m_stats;
};
}