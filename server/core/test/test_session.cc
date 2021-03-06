/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#if !defined (SS_DEBUG)
#define SS_DEBUG
#endif
#if defined (NDEBUG)
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <maxscale/poll.hh>
#include <dcb.h>

/**
 * test1    Allocate a service and do lots of other things
 *
 */

static int test1()
{
    DCB* dcb;
    int result;

    /* Poll tests */
    fprintf(stderr,
            "testpoll : Initialise the polling system.");
    poll_init();
    fprintf(stderr, "\t..done\nAdd a DCB");
    dcb = dcb_alloc(DCB_ROLE_SERVICE_LISTENER);
    dcb->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    poll_add_dcb(dcb);
    poll_remove_dcb(dcb);
    poll_add_dcb(dcb);
    fprintf(stderr, "\t..done\nStart wait for events.");
    sleep(10);
    poll_shutdown();
    fprintf(stderr, "\t..done\nTidy up.");
    dcb_close(dcb);
    fprintf(stderr, "\t..done\n");

    return 0;
}

int main(int argc, char** argv)
{
    int result = 0;

    result += test1();

    exit(result);
}
