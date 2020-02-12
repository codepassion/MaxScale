/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-02-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * The private poll header
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/poll.hh>
#include <maxscale/resultset.hh>

std::unique_ptr<ResultSet> eventTimesGetList();
