#include "ftpd.h"
#include "_string.h"
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include "_file.h"
#define TRUE 1

void init_ftp_server(struct FtpServer *ftp)
{
    ftp->_socket = socket(AF_INET, SOCK_STREAM, 0);
    int err, sock_reuse = 1;
    err = setsockopt(ftp->_socket, SOL_SOCKET, SO_REUSEADDR,
                     &sock_reuse, sizeof(sock_reuse));
    if (err != 0)
    {
        printf("套接字可重用设置失败！/n");
        exit(1);
    }
    if (ftp->_socket < 0)
    {
        perror("opening socket error");
        exit(1);
    }

    ftp->_server.sin_family = AF_INET;
    ftp->_server.sin_addr.s_addr = INADDR_ANY;
    ftp->_server.sin_port = htons(ftp->_port);
    if (bind(ftp->_socket, &ftp->_server, sizeof(struct sockaddr)) < 0)
    {
        perror("binding error");
        exit(1);
    }
    show_log("server is estabished. Waiting for connnect...\n");
}

void show_log(char *log)
{
    FILE *file = fopen("log", "a");
    fwrite(log, 1, strlen(log), file);
    fclose(file);
}

void start_ftp_server(struct FtpServer *ftp)
{
    char log[100];
    listen(ftp->_socket, 20);
    socklen_t size = sizeof(struct sockaddr);
    while (1)
    {
        int client;
        struct sockaddr_in client_addr;
        client = accept(ftp->_socket, &client_addr, &size);
        if (client < 0)
        {
            perror("accept error");
        }
        else
        {
            socklen_t sock_length = sizeof(struct sockaddr);
            char host_ip[100];
            char client_ip[100];
            // get host ip
            struct sockaddr_in host_addr;
            getsockname(client, &host_addr, &sock_length);
            inet_ntop(AF_INET, &(host_addr.sin_addr), host_ip, INET_ADDRSTRLEN);
            strcpy(ftp->_ip, host_ip);
            printf("host ip %s", host_ip);
            getpeername(client, (struct sockaddr *)&client_addr, &sock_length);
            inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip,
                      INET_ADDRSTRLEN);
            sprintf(log, "%s connect to the host %s.", client_ip, host_ip);
            show_log(log);
        }
        struct FtpClient *_c = (struct FtpClient *)malloc(sizeof(struct FtpClient));
        init_ftp_client(_c, ftp, client);
        pthread_t pid;
        pthread_create(&pid, NULL, communication, (_c));
    }
}

//initial FtpClient
void init_ftp_client(struct FtpClient *client, struct FtpServer *server, int client_socket)
{
    client->_client_socket = client_socket;
    strcpy(client->_ip, server->_ip);
    strcpy(client->_root, server->_relative_path);
    strcpy(client->_cur_path, "/");
    client->_type = 1;
    client->_name[0] = 0;
    client->_pass[0] = 0;
    client->status = 0;
    client->_data_server_socket = -1;
    client->_data_socket = -1;
    client->_name[0] = 0;
    client->_pass[0] = 0;
    client->_dataip[0] = 0;
}

//communication
void *communication(void *c)
{
    struct FtpClient *client = (struct FtpClient *)c;
    int client_socket = client->_client_socket;
    char str[] = "220 Anonymous FTP server ready.\r\n";
    send_msg(client_socket, str);
    handle_client_command(client);
    return NULL;
}

// handle command
void handle_client_command(struct FtpClient *client)
{
    int client_socket = client->_client_socket;

    char *buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE);
    char *cmd = NULL;
    char *argument = NULL;
    while (TRUE)
    {
        recv_msg(client_socket, buffer, &cmd, &argument);

        if (strcmp("OPTS", cmd) == 0)
        {
            handle_OPTS(client);
        }
        else if (strcmp("USER", cmd) == 0)
        {
            handle_USER(client, argument);
        }
        else if (strcmp("PASS", cmd) == 0)
        {
            handle_PASS(client, argument);
        }
        else if (strcmp("PORT", cmd) == 0)
        {
            handle_PORT(client, argument);
        }
        else if (strcmp("LIST", cmd) == 0 || strcmp("NLST", cmd) == 0)
        {
            handle_LIST(client);
        }
        else if (strcmp("PWD", cmd) == 0 || strcmp("XPWD", cmd) == 0)
        {
            handle_PWD(client);
        }
        else if (strcmp("CWD", cmd) == 0)
        {
            handle_CWD(client, argument);
        }
        else if (strcmp("RETR", cmd) == 0)
        {
            struct FtpRetr *retr = (struct FtpRetr *)malloc(sizeof(struct FtpRetr));
            retr->client = client;
            strcpy(retr->path, argument);
            pthread_t pid;
            pthread_create(&pid, NULL, handle_RETR, (void *)retr);
        }
        else if (strcmp("QUIT", cmd) == 0)
        {
            handle_QUIT(client);
            break;
        }
        else if (strcmp("STOR", cmd) == 0)
        {
            handle_STOR(client, argument);
        }
        else
        {
            send_msg(client->_client_socket, "500 NOT SUPPORT\r\n");
        }
    }
}

// receive message
void recv_msg(int socket, char *buf, char **cmd, char **argument)
{
    memset(buf, 0, sizeof(char) * BUFFER_SIZE);
    int n = recv(socket, buf, BUFFER_SIZE, 0);
    if (n == 0)
    {
        show_log("client leave the server.");
        pthread_exit(NULL);
    }

    int index = _find_first_of(buf, ' ');
    if (index < 0)
    {
        *cmd = _substring(buf, 0, strlen(buf) - 2);
    }
    else
    {
        *cmd = _substring(buf, 0, index);
        *argument = _substring(buf, index + 1, strlen(buf) - index - 3);
    }
    if (n < 0)
    {
        perror("recv msg error...");
    }
    else
    {
        show_log(buf);
    }
}

// send message
void send_msg(int socket, char *msg)
{
    int l = strlen(msg);
    if (l <= 0)
    {
        show_log("no message in char* msg");
    }

    int n = 0;
    while (n < l)
    {
        n += send(socket, msg + n, l, 0);
    }
    if (n < 0)
    {
        perror("send msg error...");
    }
    else
    {
        show_log(msg);
    }
}

//redefine write
void handle_USER(struct FtpClient *client, char *name)
{
    if (client->_name[0])
    {
        client->_name[0] = 0;
    }
    if (name != NULL)
    {
        strcpy(client->_name, name);
        send_msg(client->_client_socket,
                 "331 Guest login ok, send your complete e-mail address as password.\r\n");
    }
    else
    {
        send_msg(client->_client_socket, "530 You must input your name.\r\n");
    }
}

void handle_PASS(struct FtpClient *client, char *pass)
{
    if (client->_name[0] == 0)
    {
        send_msg(client->_client_socket,
                 "503 Your haven't input your username\r\n");
    }
    else
    {
        strcpy(client->_pass, pass);
        if (check_user_pass(client) < 0)
        {
            send_msg(client->_client_socket,
                     "530 username or pass is unacceptalbe\r\n");
            return;
        }
        int client_socket = client->_client_socket;
        /*send_msg(client_socket, "230-\r\n");
		 send_msg(client_socket, "230-Welcome to\r\n");
		 send_msg(client_socket, "230- School of Software\r\n");
		 send_msg(client_socket, "230-\r\n");
		 send_msg(client_socket, "230-This site is provided as a public service by School of\r\n");
		 send_msg(client_socket, "230-Software. Use in violation of any applicable laws is strictly\r\n");
		 send_msg(client_socket, "230-prohibited. We make no guarantees, explicit or implicit, about the\r\n");
		 send_msg(client_socket, "230-contents of this site. Use at your own risk.\r\n");
		 send_msg(client_socket, "230-\r\n");*/
        send_msg(client_socket,
                 "230 Guest login ok, access restrictions apply.\r\n");
    }
}

void handle_OPTS(struct FtpClient *client)
{
    send_msg(client->_client_socket, "200 UTF8 OPTS ON\r\n");
}

void handle_PORT(struct FtpClient *client, char *str)
{
    if (client->_data_socket > 0)
    {
        close(client->_data_socket);
    }
    client->_dataip[0] = 0;
    int a, b, c, d, e, f;
    sscanf(str, "%d,%d,%d,%d,%d,%d", &a, &b, &c, &d, &e, &f);
    sprintf(client->_dataip, "%d.%d.%d.%d", a, b, c, d);
    client->_dataport = e * 256 + f;
    show_log(client->_dataip);
    //show_log(client->_dataport);

    char connect[] = "200 PORT command successful.\r\n";
    send_msg(client->_client_socket, connect);
}

void handle_LIST(struct FtpClient *client)
{
    FILE *pipe_fp = NULL;
    char list_cmd_info[200];
    char path[200];
    strcpy(path, client->_root);
    strcat(path, client->_cur_path);
    sprintf(list_cmd_info, "ls -l %s", path);

    if ((pipe_fp = popen(list_cmd_info, "r")) == NULL)
    {
        send_msg(client->_client_socket,
                 "451 popen ls error\r\n");
        return;
    }

    if (establish_tcp_connection(client))
    {
        show_log("establish tcp socket");
    }
    else
    {
        send_msg(client->_client_socket,
                 "425 TCP connection cannot be established.\r\n");
    }
    send_msg(client->_client_socket,
             "150 Data connection accepted; transfer starting.\r\n");

    char buf[BUFFER_SIZE * 10];

    fread(buf, sizeof(char), BUFFER_SIZE * 10 - 1, pipe_fp);

    int l = _find_first_of(buf, '\n');
    send_msg(client->_data_socket, &(buf[l + 1]));

    pclose(pipe_fp);
    cancel_tcp_connection(client);
    send_msg(client->_client_socket, "226 Transfer ok.\r\n");
}

int check_user_pass(struct FtpClient *client)
{
    if (client->_name == NULL)
    {
        return -1;
    }
    else if (strcmp("anonymous", client->_name) == 0)
    {
        return 1;
    }
    return 0;
}

void handle_PWD(struct FtpClient *client)
{
    char buf[300];
    strcpy(buf, "200 \"");
    strcat(buf, client->_cur_path);
    strcat(buf, "\"\r\n");
    send_msg(client->_client_socket, buf);
}

void handle_CWD(struct FtpClient *client, char *_dir)
{
    int flag = 0;
    if (_dir[0] != '/')
    {
        flag = 1;
    }
    char dir[300];
    strcpy(dir, client->_root);
    if (flag)
    {
        strcat(dir, "/");
    }
    show_log("cwd:start");
    show_log(_dir);
    show_log("cwd:end");
    show_log(dir);
    strcat(dir, _dir);
    show_log(dir);
    if (is_exist_dir(dir))
    {
        show_log(dir);
        if (flag)
        {
            strcpy(client->_cur_path, "/");
            strcat(client->_cur_path, _dir);
        }
        else
        {
            memset(client->_cur_path, 0, 100);
            strcpy(client->_cur_path, _dir);
            show_log(client->_cur_path);
        }
        send_msg(client->_client_socket, "250 Okay.\r\n");
    }
    else
    {
        send_msg(client->_client_socket, "550 No such file or directory.\r\n");
    }
}

int establish_tcp_connection(struct FtpClient *client)
{
    if (client->_dataip[0])
    {
        // 只考虑port模式，不考虑pasv模式
        client->_data_socket = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in servaddr;
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(client->_dataport);
        if (inet_aton(client->_dataip, &(servaddr.sin_addr)) <= 0)
        { // 小端字节序转大端字节序，也可以叫做系统字节序转网络字节序
            printf("error in port command");
            return -1;
        }

        if (connect(client->_data_socket, &servaddr, sizeof(struct sockaddr)) == -1)
        {
            perror("connect出错");
            return -1;
        }

        show_log("port connect success\r\n");
    }

    return 1;
}

void cancel_tcp_connection(struct FtpClient *client)
{

    if (client->_data_server_socket > 0)
    {
        close(client->_data_server_socket);
        client->_data_server_socket = -1;
    }
    if (client->_data_socket > 0)
    {
        close(client->_data_socket);
        client->_data_socket = -1;
    }
    if (client->_dataip[0])
    {
        client->_dataip[0] = 0;
        client->_dataport = 0;
    }
}

void *handle_RETR(void *retr)
{
    struct FtpRetr *re = (struct FtpRetr *)retr;
    struct FtpClient *client = re->client;
    char path[200];
    strcpy(path, re->path);
    //establish_tcp_connection(client);
    FILE *file = NULL;
    char _path[400];
    strcpy(_path, client->_root);
    strcat(_path, client->_cur_path);
    if (_path[strlen(_path) - 1] != '/')
    {
        strcat(_path, "/");
    }
    strcat(_path, path);

    file = fopen(_path, "rb");

    if (file == NULL)
    {
        send_msg(client->_client_socket, "451 trouble to retr file\r\n");
        return NULL;
    }

    if (establish_tcp_connection(client) > 0)
    {
        send_msg(client->_client_socket,
                 "150 Data connection accepted; transfer starting.\r\n");
        char buf[BUFFER_SIZE];
        while (!feof(file))
        {
            int n = fread(buf, sizeof(char), BUFFER_SIZE, file);
            int j = 0;
            while (j < n)
            {
                j += send(client->_data_socket, buf + j, n - j, 0);
            }
        }

        fclose(file);
        cancel_tcp_connection(client);

        send_msg(client->_client_socket, "226 Transfer ok.\r\n");
    }
    else
    {
        send_msg(client->_client_socket,
                 "425 TCP connection cannot be established.\r\n");
    }
    pthread_exit(NULL);
    return NULL;
}

void handle_QUIT(struct FtpClient *client)
{
    send_msg(client->_client_socket, "221 goodby~\r\n");
}

void handle_STOR(struct FtpClient *client, char *path)
{
    FILE *file = NULL;
    char _path[400];
    strcpy(_path, client->_root);
    strcat(_path, client->_cur_path);
    if (_path[strlen(_path) - 1] != '/')
    {
        strcat(_path, "/");
    }
    strcat(_path, path);

    file = fopen(_path, "wb");

    if (file == NULL)
    {
        send_msg(client->_client_socket, "451 trouble to stor file\r\n");
        return;
    }

    if (establish_tcp_connection(client) > 0)
    {
        send_msg(client->_client_socket,
                 "150 Data connection accepted; transfer starting.\r\n");
        char buf[BUFFER_SIZE];
        int j = 0;
        while (1)
        {
            j = recv(client->_data_socket, buf, BUFFER_SIZE, 0);
            if (j == 0)
            {
                cancel_tcp_connection(client);
                break;
            }

            if (j < 0)
            {
                send_msg(client->_client_socket, "426 TCP connection was established but then broken\r\n");
                return;
            }

            fwrite(buf, sizeof(char), j, file);
        }

        cancel_tcp_connection(client);
        fclose(file);

        send_msg(client->_client_socket, "226 stor ok.\r\n");
    }
    else
    {
        send_msg(client->_client_socket,
                 "425 TCP connection cannot be established.\r\n");
    }
}
