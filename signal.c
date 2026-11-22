#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#define PORT 12345
#define BACKLOG 5
#define BUF_SIZE 4096

static volatile sig_atomic_t was_sighup = 0;

static void sighup_handler(int signal_number){
    (void)signal_number;
    was_sighup = 1;
}

static int set_nonblock(int fd){
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_fd(int *fd){
    if (*fd >= 0){
        close(*fd);
        *fd = -1;
    }
}

int main (void){
    int server_fd = -1;
    int client_fd = -1;
    struct sockaddr_in address;
    sigset_t block_mask, old_mask;
    struct sigaction sa;


    server_fd = socket(AF_INET, SOCK_STREAM, 0); 
    if (server_fd == -1){
        perror("socket");
        return EXIT_FAILURE;
    }

    
    int yes = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1){
        perror("setsockport");
        return EXIT_FAILURE;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT); //устанавливается порт (с каким портом будет ассоцировано)
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1){
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }


    if (listen(server_fd, BACKLOG) == -1){
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (set_nonblock(server_fd) == -1){
        perror("set_nonblock");
        close(server_fd);
        return EXIT_FAILURE; 
    }


    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sighup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGHUP, &sa, NULL) == -1){
        perror("sigaction");
        close(server_fd);
        return EXIT_FAILURE; 
    }


    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &block_mask, &old_mask) == -1){
        perror("sigprocmask");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Server listening on port %d (pid %d)\n", PORT, getpid());
    printf("Test signal: kill -HUP %d\n", getpid());
    fflush(stdout);


    for(;;){
        fd_set read_fds;
        int max_fd;
        int ready;

        if(was_sighup){
            was_sighup = 0;
            printf("Received SIGHUP\n");
            fflush(stdout);
        }

        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        max_fd = server_fd;

        if (client_fd >= 0){
            FD_SET(client_fd, &read_fds);
            if (client_fd > max_fd) max_fd = client_fd;
        }

        ready = pselect(max_fd + 1, &read_fds, NULL, NULL, NULL, &old_mask);

        if (ready == -1){
            if (errno == EINTR) continue;
            perror ("pselect");
            break;
        }

        if(was_sighup){
            was_sighup = 0;
            printf("Received SIGHUP\n");
            fflush(stdout);
        }


        if (FD_ISSET(server_fd, &read_fds)){
            int new_fd = accept(server_fd, NULL, NULL);

            if (new_fd != -1){
                printf("New connection (fd=%d)\n", new_fd);
                fflush(stdout);

                if (client_fd != -1){
                    printf("Already have a client, closing new connection\n");
                    fflush(stdout);
                    close(new_fd);
                } else {
                    if (set_nonblock(new_fd) == -1){
                        perror("set_nonblock(client)");
                        close(new_fd);
                    } else client_fd = new_fd;
                }
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) perror("accept");
        }


        if (client_fd != -1 && FD_ISSET(client_fd, &read_fds)){
            char buf[BUF_SIZE];
            ssize_t n = read(client_fd, buf, sizeof(buf));

            if (n > 0){
                printf("Received %zd bytes\n", n);
                fflush(stdout);
            } else if (n == 0){
                printf("Client disconnected\n");
                fflush(stdout);
                close_fd(&client_fd);
            } else{
                if (errno != EAGAIN && errno != EWOULDBLOCK){
                    perror("read");
                    close_fd(&client_fd);
                }
            }
        }

    }

    close_fd(&client_fd);
    close_fd(&server_fd);
    return 0;
}