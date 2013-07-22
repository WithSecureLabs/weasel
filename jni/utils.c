#include "utils.h"

/* ############################################################ */
/*              Functions borrowed from elsewhere               */
/* ############################################################ */

/* Modified function from cutils/properties.c */
int property_get(const char *key, char *value, const char *default_value)
{
    int len;

    len = __system_property_get(key, value);
    if(len > 0) {
        return len;
    }
    
    if(default_value) {
        len = strlen(default_value);
        memcpy(value, default_value, len + 1);
    }
    return len;
}

/* ############################################################ */
/*           Modified functions from dexopt-wrapper             */
/* ############################################################ */

void runDexopt(int zipFd, int odexFd, const char* inputFileName)
{
    static const char* kDexOptBin = "/bin/dexopt";
    static const int kMaxIntLen = 12;   // '-'+10dig+'\0' -OR- 0x+8dig
    char zipNum[kMaxIntLen];
    char odexNum[kMaxIntLen];
    char dexoptFlags[92];
    const char* androidRoot;
    char* execFile;

    /* pull optional configuration tweaks out of properties */
    property_get("dalvik.vm.dexopt-flags", dexoptFlags, "");

    /* find dexopt executable; this exists for simulator compatibility */
    androidRoot = getenv("ANDROID_ROOT");
    if (androidRoot == NULL)
        androidRoot = "/system";
    execFile = (char*) malloc(strlen(androidRoot) + strlen(kDexOptBin) +1);
    sprintf(execFile, "%s%s", androidRoot, kDexOptBin);

    sprintf(zipNum, "%d", zipFd);
    sprintf(odexNum, "%d", odexFd);

    execl(execFile, execFile, "--zip", zipNum, odexNum, inputFileName,
        dexoptFlags, (char*) NULL);
        
    if (DEBUG)
        printf("execl(%s) failed: %s\n", kDexOptBin, strerror(errno));
}

/* This function relies heavily on BOOTCLASSPATH being set */
bool odexAPKJAR(const char* zipName, const char* odexName)
{
    /*
     * Open the zip archive and the odex file, creating the latter (and
     * failing if it already exists).  This must be done while we still
     * have sufficient privileges to read the source file and create a file
     * in the target directory.  The "classes.dex" file will be extracted.
     */
    int zipFd, odexFd;
    zipFd = open(zipName, O_RDONLY, 0);
    if (zipFd < 0) {
        if (DEBUG)
            printf("Unable to open '%s': %s\n", zipName, strerror(errno));
        return false;
    }

    odexFd = open(odexName, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (odexFd < 0) {
        if (DEBUG)
            printf("Unable to create '%s': %s\n", odexName, strerror(errno));
        close(zipFd);
        return false;
    }

    if (DEBUG)
        printf("--- BEGIN '%s' (bootstrap=%d) ---\n", zipName, 0);

    /*
     * Fork a child process.
     */
    pid_t pid = fork();
    if (pid == 0) {

        /* lock the input file */
        if (flock(odexFd, LOCK_EX | LOCK_NB) != 0) {
            if (DEBUG)
                printf("Unable to lock '%s': %s\n", odexName, strerror(errno));
            exit(65);
        }

        runDexopt(zipFd, odexFd, zipName);  /* does not return */
        exit(67);                           /* usually */
    } else {
        /* parent -- wait for child to finish */
        if (DEBUG)
            printf("--- waiting for verify+opt, pid=%d\n", (int) pid);
        int status, oldStatus;
        pid_t gotPid;

        close(zipFd);
        close(odexFd);

        /*
         * Wait for the optimization process to finish.
         */
        while (1) {
            gotPid = waitpid(pid, &status, 0);
            if (gotPid == -1 && errno == EINTR) {
                if (DEBUG)
                    printf("waitpid interrupted, retrying\n");
            } else {
                break;
            }
        }
        if (gotPid != pid) {
            if (DEBUG)
                printf("waitpid failed: wanted %d, got %d: %s\n", (int) pid, (int) gotPid, strerror(errno));
            return false;
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            if (DEBUG)
                printf("--- END '%s' (success) ---\n", zipName);
            return true;
        } else {
            if (DEBUG)
                printf("--- END '%s' --- status=0x%04x, process failed\n", zipName, status);
            return false;
        }
    }

    /* notreached */
    return false;
}

/* ############################################################ */
/*                           Weasel                             */
/* ############################################################ */

/* Print debugging output to stdout when DEBUG = true */
void debug(char* name, char* msg)
{
    if (DEBUG)
    {
        printf("%s: %s\n", name, msg);
        __android_log_write(ANDROID_LOG_INFO, name, msg);
    }
}

/* Get the directory that the executable is running from */
char* getCurrentDirectory()
{
    /* Size 1024 */
    char *buf = malloc(sizeof(char) * 1024);

    /* Get full executable path */
    ssize_t len = readlink("/proc/self/exe", buf, 1023);
    buf[len] = '\0';
    
    /* Chop off everything after the last slash */
    char* lastSlash = strrchr(buf, '/');
    if (lastSlash != NULL)
        *(lastSlash) = '\0';

    debug("getCurrentDirectory", buf);

    return buf;
}

/* Set environmental vars using a number of methods if it is not set. */
void ensureEnvironmentalVarIsSet(char *varName)
{
    /* Check if var is already set */
    char *env = getenv(varName);
    if (env != NULL && strlen(env) > 0)
        return;
    
    /* Parse var from /proc/self/environ - environ() seems to be buggy */
    FILE *inputFile = fopen("/proc/self/environ", "rb");
    char parsedVar[3000];
    int i = 0;
    bool found = false;
    bool firstChar = true;
    bool falseAlarm = false;

    while (!found && i < 10000)
    {
        if (fgetc(inputFile) == varName[0])
        {
            if (fgetc(inputFile) == varName[1] && fgetc(inputFile) == varName[2] && fgetc(inputFile) == varName[3])
            {
                /* Check that previous char was \0 to ensure that we are not catching the "PATH" in "LD_LIBRARY_PATH" */
                if (!firstChar)
                {
                    /* Adjust to test for \0 */
                    fseek(inputFile, -5, SEEK_CUR);

                    /* If the preceding char is not null then it is a false alarm */
                    if (fgetc(inputFile) != 0)
                        falseAlarm = true;

                    /* Adjust back */
                    fseek(inputFile, 4, SEEK_CUR);
                }
                
                if (!falseAlarm)
                {
                    /* Jump over '=' to get to important bit */
                    fseek(inputFile, strlen(varName) - 3, SEEK_CUR);

                    /* Read string - all these vars are \0 separated */
                    fscanf(inputFile, "%s", parsedVar);

                    if (strlen(parsedVar) > 0)
                        found = true;    
                }
            }
        }

        firstChar = false;
        falseAlarm = false;
        i++;
    }
    fclose(inputFile);

    if (found)
    {
        /* Use var found in /proc/self/environ */
        setenv(varName, parsedVar, 1);
        debug(varName, parsedVar);
    }
    else
    {
        if (strcmp(varName, "BOOTCLASSPATH") == 0)
        {
            /* Dynamically build BOOTCLASSPATH based on observations from various devices */
            const int numJars = 14;
            char *jars[numJars];
            jars[0] = "/system/framework/core.jar";
            jars[1] = "/system/framework/core-junit.jar";
            jars[2] = "/system/framework/bouncycastle.jar";
            jars[3] = "/system/framework/ext.jar";
            jars[4] = "/system/framework/framework.jar";
            jars[5] = "/system/framework/framework2.jar";
            jars[6] = "/system/framework/telephony-common.jar";
            jars[7] = "/system/framework/mms-common.jar";
            jars[8] = "/system/framework/android.policy.jar";
            jars[9] = "/system/framework/services.jar";
            jars[10] = "/system/framework/apache-xml.jar";
            jars[11] = "/system/framework/filterfw.jar";
            jars[12] = "/system/framework/sec_edm.jar";
            jars[13] = "/system/framework/seccamera.jar";
            
            /* Get largest possible size of BOOTCLASSPATH */
            int totalLength = numJars + 1;
            for (i = 0; i < numJars; i++)
            {
                totalLength += strlen(jars[i]);
            }

            /* Generate new BOOTCLASSPATH by checking existence of jars */
            char *bootClassPath = malloc(totalLength);
            memset(bootClassPath, 0, totalLength);

            for (i = 0; i < numJars; i++)
            {
                if (access(jars[i], F_OK) != -1)
                {
                    /* Avoid prepending a : */
                    if (strlen(bootClassPath) > 0)
                        strcat(bootClassPath, ":");

                    strcat(bootClassPath, jars[i]);
                }
            }
            
            setenv("BOOTCLASSPATH", bootClassPath, 1);
        }
        else if (strcmp(varName, "PATH") == 0)
            setenv("PATH", "/sbin:/vendor/bin:/system/sbin:/system/bin:/system/xbin", 1);
        else if (strcmp(varName, "LD_LIBRARY_PATH") == 0)
            setenv("LD_LIBRARY_PATH", "/vendor/lib:/system/lib", 1);
        debug(varName, "default set");
    }
        
}

/* Determine if this package holds the INSTALL_PACKAGES permission */
bool haveInstallPackages(int uid)
{
    char line[8192];
    bool installPackages = false, foundPackage = false, endOfPackage = false, permsTagFound = false;
    int lookForMax = 0;

    /* Convert uid to searchable string */
    int uidStrLen = strlen("userId=\"") + 6 + strlen("\"") + 1;
    char *uidStr = malloc(uidStrLen);
    snprintf(uidStr, uidStrLen, "userId=\"%d\"", uid);

    /* Open file */
    FILE *inputFile = fopen("/data/system/packages.xml", "r");

    lookForPackage:

        /* Search for package definition */
        while (!foundPackage && fgets(line, sizeof(line), inputFile) != NULL)
        {
            if (strstr(line, uidStr) > 0)
                foundPackage = true;
        }

        /* Sanity check if the package is not found */
        if (!foundPackage)
        {
            /* Close file */
            fclose(inputFile);

            return false;
        }

        /* Look for end of package definition and INSTALL_PACKAGES */
        while (!endOfPackage && fgets(line, sizeof(line), inputFile) != NULL)
        {
            if (strstr(line, "android.permission.INSTALL_PACKAGES") > 0)
                installPackages = true;

            if (strstr(line, "<perms>") > 0)
                permsTagFound = true;

            if (strstr(line, "</package>") > 0)
                endOfPackage = true;
        }

    /* This check is in place because sometimes the uidStr is found more than once in the XML without the permissions */
    if (foundPackage && endOfPackage && !permsTagFound && (lookForMax < 10))
    {
        foundPackage = false;
        endOfPackage = false;
        installPackages = false;
        lookForMax++;
        goto lookForPackage;
    }

    /* Close file */
    fclose(inputFile);

    /* Return whether current package has INSTALL_PACKAGES */
    return installPackages;
}

/* Determine if drozer was successfully installed */
bool drozerInstalled()
{
    char line[8192];
    bool foundPackage = false;

    /* Open file */
    FILE *inputFile = fopen("/data/system/packages.xml", "r");

    /* Search for package name */
    while (!foundPackage && fgets(line, sizeof(line), inputFile) != NULL)
    {
        if (strstr(line, "com.mwr.dz") > 0)
            foundPackage = true;
    }

    /* Close file */
    fclose(inputFile);

    /* Return whether 'com.mwr.dz' is installed */
    return foundPackage;
}

/* Download a file from an HTTP server and put in folder - uses http_fetcher */
bool downloadFile(char* ip, char* port, char* serverFile, char* localFolder)
{
    /* Build URL */
    int urlLen = strlen("http://") + strlen(ip) + strlen(":") + strlen(port) + strlen("/") + strlen(serverFile) + 1;
    char *url = (char*) malloc(urlLen);
    snprintf(url, urlLen, "http://%s:%s/%s", ip, port, serverFile);

    /* Set the user-agent to Internet Explorer 9 :) */
    http_setUserAgent("Mozilla/5.0 (Windows; U; MSIE 9.0; WIndows NT 9.0; en-US))");

    /* Download the file */
    char *response;   
    int responseLen = http_fetch(url, &response);
    if (responseLen < 0)
        return false;

    /* Build destination */
    int destLen = strlen(localFolder) + strlen("/") + strlen(serverFile) + 1;
    char *dest = (char*) malloc(destLen);
    snprintf(dest, destLen, "%s/%s", localFolder, serverFile);

    /* Write to file */
    FILE* fp;
    fp = fopen(dest, "w");
    fwrite(response, 1, responseLen, fp);
    fclose(fp);

    /* Check file size */
    fp = fopen(dest, "r");
    fseek(fp, 0L, SEEK_END);
    int fileSize = ftell(fp);
    fclose(fp);

    /* Sanity check */
    if (responseLen == fileSize)
        return true;
    else
        return false;
}
