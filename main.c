#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

#include "ftpd.h"

int main(int argc, char **argv)
{
    struct FtpServer *server = malloc(sizeof(struct FtpServer));

    if (argc >= 2)
    {
        server->_port = atoi(argv[1]);
    }
    else
    {
        server->_port = 21;
    }

    if (argc < 3)
    {
        strcpy(server->_relative_path, "/");
    }
    else
    {
        strcpy(server->_relative_path, argv[2]);
    }

    init_ftp_server(server);
    start_ftp_server(server);
    
    return 0;
}