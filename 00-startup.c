#include "libmemc.h"
#include "libmemctest.h"

int main(int argc, char **argv)
{
    test_init(argc, argv);

    // Test 1: start the server
    ok_test(new_memcached(0, "") != NULL, "started the server",
            "failed to start the server");
    
    // Test 2: start the server with illegal arguments
    ok_test(!new_memcached(0, "-l fooble"), "died with illegal -l args",
            "started with illegal -l args");

    // Test 3: start the server
    ok_test(new_memcached(0, "-l 127.0.0.1") != NULL, "-l 127.0.0.1 works",
            "-l 127.0.0.1 not working");

    test_report();
}
