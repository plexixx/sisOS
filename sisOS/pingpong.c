#include "types.h"
#include "user.h"

int main() {
    int parent_fd[2], child_fd[2];
    char buf[16];
    pipe(parent_fd); pipe(child_fd);
    if (fork() == 0) {
    // Child
        read(parent_fd[0], buf, 4);
        buf[4] = '\0';
        printf(1, "%d: received %s\n", getpid(), buf);
        write(child_fd[1], "pong", 4);
    } else {
    // Parent
        write(parent_fd[1], "ping", 4);
        read(child_fd[0], buf, 4);
        buf[4] = '\0';
        printf(1, "%d: received %s\n", getpid(), buf);
    }
    exit();
}