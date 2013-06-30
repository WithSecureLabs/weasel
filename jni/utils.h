#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/system_properties.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "jni.h"
#include <android/log.h>
#include <http_fetcher.h>

#define DEBUG true

#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

void runDexopt(int zipFd, int odexFd, const char* inputFileName);
bool odexAPKJAR(const char* zipName, const char* odexName);
char* getCurrentDirectory();
void debug(char* name, char* msg);
void ensureEnvironmentalVarIsSet(char *varName);
bool haveInstallPackages(int uid);
bool drozerInstalled();
bool downloadFile(char* ip, char* port, char* serverFile, char* localFolder);

#endif
