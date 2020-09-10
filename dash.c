#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>
#include <sys/wait.h>

void
err() {
    write(STDERR_FILENO, "An error has occurred\n", 22);
}

int pathc = 0;
char* paths[64];

void
addPath(char *path) {
    if (paths[pathc] != NULL)
        free (paths[pathc]);

    paths[pathc] = malloc(strlen(path) + 1);
    if (paths[pathc] == NULL) {
        err();
        return;
    }
    strcpy(paths[pathc], path);

    pathc++;
    // printf("paths[0]=%s, paths[1]=%s count=%d\n", paths[0], paths[1], pathc);
}

char
*trimWhiteSpace(char *str) {
  char *end;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if (*str == 0)
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;

  end[1] = '\0';

  return str;
}

struct Cmd {
    uint argc;
    char *argv[64];
    char redir[64]; // File to redirect output to, if any
    pid_t pid;
    int status;
};


void
execCmd(struct Cmd *cmd) { 
    if (strcmp(cmd->argv[0], "exit") == 0) {
        if (cmd->argc == 1) {
            exit(0);
        } else {
            return err();
        }
    } else if (strcmp(cmd->argv[0], "cd") == 0) {
        if (cmd->argc == 2) {
            if (chdir(cmd->argv[1]) == 0) { // chdir success
                //printf("cd success\n");
            } else { // chdir failed
                return err();
            }
        } else {
           return err();
        }
    } else if (strcmp(cmd->argv[0], "path") == 0) {
        pathc = 0;
        while (pathc + 1 < cmd->argc) {
            // printf("****pathc=%d, argv[pathc+1]=%s\n", pathc, cmd->argv[pathc+1]);
            addPath(cmd->argv[pathc + 1]);
        }
    } else {
        char *foundPath = NULL;
        int pathIdx = 0;
        for (pathIdx = 0; pathIdx < pathc; pathIdx++) { // Search in paths
            // printf("paths[pathIdx]=%s\n", paths[pathIdx]);


            char *checkPath = malloc(strlen(paths[pathIdx]) + strlen(cmd->argv[0]) + 2);
            if (checkPath == NULL) {
                err();
            }
            checkPath[0] = '\0';

            strcat(checkPath, paths[pathIdx]);
            strcat(checkPath, "/");
            strcat(checkPath, cmd->argv[0]);

            if (access(checkPath, X_OK) == 0) {
                foundPath = checkPath;
                
                // printf("foundPath=%s\n", foundPath);
                break;
            }
            free(checkPath);
        }
        
        if (foundPath == NULL) {
            printf("dash: %s: command not found...\n", cmd->argv[0]);
            return;
        }

        pid_t pid = fork();
        if (pid == -1) {
            return err();
        } else if (pid == 0) { // IN CHILD
            // printf("cmd pid = %d in child\n", pid);
            // printf("execvp> argc=%d, argv[0]=%s, argv[1]=%s, pid=%d, cmd=%p, foundPath=%s\n", cmd->argc, cmd->argv[0], cmd->argv[1], cmd->pid, cmd, foundPath);

            if (strcmp(cmd->redir, "")) {
                // printf("FREOPEN '%s'\n", cmd->redir);
                if (freopen(cmd->redir, "w+", stdout) == NULL) {
                    err();
                    exit(1); // Kill child proc
                }
            }

            execvp(foundPath, cmd->argv);
            exit(0);
        } else { // IN PARENT
            // printf("cmd pid = %d in parent\n", pid);
            free(foundPath);
            cmd->pid = pid;
            // printf("cmdA> argc=%d, argv[0]=%s, argv[1]=%s, pid=%d, cmd=%p\n", cmd->argc, cmd->argv[0], cmd->argv[1], cmd->pid, cmd);
        }
    }
}

void
processLine(char* line) {
    struct Cmd cmds[64];

    int c = 0; // cmd idx
    char *cmdStr = NULL;
    char *cmdsaveptr = NULL;
    char *argsaveptr = NULL;

    do {
        if (!c) {
            cmdStr = strtok_r(line, "&\r\n", &cmdsaveptr); // Grab first cmd token
        } else {
            cmdStr = strtok_r(NULL, "&\r\n", &cmdsaveptr); // Grab next cmd tokens
        }

        if (cmdStr) {
            memset(cmds[c].redir, '\0', sizeof(cmds[c].redir) );

            char *redirPtr = strstr(cmdStr, ">");
            if (redirPtr != NULL) {
                strcpy(cmds[c].redir, trimWhiteSpace(redirPtr+1));
                redirPtr[0] = '\0'; // Cut redir part off of cmdStr
                // printf("redir=%s\n", cmds[c].redir);
            }

            // printf("cmdStr (%d) = '%s'\n", c, cmdStr);
            cmds[c].argc = 0;
            cmds[c].argv[0] = NULL;
            char* argStr = NULL;
            do {
                if (!cmds[c].argc) {
                    argStr = strtok_r(cmdStr, " \t\r\n\v\f", &argsaveptr); // Grab first arg token
                    // printf("\t1 argstr='%s'\n", argStr);
                } else {
                    argStr = strtok_r(NULL, " \t\r\n\v\f", &argsaveptr); // Grab next arg tokens
                    // printf("\t2 argstr='%s'\n", argStr);
                }

                if (argStr) {
                    cmds[c].argv[cmds[c].argc] = argStr;
                    // printf(" cmds[%d].argv[%d] = '%s'\n", c, cmds[c].argc, argStr);
                    cmds[c].argc++;
                    cmds[c].argv[cmds[c].argc] = NULL; // Preemptively nullify last argv
                }

            } while (argStr);
            if (cmds[c].argc)
                execCmd(&cmds[c]);
            c++;
        }
    } while (cmdStr != NULL);

    // After beginning execution of all cmds
    int i;
    for (i = 0; i < c; i++) { // For each cmd, make sure to wait for process
        // printf("[%d] pid%d\n", i, cmds[i].pid);
        // printf("cmdB> argc=%d, argv[0]=%s, argv[1]=%s, pid=%d, cmd=%p\n", cmds[i].argc, cmds[i].argv[0], cmds[i].argv[1], cmds[i].pid, &cmds[i]);

        if (cmds[i].pid) { // if cmd is not a built-in cmd
            waitpid(cmds[i].pid, &cmds[i].status, 0);
            // printf("PARENT: child %d: %s => %d\n",
            //     cmds[i].pid, cmds[i].argv[0], WEXITSTATUS(cmds[i].status)
            // );
        }
    }
}

char *line = NULL;
size_t len = 0;
ssize_t lineSize = 0;

int
main(int argc, char *argv[]) {
    addPath("/bin");

    if (argc == 2) {
        FILE *fp;
        char *line = NULL;
        size_t len = 0;
        ssize_t read;

        fp = fopen(argv[1], "r");
        if (fp == NULL)
            exit(1);

        while ((read = getline(&line, &len, fp)) != -1) {
            processLine(line);
        }

        fclose(fp);
        if (line)
            free(line);
        exit(0);
    } else if (argc == 1) {
        while (1) {
	        printf("dash> ");
            lineSize = getline(&line, &len, stdin);

            processLine(line);

            free(line);
            line = NULL; // ensures that getline reallocs
        }
    } else {
        printf("Usage:\t./dash\tOR\t./dash batch.txt\n");
    }

    return 1;
}